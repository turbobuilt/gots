#include "function_compilation_manager.h"
#include "compiler.h"
#include "runtime.h"
#include <iostream>
#include <algorithm>

// Additional forward declarations for AST traversal
namespace gots {
    class MethodCall;
    class ExpressionMethodCall;
    class BinaryOp;
    class Assignment;
    class IfStatement;
    class ReturnStatement;
}

namespace gots {

FunctionCompilationManager& FunctionCompilationManager::instance() {
    static FunctionCompilationManager instance;
    return instance;
}

void FunctionCompilationManager::discover_functions(const std::vector<std::unique_ptr<ASTNode>>& ast) {
    
    for (size_t i = 0; i < ast.size(); i++) {
        if (ast[i]) {
            discover_functions_recursive(ast[i].get());
        }
    }
    
    for (const auto& order : compilation_order_) {
    }
}

void FunctionCompilationManager::discover_functions_recursive(ASTNode* node) {
    if (!node) return;
    
    
    // Check if this node is a function expression
    if (auto func_expr = dynamic_cast<FunctionExpression*>(node)) {
        
        // Create a shared_ptr with a no-op deleter (we don't own the raw pointer)
        auto shared_func = std::shared_ptr<FunctionExpression>(func_expr, [](FunctionExpression*){});
        std::string func_name = register_function(shared_func, "");
        
        // CRITICAL: Set the assigned name on the original AST node
        // This ensures the name is preserved when the AST is processed during Phase 3
        func_expr->set_compilation_assigned_name(func_name);
        
        // CRITICAL: We must traverse into the function body to find nested function expressions
        for (const auto& stmt : func_expr->body) {
            if (stmt) {
                discover_functions_recursive(stmt.get());
            }
        }
        
        return; // Done with this function expression
    }
    
    // For function calls, recurse into arguments to find nested function expressions
    if (auto func_call = dynamic_cast<FunctionCall*>(node)) {
        if (func_call->is_goroutine) {
        }
        
        // Recurse into arguments
        for (const auto& arg : func_call->arguments) {
            discover_functions_recursive(arg.get());
        }
        return;
    }
    
    // For method calls, recurse into arguments and object
    if (auto method_call = dynamic_cast<MethodCall*>(node)) {
        for (const auto& arg : method_call->arguments) {
            discover_functions_recursive(arg.get());
        }
        return;
    }
    
    // For expression method calls, recurse into arguments and object
    if (auto expr_method_call = dynamic_cast<ExpressionMethodCall*>(node)) {
        if (expr_method_call->object) {
            discover_functions_recursive(expr_method_call->object.get());
        }
        for (const auto& arg : expr_method_call->arguments) {
            discover_functions_recursive(arg.get());
        }
        return;
    }
    
    // For binary operations, recurse into left and right operands
    if (auto binary_op = dynamic_cast<BinaryOp*>(node)) {
        if (binary_op->left) {
            discover_functions_recursive(binary_op->left.get());
        }
        if (binary_op->right) {
            discover_functions_recursive(binary_op->right.get());
        }
        return;
    }
    
    // For assignments, recurse into the value
    if (auto assignment = dynamic_cast<Assignment*>(node)) {
        if (assignment->value) {
            discover_functions_recursive(assignment->value.get());
        }
        return;
    }
    
    // For if statements, recurse into condition and bodies
    if (auto if_stmt = dynamic_cast<IfStatement*>(node)) {
        if (if_stmt->condition) {
            discover_functions_recursive(if_stmt->condition.get());
        }
        for (const auto& stmt : if_stmt->then_body) {
            discover_functions_recursive(stmt.get());
        }
        for (const auto& stmt : if_stmt->else_body) {
            discover_functions_recursive(stmt.get());
        }
        return;
    }
    
    // For return statements, recurse into the value
    if (auto return_stmt = dynamic_cast<ReturnStatement*>(node)) {
        if (return_stmt->value) {
            discover_functions_recursive(return_stmt->value.get());
        }
        return;
    }
    
    // Add more node types as needed for complete traversal
}

std::string FunctionCompilationManager::register_function(std::shared_ptr<FunctionExpression> func_expr, const std::string& preferred_name) {
    std::string func_name;
    
    if (!preferred_name.empty()) {
        func_name = preferred_name;
    } else if (!func_expr->name.empty()) {
        func_name = func_expr->name;
    } else {
        func_name = generate_unique_function_name("__func_expr");
    }
    
    // Ensure uniqueness
    if (functions_.find(func_name) != functions_.end()) {
        func_name = generate_unique_function_name(func_name);
    }
    
    // Create function info and assign function ID
    auto func_info = std::make_unique<FunctionInfo>(func_name, func_expr);
    
    // Assign fast function ID using the runtime system
    extern uint16_t __register_function_fast(void* func_ptr, uint16_t arg_count, uint8_t calling_convention);
    func_info->function_id = __register_function_fast(nullptr, 0, 0);  // Address will be set later
    uint16_t assigned_id = func_info->function_id;
    
    functions_[func_name] = std::move(func_info);
    compilation_order_.push_back(func_name);
    
    return func_name;
}

void FunctionCompilationManager::compile_all_functions(CodeGenerator& gen, TypeInference& types) {
    
    total_function_code_size_ = 0;
    
    // CRITICAL: Compile functions in REVERSE order (innermost first)
    // This ensures that when we compile an outer function, all inner functions are already compiled
    
    for (int i = compilation_order_.size() - 1; i >= 0; i--) {
        const std::string& func_name = compilation_order_[i];
        auto it = functions_.find(func_name);
        if (it == functions_.end()) {
            continue;
        }
        
        FunctionInfo* func_info = it->second.get();
        if (func_info->is_compiled) {
            continue;
        }
        
        
        // Record start position
        size_t start_offset = gen.get_current_offset();
        
        // Compile function body
        try {
            compile_function_body(gen, types, func_info);
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Exception during function compilation: " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cerr << "ERROR: Unknown exception during function compilation" << std::endl;
            throw;
        }
        
        // Record end position and size
        size_t end_offset = gen.get_current_offset();
        func_info->code_offset = start_offset;
        func_info->code_size = end_offset - start_offset;
        func_info->is_compiled = true;
        
        total_function_code_size_ += func_info->code_size;
        
        // std::cout << " size " << func_info->code_size << std::endl;
    }
    
}

void FunctionCompilationManager::assign_function_addresses(void* executable_memory, size_t memory_size) {
    
    uint8_t* memory_base = static_cast<uint8_t*>(executable_memory);
    
    for (const std::string& func_name : compilation_order_) {
        auto it = functions_.find(func_name);
        if (it == functions_.end()) continue;
        
        FunctionInfo* func_info = it->second.get();
        if (!func_info->is_compiled) continue;
        
        // Calculate address
        func_info->address = memory_base + func_info->code_offset;
        
        // Update the fast function table with the actual address
        extern void* __lookup_function_fast(uint16_t func_id);
        extern FunctionEntry g_function_table[];
        if (func_info->function_id > 0) {
            g_function_table[func_info->function_id].func_ptr = func_info->address;
        }
        
    }
}

void FunctionCompilationManager::register_function_in_runtime() {
    
    for (const auto& pair : functions_) {
        const std::string& func_name = pair.first;
        const FunctionInfo* func_info = pair.second.get();
        
        if (func_info->is_compiled && func_info->address) {
            // Register in runtime system
            __register_function_fast(func_info->address, 0, 0);
        }
    }
}

void* FunctionCompilationManager::get_function_address(const std::string& function_name) {
    auto it = functions_.find(function_name);
    if (it != functions_.end() && it->second->is_compiled) {
        return it->second->address;
    }
    return nullptr;
}

size_t FunctionCompilationManager::get_function_offset(const std::string& function_name) {
    auto it = functions_.find(function_name);
    if (it != functions_.end() && it->second->is_compiled) {
        return it->second->code_offset;
    }
    return 0; // Invalid offset
}

uint16_t FunctionCompilationManager::get_function_id(const std::string& function_name) {
    auto it = functions_.find(function_name);
    if (it != functions_.end()) {
        return it->second->function_id;
    }
    return 0;  // Invalid function ID
}

bool FunctionCompilationManager::is_function_compiled(const std::string& function_name) {
    auto it = functions_.find(function_name);
    return it != functions_.end() && it->second->is_compiled;
}

void FunctionCompilationManager::clear() {
    functions_.clear();
    compilation_order_.clear();
    next_function_id_ = 0;
    total_function_code_size_ = 0;
}

size_t FunctionCompilationManager::get_total_function_code_size() const {
    return total_function_code_size_;
}

void FunctionCompilationManager::print_function_registry() const {
    for (const auto& pair : functions_) {
        const FunctionInfo* info = pair.second.get();
        std::cout << "  " << pair.first << " -> " << info->address 
                  << " (ID: " << info->function_id << ", offset: " << info->code_offset << ", size: " << info->code_size 
                  << ", compiled: " << info->is_compiled << ")" << std::endl;
    }
}

std::string FunctionCompilationManager::generate_unique_function_name(const std::string& base_name) {
    std::string name = base_name + "_" + std::to_string(next_function_id_++);
    
    // Ensure uniqueness
    while (functions_.find(name) != functions_.end()) {
        name = base_name + "_" + std::to_string(next_function_id_++);
    }
    
    return name;
}

void FunctionCompilationManager::compile_function_body(CodeGenerator& gen, TypeInference& types, FunctionInfo* func_info) {
    FunctionExpression* func_expr = func_info->function_expr.get();
    
    // Safety check
    if (!func_expr) {
        std::cerr << "ERROR: Null function expression for " << func_info->name << std::endl;
        return;
    }
    
    
    // Emit function label
    gen.emit_label(func_info->name);
    
    // Calculate estimated stack size
    int64_t estimated_stack_size = (func_expr->parameters.size() * 8) + (func_expr->body.size() * 16) + 64;
    if (estimated_stack_size < 80) estimated_stack_size = 80;
    if (estimated_stack_size % 16 != 0) {
        estimated_stack_size += 16 - (estimated_stack_size % 16);
    }
    
    // Set stack size for this function
    if (auto x86_gen = dynamic_cast<X86CodeGen*>(&gen)) {
        x86_gen->set_function_stack_size(estimated_stack_size);
    }
    
    gen.emit_prologue();
    
    // Set up local type context
    TypeInference local_types;
    local_types.reset_for_function();
    
    // Generate function body statements
    for (size_t i = 0; i < func_expr->body.size(); i++) {
        if (func_expr->body[i]) {
            
            try {
                func_expr->body[i]->generate_code(gen, local_types);
            } catch (const std::exception& e) {
                std::cerr << "ERROR: Exception in statement " << i << ": " << e.what() << std::endl;
                throw;
            } catch (...) {
                std::cerr << "ERROR: Unknown exception in statement " << i << std::endl;
                throw;
            }
        }
    }
    
    gen.emit_epilogue();
    
}

} // namespace gots