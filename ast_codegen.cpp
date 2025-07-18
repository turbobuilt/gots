#include "compiler.h"
#include "runtime.h"
#include "runtime_object.h"
#include "compilation_context.h"
#include "function_compilation_manager.h"
#include <iostream>
#include <unordered_map>
#include <cstring>
#include <cstdlib>
#include <queue>

// Simple global constant storage for imported constants
static std::unordered_map<std::string, double> global_imported_constants;

namespace gots {

// Forward declarations for function ID registry
void __register_function_id(int64_t function_id, const std::string& function_name);
void ensure_lookup_function_by_id_registered();

// Forward declaration for fast runtime function lookup
extern "C" void* __lookup_function_fast(uint16_t func_id);

// Helper function to get the global deferred functions list
static std::vector<std::pair<std::string, FunctionExpression*>>& get_deferred_functions() {
    static std::vector<std::pair<std::string, FunctionExpression*>> deferred_functions;
    return deferred_functions;
}

// Static member definition
GoTSCompiler* ConstructorDecl::current_compiler_context = nullptr;

void NumberLiteral::generate_code(CodeGenerator& gen, TypeInference&) {
    gen.emit_mov_reg_imm(0, static_cast<int64_t>(value));
    result_type = DataType::NUMBER;  // JavaScript compatibility: number literals are number (float64)
}

void StringLiteral::generate_code(CodeGenerator& gen, TypeInference&) {
    // High-performance string creation using interned strings for literals
    // This provides both memory efficiency and extremely fast string creation
    
    if (value.empty()) {
        // Handle empty string efficiently - call __string_create_empty()
        gen.emit_call("__string_create_empty");
    } else {
        // SAFE APPROACH: Use string interning with proper fixed StringPool
        // The StringPool now uses std::string keys instead of char* keys,
        // which makes it safe to use with temporary string data
        
        // Store the string content safely for the call
        // We need to ensure the string data is available during the __string_intern call
        static std::unordered_map<std::string, const char*> literal_storage;
        
        // Check if we already have this literal stored
        auto it = literal_storage.find(value);
        const char* str_ptr;
        if (it != literal_storage.end()) {
            str_ptr = it->second;
        } else {
            // Allocate permanent storage for this literal
            char* permanent_str = new char[value.length() + 1];
            strcpy(permanent_str, value.c_str());
            literal_storage[value] = permanent_str;
            str_ptr = permanent_str;
        }
        
        uint64_t str_literal_addr = reinterpret_cast<uint64_t>(str_ptr);
        gen.emit_mov_reg_imm(7, static_cast<int64_t>(str_literal_addr)); // RDI = first argument
        
        // Use string interning for memory efficiency
        gen.emit_call("__string_intern");
    }
    
    // Result is now in RAX (pointer to GoTSString)
    result_type = DataType::STRING;
}

void RegexLiteral::generate_code(CodeGenerator& gen, TypeInference&) {
    // Create a runtime regex object from pattern and flags
    
    // Store pattern string
    static std::unordered_map<std::string, const char*> pattern_storage;
    
    // Check if we already have this pattern stored
    auto pattern_it = pattern_storage.find(pattern);
    const char* pattern_ptr;
    if (pattern_it != pattern_storage.end()) {
        pattern_ptr = pattern_it->second;
    } else {
        // Allocate permanent storage for this pattern
        char* permanent_pattern = new char[pattern.length() + 1];
        strcpy(permanent_pattern, pattern.c_str());
        pattern_storage[pattern] = permanent_pattern;
        pattern_ptr = permanent_pattern;
    }
    
    // Store flags string
    static std::unordered_map<std::string, const char*> flags_storage;
    
    auto flags_it = flags_storage.find(flags);
    const char* flags_ptr;
    if (flags_it != flags_storage.end()) {
        flags_ptr = flags_it->second;
    } else {
        // Allocate permanent storage for this flags string
        char* permanent_flags = new char[flags.length() + 1];
        strcpy(permanent_flags, flags.c_str());
        flags_storage[flags] = permanent_flags;
        flags_ptr = permanent_flags;
    }
    
    // CREATIVE FIX: Use a safer method to pass the pattern
    // Instead of loading address directly, use a pattern ID lookup system
    
    // Store pattern in a safe global registry with integer IDs
    static std::unordered_map<std::string, int> pattern_registry;
    static int next_pattern_id = 1;
    
    int pattern_id;
    auto registry_it = pattern_registry.find(pattern);
    if (registry_it != pattern_registry.end()) {
        pattern_id = registry_it->second;
    } else {
        pattern_id = next_pattern_id++;
        pattern_registry[pattern] = pattern_id;
    }
    
    // Register the pattern with the runtime first
    gen.emit_mov_reg_imm(7, reinterpret_cast<int64_t>(pattern_ptr)); // RDI = pattern string (permanent storage)
    gen.emit_call("__register_regex_pattern");
    
    // The function returns the pattern ID in RAX, use it to create the regex
    gen.emit_mov_reg_reg(7, 0); // RDI = RAX (pattern ID returned)
    gen.emit_call("__regex_create_by_id");
    
    // Result is now in RAX (pointer to GoTSRegExp)
    result_type = DataType::REGEX;
}

void Identifier::generate_code(CodeGenerator& gen, TypeInference& types) {
    // SPECIAL CASE: Handle "runtime" global object
    if (name == "runtime") {
        // The runtime object is a special global that doesn't need any code generation
        // PropertyAccess and MethodCall nodes will optimize runtime.x.y() calls
        result_type = DataType::RUNTIME_OBJECT;
        return;
    }
    
    // Check if this is a global imported constant first
    auto it = global_imported_constants.find(name);
    if (it != global_imported_constants.end()) {
        // Load the constant value directly as an immediate using bit preservation
        union {
            double f;
            int64_t i;
        } converter;
        converter.f = it->second;
        gen.emit_mov_reg_imm(0, converter.i);
        result_type = DataType::FLOAT64;
        return;
    }
    
    // For now, let function names fall through to variable lookup
    // TODO: Implement proper function reference handling
    
    // Fall back to local variable lookup
    DataType var_type = types.get_variable_type(name);
    result_type = var_type;
    
    // Get the actual stack offset for this variable
    int64_t offset = types.get_variable_offset(name);
    if (offset == 0) {
        // Default to -8 for backward compatibility
        offset = -8;
    }
    
    gen.emit_mov_reg_mem(0, offset);
}

void BinaryOp::generate_code(CodeGenerator& gen, TypeInference& types) {
    if (left) {
        left->generate_code(gen, types);
        // Push left operand result onto stack to protect it during right operand evaluation
        gen.emit_sub_reg_imm(4, 8);   // sub rsp, 8 (allocate stack space)
        // Store to RSP-relative location to match the RSP-relative load later
        if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
            x86_gen->emit_mov_mem_rsp_reg(0, 0);   // mov [rsp], rax (save left operand on stack)
        } else {
            gen.emit_mov_mem_reg(0, 0);   // fallback for other backends
        }
    }
    
    if (right) {
        right->generate_code(gen, types);
    }
    
    DataType left_type = left ? left->result_type : DataType::UNKNOWN;
    DataType right_type = right ? right->result_type : DataType::UNKNOWN;
    
    switch (op) {
        case TokenType::PLUS:
            if (left_type == DataType::STRING || right_type == DataType::STRING) {
                result_type = DataType::STRING;
                if (left) {
                    // String concatenation - extremely optimized
                    // Right operand (string) is in RAX
                    gen.emit_mov_reg_reg(6, 0);   // mov rsi, rax (right operand -> second argument)
                    
                    // Pop left operand from stack
                    if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                        x86_gen->emit_mov_reg_mem_rsp(7, 0);   // mov rdi, [rsp] (left operand -> first argument)
                    } else {
                        gen.emit_mov_reg_mem(7, 0);   // fallback for other backends
                    }
                    gen.emit_add_reg_imm(4, 8);   // add rsp, 8 (restore stack)
                    
                    // Robust string concatenation with proper type handling
                    if (left_type == DataType::STRING && right_type == DataType::STRING) {
                        // Both are GoTSString* - use optimized __string_concat
                        // Parameters: RDI = left GoTSString*, RSI = right GoTSString*
                        gen.emit_call("__string_concat");
                    } else if (left_type == DataType::STRING && right_type != DataType::STRING) {
                        // Left is GoTSString*, right needs conversion to string
                        // For now, assume right operand is a C string or can be cast to one
                        // In future: add runtime type conversion here
                        // Parameters: RDI = left GoTSString*, RSI = right const char*
                        gen.emit_call("__string_concat_cstr");
                    } else if (left_type != DataType::STRING && right_type == DataType::STRING) {
                        // Left needs conversion to string, right is GoTSString*
                        // Parameters: RDI = left const char*, RSI = right GoTSString*
                        gen.emit_call("__string_concat_cstr_left");
                    } else {
                        // Neither operand is a string - fallback to regular concatenation
                        // This should not happen in string concatenation context, but handle gracefully
                        // Convert both to strings first, then concatenate
                        // For robust implementation, we'd add runtime type conversion here
                        gen.emit_call("__string_concat");
                    }
                    // Result (new GoTSString*) is now in RAX
                }
            } else {
                result_type = types.get_cast_type(left_type, right_type);
                if (left) {
                    // Pop left operand from stack and add to right operand (in RAX)
                    if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                        x86_gen->emit_mov_reg_mem_rsp(3, 0);   // mov rbx, [rsp] (load left operand from stack)
                    } else {
                        gen.emit_mov_reg_mem(3, 0);   // fallback for other backends
                    }
                    gen.emit_add_reg_imm(4, 8);   // add rsp, 8 (restore stack)
                    gen.emit_add_reg_reg(0, 3);   // add rax, rbx (add left to right)
                }
            }
            break;
            
        case TokenType::MINUS:
            result_type = types.get_cast_type(left_type, right_type);
            if (left) {
                // Binary minus: Pop left operand from stack and subtract right operand from it
                if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                    x86_gen->emit_mov_reg_mem_rsp(3, 0);   // mov rbx, [rsp] (load left operand from stack)
                } else {
                    gen.emit_mov_reg_mem(3, 0);   // fallback for other backends
                }
                gen.emit_add_reg_imm(4, 8);   // add rsp, 8 (restore stack)
                gen.emit_sub_reg_reg(3, 0);   // sub rbx, rax (subtract right from left)
                gen.emit_mov_reg_reg(0, 3);   // mov rax, rbx (result in rax)
            } else {
                // Unary minus: negate the value in RAX
                gen.emit_mov_reg_imm(1, 0);   // mov rcx, 0
                gen.emit_sub_reg_reg(1, 0);   // sub rcx, rax (0 - rax)
                gen.emit_mov_reg_reg(0, 1);   // mov rax, rcx (result in rax)
                result_type = right_type;     // Result type is same as right operand for unary minus
            }
            break;
            
        case TokenType::MULTIPLY:
            result_type = types.get_cast_type(left_type, right_type);
            if (left) {
                // Pop left operand from stack and multiply with right operand
                if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                    x86_gen->emit_mov_reg_mem_rsp(3, 0);   // mov rbx, [rsp] (load left operand from stack)
                } else {
                    gen.emit_mov_reg_mem(3, 0);   // fallback for other backends
                }
                gen.emit_add_reg_imm(4, 8);   // add rsp, 8 (restore stack)
                gen.emit_mul_reg_reg(3, 0);   // imul rbx, rax (multiply left with right)
                gen.emit_mov_reg_reg(0, 3);   // mov rax, rbx (result in rax)
            }
            break;
            
        case TokenType::POWER:
            result_type = DataType::INT64; // Power operation returns int64 for now
            if (left) {
                // For exponentiation: base ** exponent
                // x86-64 calling convention: RDI = first arg, RSI = second arg
                
                // Right operand (exponent) is currently in RAX
                gen.emit_mov_reg_reg(6, 0);   // mov rsi, rax (exponent -> second argument)
                
                // Pop left operand from stack (base)
                if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                    x86_gen->emit_mov_reg_mem_rsp(7, 0);   // mov rdi, [rsp] (base -> first argument)
                } else {
                    gen.emit_mov_reg_mem(7, 0);   // fallback for other backends
                }
                gen.emit_add_reg_imm(4, 8);   // add rsp, 8 (restore stack)
                
                // Call the power function: __runtime_pow(base, exponent)
                gen.emit_call("__runtime_pow");
                // Result will be in RAX
            }
            break;
            
        case TokenType::DIVIDE:
            result_type = types.get_cast_type(left_type, right_type);
            if (left) {
                // Pop left operand from stack and divide by right operand
                if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                    x86_gen->emit_mov_reg_mem_rsp(1, 0);   // mov rcx, [rsp] (load left operand from stack)
                } else {
                    gen.emit_mov_reg_mem(1, 0);   // fallback for other backends
                }
                gen.emit_add_reg_imm(4, 8);   // add rsp, 8 (restore stack)
                gen.emit_div_reg_reg(1, 0);   // div rcx by rax (divide left by right)
                gen.emit_mov_reg_reg(0, 1);   // mov rax, rcx (result in rax)
            }
            break;
            
        case TokenType::MODULO:
            result_type = types.get_cast_type(left_type, right_type);
            if (left) {
                // Use runtime function for modulo to ensure robustness
                // Right operand is in RAX, move to RSI (second argument)
                gen.emit_mov_reg_reg(6, 0);   // RSI = right operand (from RAX)
                
                // Pop left operand from stack directly to RDI (first argument)
                if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                    x86_gen->emit_mov_reg_mem_rsp(7, 0);   // RDI = left operand from [rsp]
                } else {
                    gen.emit_mov_reg_mem(7, 0);   // fallback for other backends
                }
                gen.emit_add_reg_imm(4, 8);   // add rsp, 8 (restore stack)
                
                // Call __runtime_modulo(left, right)
                gen.emit_call("__runtime_modulo");
                // Result is now in RAX
            }
            break;
            
        case TokenType::EQUAL:
        case TokenType::NOT_EQUAL:
        case TokenType::STRICT_EQUAL:
        case TokenType::LESS:
        case TokenType::GREATER:
        case TokenType::LESS_EQUAL:
        case TokenType::GREATER_EQUAL:
            result_type = DataType::BOOLEAN;
            if (left) {
                // Pop left operand from stack and compare with right operand (in RAX)
                if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                    x86_gen->emit_mov_reg_mem_rsp(1, 0);   // mov rcx, [rsp] (load left operand from stack)
                } else {
                    gen.emit_mov_reg_mem(1, 0);   // fallback for other backends
                }
                gen.emit_add_reg_imm(4, 8);   // add rsp, 8 (restore stack)
                
                // Optimized comparison logic with string-specific handling
                if (left_type == DataType::STRING && right_type == DataType::STRING) {
                    // Both operands are strings - use high-performance string comparison
                    // Left value is in RCX, right value is in RAX
                    gen.emit_mov_reg_reg(7, 1);   // mov rdi, rcx (left string -> first argument)
                    gen.emit_mov_reg_reg(6, 0);   // mov rsi, rax (right string -> second argument)
                    
                    switch (op) {
                        case TokenType::EQUAL:
                        case TokenType::STRICT_EQUAL:
                            gen.emit_call("__string_equals");
                            // Result (bool) is already in RAX
                            break;
                        case TokenType::NOT_EQUAL:
                            gen.emit_call("__string_equals");
                            // Invert the result: XOR with 1
                            gen.emit_mov_reg_imm(1, 1);
                            gen.emit_xor_reg_reg(0, 1);
                            break;
                        case TokenType::LESS:
                        case TokenType::GREATER:
                        case TokenType::LESS_EQUAL:
                        case TokenType::GREATER_EQUAL:
                            gen.emit_call("__string_compare");
                            // __string_compare returns -1, 0, or 1
                            // Convert to boolean based on comparison type
                            gen.emit_mov_reg_imm(1, 0);   // mov rcx, 0
                            gen.emit_compare(0, 1);       // compare result with 0
                            
                            switch (op) {
                                case TokenType::LESS:
                                    gen.emit_setl(0);     // Set AL to 1 if result < 0
                                    break;
                                case TokenType::GREATER:
                                    gen.emit_setg(0);     // Set AL to 1 if result > 0
                                    break;
                                case TokenType::LESS_EQUAL:
                                    gen.emit_setle(0);    // Set AL to 1 if result <= 0
                                    break;
                                case TokenType::GREATER_EQUAL:
                                    gen.emit_setge(0);    // Set AL to 1 if result >= 0
                                    break;
                            }
                            gen.emit_and_reg_imm(0, 0xFF); // Zero out upper bits
                            break;
                    }
                } else {
                    // Handle non-string or mixed type comparisons
                    switch (op) {
                        case TokenType::EQUAL:
                            // JavaScript-style equality with type coercion
                            // Call __runtime_js_equal(left_value, left_type, right_value, right_type)
                            // Arguments: RDI = left_value, RSI = left_type, RDX = right_value, RCX = right_type
                            
                            // Right value is already in RAX, move to RDX (3rd argument)
                            gen.emit_mov_reg_reg(2, 0);   // mov rdx, rax (right_value)
                            
                            // Left value is in RCX, move to RDI (1st argument)  
                            gen.emit_mov_reg_reg(7, 1);   // mov rdi, rcx (left_value)
                            
                            // Set type arguments - use the types determined from operands
                            // Left type (RSI) 
                            gen.emit_mov_reg_imm(6, static_cast<int64_t>(left_type));  // mov rsi, left_type
                            
                            // Right type (RCX)  
                            gen.emit_mov_reg_imm(1, static_cast<int64_t>(right_type)); // mov rcx, right_type
                            
                            // Call the JavaScript equality function
                            gen.emit_call("__runtime_js_equal");
                            // Result will be in RAX (1 for equal, 0 for not equal)
                            break;
                        default:
                            // For all other comparisons, do the compare first
                            gen.emit_compare(1, 0);       // compare rcx (left) with rax (right)
                            
                            switch (op) {
                                case TokenType::LESS:
                                    gen.emit_setl(0); // Set AL to 1 if RCX < RAX, 0 otherwise
                                    break;
                                case TokenType::GREATER:
                                    gen.emit_setg(0); // Set AL to 1 if RCX > RAX, 0 otherwise
                                    break;
                                case TokenType::NOT_EQUAL:
                                    gen.emit_setne(0); // Set AL to 1 if RCX != RAX, 0 otherwise
                                    break;
                                case TokenType::STRICT_EQUAL:
                                    // For strict equality, we need to check both value and type
                                    // For now, use same logic as EQUAL but this should be enhanced for type checking
                                    gen.emit_sete(0); // Set AL to 1 if RCX == RAX, 0 otherwise
                                    break;
                                case TokenType::LESS_EQUAL:
                                    gen.emit_setle(0); // Set AL to 1 if RCX <= RAX, 0 otherwise
                                    break;
                                case TokenType::GREATER_EQUAL:
                                    gen.emit_setge(0); // Set AL to 1 if RCX >= RAX, 0 otherwise
                                    break;
                                default:
                                    gen.emit_mov_reg_imm(0, 0); // Default to false
                                    break;
                            }
                            // Zero out the upper bits of RAX since SETcc only sets AL
                            gen.emit_and_reg_imm(0, 0xFF);
                            break;
                    }
                }
            }
            break;
            
        case TokenType::AND:
        case TokenType::OR:
            result_type = DataType::BOOLEAN;
            if (left) {
                // Generate unique labels for short-circuiting
                static int logic_counter = 0;
                std::string end_label = "__logic_end_" + std::to_string(logic_counter);
                std::string short_circuit_label = "__logic_short_" + std::to_string(logic_counter++);
                
                // Pop left operand from stack
                if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                    x86_gen->emit_mov_reg_mem_rsp(1, 0);   // mov rcx, [rsp] (load left operand from stack)
                } else {
                    gen.emit_mov_reg_mem(1, 0);   // fallback for other backends
                }
                gen.emit_add_reg_imm(4, 8);   // add rsp, 8 (restore stack)
                
                if (op == TokenType::AND) {
                    // For AND: if left is false (0), short-circuit to false
                    gen.emit_mov_reg_imm(2, 0);       // mov rdx, 0
                    gen.emit_compare(1, 2);           // compare rcx with 0
                    gen.emit_jump_if_zero(short_circuit_label); // jump if left is false
                    
                    // Left is true, so result depends on right operand (already in RAX)
                    // Test if right operand is non-zero
                    gen.emit_compare(0, 2);           // compare rax with 0
                    gen.emit_setne(0);                // Set AL to 1 if RAX != 0
                    gen.emit_and_reg_imm(0, 0xFF);    // Zero out upper bits
                    gen.emit_jump(end_label);
                    
                    // Short-circuit: left was false, so result is false
                    gen.emit_label(short_circuit_label);
                    gen.emit_mov_reg_imm(0, 0);       // mov rax, 0
                } else { // OR
                    // For OR: if left is true (non-zero), short-circuit to true
                    gen.emit_mov_reg_imm(2, 0);       // mov rdx, 0
                    gen.emit_compare(1, 2);           // compare rcx with 0
                    gen.emit_jump_if_not_zero(short_circuit_label); // jump if left is true
                    
                    // Left is false, so result depends on right operand (already in RAX)
                    // Test if right operand is non-zero
                    gen.emit_compare(0, 2);           // compare rax with 0
                    gen.emit_setne(0);                // Set AL to 1 if RAX != 0
                    gen.emit_and_reg_imm(0, 0xFF);    // Zero out upper bits
                    gen.emit_jump(end_label);
                    
                    // Short-circuit: left was true, so result is true
                    gen.emit_label(short_circuit_label);
                    gen.emit_mov_reg_imm(0, 1);       // mov rax, 1
                }
                
                gen.emit_label(end_label);
            }
            break;
            
        case TokenType::NOT:
            result_type = DataType::BOOLEAN;
            // For unary NOT, right operand is in RAX, left should be null
            if (!left) {
                // Compare RAX with 0 to check if it's false (0)
                gen.emit_mov_reg_imm(1, 0);   // mov rcx, 0
                gen.emit_compare(0, 1);       // compare rax with 0
                gen.emit_sete(0);             // Set AL to 1 if RAX == 0 (i.e., NOT false = true)
                gen.emit_and_reg_imm(0, 0xFF); // Zero out upper bits
            }
            break;
            
        default:
            result_type = DataType::UNKNOWN;
            break;
    }
}

void TernaryOperator::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Generate unique labels for the ternary branches
    static int label_counter = 0;
    std::string false_label = "__ternary_false_" + std::to_string(label_counter);
    std::string end_label = "__ternary_end_" + std::to_string(label_counter++);
    
    // Generate code for condition
    condition->generate_code(gen, types);
    
    // Test if condition is zero (false) - compare RAX with 0
    gen.emit_mov_reg_imm(1, 0); // mov rcx, 0
    gen.emit_compare(0, 1); // Compare RAX with RCX (0)
    gen.emit_jump_if_zero(false_label);
    
    // Generate code for true expression
    true_expr->generate_code(gen, types);
    gen.emit_jump(end_label);
    
    // False branch
    gen.emit_label(false_label);
    false_expr->generate_code(gen, types);
    
    // End label
    gen.emit_label(end_label);
    
    // Result type is the common type of true and false expressions
    result_type = types.get_cast_type(true_expr->result_type, false_expr->result_type);
}

void FunctionCall::generate_code(CodeGenerator& gen, TypeInference& types) {
    if (is_goroutine) {
        // For goroutines, we need to build an argument array on the stack
        if (arguments.size() > 0) {
            // Push arguments onto stack in reverse order to create array
            for (int i = arguments.size() - 1; i >= 0; i--) {
                arguments[i]->generate_code(gen, types);
                gen.emit_sub_reg_imm(4, 8);  // sub rsp, 8
                if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                    x86_gen->emit_mov_mem_rsp_reg(0, 0);  // mov [rsp], rax
                } else {
                    gen.emit_mov_mem_reg(0, 0);  // fallback for other backends
                }
            }
            
            // Now stack contains arguments in correct order: arg0, arg1, arg2...
            gen.emit_goroutine_spawn_with_args(name, arguments.size());
            
            // Clean up the argument array from stack
            int64_t array_size = arguments.size() * 8;
            gen.emit_add_reg_imm(4, array_size);  // add rsp, array_size
        } else {
            gen.emit_goroutine_spawn(name);
        }
        result_type = DataType::PROMISE;
    } else {
        // Check for global timer functions and map them to runtime equivalents
        if (name == "setTimeout") {
            // Map setTimeout to runtime.timer.setTimeout
            for (size_t i = 0; i < arguments.size() && i < 6; i++) {
                arguments[i]->generate_code(gen, types);
                switch (i) {
                    case 0: gen.emit_mov_reg_reg(7, 0); break;  // RDI = RAX
                    case 1: gen.emit_mov_reg_reg(6, 0); break;  // RSI = RAX
                    case 2: gen.emit_mov_reg_reg(2, 0); break;  // RDX = RAX
                    case 3: gen.emit_mov_reg_reg(1, 0); break;  // RCX = RAX
                    case 4: gen.emit_mov_reg_reg(8, 0); break;  // R8 = RAX
                    case 5: gen.emit_mov_reg_reg(9, 0); break;  // R9 = RAX
                }
            }
            gen.emit_call("__gots_set_timeout");
            result_type = DataType::INT64; // Timer ID
            return; // Skip normal function call handling
        } else if (name == "setInterval") {
            for (size_t i = 0; i < arguments.size() && i < 6; i++) {
                arguments[i]->generate_code(gen, types);
                switch (i) {
                    case 0: gen.emit_mov_reg_reg(7, 0); break;  // RDI = RAX
                    case 1: gen.emit_mov_reg_reg(6, 0); break;  // RSI = RAX
                    case 2: gen.emit_mov_reg_reg(2, 0); break;  // RDX = RAX
                    case 3: gen.emit_mov_reg_reg(1, 0); break;  // RCX = RAX
                    case 4: gen.emit_mov_reg_reg(8, 0); break;  // R8 = RAX
                    case 5: gen.emit_mov_reg_reg(9, 0); break;  // R9 = RAX
                }
            }
            gen.emit_call("__gots_set_interval");
            result_type = DataType::INT64; // Timer ID
            return;
        } else if (name == "clearTimeout") {
            for (size_t i = 0; i < arguments.size() && i < 6; i++) {
                arguments[i]->generate_code(gen, types);
                switch (i) {
                    case 0: gen.emit_mov_reg_reg(7, 0); break;  // RDI = RAX
                    case 1: gen.emit_mov_reg_reg(6, 0); break;  // RSI = RAX
                    case 2: gen.emit_mov_reg_reg(2, 0); break;  // RDX = RAX
                    case 3: gen.emit_mov_reg_reg(1, 0); break;  // RCX = RAX
                    case 4: gen.emit_mov_reg_reg(8, 0); break;  // R8 = RAX
                    case 5: gen.emit_mov_reg_reg(9, 0); break;  // R9 = RAX
                }
            }
            gen.emit_call("__gots_clear_timeout");
            result_type = DataType::BOOLEAN; // Success/failure
            return;
        } else if (name == "clearInterval") {
            for (size_t i = 0; i < arguments.size() && i < 6; i++) {
                arguments[i]->generate_code(gen, types);
                switch (i) {
                    case 0: gen.emit_mov_reg_reg(7, 0); break;  // RDI = RAX
                    case 1: gen.emit_mov_reg_reg(6, 0); break;  // RSI = RAX
                    case 2: gen.emit_mov_reg_reg(2, 0); break;  // RDX = RAX
                    case 3: gen.emit_mov_reg_reg(1, 0); break;  // RCX = RAX
                    case 4: gen.emit_mov_reg_reg(8, 0); break;  // R8 = RAX
                    case 5: gen.emit_mov_reg_reg(9, 0); break;  // R9 = RAX
                }
            }
            gen.emit_call("__gots_clear_interval");
            result_type = DataType::BOOLEAN; // Success/failure
            return;
        } else if (name == "console.log") {
            // Handle console.log function calls in operator overloads - same as MethodCall handling
            
            // Generate first string
            if (arguments.size() >= 1) {
                arguments[0]->generate_code(gen, types);
                gen.emit_call("__console_log_string");
            }
            
            // Generate remaining arguments with spaces between them
            for (size_t i = 1; i < arguments.size(); i++) {
                gen.emit_call("__console_log_space");
                
                arguments[i]->generate_code(gen, types);
                
                // Use type-aware console logging based on argument type
                DataType arg_type = arguments[i]->result_type;
                if (arg_type == DataType::STRING) {
                    gen.emit_call("__console_log_string");
                } else if (arg_type == DataType::NUMBER || arg_type == DataType::INT64 || arg_type == DataType::FLOAT64) {
                    gen.emit_call("__console_log_number");
                } else {
                    gen.emit_call("__console_log_auto");
                }
            }
            
            // Add newline at the end
            gen.emit_call("__console_log_newline");
            result_type = DataType::UNKNOWN;
            return;
        }
        
        // Regular function call - use x86-64 calling convention
        
        // Check if this is a variable containing a function ID that needs to be resolved
        DataType var_type = types.get_variable_type(name);
        bool is_function_variable = (var_type == DataType::FUNCTION);
        
        // Generate code for arguments and place them in appropriate registers
        for (size_t i = 0; i < arguments.size() && i < 6; i++) {
            arguments[i]->generate_code(gen, types);
            
            // Move result to appropriate argument register
            switch (i) {
                case 0: gen.emit_mov_reg_reg(7, 0); break;  // RDI = RAX
                case 1: gen.emit_mov_reg_reg(6, 0); break;  // RSI = RAX
                case 2: gen.emit_mov_reg_reg(2, 0); break;  // RDX = RAX
                case 3: gen.emit_mov_reg_reg(1, 0); break;  // RCX = RAX
                case 4: gen.emit_mov_reg_reg(8, 0); break;  // R8 = RAX
                case 5: gen.emit_mov_reg_reg(9, 0); break;  // R9 = RAX
            }
        }
        
        // For more than 6 arguments, push them onto stack (in reverse order)
        for (int i = arguments.size() - 1; i >= 6; i--) {
            arguments[i]->generate_code(gen, types);
            // Push RAX onto stack
            gen.emit_sub_reg_imm(4, 8);  // sub rsp, 8
            gen.emit_mov_mem_reg(0, 0);  // mov [rsp], rax
        }
        
        if (is_function_variable) {
            // This is a variable containing a function ID - resolve it to function address
            
            // Ensure our lookup function is registered
            ensure_lookup_function_by_id_registered();
            
            // Load the function ID from the variable
            int64_t var_offset = types.get_variable_offset(name);
            if (var_offset == 0) var_offset = -8; // Default offset
            gen.emit_mov_reg_mem(0, var_offset);  // RAX = function_id
            
            // Call runtime function to resolve function ID to address
            gen.emit_mov_reg_reg(7, 0);  // RDI = function_id (first arg)
            gen.emit_call("__lookup_function_by_id");
            
            // RAX now contains the function address, call it
            if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                x86_gen->emit_call_reg(0);  // call rax
            } else {
                gen.emit_call(name);  // fallback
            }
        } else {
            // Direct function call by name
            gen.emit_call(name);
        }
        
        // Look up function return type from compiler registry
        auto* compiler = get_current_compiler();
        if (compiler) {
            Function* func = compiler->get_function(name);
            if (func) {
                result_type = func->return_type;
                //           << static_cast<int>(result_type) << std::endl;
            } else {
                // Function not found in registry, assume NUMBER for built-in functions
                result_type = DataType::NUMBER;
            }
        } else {
            // No compiler context, fall back to default
            result_type = DataType::NUMBER;
        }
        
        // Clean up stack if we pushed arguments
        if (arguments.size() > 6) {
            int stack_cleanup = (arguments.size() - 6) * 8;
            gen.emit_add_reg_imm(4, stack_cleanup);  // add rsp, cleanup_amount
        }
    }
    
    if (is_awaited) {
        gen.emit_promise_await(0);
    }
}

void MethodCall::generate_code(CodeGenerator& gen, TypeInference& types) {
    
    // Handle built-in methods
    if (object_name == "console") {
        if (method_name == "log") {
            // Call console.log built-in function - handle all arguments
            for (size_t i = 0; i < arguments.size(); i++) {
                if (i > 0) {
                    // Add space between arguments
                    gen.emit_call("__console_log_space");
                }
                
                arguments[i]->generate_code(gen, types);
                
                
                // Check the type of each argument to call the appropriate console function
                if (arguments[i]->result_type == DataType::TENSOR) {
                    // For arrays, we need to get the array data and size
                    gen.emit_mov_mem_reg(-8, 0); // Save array pointer on stack
                    
                    // Get array size first
                    gen.emit_mov_reg_mem(7, -8); // RDI = array pointer from stack
                    gen.emit_call("__array_size");
                    gen.emit_mov_reg_reg(6, 0); // RSI = size
                    
                    // Get array data pointer
                    gen.emit_mov_reg_mem(7, -8); // RDI = array pointer from stack
                    gen.emit_call("__array_data");
                    gen.emit_mov_reg_reg(7, 0); // RDI = data pointer
                    
                    // Call console.log_array with data pointer in RDI and size in RSI
                    gen.emit_call("__console_log_array");
                } else if (arguments[i]->result_type == DataType::STRING) {
                    // Optimized string console.log - RAX contains GoTSString*
                    gen.emit_mov_reg_reg(7, 0); // RDI = RAX (GoTSString*)
                    gen.emit_call("__console_log_string");
                } else if (arguments[i]->result_type == DataType::NUMBER || 
                          arguments[i]->result_type == DataType::FLOAT64 ||
                          arguments[i]->result_type == DataType::INT64) {
                    // For numbers - handle all numeric types explicitly
                    gen.emit_mov_reg_reg(7, 0); // RDI = RAX
                    gen.emit_call("__console_log_number");
                } else if (arguments[i]->result_type == DataType::CLASS_INSTANCE) {
                    // For objects - print them properly
                    gen.emit_mov_reg_reg(7, 0); // RDI = RAX (object_id)
                    gen.emit_call("__console_log_object");
                } else {
                    // For unknown types, use auto-detection to handle arrays or numbers
                    gen.emit_mov_reg_reg(7, 0); // RDI = RAX
                    gen.emit_call("__console_log_auto");
                }
            }
            
            // Print newline at the end
            gen.emit_call("__console_log_newline");
            result_type = DataType::VOID;
        } else if (method_name == "time") {
            // Call console.time built-in function
            if (arguments.size() > 0) {
                arguments[0]->generate_code(gen, types);
                // Move first argument to RDI register (x86-64 calling convention)
                gen.emit_mov_reg_reg(7, 0); // RDI = RAX
            }
            // Ensure stack is aligned for C calling convention
            gen.emit_sub_reg_imm(4, 8);  // Align stack to 16-byte boundary
            gen.emit_call("__console_time");
            gen.emit_add_reg_imm(4, 8);  // Restore stack
            result_type = DataType::VOID;
        } else if (method_name == "timeEnd") {
            // Call console.timeEnd built-in function
            if (arguments.size() > 0) {
                arguments[0]->generate_code(gen, types);
                // Move first argument to RDI register (x86-64 calling convention)
                gen.emit_mov_reg_reg(7, 0); // RDI = RAX
            }
            // Ensure stack is aligned for C calling convention
            gen.emit_sub_reg_imm(4, 8);  // Align stack to 16-byte boundary
            gen.emit_call("__console_timeEnd");
            gen.emit_add_reg_imm(4, 8);  // Restore stack
            result_type = DataType::VOID;
        } else {
            throw std::runtime_error("Unknown console method: " + method_name);
        }
    } else if (object_name == "Promise") {
        if (method_name == "all") {
            // Promise.all expects an array as its first argument
            if (arguments.size() > 0) {
                arguments[0]->generate_code(gen, types);
                // Move the array pointer to RDI (first argument register)
                gen.emit_mov_reg_reg(7, 0); // RDI = RAX
            } else {
                // No arguments, pass nullptr
                gen.emit_mov_reg_imm(7, 0); // RDI = 0 (nullptr)
            }
            gen.emit_call("__promise_all");
            result_type = DataType::PROMISE;
        } else {
            throw std::runtime_error("Unknown Promise method: " + method_name);
        }
    } else {
        // Handle variable method calls (like array.push())
        DataType object_type = types.get_variable_type(object_name);
        
        // Debug output to see what type we get
        // std::cout << "' has type " << static_cast<int>(object_type) << std::endl;
        
        if (object_type == DataType::TENSOR) {
            // Handle array/tensor methods
            if (method_name == "push") {
                // Get the proper offset for the array variable
                int64_t array_offset = types.get_variable_offset(object_name);
                gen.emit_mov_reg_mem(2, array_offset); // Load array pointer from proper offset
                
                // Call array push for each argument
                for (size_t i = 0; i < arguments.size(); i++) {
                    // Save array pointer to a temporary stack location before evaluating argument
                    // Use a safe offset that doesn't conflict with variables
                    gen.emit_mov_mem_reg(-32, 2); // Save array pointer to stack
                    
                    // Generate code for the argument (this may be a goroutine call)
                    arguments[i]->generate_code(gen, types);
                    
                    // Restore array pointer and set up call parameters
                    gen.emit_mov_reg_mem(7, -32); // RDI = array pointer from stack
                    gen.emit_mov_reg_reg(6, 0); // RSI = value to push
                    gen.emit_call("__array_push");
                }
                result_type = DataType::VOID;
            } else {
                throw std::runtime_error("Unknown array method: " + method_name);
            }
        } else if (object_type == DataType::ARRAY) {
            // Handle simplified Array methods
            if (method_name == "push") {
                // Get the array variable offset
                int64_t array_offset = types.get_variable_offset(object_name);
                
                // For each argument, call push
                for (size_t i = 0; i < arguments.size(); i++) {
                    // Load array pointer
                    gen.emit_mov_reg_mem(7, array_offset); // RDI = array pointer
                    
                    // Generate argument value
                    arguments[i]->generate_code(gen, types);
                    
                    // Move argument to RSI (second parameter)
                    gen.emit_mov_reg_reg(6, 0); // RSI = value (from RAX)
                    
                    // Call simplified array push
                    gen.emit_call("__simple_array_push");
                }
                result_type = DataType::VOID;
            } else if (method_name == "pop") {
                // Get the array variable offset
                int64_t array_offset = types.get_variable_offset(object_name);
                gen.emit_mov_reg_mem(7, array_offset); // RDI = array pointer
                gen.emit_call("__simple_array_pop");
                result_type = DataType::NUMBER;
            } else if (method_name == "slice") {
                // Get the array variable offset
                int64_t array_offset = types.get_variable_offset(object_name);
                gen.emit_mov_reg_mem(7, array_offset); // RDI = array pointer
                
                // Handle arguments: start, end, step (with defaults)
                if (arguments.size() >= 1) {
                    arguments[0]->generate_code(gen, types);
                    gen.emit_mov_reg_reg(6, 0); // RSI = start
                } else {
                    gen.emit_mov_reg_imm(6, 0); // RSI = 0 (default start)
                }
                
                if (arguments.size() >= 2) {
                    arguments[1]->generate_code(gen, types);
                    gen.emit_mov_reg_reg(2, 0); // RDX = end
                } else {
                    gen.emit_mov_reg_imm(2, -1); // RDX = -1 (default end)
                }
                
                if (arguments.size() >= 3) {
                    arguments[2]->generate_code(gen, types);
                    gen.emit_mov_reg_reg(1, 0); // RCX = step
                } else {
                    gen.emit_mov_reg_imm(1, 1); // RCX = 1 (default step)
                }
                
                gen.emit_call("__simple_array_slice");
                result_type = DataType::ARRAY;
            } else if (method_name == "slice_all") {
                // Get the array variable offset
                int64_t array_offset = types.get_variable_offset(object_name);
                gen.emit_mov_reg_mem(7, array_offset); // RDI = array pointer
                gen.emit_call("__simple_array_slice_all");
                result_type = DataType::ARRAY;
            } else if (method_name == "toString") {
                // Get the array variable offset
                int64_t array_offset = types.get_variable_offset(object_name);
                gen.emit_mov_reg_mem(7, array_offset); // RDI = array pointer
                gen.emit_call("__simple_array_tostring");
                result_type = DataType::STRING;
            } else if (method_name == "sum") {
                // Get the array variable offset
                int64_t array_offset = types.get_variable_offset(object_name);
                gen.emit_mov_reg_mem(7, array_offset); // RDI = array pointer
                gen.emit_call("__simple_array_sum");
                result_type = DataType::NUMBER;
            } else if (method_name == "mean") {
                // Get the array variable offset
                int64_t array_offset = types.get_variable_offset(object_name);
                gen.emit_mov_reg_mem(7, array_offset); // RDI = array pointer
                gen.emit_call("__simple_array_mean");
                result_type = DataType::NUMBER;
            } else if (method_name == "max") {
                // Get the array variable offset
                int64_t array_offset = types.get_variable_offset(object_name);
                gen.emit_mov_reg_mem(7, array_offset); // RDI = array pointer
                gen.emit_call("__simple_array_max");
                result_type = DataType::NUMBER;
            } else if (method_name == "min") {
                // Get the array variable offset
                int64_t array_offset = types.get_variable_offset(object_name);
                gen.emit_mov_reg_mem(7, array_offset); // RDI = array pointer
                gen.emit_call("__simple_array_min");
                result_type = DataType::NUMBER;
            } else {
                throw std::runtime_error("Unknown Array method: " + method_name);
            }
        } else if (object_type == DataType::REGEX) {
            // Handle regex methods like test, exec
            if (method_name == "test") {
                // Get the regex variable offset
                int64_t regex_offset = types.get_variable_offset(object_name);
                
                // Load regex pointer from stack
                gen.emit_mov_reg_mem(0, regex_offset); // RAX = [RBP + offset]
                gen.emit_mov_reg_reg(12, 0); // R12 = RAX (save in callee-saved register)
                
                
                if (arguments.size() > 0) {
                    arguments[0]->generate_code(gen, types);
                    gen.emit_mov_reg_reg(6, 0); // RSI = string pointer (RAX has the string)
                    gen.emit_mov_reg_reg(7, 12); // RDI = R12 (restore regex pointer)
                    
                    gen.emit_call("__regex_test");
                    result_type = DataType::BOOLEAN;
                } else {
                    throw std::runtime_error("RegExp.test() requires a string argument");
                }
            } else if (method_name == "exec") {
                // SAFE APPROACH: Use callee-saved register to preserve regex pointer
                int64_t regex_offset = types.get_variable_offset(object_name);
                
                // Load regex pointer and save in callee-saved register
                gen.emit_mov_reg_mem(0, regex_offset); // RAX = [RBP + offset]
                gen.emit_mov_reg_reg(12, 0); // R12 = RAX (save in callee-saved register)
                
                if (arguments.size() > 0) {
                    // Generate string argument (this may call __string_intern)
                    arguments[0]->generate_code(gen, types);
                    
                    // Set up function call parameters from preserved registers
                    gen.emit_mov_reg_reg(6, 0);  // RSI = string pointer from RAX
                    gen.emit_mov_reg_reg(7, 12); // RDI = regex pointer from R12
                    
                    gen.emit_call("__regex_exec");
                    result_type = DataType::TENSOR; // Match object/array
                } else {
                    throw std::runtime_error("RegExp.exec() requires a string argument");
                }
            } else {
                throw std::runtime_error("Unknown regex method: " + method_name);
            }
        } else if (object_type == DataType::STRING) {
            // Handle string methods like match, replace, search, split
            // std::cout << "' method '" << method_name << "'" << std::endl;
            if (method_name == "match") {
                // Get the string variable offset and load the string pointer
                int64_t string_offset = types.get_variable_offset(object_name);
                gen.emit_mov_reg_mem(0, string_offset); // Load string pointer
                gen.emit_mov_mem_reg(-8, 0); // Save string at RBP-8
                
                if (arguments.size() > 0) {
                    // Generate code for regex argument
                    arguments[0]->generate_code(gen, types);
                    
                    // Set up call: string_match(string_ptr, regex_ptr)
                    gen.emit_mov_reg_mem(7, -8);  // RDI = string pointer
                    gen.emit_mov_reg_reg(6, 0);   // RSI = regex pointer
                    gen.emit_call("__string_match");
                    result_type = DataType::TENSOR; // Array of matches
                } else {
                    throw std::runtime_error("String.match() requires a regex argument");
                }
            } else {
                throw std::runtime_error("Unknown string method: " + method_name);
            }
        } else if (object_type == DataType::UNKNOWN) {
            // If object_name is not a variable, it might be a static method call
            // Generate static method call: ClassName.methodName()
            
            // Special handling for Array static methods
            if (object_name == "Array") {
                if (method_name == "zeros") {
                    // Handle Array.zeros([shape]) 
                    if (arguments.size() >= 1) {
                        // Generate the shape array argument
                        arguments[0]->generate_code(gen, types);
                        // RAX now contains the shape array, extract first dimension
                        gen.emit_mov_reg_reg(7, 0); // RDI = shape array
                        gen.emit_call("__simple_array_get_first_dimension");
                        gen.emit_mov_reg_reg(7, 0); // RDI = first dimension size
                    } else {
                        gen.emit_mov_reg_imm(7, 0); // RDI = 0 (empty array)
                    }
                    gen.emit_call("__simple_array_zeros");
                    result_type = DataType::ARRAY;
                    return;
                } else if (method_name == "ones") {
                    // Handle Array.ones([shape])
                    if (arguments.size() >= 1) {
                        // Generate the shape array argument
                        arguments[0]->generate_code(gen, types);
                        // RAX now contains the shape array, extract first dimension
                        gen.emit_mov_reg_reg(7, 0); // RDI = shape array
                        gen.emit_call("__simple_array_get_first_dimension");
                        gen.emit_mov_reg_reg(7, 0); // RDI = first dimension size
                    } else {
                        gen.emit_mov_reg_imm(7, 0); // RDI = 0 (empty array)
                    }
                    gen.emit_call("__simple_array_ones");
                    result_type = DataType::ARRAY;
                    return;
                } else if (method_name == "arange") {
                    // Handle Array.arange(start, stop, step)
                    if (arguments.size() >= 2) {
                        arguments[0]->generate_code(gen, types);
                        gen.emit_mov_mem_reg(-8, 0); // Save start
                        arguments[1]->generate_code(gen, types);
                        gen.emit_mov_mem_reg(-16, 0); // Save stop
                        
                        gen.emit_mov_reg_mem(7, -8); // RDI = start
                        gen.emit_mov_reg_mem(6, -16); // RSI = stop
                        
                        if (arguments.size() >= 3) {
                            arguments[2]->generate_code(gen, types);
                            gen.emit_mov_reg_reg(2, 0); // RDX = step
                        } else {
                            gen.emit_mov_reg_imm(2, 1); // RDX = 1 (default step)
                        }
                        
                        gen.emit_call("__simple_array_arange");
                        result_type = DataType::ARRAY;
                        return;
                    }
                } else if (method_name == "linspace") {
                    // Handle Array.linspace(start, stop, num)
                    if (arguments.size() >= 2) {
                        arguments[0]->generate_code(gen, types);
                        gen.emit_mov_mem_reg(-8, 0); // Save start
                        arguments[1]->generate_code(gen, types);
                        gen.emit_mov_mem_reg(-16, 0); // Save stop
                        
                        gen.emit_mov_reg_mem(7, -8); // RDI = start
                        gen.emit_mov_reg_mem(6, -16); // RSI = stop
                        
                        if (arguments.size() >= 3) {
                            arguments[2]->generate_code(gen, types);
                            gen.emit_mov_reg_reg(2, 0); // RDX = num
                        } else {
                            gen.emit_mov_reg_imm(2, 50); // RDX = 50 (default)
                        }
                        
                        gen.emit_call("__simple_array_linspace");
                        result_type = DataType::ARRAY;
                        return;
                    }
                }
            }
            
            std::string static_method_label = "__static_" + method_name;
            
            // Set up arguments for static method call (no 'this' parameter)
            for (size_t i = 0; i < arguments.size() && i < 6; i++) {
                arguments[i]->generate_code(gen, types);
                
                // Store argument in temporary stack location
                gen.emit_mov_mem_reg(-(int64_t)(i + 1) * 8, 0);
            }
            
            // Load arguments into registers
            for (size_t i = 0; i < arguments.size() && i < 6; i++) {
                switch (i) {
                    case 0: gen.emit_mov_reg_mem(7, -8); break;   // RDI
                    case 1: gen.emit_mov_reg_mem(6, -16); break;  // RSI
                    case 2: gen.emit_mov_reg_mem(2, -24); break;  // RDX
                    case 3: gen.emit_mov_reg_mem(1, -32); break;  // RCX
                    case 4: gen.emit_mov_reg_mem(8, -40); break;  // R8
                    case 5: gen.emit_mov_reg_mem(9, -48); break;  // R9
                }
            }
            
            // Call the static method
            gen.emit_call(static_method_label);
            
            result_type = DataType::UNKNOWN; // TODO: Get actual return type from method signature
            
            // std::cout << object_name << "." << method_name << " at label " << static_method_label << std::endl;
        } else {
            // Check if this is a class instance method call
            DataType object_type = types.get_variable_type(object_name);
            std::string class_name = types.get_variable_class_name(object_name);
            
            if (object_type == DataType::CLASS_INSTANCE && !class_name.empty()) {
                // Get object ID from variable
                int64_t object_offset = types.get_variable_offset(object_name);
                gen.emit_mov_reg_mem(0, object_offset); // RAX = object_id
                
                // Call method via object system
                // For now, just call __object_call_method with basic setup
                gen.emit_mov_reg_reg(7, 0); // RDI = object_id
                
                // Call the generated method function directly
                std::string method_label = "__method_" + method_name;
                gen.emit_call(method_label);
                
                result_type = DataType::UNKNOWN; // TODO: Get actual return type from method signature
                
                // std::cout << class_name << "::" << object_name << "." << method_name << std::endl;
            } else {
                // Unknown object type
                gen.emit_mov_reg_imm(0, 0);
                // std::cout << object_name << "." << method_name << std::endl;
                result_type = DataType::UNKNOWN;
            }
        }
    }
    
    if (is_awaited) {
        gen.emit_promise_await(0);
    }
}

void FunctionExpression::generate_code(CodeGenerator& gen, TypeInference& types) {
    // NEW THREE-PHASE SYSTEM: Function should already be compiled in Phase 2
    // During Phase 3, we just generate the appropriate code to reference the function
    
    
    // The function should already be registered in the FunctionCompilationManager
    // We need to find its name and address
    std::string func_name = compilation_assigned_name_;
    if (func_name.empty()) {
        std::cerr << "ERROR: Function expression at " << this << " has no assigned name during Phase 3!" << std::endl;
        std::cerr << "ERROR: Current compilation_assigned_name_: '" << compilation_assigned_name_ << "'" << std::endl;
        throw std::runtime_error("Function not properly registered in compilation manager");
    }
    
    // ULTRA-FAST SYSTEM: Try direct address first, then relative offset, fallback to function ID
    void* func_address = FunctionCompilationManager::instance().get_function_address(func_name);
    
    if (func_address) {
        // OPTIMAL PATH: Direct address call - zero overhead
        
        if (is_goroutine) {
            // ULTRA-OPTIMIZED: Direct goroutine spawn with address
            if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                x86_gen->emit_goroutine_spawn_direct(func_address);
            }
            result_type = DataType::PROMISE;
        } else {
            // ULTRA-OPTIMIZED: Direct function address return (no lookup needed)
            gen.emit_mov_reg_imm(0, reinterpret_cast<int64_t>(func_address)); // RAX = function address
            result_type = DataType::FUNCTION;
        }
    } else {
        // SECOND BEST PATH: Try relative offset calculation (address = base + offset)
        size_t func_offset = FunctionCompilationManager::instance().get_function_offset(func_name);
        
        if (FunctionCompilationManager::instance().is_function_compiled(func_name)) {
            // NEAR-OPTIMAL PATH: Calculate address at runtime (base + offset)
            
            if (is_goroutine) {
                // Calculate function address as exec_memory_base + offset
                if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                    // Use RIP-relative addressing to calculate function address
                    // This is still very fast - just one LEA instruction
                    x86_gen->emit_goroutine_spawn_with_offset(func_offset);
                }
                result_type = DataType::PROMISE;
            } else {
                // Calculate function address as exec_memory_base + offset  
                if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                    x86_gen->emit_calculate_function_address_from_offset(func_offset);
                }
                result_type = DataType::FUNCTION;
            }
        } else {
            // FALLBACK PATH: Use function ID (should rarely happen with proper phase ordering)
            uint16_t func_id = FunctionCompilationManager::instance().get_function_id(func_name);
            if (func_id == 0) {
                std::cerr << "ERROR: Function " << func_name << " not found in either address or ID registry!" << std::endl;
                throw std::runtime_error("Function not found in fast function registry");
            }
        
        
        if (is_goroutine) {
            // Fallback: Use fast spawn with function ID
            if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
                x86_gen->emit_goroutine_spawn_fast(func_id);
            }
            result_type = DataType::PROMISE;
        } else {
            // Fallback: Use fast lookup with function ID
            
            // Load function ID into RDI and call fast lookup
            gen.emit_mov_reg_imm(7, static_cast<int64_t>(func_id)); // RDI = function ID
            gen.emit_call("__lookup_function_fast"); // Fast O(1) lookup
            result_type = DataType::FUNCTION;
        }
        }
    }
}


void FunctionExpression::compile_function_body(CodeGenerator& gen, TypeInference& types, const std::string& func_name) {
    // Safety check for corrupted function name
    if (func_name.empty() || func_name.size() > 1000) {
        std::cerr << "ERROR: Invalid function name detected, skipping compilation" << std::endl;
        return;
    }
    
    // Save current stack offset state
    TypeInference local_types;
    local_types.reset_for_function();
    
    // Emit function label
    gen.emit_label(func_name);
    
    // Calculate estimated stack size for the function
    int64_t estimated_stack_size = (parameters.size() * 8) + (body.size() * 16) + 64;
    if (estimated_stack_size < 80) estimated_stack_size = 80;
    if (estimated_stack_size % 16 != 0) {
        estimated_stack_size += 16 - (estimated_stack_size % 16);
    }
    
    // Set stack size for this function
    if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
        x86_gen->set_function_stack_size(estimated_stack_size);
    }
    
    gen.emit_prologue();
    
    // Set up parameter types and save parameters from registers to stack
    for (size_t i = 0; i < parameters.size() && i < 6; i++) {
        const auto& param = parameters[i];
        local_types.set_variable_type(param.name, param.type);
        
        int stack_offset = -(int)(i + 1) * 8;
        local_types.set_variable_offset(param.name, stack_offset);
        
        switch (i) {
            case 0: gen.emit_mov_mem_reg(stack_offset, 7); break;  // save RDI
            case 1: gen.emit_mov_mem_reg(stack_offset, 6); break;  // save RSI
            case 2: gen.emit_mov_mem_reg(stack_offset, 2); break;  // save RDX
            case 3: gen.emit_mov_mem_reg(stack_offset, 1); break;  // save RCX
            case 4: gen.emit_mov_mem_reg(stack_offset, 8); break;  // save R8
            case 5: gen.emit_mov_mem_reg(stack_offset, 9); break;  // save R9
        }
    }
    
    // Generate function body
    bool has_explicit_return = false;
    for (size_t i = 0; i < body.size(); i++) {
        const auto& stmt = body[i];
        
        // Safety check for null pointers
        if (!stmt) {
            std::cout << "ERROR: Statement " << i << " is null!" << std::endl;
            continue;
        }
        
        try {
            stmt->generate_code(gen, local_types);
        } catch (const std::exception& e) {
            std::cout << "ERROR: Statement " << i << " threw exception: " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cout << "ERROR: Statement " << i << " threw unknown exception" << std::endl;
            throw;
        }
        
        if (dynamic_cast<const ReturnStatement*>(stmt.get())) {
            has_explicit_return = true;
        }
    }
    
    // If no explicit return, add implicit return 0
    if (!has_explicit_return) {
        gen.emit_mov_reg_imm(0, 0);  // mov rax, 0
        gen.emit_function_return();
    }
    
    // Safety check before final debug output
    if (!func_name.empty() && func_name.size() <= 1000) {
    }
}

// Shared registry for function ID to name mapping - used by both registration and lookup
static std::unordered_map<int64_t, std::string>& get_function_id_registry() {
    static std::unordered_map<int64_t, std::string> shared_function_registry;
    return shared_function_registry;
}

// Global function to look up function names by ID for runtime callbacks
extern "C" const char* __lookup_function_name_by_id(int64_t function_id) {
    auto& registry = get_function_id_registry();
    auto it = registry.find(function_id);
    if (it != registry.end()) {
        return it->second.c_str();
    }
    return nullptr;
}

// Global function to look up function address by ID for JIT calls
extern "C" void* __lookup_function_by_id(int64_t function_id) {
    // Direct fast lookup using function ID (assumes function_id is valid uint16_t)
    if (function_id >= 0 && function_id <= 65535) {
        return __lookup_function_fast(static_cast<uint16_t>(function_id));
    }
    std::cout << "ERROR: Function ID " << function_id << " out of range!" << std::endl;
    return nullptr;
}

// Function to register a function ID with its name (called from generate_code)
void __register_function_id(int64_t function_id, const std::string& function_name) {
    auto& registry = get_function_id_registry();
    registry[function_id] = function_name;
}

// Register our function in the runtime on first use
static bool __lookup_function_by_id_registered = false;
void ensure_lookup_function_by_id_registered() {
    if (!__lookup_function_by_id_registered) {
        __register_function_fast(reinterpret_cast<void*>(__lookup_function_by_id), 1, 0);
        __lookup_function_by_id_registered = true;
    }
}

void ExpressionMethodCall::generate_code(CodeGenerator& gen, TypeInference& types) {
    
    // OPTIMIZATION: Check if this is runtime.x.y() pattern (like runtime.time.now())
    ExpressionPropertyAccess* expr_prop = dynamic_cast<ExpressionPropertyAccess*>(object.get());
    if (expr_prop) {
        
        // Check if the inner object is an Identifier with name "runtime"
        Identifier* runtime_ident = dynamic_cast<Identifier*>(expr_prop->object.get());
        if (runtime_ident && runtime_ident->name == "runtime") {
            // This is runtime.x.y() - generate direct function call
            std::string sub_object = expr_prop->property_name;  // e.g., "time"
            std::string function_name = "__runtime_" + sub_object + "_" + method_name;
            
            // Map common patterns to actual function names
            if (sub_object == "time" && method_name == "now") {
                function_name = "__runtime_time_now_millis";
            } else if (sub_object == "time" && method_name == "nowNanos") {
                function_name = "__runtime_time_now_nanos";
            } else if (sub_object == "process" && method_name == "pid") {
                function_name = "__runtime_process_pid";
            } else if (sub_object == "process" && method_name == "cwd") {
                function_name = "__runtime_process_cwd";
            } else if (sub_object == "timer" && method_name == "setTimeout") {
                function_name = "__gots_set_timeout";
            } else if (sub_object == "timer" && method_name == "clearTimeout") {
                function_name = "__gots_clear_timeout";
            } else if (sub_object == "timer" && method_name == "setInterval") {
                function_name = "__gots_set_interval";
            } else if (sub_object == "timer" && method_name == "clearInterval") {
                function_name = "__gots_clear_interval";
            }
            // Add more mappings as needed
            
            // std::cout << " -> " << function_name << std::endl;
            
            // Generate argument code using proper x86-64 calling convention
            for (size_t i = 0; i < arguments.size() && i < 6; i++) {
                arguments[i]->generate_code(gen, types);
                // Move argument to appropriate register (x86-64 calling convention)
                switch (i) {
                    case 0: 
                        gen.emit_mov_reg_reg(7, 0); // RDI = RAX (1st arg)
                        break;
                    case 1: 
                        gen.emit_mov_reg_reg(6, 0); // RSI = RAX (2nd arg)
                        break;  
                    case 2: gen.emit_mov_reg_reg(2, 0); break;  // RDX = RAX (3rd arg)
                    case 3: gen.emit_mov_reg_reg(1, 0); break;  // RCX = RAX (4th arg)
                    case 4: gen.emit_mov_reg_reg(8, 0); break;  // R8 = RAX (5th arg)
                    case 5: gen.emit_mov_reg_reg(9, 0); break;  // R9 = RAX (6th arg)
                }
            }
            
            // Generate the optimized direct function call
            gen.emit_call(function_name);
            
            // Set appropriate result type based on the method
            if (sub_object == "time" && (method_name == "now" || method_name == "nowNanos")) {
                result_type = DataType::INT64;
            } else if (sub_object == "process" && method_name == "cwd") {
                result_type = DataType::STRING;
            } else if (sub_object == "timer" && (method_name == "setTimeout" || method_name == "setInterval" || method_name == "setImmediate")) {
                result_type = DataType::INT64; // Timer ID
            } else if (sub_object == "timer" && (method_name == "clearTimeout" || method_name == "clearInterval" || method_name == "clearImmediate")) {
                result_type = DataType::BOOLEAN; // Success/failure
            } else {
                result_type = DataType::UNKNOWN;
            }
            
            return; // Skip normal method call handling
        }
    }
    
    // First, generate code for the object expression and get its result
    object->generate_code(gen, types);
    DataType object_type = object->result_type;
    
    // Handle different types of objects for method calls
    if (object_type == DataType::STRING) {
        // Handle string methods like match, replace, search, split
        if (method_name == "match") {
            // String.match() method - returns array of matches
            // Save string pointer to stack location
            gen.emit_mov_mem_reg(-8, 0); // Save string at RBP-8
            
            if (arguments.size() > 0) {
                // Generate code for regex argument
                arguments[0]->generate_code(gen, types);
                
                // Set up call: string_match(string_ptr, regex_ptr)
                gen.emit_mov_reg_mem(7, -8);  // RDI = string pointer
                gen.emit_mov_reg_reg(6, 0);   // RSI = regex pointer
                gen.emit_call("__string_match");
                result_type = DataType::TENSOR; // Array of matches
            } else {
                throw std::runtime_error("String.match() requires a regex argument");
            }
        } else if (method_name == "replace") {
            // String.replace() method
            gen.emit_mov_mem_reg(-8, 0); // Save string at RBP-8
            
            if (arguments.size() >= 2) {
                // Generate code for pattern (regex or string)
                arguments[0]->generate_code(gen, types);
                gen.emit_mov_mem_reg(-16, 0); // Save pattern at RBP-16
                
                // Generate code for replacement string
                arguments[1]->generate_code(gen, types);
                
                // Set up call: string_replace(string_ptr, pattern_ptr, replacement_ptr)
                gen.emit_mov_reg_mem(7, -8);   // RDI = string pointer
                gen.emit_mov_reg_mem(6, -16);  // RSI = pattern pointer
                gen.emit_mov_reg_reg(2, 0);    // RDX = replacement pointer
                gen.emit_call("__string_replace");
                result_type = DataType::STRING;
            } else {
                throw std::runtime_error("String.replace() requires pattern and replacement arguments");
            }
        } else if (method_name == "search") {
            // String.search() method - returns index of first match
            gen.emit_mov_mem_reg(-8, 0); // Save string at RBP-8
            
            if (arguments.size() > 0) {
                arguments[0]->generate_code(gen, types);
                
                gen.emit_mov_reg_mem(7, -8);  // RDI = string pointer  
                gen.emit_mov_reg_reg(6, 0);   // RSI = regex pointer
                gen.emit_call("__string_search");
                result_type = DataType::NUMBER; // Index or -1
            } else {
                throw std::runtime_error("String.search() requires a regex argument");
            }
        } else if (method_name == "split") {
            // String.split() method - returns array of strings
            gen.emit_mov_mem_reg(-8, 0); // Save string at RBP-8
            
            if (arguments.size() > 0) {
                arguments[0]->generate_code(gen, types);
                
                gen.emit_mov_reg_mem(7, -8);  // RDI = string pointer
                gen.emit_mov_reg_reg(6, 0);   // RSI = delimiter/regex pointer
                gen.emit_call("__string_split");
                result_type = DataType::TENSOR; // Array of strings
            } else {
                throw std::runtime_error("String.split() requires a delimiter argument");
            }
        } else {
            throw std::runtime_error("Unknown string method: " + method_name);
        }
    } else if (object_type == DataType::REGEX) {
        // Handle regex methods like test, exec
        if (method_name == "test") {
            gen.emit_mov_mem_reg(-8, 0); // Save regex at RBP-8
            
            if (arguments.size() > 0) {
                arguments[0]->generate_code(gen, types);
                
                gen.emit_mov_reg_mem(7, -8);  // RDI = regex pointer
                gen.emit_mov_reg_reg(6, 0);   // RSI = string pointer
                gen.emit_call("__regex_test");
                result_type = DataType::BOOLEAN;
            } else {
                throw std::runtime_error("RegExp.test() requires a string argument");
            }
        } else if (method_name == "exec") {
            gen.emit_mov_mem_reg(-8, 0); // Save regex at RBP-8
            
            if (arguments.size() > 0) {
                arguments[0]->generate_code(gen, types);
                
                gen.emit_mov_reg_mem(7, -8);  // RDI = regex pointer
                gen.emit_mov_reg_reg(6, 0);   // RSI = string pointer
                gen.emit_call("__regex_exec");
                result_type = DataType::TENSOR; // Match object/array
            } else {
                throw std::runtime_error("RegExp.exec() requires a string argument");
            }
        } else {
            throw std::runtime_error("Unknown regex method: " + method_name);
        }
    } else if (object_type == DataType::TENSOR) {
        // Handle array/tensor methods
        if (method_name == "push") {
            gen.emit_mov_mem_reg(-8, 0); // Save array pointer
            
            for (size_t i = 0; i < arguments.size(); i++) {
                arguments[i]->generate_code(gen, types);
                
                gen.emit_mov_reg_mem(7, -8);  // RDI = array pointer
                gen.emit_mov_reg_reg(6, 0);   // RSI = value to push
                gen.emit_call("__array_push");
            }
            result_type = DataType::VOID;
        } else if (method_name == "pop") {
            gen.emit_mov_reg_reg(7, 0);   // RDI = array pointer
            gen.emit_call("__array_pop");
            result_type = DataType::NUMBER; // Popped value
        } else {
            throw std::runtime_error("Unknown array method: " + method_name);
        }
    } else {
        // For other types, try a generic method call
        // This is a fallback for custom objects or future types
        gen.emit_mov_mem_reg(-8, 0); // Save object pointer
        
        // For now, we'll emit a placeholder call
        // In a full implementation, this would do dynamic method lookup
        std::string method_label = "__dynamic_method_" + method_name;
        gen.emit_mov_reg_mem(7, -8);  // RDI = object pointer
        
        // Set up arguments
        for (size_t i = 0; i < arguments.size() && i < 5; i++) {
            arguments[i]->generate_code(gen, types);
            gen.emit_mov_mem_reg(-(int64_t)(i + 2) * 8, 0); // Save to stack
        }
        
        // Load arguments into registers (starting from RSI since RDI has object)
        for (size_t i = 0; i < arguments.size() && i < 5; i++) {
            switch (i) {
                case 0: gen.emit_mov_reg_mem(6, -16); break;  // RSI
                case 1: gen.emit_mov_reg_mem(2, -24); break;  // RDX
                case 2: gen.emit_mov_reg_mem(1, -32); break;  // RCX
                case 3: gen.emit_mov_reg_mem(8, -40); break;  // R8
                case 4: gen.emit_mov_reg_mem(9, -48); break;  // R9
            }
        }
        
        gen.emit_call(method_label);
        result_type = DataType::UNKNOWN; // Unknown return type for dynamic calls
    }
    
    if (is_awaited) {
        gen.emit_promise_await(0);
    }
}

void ArrayLiteral::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Create empty array
    gen.emit_mov_reg_imm(7, 0);  // RDI = 0 (empty array)
    gen.emit_call("__simple_array_zeros");  // Creates empty array
    
    // Save array pointer on stack
    gen.emit_sub_reg_imm(4, 8);   // sub rsp, 8
    X86CodeGen* x86_gen = dynamic_cast<X86CodeGen*>(&gen);
    if (x86_gen) {
        x86_gen->emit_mov_mem_rsp_reg(0, 0);   // mov [rsp], rax
    }
    
    // Push each element into the array
    for (const auto& element : elements) {
        element->generate_code(gen, types);
        gen.emit_mov_reg_reg(6, 0); // RSI = value to push
        
        // Get array pointer from stack
        if (x86_gen) {
            x86_gen->emit_mov_reg_mem_rsp(7, 0);   // mov rdi, [rsp]
        }
        
        // Call __simple_array_push(array_ptr, value)
        gen.emit_call("__simple_array_push");
    }
    
    // Return the array pointer in RAX
    if (x86_gen) {
        x86_gen->emit_mov_reg_mem_rsp(0, 0);   // mov rax, [rsp]
    }
    gen.emit_add_reg_imm(4, 8);   // add rsp, 8
    
    result_type = DataType::ARRAY;
}

void ObjectLiteral::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Create an object using the existing runtime object system
    // Use a special class name for object literals
    
    // Create string literal for the object literal class name
    static const char* object_literal_class = "ObjectLiteral";
    
    // Call __object_create with class name and property count
    gen.emit_mov_reg_imm(7, reinterpret_cast<int64_t>(object_literal_class)); // RDI = class_name
    gen.emit_mov_reg_imm(6, properties.size()); // RSI = property count
    gen.emit_call("__object_create");
    
    // RAX now contains the object_id
    // Store it temporarily while we add properties
    int64_t object_offset = types.allocate_variable("__temp_object_" + std::to_string(rand()), DataType::CLASS_INSTANCE);
    gen.emit_mov_mem_reg(object_offset, 0); // Save object_id
    
    // Add each property to the object using property indices
    for (size_t i = 0; i < properties.size(); i++) {
        const auto& prop = properties[i];
        
        // First set the property name
        // Store property name in static storage for the runtime call
        static std::unordered_map<std::string, const char*> property_name_storage;
        auto it = property_name_storage.find(prop.first);
        const char* name_ptr;
        if (it != property_name_storage.end()) {
            name_ptr = it->second;
        } else {
            // Allocate permanent storage for this property name
            char* permanent_name = new char[prop.first.length() + 1];
            strcpy(permanent_name, prop.first.c_str());
            property_name_storage[prop.first] = permanent_name;
            name_ptr = permanent_name;
        }
        
        // Call __object_set_property_name(object_id, property_index, property_name)
        gen.emit_mov_reg_mem(7, object_offset); // RDI = object_id
        gen.emit_mov_reg_imm(6, i); // RSI = property_index
        gen.emit_mov_reg_imm(2, reinterpret_cast<int64_t>(name_ptr)); // RDX = property_name
        gen.emit_call("__object_set_property_name");
        
        // Generate code for the property value
        prop.second->generate_code(gen, types);
        
        // Set up call to __object_set_property(object_id, property_index, value)
        gen.emit_mov_reg_reg(2, 0); // RDX = value (save from RAX)
        gen.emit_mov_reg_mem(7, object_offset); // RDI = object_id
        gen.emit_mov_reg_imm(6, i); // RSI = property_index
        gen.emit_call("__object_set_property");
    }
    
    // Return the object_id in RAX
    gen.emit_mov_reg_mem(0, object_offset);
    result_type = DataType::CLASS_INSTANCE; // Objects are class instances
}

void TypedArrayLiteral::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Create typed array with initial capacity - maximum performance
    gen.emit_mov_reg_imm(7, elements.size() > 0 ? elements.size() : 8); // RDI = initial capacity
    
    // Call appropriate typed array creation function based on type
    switch (array_type) {
        case DataType::INT32:
            gen.emit_call("__typed_array_create_int32");
            break;
        case DataType::INT64:
            gen.emit_call("__typed_array_create_int64");
            break;
        case DataType::FLOAT32:
            gen.emit_call("__typed_array_create_float32");
            break;
        case DataType::FLOAT64:
        // Note: DataType::NUMBER is an alias for FLOAT64, so it's automatically handled here
            gen.emit_call("__typed_array_create_float64");
            break;
        case DataType::UINT8:
            gen.emit_call("__typed_array_create_uint8");
            break;
        case DataType::UINT16:
            gen.emit_call("__typed_array_create_uint16");
            break;
        case DataType::UINT32:
            gen.emit_call("__typed_array_create_uint32");
            break;
        case DataType::UINT64:
            gen.emit_call("__typed_array_create_uint64");
            break;
        default:
            throw std::runtime_error("Unsupported typed array type");
    }
    
    gen.emit_mov_mem_reg(-16, 0); // Save array pointer on stack
    
    // Push each element into the typed array using appropriate typed push function
    for (const auto& element : elements) {
        element->generate_code(gen, types);
        gen.emit_mov_reg_mem(7, -16); // RDI = array pointer from stack
        gen.emit_mov_reg_reg(6, 0); // RSI = value to push
        
        // Call appropriate push function based on type for maximum performance
        switch (array_type) {
            case DataType::INT32:
                gen.emit_call("__typed_array_push_int32");
                break;
            case DataType::INT64:
                gen.emit_call("__typed_array_push_int64");
                break;
            case DataType::FLOAT32:
                gen.emit_call("__typed_array_push_float32");
                break;
            case DataType::FLOAT64:
            // Note: DataType::NUMBER is an alias for FLOAT64, so it's automatically handled here
                gen.emit_call("__typed_array_push_float64");
                break;
            case DataType::UINT8:
                gen.emit_call("__typed_array_push_uint8");
                break;
            case DataType::UINT16:
                gen.emit_call("__typed_array_push_uint16");
                break;
            case DataType::UINT32:
                gen.emit_call("__typed_array_push_uint32");
                break;
            case DataType::UINT64:
                gen.emit_call("__typed_array_push_uint64");
                break;
            default:
                throw std::runtime_error("Unsupported typed array type");
        }
    }
    
    // Return the array pointer in RAX
    gen.emit_mov_reg_mem(0, -16); // Load array pointer from stack
    result_type = array_type; // Set to the specific typed array type
}

void ArrayAccess::generate_code(CodeGenerator& gen, TypeInference& types) {
    
    // First check if the object is a class instance with operator[] overload
    bool use_operator_overload = false;
    std::string class_name;
    
    // Try to determine if object is a class instance
    if (auto* var_expr = dynamic_cast<Identifier*>(object.get())) {
        DataType var_type = types.get_variable_type(var_expr->name);
        if (var_type == DataType::CLASS_INSTANCE) {
            class_name = types.get_variable_class_name(var_expr->name);
            
            // Check if this class has operator[] or operator[:] overload
            auto* compiler = get_current_compiler();
            if (compiler) {
                bool has_bracket_overload = compiler->has_operator_overload(class_name, TokenType::LBRACKET);
                bool has_slice_overload = compiler->has_operator_overload(class_name, TokenType::SLICE_BRACKET);
                
                // Prefer slice operator for slice expressions, bracket operator otherwise
                if (is_slice_expression && has_slice_overload) {
                    use_operator_overload = true;
                } else if (!is_slice_expression && has_bracket_overload) {
                    use_operator_overload = true;
                } else if (has_bracket_overload) {
                    // Fallback to bracket operator if available
                    use_operator_overload = true;
                }
            } else {
            }
        } else if (var_type == DataType::ARRAY) {
            // Handle simplified Array access directly
            
            // Get the array variable offset
            int64_t array_offset = types.get_variable_offset(var_expr->name);
            gen.emit_mov_reg_mem(7, array_offset); // RDI = array pointer
            
            // Generate code for the index
            if (index) {
                index->generate_code(gen, types);
                gen.emit_mov_reg_reg(6, 0); // RSI = index
            } else if (!slices.empty()) {
                // For slice syntax, use slice function instead
                slices[0]->generate_code(gen, types);
                // TODO: Handle slice properly
                gen.emit_mov_reg_reg(6, 0); // RSI = slice object
            } else {
                gen.emit_mov_reg_imm(6, 0); // RSI = 0 (default index)
            }
            
            gen.emit_call("__simple_array_get");
            result_type = DataType::NUMBER;
            
            return;
        }
    }
    
    if (use_operator_overload) {
        // Determine the index expression as a string for type inference
        std::string index_expr_str = "";
        if (is_slice_expression) {
            index_expr_str = slice_expression;
        } else if (index) {
            // Extract the expression string using the helper method
            index_expr_str = types.extract_expression_string(index.get());
            if (index_expr_str.empty()) {
                index_expr_str = "complex_expression";
            }
        } else {
            // Handle case where we have slices but no index (new slice syntax)
            index_expr_str = "slice_expression";
        }
        
        // Use enhanced type inference to determine the best operator overload
        DataType index_type = types.infer_operator_index_type(class_name, index_expr_str);
        
        // Generate argument 0 (object)
        object->generate_code(gen, types);
        gen.emit_mov_reg_reg(7, 0);  // Move object to RDI (first parameter)
        
        // Generate argument 1 (index/string) and place in RSI
        if (is_slice_expression) {
            // For slice expressions, create a string literal directly
            auto string_literal = std::make_unique<StringLiteral>(slice_expression);
            string_literal->generate_code(gen, types);
        } else if (index) {
            // For normal expressions, evaluate them
            index->generate_code(gen, types);
        } else if (!slices.empty()) {
            // For new slice syntax, generate slice object code
            slices[0]->generate_code(gen, types);
        } else {
            // Fallback - generate a zero index
            gen.emit_mov_reg_imm(0, 0);
        }
        gen.emit_mov_reg_reg(6, 0);  // Move string/index to RSI (second parameter)
        
        // Find the best operator overload based on the inferred index type
        auto* compiler = get_current_compiler();
        if (compiler) {
            std::vector<DataType> operand_types = {index_type};
            // Choose the appropriate operator token based on whether it's a slice expression
            TokenType operator_token = (is_slice_expression && compiler->has_operator_overload(class_name, TokenType::SLICE_BRACKET)) 
                                     ? TokenType::SLICE_BRACKET 
                                     : TokenType::LBRACKET;
            const auto* best_overload = compiler->find_best_operator_overload(class_name, operator_token, operand_types);
            
            if (best_overload) {
                // Call the specific operator overload function
                std::string op_name = best_overload->function_name;
                gen.emit_call(op_name);
                result_type = best_overload->return_type;
            } else {
                // No typed overload found, try to fall back to ANY overload
                
                // Try ANY type overload as fallback
                std::vector<DataType> any_operand_types = {DataType::ANY};
                const auto* any_overload = compiler->find_best_operator_overload(class_name, operator_token, any_operand_types);
                
                if (any_overload) {
                    gen.emit_call(any_overload->function_name);
                    result_type = any_overload->return_type;
                } else {
                    // Last resort: try direct function name construction for compatibility
                    std::string param_signature;
                    if (is_slice_expression || index_type == DataType::STRING) {
                        param_signature = std::to_string(static_cast<int>(DataType::STRING)); // string parameter
                    } else {
                        param_signature = "any"; // ANY type parameter
                    }
                    
                    std::string op_function_name = class_name + "::__op_" + std::to_string(static_cast<int>(operator_token)) + "_any_" + param_signature + "__";
                    gen.emit_call(op_function_name);
                    result_type = DataType::CLASS_INSTANCE; // Assume operator overloads return class instances
                }
            }
        } else {
            result_type = DataType::UNKNOWN;
        }
    } else {
        // Standard array access
        // Generate code for the object expression
        object->generate_code(gen, types);
        
        // Save object on stack
        gen.emit_sub_reg_imm(4, 8);   // sub rsp, 8 (allocate stack space)
        X86CodeGen* x86_gen = dynamic_cast<X86CodeGen*>(&gen);
        if (x86_gen) {
            x86_gen->emit_mov_mem_rsp_reg(0, 0);   // mov [rsp], rax (save object on stack)
        }
        
        // Generate code for the index expression
        if (index) {
            index->generate_code(gen, types);
        } else if (!slices.empty()) {
            // For new slice syntax, generate slice object code
            slices[0]->generate_code(gen, types);
        } else {
            // Fallback - generate a zero index
            gen.emit_mov_reg_imm(0, 0);
        }
        gen.emit_mov_reg_reg(6, 0); // Move index to RSI
        
        // Pop object into RDI
        if (x86_gen) {
            x86_gen->emit_mov_reg_mem_rsp(7, 0);   // mov rdi, [rsp] (load object from stack)
        }
        gen.emit_add_reg_imm(4, 8);   // add rsp, 8 (restore stack)
        
        // Call array access function
        gen.emit_call("__array_access");
        
        // Result is in RAX
        result_type = DataType::UNKNOWN; // Array access returns unknown type for JavaScript compatibility
    }
    
}

void Assignment::generate_code(CodeGenerator& gen, TypeInference& types) {
    if (value) {
        value->generate_code(gen, types);
        
        DataType variable_type;
        if (declared_type != DataType::UNKNOWN) {
            // Explicitly typed variable - use the declared type for performance
            variable_type = declared_type;
        } else {
            // Untyped variable - infer type from value for arrays and other structured types
            // For simple values, keep as UNKNOWN for JavaScript compatibility
            if (value->result_type == DataType::TENSOR || value->result_type == DataType::STRING || 
                value->result_type == DataType::REGEX || value->result_type == DataType::FUNCTION ||
                value->result_type == DataType::ARRAY) {
                // Arrays, tensors, strings, regex, and functions should preserve their type for proper method dispatch
                variable_type = value->result_type;
            } else {
                // Other types keep as UNKNOWN/ANY for JavaScript compatibility
                // This allows dynamic type changes but sacrifices some performance
                variable_type = DataType::UNKNOWN;
            }
        }
        
        // Handle class instance assignments specially - robust object instance detection
        // std::cout << ", value->result_type: " << static_cast<int>(value->result_type) << std::endl;
        if (declared_type == DataType::CLASS_INSTANCE || 
            (declared_type == DataType::UNKNOWN && value->result_type == DataType::CLASS_INSTANCE)) {
            auto new_expr = dynamic_cast<NewExpression*>(value.get());
            if (new_expr) {
                // Set the class type information for this variable
                types.set_variable_class_type(variable_name, new_expr->class_name);
            }
            // ALWAYS set the variable type to CLASS_INSTANCE for object instances
            // This includes both NewExpression and ObjectLiteral
            variable_type = DataType::CLASS_INSTANCE;
        }
        
        // Allocate or get the proper stack offset for this variable
        int64_t offset = types.allocate_variable(variable_name, variable_type);
        
        
        gen.emit_mov_mem_reg(offset, 0);
        result_type = variable_type;
    }
}

void PostfixIncrement::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Load the current value
    DataType var_type = types.get_variable_type(variable_name);
    int64_t offset = types.get_variable_offset(variable_name);
    gen.emit_mov_reg_mem(0, offset); // Load current value into register 0
    
    // Increment the value
    gen.emit_add_reg_imm(0, 1);
    
    // Store back to memory
    gen.emit_mov_mem_reg(offset, 0);
    
    result_type = var_type;
}

void PostfixDecrement::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Load the current value
    DataType var_type = types.get_variable_type(variable_name);
    int64_t offset = types.get_variable_offset(variable_name);
    gen.emit_mov_reg_mem(0, offset); // Load current value into register 0
    
    // Decrement the value
    gen.emit_sub_reg_imm(0, 1);
    
    // Store back to memory
    gen.emit_mov_mem_reg(offset, 0);
    
    result_type = var_type;
}

void FunctionDecl::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Reset type inference for new function to avoid offset conflicts
    types.reset_for_function();
    
    gen.emit_label(name);
    
    // Calculate estimated stack size (parameters + locals + temporaries)
    int64_t estimated_stack_size = (parameters.size() * 8) + (body.size() * 16) + 64;
    // Ensure minimum stack size and 16-byte alignment
    if (estimated_stack_size < 80) estimated_stack_size = 80;
    if (estimated_stack_size % 16 != 0) {
        estimated_stack_size += 16 - (estimated_stack_size % 16);
    }
    
    // Set stack size for this function
    if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
        x86_gen->set_function_stack_size(estimated_stack_size);
    }
    
    gen.emit_prologue();
    
    // Set up parameter types and save parameters from registers to stack
    for (size_t i = 0; i < parameters.size() && i < 6; i++) {
        const auto& param = parameters[i];
        types.set_variable_type(param.name, param.type);
        
        // Use fixed offsets for parameters to avoid conflicts with local variables  
        int stack_offset = -(int)(i + 1) * 8;  // Start at -8, -16, -24 etc
        types.set_variable_offset(param.name, stack_offset);
        
        switch (i) {
            case 0: gen.emit_mov_mem_reg(stack_offset, 7); break;  // save RDI
            case 1: gen.emit_mov_mem_reg(stack_offset, 6); break;  // save RSI
            case 2: gen.emit_mov_mem_reg(stack_offset, 2); break;  // save RDX
            case 3: gen.emit_mov_mem_reg(stack_offset, 1); break;  // save RCX
            case 4: gen.emit_mov_mem_reg(stack_offset, 8); break;  // save R8
            case 5: gen.emit_mov_mem_reg(stack_offset, 9); break;  // save R9
        }
    }
    
    // Handle stack parameters (beyond first 6)
    for (size_t i = 6; i < parameters.size(); i++) {
        const auto& param = parameters[i];
        types.set_variable_type(param.name, param.type);
        // Stack parameters are at positive offsets from RBP
        int stack_offset = (int)(i - 6 + 2) * 8;  // +16 for return addr and old RBP, then +8 for each param
        types.set_variable_offset(param.name, stack_offset);
    }
    
    // Generate function body
    bool has_explicit_return = false;
    for (const auto& stmt : body) {
        stmt->generate_code(gen, types);
        // Check if this statement is a return statement
        if (dynamic_cast<const ReturnStatement*>(stmt.get())) {
            has_explicit_return = true;
        }
    }
    
    // If no explicit return, add implicit return 0
    if (!has_explicit_return) {
        gen.emit_mov_reg_imm(0, 0);  // mov rax, 0 (default return value)
        gen.emit_function_return();
    }
    
    // Register function with compiler for return type lookup
    auto* compiler = get_current_compiler();
    if (compiler) {
        Function func;
        func.name = name;
        func.return_type = (return_type == DataType::UNKNOWN) ? DataType::NUMBER : return_type;
        func.parameters = parameters;
        func.stack_size = 0; // Will be filled during execution
        compiler->register_function(name, func);
        
        //           << static_cast<int>(func.return_type) << std::endl;
    }
}

void IfStatement::generate_code(CodeGenerator& gen, TypeInference& types) {
    static int if_counter = 0;
    std::string else_label = "else_" + std::to_string(if_counter);
    std::string end_label = "end_if_" + std::to_string(if_counter);
    if_counter++;
    
    // Generate condition code - this puts the result in RAX
    condition->generate_code(gen, types);
    
    // Compare RAX with 0 (false)
    gen.emit_mov_reg_imm(1, 0);      // RCX = 0
    gen.emit_compare(0, 1);          // Compare RAX with RCX (0)
    gen.emit_jump_if_zero(else_label); // Jump to else if RAX == 0 (false)
    
    // Generate then body
    for (const auto& stmt : then_body) {
        stmt->generate_code(gen, types);
    }
    
    // Skip else body
    gen.emit_jump(end_label);
    
    // Generate else body
    gen.emit_label(else_label);
    for (const auto& stmt : else_body) {
        stmt->generate_code(gen, types);
    }
    
    gen.emit_label(end_label);
}

void ForLoop::generate_code(CodeGenerator& gen, TypeInference& types) {
    static int loop_counter = 0;
    std::string loop_start = "loop_start_" + std::to_string(loop_counter);
    std::string loop_end = "loop_end_" + std::to_string(loop_counter);
    loop_counter++;
    
    if (init) {
        init->generate_code(gen, types);
    }
    
    gen.emit_label(loop_start);
    
    if (condition) {
        condition->generate_code(gen, types);
        // Check if RAX (result of condition) is zero
        gen.emit_mov_reg_imm(1, 0); // RCX = 0
        gen.emit_compare(0, 1); // Compare RAX with 0
        gen.emit_jump_if_zero(loop_end);
    }
    
    for (const auto& stmt : body) {
        stmt->generate_code(gen, types);
    }
    
    if (update) {
        update->generate_code(gen, types);
    }
    
    gen.emit_jump(loop_start);
    gen.emit_label(loop_end);
}

void ForEachLoop::generate_code(CodeGenerator& gen, TypeInference& types) {
    static int loop_counter = 0;
    std::string loop_start = "foreach_start_" + std::to_string(loop_counter);
    std::string loop_end = "foreach_end_" + std::to_string(loop_counter);
    std::string loop_check = "foreach_check_" + std::to_string(loop_counter);
    
    // Create scoped variable names to avoid conflicts (let semantics)
    std::string scoped_index_name = "__foreach_" + std::to_string(loop_counter) + "_" + index_var_name;
    std::string scoped_value_name = "__foreach_" + std::to_string(loop_counter) + "_" + value_var_name;
    loop_counter++;
    
    // Generate code for the iterable expression
    iterable->generate_code(gen, types);
    
    // Store the iterable in a temporary location
    int64_t iterable_offset = types.allocate_variable("__temp_iterable_" + std::to_string(loop_counter - 1), iterable->result_type);
    gen.emit_mov_mem_reg(iterable_offset, 0); // Store iterable pointer
    
    // Initialize loop index to 0 (use let semantics - create scoped variable)
    int64_t index_offset = types.allocate_variable(scoped_index_name, DataType::INT64);
    gen.emit_mov_reg_imm(0, 0); // RAX = 0
    gen.emit_mov_mem_reg(index_offset, 0); // Store index = 0
    
    // But also create user-visible variables for the loop body  
    // Arrays use INT64 indices, objects use STRING keys
    DataType index_type = (iterable->result_type == DataType::TENSOR) ? DataType::INT64 : DataType::STRING;
    int64_t user_index_offset = types.allocate_variable(index_var_name, index_type);
    int64_t user_value_offset = types.allocate_variable(value_var_name, DataType::UNKNOWN);
    
    gen.emit_label(loop_check);
    
    // Check if we've reached the end of the iterable
    if (iterable->result_type == DataType::TENSOR) {
        // HIGHLY OPTIMIZED PATHWAY FOR TYPED ARRAYS
        // For arrays: check if index < array.length
        gen.emit_mov_reg_mem(7, iterable_offset); // RDI = array pointer
        gen.emit_call("__array_size"); // RAX = array size
        gen.emit_mov_reg_reg(3, 0); // RBX = array size
        gen.emit_mov_reg_mem(0, index_offset); // RAX = current index
        gen.emit_compare(0, 3); // Compare index with size
        
        // Use setge to check if index >= size, then jump if result is non-zero
        gen.emit_setge(1); // RCX = 1 if index >= size, 0 otherwise
        gen.emit_mov_reg_imm(2, 0); // RDX = 0
        gen.emit_compare(1, 2); // Compare RCX with 0
        gen.emit_jump_if_not_zero(loop_end); // Jump if RCX != 0 (i.e., if index >= size)
        
        // OPTIMIZED: Copy index to user variable
        gen.emit_mov_reg_mem(0, index_offset); // RAX = current index
        gen.emit_mov_mem_reg(user_index_offset, 0); // Store in user index variable
        
        // OPTIMIZED: Get the value at current index using fastest possible method
        gen.emit_mov_reg_mem(7, iterable_offset); // RDI = array pointer
        gen.emit_mov_reg_mem(6, index_offset); // RSI = index
        
        // ULTRA-FAST OPTIMIZATION: Check if we know the array element type
        // For explicitly typed arrays, we can use direct memory access
        if (auto typed_array = dynamic_cast<TypedArrayLiteral*>(iterable.get())) {
            // MAXIMUM PERFORMANCE: Direct typed array access
            switch (typed_array->array_type) {
                case DataType::INT32:
                    gen.emit_call("__typed_array_get_int32_fast");
                    break;
                case DataType::INT64:
                    gen.emit_call("__typed_array_get_int64_fast");
                    break;
                case DataType::FLOAT32:
                    gen.emit_call("__typed_array_get_float32_fast");
                    break;
                case DataType::FLOAT64:
                // Note: DataType::NUMBER is an alias for FLOAT64, so no separate case needed
                    gen.emit_call("__typed_array_get_float64_fast");
                    break;
                default:
                    gen.emit_call("__array_get"); // Fallback to general case
                    break;
            }
        } else {
            // General case for dynamic arrays
            gen.emit_call("__array_get"); // RAX = array[index]
        }
        gen.emit_mov_mem_reg(user_value_offset, 0); // Store value in user variable
        
    } else {
        // For objects: SIMPLIFIED IMPLEMENTATION
        // Since object iteration is complex and __object_iterate doesn't exist,
        // implement a basic version that works for object literals
        
        // Check if we've exceeded the reasonable property limit (simpler logic)
        gen.emit_mov_reg_mem(0, index_offset); // RAX = current index
        gen.emit_mov_reg_imm(1, 3); // RCX = max properties for basic object literal
        gen.emit_compare(0, 1); // Compare index with max properties
        
        // Direct jump if index >= max_properties (much simpler)
        gen.emit_setge(0); // AL = 1 if index >= max_properties, 0 otherwise
        gen.emit_and_reg_imm(0, 0xFF); // Zero out upper bits, keep AL
        gen.emit_mov_reg_imm(1, 0); // RCX = 0
        gen.emit_compare(0, 1); // Compare AL with 0
        gen.emit_jump_if_not_zero(loop_end); // Jump if AL != 0 (i.e., if index >= max_properties)
        
        // Get the property name for the current index
        gen.emit_mov_reg_mem(7, iterable_offset); // RDI = object_id
        gen.emit_mov_reg_mem(6, index_offset); // RSI = property_index
        gen.emit_call("__object_get_property_name"); // RAX = property name (const char*)
        
        // Create a GoTS string from the property name
        gen.emit_mov_reg_reg(7, 0); // RDI = property name
        gen.emit_call("__string_intern"); // RAX = GoTS string
        gen.emit_mov_mem_reg(user_index_offset, 0); // Store property name string in key variable
        
        // Get the value at current property index
        gen.emit_mov_reg_mem(7, iterable_offset); // RDI = object_id
        gen.emit_mov_reg_mem(6, index_offset); // RSI = property_index
        gen.emit_call("__object_get_property"); // RAX = property value (which should be a string pointer)
        gen.emit_mov_mem_reg(user_value_offset, 0); // Store value in user variable
    }
    
    gen.emit_label(loop_start);
    
    // Generate loop body - user variables are now populated
    for (const auto& stmt : body) {
        stmt->generate_code(gen, types);
    }
    
    // Increment internal index counter
    gen.emit_mov_reg_mem(0, index_offset); // RAX = current internal index
    gen.emit_add_reg_imm(0, 1); // RAX++
    gen.emit_mov_mem_reg(index_offset, 0); // Store incremented internal index
    
    // Jump back to condition check
    gen.emit_jump(loop_check);
    
    gen.emit_label(loop_end);
}

void ReturnStatement::generate_code(CodeGenerator& gen, TypeInference& types) {
    if (value) {
        value->generate_code(gen, types);
    }
    
    // Use function return to properly restore stack frame and return
    gen.emit_function_return();
}

// Global variable to track current break target
static std::string current_break_target = "";

void BreakStatement::generate_code(CodeGenerator& gen, TypeInference& types) {
    (void)types; // Suppress unused parameter warning
    
    if (!current_break_target.empty()) {
        gen.emit_jump(current_break_target);
    } else {
        // No active switch/loop context
        // For now, just emit a nop or comment
        gen.emit_label("__break_without_context");
    }
}

void SwitchStatement::generate_code(CodeGenerator& gen, TypeInference& types) {
    static int switch_counter = 0;
    std::string switch_end = "switch_end_" + std::to_string(switch_counter);
    switch_counter++;
    
    // Save previous break target and set new one
    std::string previous_break_target = current_break_target;
    current_break_target = switch_end;
    
    // Generate discriminant code - this puts the result in RAX
    discriminant->generate_code(gen, types);
    DataType discriminant_type = discriminant->result_type;
    
    // Store discriminant value in a temporary location using dynamic stack allocation
    int64_t discriminant_offset = types.allocate_variable("__temp_discriminant_" + std::to_string(switch_counter - 1), discriminant_type);
    int64_t discriminant_type_offset = types.allocate_variable("__temp_discriminant_type_" + std::to_string(switch_counter - 1), DataType::INT64);
    
    gen.emit_mov_mem_reg(discriminant_offset, 0); // Store RAX to discriminant offset
    // Store discriminant type
    gen.emit_mov_reg_imm(0, static_cast<int64_t>(discriminant_type));
    gen.emit_mov_mem_reg(discriminant_type_offset, 0); // Store discriminant type to type offset
    
    // Generate code for each case
    std::vector<std::string> case_labels;
    std::string default_label;
    bool has_default = false;
    
    // First pass: create labels and generate comparison jumps
    for (size_t i = 0; i < cases.size(); i++) {
        const auto& case_clause = cases[i];
        
        if (case_clause->is_default) {
            default_label = "case_default_" + std::to_string(switch_counter - 1);
            has_default = true;
        } else {
            std::string case_label = "case_" + std::to_string(switch_counter - 1) + "_" + std::to_string(i);
            case_labels.push_back(case_label);
            
            // Generate case value and compare with discriminant
            case_clause->value->generate_code(gen, types);
            DataType case_type = case_clause->value->result_type;
            
            // ULTRA HIGH PERFORMANCE: Fast path for typed comparisons, slow path for ANY/UNKNOWN
            if (discriminant_type != DataType::UNKNOWN && discriminant_type != DataType::ANY &&
                case_type != DataType::UNKNOWN && case_type != DataType::ANY &&
                discriminant_type == case_type) {
                
                // FAST PATH: Both operands are the same known type - direct comparison
                gen.emit_mov_reg_mem(3, discriminant_offset); // RBX = discriminant value from stack
                gen.emit_compare(3, 0); // Compare discriminant (RBX) with case value (RAX)
                gen.emit_sete(1); // Set RCX = 1 if equal, 0 if not equal
                gen.emit_mov_reg_imm(2, 0); // RDX = 0
                gen.emit_compare(1, 2); // Compare RCX with 0
                gen.emit_jump_if_not_zero(case_label); // Jump if RCX != 0 (i.e., if equal)
                
            } else if (discriminant_type != DataType::UNKNOWN && discriminant_type != DataType::ANY &&
                       case_type != DataType::UNKNOWN && case_type != DataType::ANY &&
                       discriminant_type != case_type) {
                
                // FAST PATH: Both operands are known types but different - never equal
                // Skip this case entirely (no jump, fall through to next case)
                
            } else {
                
                // SLOW PATH: At least one operand is ANY/UNKNOWN - use type-aware comparison
                // Prepare arguments for __runtime_js_equal(left_value, left_type, right_value, right_type)
                gen.emit_mov_reg_mem(7, discriminant_offset); // RDI = discriminant value from stack
                gen.emit_mov_reg_mem(6, discriminant_type_offset); // RSI = discriminant type from stack
                gen.emit_mov_reg_reg(2, 0);   // RDX = case value (currently in RAX)
                gen.emit_mov_reg_imm(1, static_cast<int64_t>(case_type)); // RCX = case type
                
                // Call __runtime_js_equal
                gen.emit_sub_reg_imm(4, 8);  // Align stack to 16-byte boundary
                gen.emit_call("__runtime_js_equal");
                gen.emit_add_reg_imm(4, 8);  // Restore stack
                
                // RAX now contains 1 if equal, 0 if not equal
                gen.emit_mov_reg_imm(3, 0); // RBX = 0
                gen.emit_compare(0, 3); // Compare RAX with 0
                gen.emit_jump_if_not_zero(case_label); // Jump if RAX != 0 (i.e., if equal)
            }
        }
    }
    
    // If no case matched, jump to default or end
    if (has_default) {
        gen.emit_jump(default_label);
    } else {
        gen.emit_jump(switch_end);
    }
    
    // Second pass: generate case bodies
    size_t case_index = 0;
    for (size_t i = 0; i < cases.size(); i++) {
        const auto& case_clause = cases[i];
        
        if (case_clause->is_default) {
            gen.emit_label(default_label);
        } else {
            gen.emit_label(case_labels[case_index++]);
        }
        
        // Generate case body
        for (const auto& stmt : case_clause->body) {
            stmt->generate_code(gen, types);
        }
        
        // Fall through to next case (JavaScript/C-style behavior)
        // Break statements will jump to switch_end
    }
    
    gen.emit_label(switch_end);
    
    // Restore previous break target
    current_break_target = previous_break_target;
}

void CaseClause::generate_code(CodeGenerator& gen, TypeInference& types) {
    // CaseClause code generation is handled by SwitchStatement
    // This method should not be called directly
    (void)gen;
    (void)types;
}

// Class-related AST node implementations
void PropertyAccess::generate_code(CodeGenerator& gen, TypeInference& types) {
    // OPTIMIZATION: Check if this is a runtime object property access
    // The JIT will convert runtime.time to a direct pointer without any lookups
    if (object_name == "runtime") {
        // This is accessing a runtime sub-object like runtime.time, runtime.fs, etc.
        // We DON'T generate any code here - just mark the type
        // The parent ExpressionPropertyAccess or MethodCall will handle the optimization
        result_type = DataType::RUNTIME_OBJECT;
        return;
    }
    
    if (object_name == "this") {
        // Handle this.property access in constructor/method context
        // Get the object_id from the saved this context
        int64_t this_offset = types.get_variable_offset("__this_object_id");
        if (this_offset != 0) {
            // In method context - get object_id from saved location
            gen.emit_mov_reg_mem(7, this_offset); // RDI = object_id from method context
        } else {
            // Fallback for constructor context
            gen.emit_mov_reg_imm(7, 1);  // RDI = object_id (hardcoded for now)
        }
        gen.emit_mov_reg_imm(6, 0);  // RSI = property_index (hardcoded for first property)
        gen.emit_call("__object_get_property");
        // Result will be in RAX
        
        result_type = DataType::UNKNOWN; // TODO: Get actual property type
    } else {
        // Handle regular object.property access
        // Check if the object exists as a variable first
        if (types.variable_exists(object_name)) {
            // Object exists as a variable - treat as instance property access
            int64_t obj_offset = types.get_variable_offset(object_name);
            
            // Get the class name for this object instance
            std::string class_name = types.get_variable_class_name(object_name);
            
            // Map property name to index - simple implementation for Point class
            int64_t property_index = 0;
            if (class_name == "Point") {
                if (property_name == "x") property_index = 0;
                else if (property_name == "y") property_index = 1;
            }
            // TODO: Implement general property mapping system using class registry
            
            gen.emit_mov_reg_mem(7, obj_offset); // RDI = object_id or value
            gen.emit_mov_reg_imm(6, property_index);  // RSI = property_index
            gen.emit_call("__object_get_property");
            // Result will be in RAX
            result_type = DataType::UNKNOWN; // TODO: Get actual property type
        } else {
            // Object not found as variable - might be static property access (ClassName.property)
            // Setup string pooling for class name and property name
            static std::unordered_map<std::string, const char*> string_pool;
            
            auto get_pooled_string = [&](const std::string& str) -> const char* {
                auto it = string_pool.find(str);
                if (it == string_pool.end()) {
                    char* str_copy = new char[str.length() + 1];
                    strcpy(str_copy, str.c_str());
                    string_pool[str] = str_copy;
                    return str_copy;
                } else {
                    return it->second;
                }
            };
            
            const char* class_name_ptr = get_pooled_string(object_name);
            const char* property_name_ptr = get_pooled_string(property_name);
            
            // Call __static_get_property(class_name, property_name)
            gen.emit_mov_reg_imm(7, reinterpret_cast<int64_t>(class_name_ptr));   // RDI = class_name
            gen.emit_mov_reg_imm(6, reinterpret_cast<int64_t>(property_name_ptr)); // RSI = property_name
            gen.emit_call("__static_get_property");
            // Result will be in RAX
            result_type = DataType::UNKNOWN; // TODO: Get actual property type
        }
    }
}

void ExpressionPropertyAccess::generate_code(CodeGenerator& gen, TypeInference& types) {
    // OPTIMIZATION: Check if this is runtime.x access (like runtime.time)
    // If the object is a PropertyAccess with object_name == "runtime"
    PropertyAccess* prop_access = dynamic_cast<PropertyAccess*>(object.get());
    if (prop_access && prop_access->object_name == "runtime") {
        // This is runtime.x access - store the sub-object name for method call optimization
        // We don't generate any code here - MethodCall will handle the direct call
        result_type = DataType::RUNTIME_OBJECT;
        return;
    }
    
    // Generate code for the object expression first
    object->generate_code(gen, types);
    DataType object_type = object->result_type;
    
    // Handle different types of objects for property access
    if (object_type == DataType::STRING) {
        // Handle string properties like length
        if (property_name == "length") {
            // String object is now in RAX, get its length
            gen.emit_mov_reg_reg(7, 0);  // RDI = string pointer
            gen.emit_call("__string_length");
            result_type = DataType::NUMBER;
        } else {
            throw std::runtime_error("Unknown string property: " + property_name);
        }
    } else if (object_type == DataType::TENSOR) {
        // Handle array properties like length, and special match result properties
        if (property_name == "length") {
            gen.emit_mov_reg_reg(7, 0);  // RDI = array pointer
            gen.emit_call("__array_size");
            result_type = DataType::NUMBER;
        } else if (property_name == "index") {
            // JavaScript match result property - lazily computed
            gen.emit_mov_reg_reg(7, 0);  // RDI = match array pointer
            gen.emit_call("__match_result_get_index");
            result_type = DataType::NUMBER;
        } else if (property_name == "input") {
            // JavaScript match result property - lazily computed
            gen.emit_mov_reg_reg(7, 0);  // RDI = match array pointer
            gen.emit_call("__match_result_get_input");
            result_type = DataType::STRING;
        } else if (property_name == "groups") {
            // JavaScript match result property - always undefined for basic matches
            gen.emit_mov_reg_reg(7, 0);  // RDI = match array pointer
            gen.emit_call("__match_result_get_groups");
            result_type = DataType::UNKNOWN; // undefined
        } else {
            throw std::runtime_error("Unknown array property: " + property_name);
        }
    } else if (object_type == DataType::ARRAY) {
        // Handle simplified Array properties
        if (property_name == "length") {
            gen.emit_mov_reg_reg(7, 0);  // RDI = array pointer
            gen.emit_call("__simple_array_length");
            result_type = DataType::NUMBER;
        } else if (property_name == "shape") {
            gen.emit_mov_reg_reg(7, 0);  // RDI = array pointer
            gen.emit_call("__simple_array_shape");
            result_type = DataType::ARRAY;
        } else {
            throw std::runtime_error("Unknown Array property: " + property_name);
        }
    } else if (object_type == DataType::REGEX) {
        // Handle regex properties
        if (property_name == "source") {
            // Get regex pattern as string
            gen.emit_mov_reg_reg(7, 0);  // RDI = regex pointer
            gen.emit_call("__regex_get_source");
            result_type = DataType::STRING;
        } else if (property_name == "global") {
            gen.emit_mov_reg_reg(7, 0);  // RDI = regex pointer
            gen.emit_call("__regex_get_global");
            result_type = DataType::BOOLEAN;
        } else if (property_name == "ignoreCase") {
            gen.emit_mov_reg_reg(7, 0);  // RDI = regex pointer
            gen.emit_call("__regex_get_ignore_case");
            result_type = DataType::BOOLEAN;
        } else {
            throw std::runtime_error("Unknown regex property: " + property_name);
        }
    } else {
        // For other types or custom objects, use dynamic property access
        gen.emit_mov_mem_reg(-8, 0); // Save object pointer on stack
        
        // Create a pooled string for the property name
        static std::unordered_map<std::string, const char*> property_name_pool;
        
        auto it = property_name_pool.find(property_name);
        if (it == property_name_pool.end()) {
            char* name_copy = new char[property_name.length() + 1];
            strcpy(name_copy, property_name.c_str());
            property_name_pool[property_name] = name_copy;
            it = property_name_pool.find(property_name);
        }
        
        const char* property_name_ptr = it->second;
        
        // Call dynamic property getter
        gen.emit_mov_reg_mem(7, -8);  // RDI = object pointer
        gen.emit_mov_reg_imm(6, reinterpret_cast<int64_t>(property_name_ptr)); // RSI = property name
        gen.emit_call("__dynamic_get_property");
        result_type = DataType::UNKNOWN; // Unknown return type for dynamic access
    }
}

void ThisExpression::generate_code(CodeGenerator& gen, TypeInference& types) {
    // TODO: Implement 'this' code generation
    // For now, just put 0 in RAX as placeholder
    gen.emit_mov_reg_imm(0, 0);
}

void NewExpression::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Create object instance - get actual property count from class registry
    int64_t property_count = 1; // Default fallback
    if (ConstructorDecl::current_compiler_context) {
        ClassInfo* class_info = ConstructorDecl::current_compiler_context->get_class(class_name);
        if (class_info) {
            property_count = class_info->fields.size();
        }
    }
    
    // Call __object_create(class_name, property_count)
    // Setup string pooling for class name
    static std::unordered_map<std::string, const char*> class_name_pool;
    
    auto it = class_name_pool.find(class_name);
    if (it == class_name_pool.end()) {
        char* name_copy = new char[class_name.length() + 1];
        strcpy(name_copy, class_name.c_str());
        class_name_pool[class_name] = name_copy;
        it = class_name_pool.find(class_name);
    }
    
    // Set up call to __object_create
    gen.emit_mov_reg_imm(7, reinterpret_cast<int64_t>(it->second)); // RDI = class_name
    gen.emit_mov_reg_imm(6, property_count); // RSI = property_count
    gen.emit_call("__object_create");
    
    // __object_create returns object_id in RAX
    // Store object_id temporarily for constructor call
    gen.emit_mov_mem_reg(-8, 0); // Save object_id on stack
    
    // Call constructor function if it exists
    std::string constructor_label = "__constructor_" + class_name;
    
    // Set up constructor arguments in registers
    gen.emit_mov_reg_mem(7, -8); // RDI = object_id (this)
    
    // Pass constructor arguments in registers
    for (size_t i = 0; i < arguments.size() && i < 5; i++) { // Max 5 constructor params (RDI is object_id)
        arguments[i]->generate_code(gen, types);
        
        // Store argument value in temporary stack location
        gen.emit_mov_mem_reg(-(int64_t)(i + 2) * 8, 0); // Store at -16, -24, etc.
    }
    
    // Load arguments into appropriate registers
    for (size_t i = 0; i < arguments.size() && i < 5; i++) {
        switch (i) {
            case 0: gen.emit_mov_reg_mem(6, -16); break; // RSI
            case 1: gen.emit_mov_reg_mem(2, -24); break; // RDX  
            case 2: gen.emit_mov_reg_mem(1, -32); break; // RCX
            case 3: gen.emit_mov_reg_mem(8, -40); break; // R8
            case 4: gen.emit_mov_reg_mem(9, -48); break; // R9
        }
    }
    
    // Call the constructor function
    gen.emit_call(constructor_label);
    
    // Restore object_id to RAX for return value
    gen.emit_mov_reg_mem(0, -8);
    
    result_type = DataType::CLASS_INSTANCE;
    
}

void ConstructorDecl::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Reset type inference for new constructor to avoid offset conflicts
    types.reset_for_function();
    
    // Generate constructor as a function with 'this' (object_id) as first parameter, then constructor parameters
    std::string constructor_label = "__constructor_" + class_name;
    
    gen.emit_label(constructor_label);
    
    // Calculate estimated stack size for constructor
    int64_t estimated_stack_size = ((parameters.size() + 1) * 8) + (body.size() * 16) + 64; // +1 for 'this' parameter
    if (estimated_stack_size < 80) estimated_stack_size = 80;
    if (estimated_stack_size % 16 != 0) {
        estimated_stack_size += 16 - (estimated_stack_size % 16);
    }
    
    if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
        x86_gen->set_function_stack_size(estimated_stack_size);
    }
    
    gen.emit_prologue();
    
    // Set up 'this' parameter (object_id) in first stack slot
    types.set_variable_type("this", DataType::CLASS_INSTANCE);
    types.set_variable_offset("this", -8);
    gen.emit_mov_mem_reg(-8, 7); // save RDI (object_id) to 'this'
    
    // Set up constructor parameters from registers to stack (starting from second parameter)
    for (size_t i = 0; i < parameters.size() && i < 5; i++) { // Max 5 params (RDI is used for 'this')
        const auto& param = parameters[i];
        types.set_variable_type(param.name, param.type);
        
        int stack_offset = -(int)(i + 2) * 8;  // Start at -16, -24, -32 etc ('this' is at -8)
        types.set_variable_offset(param.name, stack_offset);
        
        switch (i) {
            case 0: gen.emit_mov_mem_reg(stack_offset, 6); break;  // save RSI
            case 1: gen.emit_mov_mem_reg(stack_offset, 2); break;  // save RDX
            case 2: gen.emit_mov_mem_reg(stack_offset, 1); break;  // save RCX
            case 3: gen.emit_mov_mem_reg(stack_offset, 8); break;  // save R8
            case 4: gen.emit_mov_mem_reg(stack_offset, 9); break;  // save R9
        }
    }
    
    // Initialize fields with default values
    if (current_compiler_context) {
        ClassInfo* class_info = current_compiler_context->get_class(class_name);
        if (class_info) {
            for (size_t i = 0; i < class_info->fields.size(); i++) {
                const auto& field = class_info->fields[i];
                if (field.default_value) {
                    // Generate code for default value expression
                    field.default_value->generate_code(gen, types);
                    
                    // Set the property on 'this' object
                    // RAX contains the result of the default value expression
                    gen.emit_mov_reg_reg(2, 0);  // RDX = value (from RAX)
                    gen.emit_mov_reg_mem(7, -8); // RDI = object_id (from 'this')
                    gen.emit_mov_reg_imm(6, i);  // RSI = property_index
                    gen.emit_call("__object_set_property");
                    
                }
            }
        }
    }
    
    // Generate constructor body
    for (const auto& stmt : body) {
        stmt->generate_code(gen, types);
    }
    
    gen.emit_epilogue();
    
}

void MethodDecl::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Reset type inference for new method to avoid offset conflicts
    types.reset_for_function();
    
    // Generate different labels and parameter handling for static vs instance methods
    std::string method_label = is_static ? "__static_" + name : "__method_" + name;
    
    gen.emit_label(method_label);
    
    // Calculate estimated stack size for method
    int64_t estimated_stack_size = (parameters.size() * 8) + (body.size() * 16) + 64;
    if (estimated_stack_size < 80) estimated_stack_size = 80;
    if (estimated_stack_size % 16 != 0) {
        estimated_stack_size += 16 - (estimated_stack_size % 16);
    }
    
    if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
        x86_gen->set_function_stack_size(estimated_stack_size);
    }
    
    gen.emit_prologue();
    
    if (!is_static) {
        // Instance method: first parameter (RDI) is the object_id (this)
        types.set_variable_offset("__this_object_id", -8);
        gen.emit_mov_mem_reg(-8, 7); // Save object_id from RDI
        
        // Set up other parameters starting from -16
        for (size_t i = 0; i < parameters.size() && i < 5; i++) { // 5 because RDI is used for this
            const auto& param = parameters[i];
            types.set_variable_type(param.name, param.type);
            int stack_offset = -(int)(i + 2) * 8;  // Start at -16 (after this)
            types.set_variable_offset(param.name, stack_offset);
            
            switch (i) {
                case 0: gen.emit_mov_mem_reg(stack_offset, 6); break;  // save RSI  
                case 1: gen.emit_mov_mem_reg(stack_offset, 2); break;  // save RDX
                case 2: gen.emit_mov_mem_reg(stack_offset, 1); break;  // save RCX
                case 3: gen.emit_mov_mem_reg(stack_offset, 8); break;  // save R8
                case 4: gen.emit_mov_mem_reg(stack_offset, 9); break;  // save R9
            }
        }
    } else {
        // Static method: no 'this' parameter, parameters start from -8
        for (size_t i = 0; i < parameters.size() && i < 6; i++) { // 6 registers available for static methods
            const auto& param = parameters[i];
            types.set_variable_type(param.name, param.type);
            int stack_offset = -(int)(i + 1) * 8;  // Start at -8
            types.set_variable_offset(param.name, stack_offset);
            
            switch (i) {
                case 0: gen.emit_mov_mem_reg(stack_offset, 7); break;  // save RDI
                case 1: gen.emit_mov_mem_reg(stack_offset, 6); break;  // save RSI
                case 2: gen.emit_mov_mem_reg(stack_offset, 2); break;  // save RDX
                case 3: gen.emit_mov_mem_reg(stack_offset, 1); break;  // save RCX
                case 4: gen.emit_mov_mem_reg(stack_offset, 8); break;  // save R8
                case 5: gen.emit_mov_mem_reg(stack_offset, 9); break;  // save R9
            }
        }
    }
    
    // Generate method body
    bool has_explicit_return = false;
    for (const auto& stmt : body) {
        stmt->generate_code(gen, types);
        if (dynamic_cast<const ReturnStatement*>(stmt.get())) {
            has_explicit_return = true;
        }
    }
    
    // If no explicit return, return 0 for non-void methods
    if (!has_explicit_return && return_type != DataType::VOID) {
        gen.emit_mov_reg_imm(0, 0);
    }
    
    gen.emit_function_return();
    
}

void PropertyAssignment::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Generate code for the value expression first
    value->generate_code(gen, types);
    
    if (object_name == "this") {
        // Handle this.property = value in constructor/method context
        // Get the 'this' object_id from the stack where it was stored by the constructor
        // Function signature: __object_set_property(object_id, property_index, value)
        gen.emit_mov_reg_reg(2, 0); // RDX = value (from RAX)
        gen.emit_mov_reg_mem(7, -8); // RDI = object_id (from 'this' parameter on stack)
        gen.emit_mov_reg_imm(6, 0);  // RSI = property_index (hardcoded for first property)
        gen.emit_call("__object_set_property");
        
    } else {
        // Handle regular object.property = value
        // Get object from variable
        DataType obj_type = types.get_variable_type(object_name);
        if (obj_type == DataType::CLASS_INSTANCE) {
            // Get object ID from variable
            int64_t obj_offset = types.get_variable_offset(object_name);
            
            // Get the class name for this object instance
            std::string class_name = types.get_variable_class_name(object_name);
            
            // Map property name to index - simple implementation for Point class
            int64_t property_index = 0;
            if (class_name == "Point") {
                if (property_name == "x") property_index = 0;
                else if (property_name == "y") property_index = 1;
            }
            // TODO: Implement general property mapping system using class registry
            
            // Function signature: __object_set_property(object_id, property_index, value)
            gen.emit_mov_reg_reg(2, 0); // RDX = value (save value from RAX first)
            gen.emit_mov_reg_mem(7, obj_offset); // RDI = object_id
            gen.emit_mov_reg_imm(6, property_index);  // RSI = property_index
            gen.emit_call("__object_set_property");
        } else {
            // Object not found as variable - might be static property assignment (ClassName.property = value)
            // Setup string pooling for class name and property name
            static std::unordered_map<std::string, const char*> string_pool;
            
            auto get_pooled_string = [&](const std::string& str) -> const char* {
                auto it = string_pool.find(str);
                if (it == string_pool.end()) {
                    char* str_copy = new char[str.length() + 1];
                    strcpy(str_copy, str.c_str());
                    string_pool[str] = str_copy;
                    return str_copy;
                } else {
                    return it->second;
                }
            };
            
            const char* class_name_ptr = get_pooled_string(object_name);
            const char* property_name_ptr = get_pooled_string(property_name);
            
            // Call __static_set_property(class_name, property_name, value)
            gen.emit_mov_reg_imm(7, reinterpret_cast<int64_t>(class_name_ptr));   // RDI = class_name
            gen.emit_mov_reg_imm(6, reinterpret_cast<int64_t>(property_name_ptr)); // RSI = property_name
            gen.emit_mov_reg_reg(2, 0); // RDX = value (from RAX)
            gen.emit_call("__static_set_property");
        }
    }
    
    result_type = DataType::VOID;
}

void ClassDecl::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Class declarations don't generate code during main execution
    // Constructor and methods are generated separately in the function generation phase
    
    // No code generation needed here - everything is handled in the function phase
}

void SuperCall::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Super constructor call: calls the parent class constructor
    // TODO: Need to determine the parent class name from context
    // For now, generate a call that will need to be resolved at runtime
    
    // Get the object_id from 'this' parameter (should be available in constructor context)
    gen.emit_mov_reg_mem(7, -8); // RDI = object_id (this)
    
    // Set up constructor arguments
    for (size_t i = 0; i < arguments.size() && i < 5; i++) {
        arguments[i]->generate_code(gen, types);
        
        // Store argument value in temporary stack location
        gen.emit_mov_mem_reg(-(int64_t)(i + 2) * 8, 0); // Store at -16, -24, etc.
    }
    
    // Load arguments into appropriate registers
    for (size_t i = 0; i < arguments.size() && i < 5; i++) {
        switch (i) {
            case 0: gen.emit_mov_reg_mem(6, -16); break; // RSI
            case 1: gen.emit_mov_reg_mem(2, -24); break; // RDX  
            case 2: gen.emit_mov_reg_mem(1, -32); break; // RCX
            case 3: gen.emit_mov_reg_mem(8, -40); break; // R8
            case 4: gen.emit_mov_reg_mem(9, -48); break; // R9
        }
    }
    
    // Call a runtime function to resolve and call the parent constructor
    // This will need to be implemented to look up the parent class constructor
    gen.emit_call("__super_constructor_call");
    
    
    result_type = DataType::VOID;
}

void SuperMethodCall::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Super method call: calls the parent class method
    // TODO: This needs to be enhanced to dynamically resolve the parent class method
    // For now, generate a call that assumes parent method naming convention
    
    // Get the object_id from 'this' parameter (should be available in method context)
    gen.emit_mov_reg_mem(7, -8); // RDI = object_id (this)
    
    // Set up method arguments
    for (size_t i = 0; i < arguments.size() && i < 5; i++) {
        arguments[i]->generate_code(gen, types);
        
        // Store argument value in temporary stack location
        gen.emit_mov_mem_reg(-(int64_t)(i + 2) * 8, 0); // Store at -16, -24, etc.
    }
    
    // Load arguments into appropriate registers
    for (size_t i = 0; i < arguments.size() && i < 5; i++) {
        switch (i) {
            case 0: gen.emit_mov_reg_mem(6, -16); break; // RSI
            case 1: gen.emit_mov_reg_mem(2, -24); break; // RDX  
            case 2: gen.emit_mov_reg_mem(1, -32); break; // RCX
            case 3: gen.emit_mov_reg_mem(8, -40); break; // R8
            case 4: gen.emit_mov_reg_mem(9, -48); break; // R9
        }
    }
    
    // Call the parent method - for now use a simple naming convention
    // TODO: This should be enhanced to dynamically resolve parent class methods
    std::string parent_method_label = "__parent_method_" + method_name;
    gen.emit_call(parent_method_label);
    
    result_type = DataType::UNKNOWN; // TODO: Get actual return type from method signature
}

void ImportStatement::generate_code(CodeGenerator& gen, TypeInference& types) {
    // At code generation time, we need to load the module and make its exports available
    
    // Get the compiler context to access the module system
    GoTSCompiler* compiler = ConstructorDecl::current_compiler_context;
    if (!compiler) {
        throw std::runtime_error("No compiler context available for module loading");
    }
    
    try {
        // Load the module using lazy loading with circular import support
        Module* module = compiler->load_module_lazy(module_path);
        if (!module) {
            throw std::runtime_error("Failed to load module: " + module_path);
        }
        
        // Check if module has circular import issues
        if (module->exports_partial) {
            std::cerr << "Warning: Module " << module_path << " has partial exports due to circular imports" << std::endl;
            std::cerr << compiler->get_import_stack_trace() << std::endl;
        }
        
        // Execute the module to populate its exports
        // For now, we'll simulate this by parsing the exports and binding known values
        if (is_namespace_import) {
            // Create a namespace object containing all exports from the module
            // This would require runtime module loading and export collection
            types.set_variable_type(namespace_name, DataType::UNKNOWN);
        } else {
            for (const auto& spec : specifiers) {
                
                // Look for the exported value in the module's AST
                for (const auto& stmt : module->ast) {
                    if (auto export_stmt = dynamic_cast<ExportStatement*>(stmt.get())) {
                        
                        // Check if this export statement has a declaration (like "export const bobby = 'hello'")
                        if (export_stmt->declaration) {
                            
                            // Check if this is an Assignment with a number literal value
                            if (auto assignment = dynamic_cast<Assignment*>(export_stmt->declaration.get())) {
                                
                                if (assignment->variable_name == spec.local_name) {
                                    
                                    // Check if the value is a number literal
                                    if (auto number_literal = dynamic_cast<NumberLiteral*>(assignment->value.get())) {
                                        // Store the constant value globally instead of using stack
                                        global_imported_constants[spec.local_name] = number_literal->value;
                                        types.set_variable_type(spec.local_name, DataType::FLOAT64);
                                        break;
                                    } else {
                                    }
                                }
                            } else {
                                
                                // Try to cast to other possible types
                                if (auto func_decl = dynamic_cast<FunctionDecl*>(export_stmt->declaration.get())) {
                                } else {
                                }
                            }
                            
                            // For non-constant exports, use the original stack-based approach
                            // Allocate stack space for the imported variable
                            int64_t offset = types.allocate_variable(spec.local_name, DataType::STRING);
                            
                            // Generate code for the export declaration
                            export_stmt->declaration->generate_code(gen, types);
                            
                            // Store the result in the imported variable's location
                            gen.emit_mov_mem_reg(offset, 0);
                            
                            break;
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error loading module " << module_path << ": " << e.what() << std::endl;
        // Fall back to registering as unknown type
        for (const auto& spec : specifiers) {
            types.set_variable_type(spec.local_name, DataType::UNKNOWN);
        }
    }
}

void ExportStatement::generate_code(CodeGenerator& gen, TypeInference& types) {
    
    if (is_default) {
        if (declaration) {
            // Generate code for the default export declaration
            declaration->generate_code(gen, types);
            // The result should be stored as the default export value
        }
    } else if (!specifiers.empty()) {
        for (const auto& spec : specifiers) {
            std::cout << "  " << spec.local_name << " as " << spec.exported_name << std::endl;
        }
        // Named exports just mark existing variables/functions as exported
        // The actual export registration happens in the module system
    } else if (declaration) {
        // Generate code for the exported declaration
        declaration->generate_code(gen, types);
        // Mark the declared item as exported
    }
}

void OperatorOverloadDecl::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Generate operator overload function with unique name based on parameter types
    std::string param_signature = "";
    for (size_t i = 0; i < parameters.size(); ++i) {
        if (i > 0) param_signature += "_";
        if (parameters[i].type == DataType::UNKNOWN) {
            param_signature += "any";
        } else {
            param_signature += std::to_string(static_cast<int>(parameters[i].type));
        }
    }
    
    std::string op_function_name = class_name + "::__op_" + std::to_string(static_cast<int>(operator_type)) + "_" + param_signature + "__";
    
    // Generate function label
    gen.emit_label(op_function_name);
    gen.emit_prologue();
    
    // Reset type inference for new function
    types.reset_for_function();
    
    // Set up parameter types and save parameters from registers to stack
    for (size_t i = 0; i < parameters.size() && i < 6; i++) {
        const auto& param = parameters[i];
        types.set_variable_type(param.name, param.type);
        
        int stack_offset = -(int)(i + 1) * 8;
        types.set_variable_offset(param.name, stack_offset);
        
        // Save parameters from calling convention registers to stack
        switch (i) {
            case 0: gen.emit_mov_mem_reg(stack_offset, 7); break;  // save RDI
            case 1: gen.emit_mov_mem_reg(stack_offset, 6); break;  // save RSI
            case 2: gen.emit_mov_mem_reg(stack_offset, 2); break;  // save RDX
            case 3: gen.emit_mov_mem_reg(stack_offset, 1); break;  // save RCX
            case 4: gen.emit_mov_mem_reg(stack_offset, 8); break;  // save R8
            case 5: gen.emit_mov_mem_reg(stack_offset, 9); break;  // save R9
        }
        
        // Special handling for class instances - set class name
        if (param.type == DataType::CLASS_INSTANCE && !param.class_name.empty()) {
            types.set_variable_class_type(param.name, param.class_name);
        }
    }
    
    // Generate function body
    for (const auto& stmt : body) {
        stmt->generate_code(gen, types);
    }
    
    // Ensure there's a return value in RAX
    gen.emit_mov_reg_imm(0, 0); // Default return 0
    
    gen.emit_epilogue();
    
    // Register the operator overload with the compiler
    auto* compiler = get_current_compiler();
    if (compiler) {
        OperatorOverload overload(operator_type, parameters, return_type);
        overload.function_name = op_function_name;
        compiler->register_operator_overload(class_name, overload);
        
        // Verify registration
        bool has_overload = compiler->has_operator_overload(class_name, operator_type);
        (void)has_overload; // Suppress unused variable warning
    }
}

void SliceExpression::generate_code(CodeGenerator& gen, TypeInference& types) {
    // Create a runtime slice object that can be used by array operations
    // The slice object should contain start, end, step, and specification flags
    
    // Load slice parameters into registers
    gen.emit_mov_reg_imm(7, start_specified ? start : 0);                    // RDI = start
    gen.emit_mov_reg_imm(6, end_specified ? end : -1);                       // RSI = end  
    gen.emit_mov_reg_imm(2, step_specified ? step : 1);                      // RDX = step
    gen.emit_mov_reg_imm(1, (start_specified ? 1 : 0) | 
                            (end_specified ? 2 : 0) | 
                            (step_specified ? 4 : 0));                       // RCX = flags
    
    // Call runtime function to create slice object
    gen.emit_call("__slice_create");
    
    // Result (slice pointer) is now in RAX
    result_type = DataType::SLICE;
}

}