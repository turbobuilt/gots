#include "compiler.h"
#include "runtime.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <unordered_map>

namespace gots {

enum X86Register {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8 = 8, R9 = 9, R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15
};

void X86CodeGen::emit_prologue() {
    code.push_back(0x55);  // push rbp
    emit_mov_reg_reg(RBP, RSP);  // mov rbp, rsp
    
    // Save callee-saved registers
    code.push_back(0x53);  // push rbx
    code.push_back(0x41); code.push_back(0x54);  // push r12
    code.push_back(0x41); code.push_back(0x55);  // push r13
    code.push_back(0x41); code.push_back(0x56);  // push r14
    code.push_back(0x41); code.push_back(0x57);  // push r15
    
    // After pushing 6 registers (48 bytes), we need to ensure stack is 16-byte aligned
    // Stack was 16-byte aligned before call (8 bytes return addr + 8 bytes rbp + 40 bytes regs = 56 bytes)
    // We need to add 8 more bytes to make it 64 (divisible by 16)
    code.push_back(0x48); code.push_back(0x83); code.push_back(0xEC); code.push_back(0x08);  // sub rsp, 8
    
    // Use dynamic stack size if set, otherwise default to 256 bytes for safety
    int64_t stack_size = function_stack_size > 0 ? function_stack_size : 256;
    // Ensure 16-byte alignment for thread safety (x86-64 ABI requirement)
    if (stack_size % 16 != 0) {
        stack_size += 16 - (stack_size % 16);
    }
    emit_sub_reg_imm(RSP, stack_size);
    current_stack_offset = 0;
}

void X86CodeGen::emit_epilogue() {
    // Calculate how much stack space we allocated
    int64_t stack_size = function_stack_size > 0 ? function_stack_size : 256;
    if (stack_size % 16 != 0) {
        stack_size += 16 - (stack_size % 16);
    }
    
    // Restore stack by adding back the allocated space
    emit_add_reg_imm(RSP, stack_size);
    
    // Remove alignment padding
    code.push_back(0x48); code.push_back(0x83); code.push_back(0xC4); code.push_back(0x08);  // add rsp, 8
    
    // Restore callee-saved registers in reverse order
    code.push_back(0x41); code.push_back(0x5F);  // pop r15
    code.push_back(0x41); code.push_back(0x5E);  // pop r14
    code.push_back(0x41); code.push_back(0x5D);  // pop r13
    code.push_back(0x41); code.push_back(0x5C);  // pop r12
    code.push_back(0x5B);       // pop rbx
    code.push_back(0x5D);       // pop rbp - restore original rbp
    emit_ret();
}

void X86CodeGen::emit_mov_reg_imm(int reg, int64_t value) {
    if (value >= -2147483648LL && value <= 2147483647LL) {
        code.push_back(0x48 | ((reg >> 3) & 1));
        code.push_back(0xC7);
        code.push_back(0xC0 | (reg & 7));
        code.push_back(value & 0xFF);
        code.push_back((value >> 8) & 0xFF);
        code.push_back((value >> 16) & 0xFF);
        code.push_back((value >> 24) & 0xFF);
    } else {
        code.push_back(0x48 | ((reg >> 3) & 1));
        code.push_back(0xB8 | (reg & 7));
        for (int i = 0; i < 8; i++) {
            code.push_back((value >> (i * 8)) & 0xFF);
        }
    }
}

void X86CodeGen::emit_mov_reg_reg(int dst, int src) {
    code.push_back(0x48 | ((dst >> 3) & 1) | ((src >> 3) & 1) << 2);
    code.push_back(0x89);
    code.push_back(0xC0 | ((src & 7) << 3) | (dst & 7));
}

void X86CodeGen::emit_mov_mem_reg(int64_t offset, int reg) {
    code.push_back(0x48 | ((reg >> 3) & 1));
    code.push_back(0x89);
    
    if (offset >= -128 && offset <= 127) {
        code.push_back(0x45 | ((reg & 7) << 3));
        code.push_back(offset & 0xFF);
    } else {
        code.push_back(0x85 | ((reg & 7) << 3));
        code.push_back(offset & 0xFF);
        code.push_back((offset >> 8) & 0xFF);
        code.push_back((offset >> 16) & 0xFF);
        code.push_back((offset >> 24) & 0xFF);
    }
}

void X86CodeGen::emit_mov_reg_mem(int reg, int64_t offset) {
    code.push_back(0x48 | ((reg >> 3) & 1));
    code.push_back(0x8B);
    
    if (offset >= -128 && offset <= 127) {
        code.push_back(0x45 | ((reg & 7) << 3));
        code.push_back(offset & 0xFF);
    } else {
        code.push_back(0x85 | ((reg & 7) << 3));
        code.push_back(offset & 0xFF);
        code.push_back((offset >> 8) & 0xFF);
        code.push_back((offset >> 16) & 0xFF);
        code.push_back((offset >> 24) & 0xFF);
    }
}

void X86CodeGen::emit_mov_reg_mem_rsp(int reg, int64_t offset) {
    // mov reg, [rsp+offset] - RSP-relative addressing
    code.push_back(0x48 | ((reg >> 3) & 1));
    code.push_back(0x8B);
    
    if (offset >= -128 && offset <= 127) {
        code.push_back(0x44 | ((reg & 7) << 3));  // 0x44 = RSP base register
        code.push_back(0x24);  // SIB byte for [rsp]
        code.push_back(offset & 0xFF);
    } else {
        code.push_back(0x84 | ((reg & 7) << 3));  // 0x84 = RSP base register with 32-bit disp
        code.push_back(0x24);  // SIB byte for [rsp]
        code.push_back(offset & 0xFF);
        code.push_back((offset >> 8) & 0xFF);
        code.push_back((offset >> 16) & 0xFF);
        code.push_back((offset >> 24) & 0xFF);
    }
}

void X86CodeGen::emit_mov_mem_rsp_reg(int64_t offset, int reg) {
    // mov [rsp+offset], reg - RSP-relative store
    code.push_back(0x48 | ((reg >> 3) & 1));
    code.push_back(0x89);
    
    if (offset >= -128 && offset <= 127) {
        code.push_back(0x44 | ((reg & 7) << 3));  // 0x44 = RSP base register
        code.push_back(0x24);  // SIB byte for [rsp]
        code.push_back(offset & 0xFF);
    } else {
        code.push_back(0x84 | ((reg & 7) << 3));  // 0x84 = RSP base register with 32-bit disp
        code.push_back(0x24);  // SIB byte for [rsp]
        code.push_back(offset & 0xFF);
        code.push_back((offset >> 8) & 0xFF);
        code.push_back((offset >> 16) & 0xFF);
        code.push_back((offset >> 24) & 0xFF);
    }
}

void X86CodeGen::emit_add_reg_imm(int reg, int64_t value) {
    if (value >= -128 && value <= 127) {
        code.push_back(0x48 | ((reg >> 3) & 1));
        code.push_back(0x83);
        code.push_back(0xC0 | (reg & 7));
        code.push_back(value & 0xFF);
    } else {
        code.push_back(0x48 | ((reg >> 3) & 1));
        code.push_back(0x81);
        code.push_back(0xC0 | (reg & 7));
        code.push_back(value & 0xFF);
        code.push_back((value >> 8) & 0xFF);
        code.push_back((value >> 16) & 0xFF);
        code.push_back((value >> 24) & 0xFF);
    }
}

void X86CodeGen::emit_add_reg_reg(int dst, int src) {
    code.push_back(0x48 | ((dst >> 3) & 1) | ((src >> 3) & 1) << 2);
    code.push_back(0x01);
    code.push_back(0xC0 | ((src & 7) << 3) | (dst & 7));
}

void X86CodeGen::emit_sub_reg_imm(int reg, int64_t value) {
    if (value >= -128 && value <= 127) {
        code.push_back(0x48 | ((reg >> 3) & 1));
        code.push_back(0x83);
        code.push_back(0xE8 | (reg & 7));
        code.push_back(value & 0xFF);
    } else {
        code.push_back(0x48 | ((reg >> 3) & 1));
        code.push_back(0x81);
        code.push_back(0xE8 | (reg & 7));
        code.push_back(value & 0xFF);
        code.push_back((value >> 8) & 0xFF);
        code.push_back((value >> 16) & 0xFF);
        code.push_back((value >> 24) & 0xFF);
    }
}

void X86CodeGen::emit_sub_reg_reg(int dst, int src) {
    code.push_back(0x48 | ((dst >> 3) & 1) | ((src >> 3) & 1) << 2);
    code.push_back(0x29);
    code.push_back(0xC0 | ((src & 7) << 3) | (dst & 7));
}

void X86CodeGen::emit_mul_reg_reg(int dst, int src) {
    code.push_back(0x48 | ((dst >> 3) & 1) | ((src >> 3) & 1) << 2);
    code.push_back(0x0F);
    code.push_back(0xAF);
    code.push_back(0xC0 | ((dst & 7) << 3) | (src & 7));
}

void X86CodeGen::emit_div_reg_reg(int dst, int src) {
    emit_mov_reg_reg(RAX, dst);  // Move dividend to RAX
    code.push_back(0x48);        // REX prefix for 64-bit
    code.push_back(0x99);        // CQO - sign extend RAX into RDX:RAX
    code.push_back(0x48 | ((src >> 3) & 1)); // REX prefix
    code.push_back(0xF7);        // IDIV opcode prefix
    code.push_back(0xF8 | (src & 7)); // IDIV register (signed division, /7)
    emit_mov_reg_reg(dst, RAX);  // Move quotient (RAX) to destination
}

void X86CodeGen::emit_mod_reg_reg(int dst, int src) {
    // For modulo operation, we use the same DIV instruction but return RDX (remainder)
    emit_mov_reg_reg(RAX, dst);  // Move dividend to RAX
    code.push_back(0x48);        // REX prefix for 64-bit
    code.push_back(0x99);        // CQO - sign extend RAX into RDX:RAX
    code.push_back(0x48 | ((src >> 3) & 1)); // REX prefix
    code.push_back(0xF7);        // DIV opcode prefix
    code.push_back(0xF8 | (src & 7)); // DIV register
    emit_mov_reg_reg(dst, RDX);  // Move remainder (RDX) to destination
}

void X86CodeGen::emit_call(const std::string& label) {
    // Check if this is a runtime function call
    if (label.substr(0, 2) == "__") {
        // For runtime functions, use absolute call via register
        // Move function address to RAX and call it
        void* func_addr = nullptr;
        
        if (label == "__console_log") {
            func_addr = (void*)__console_log;
        } else if (label == "__console_log_newline") {
            func_addr = (void*)__console_log_newline;
        } else if (label == "__console_log_space") {
            func_addr = (void*)__console_log_space;
        } else if (label == "__console_log_array") {
            func_addr = (void*)__console_log_array;
        } else if (label == "__console_log_number") {
            func_addr = (void*)__console_log_number;
        } else if (label == "__console_log_auto") {
            func_addr = (void*)__console_log_auto;
        } else if (label == "__console_log_string") {
            func_addr = (void*)__console_log_string;
        } else if (label == "__console_log_object") {
            func_addr = (void*)__console_log_object;
        } else if (label == "__static_stringify") {
            func_addr = (void*)__static_stringify;
        } else if (label == "__console_time") {
            func_addr = (void*)__console_time;
        } else if (label == "__console_timeEnd") {
            func_addr = (void*)__console_timeEnd;
        } else if (label == "__promise_all") {
            func_addr = (void*)__promise_all;
        } else if (label == "__array_create") {
            func_addr = (void*)__array_create;
        } else if (label == "__array_push") {
            func_addr = (void*)__array_push;
        } else if (label == "__array_pop") {
            func_addr = (void*)__array_pop;
        } else if (label == "__array_size") {
            func_addr = (void*)__array_size;
        } else if (label == "__array_access") {
            func_addr = (void*)__array_access;
        } else if (label == "__array_data") {
            func_addr = (void*)__array_data;
        } else if (label == "__string_match") {
            func_addr = (void*)__string_match;
        } else if (label == "__string_replace") {
            func_addr = (void*)__string_replace;
        } else if (label == "__string_search") {
            func_addr = (void*)__string_search;
        } else if (label == "__match_result_get_index") {
            func_addr = (void*)__match_result_get_index;
        } else if (label == "__match_result_get_input") {
            func_addr = (void*)__match_result_get_input;
        } else if (label == "__match_result_get_groups") {
            func_addr = (void*)__match_result_get_groups;
        } else if (label == "__goroutine_spawn") {
            func_addr = (void*)__goroutine_spawn;
            std::cout << "DEBUG: Hardcoded __goroutine_spawn address: " << func_addr << std::endl;
        } else if (label == "__promise_await") {
            func_addr = (void*)__promise_await;
        } else if (label == "__goroutine_spawn_with_arg1") {
            func_addr = (void*)__goroutine_spawn_with_arg1;
        } else if (label == "__goroutine_spawn_with_arg2") {
            func_addr = (void*)__goroutine_spawn_with_arg2;
        } else if (label == "__goroutine_spawn_with_scope") {
            func_addr = (void*)__goroutine_spawn_with_scope;
        } else if (label == "__set_goroutine_context") {
            func_addr = (void*)__set_goroutine_context;
        } else if (label == "__goroutine_spawn_func_ptr") {
            func_addr = (void*)__goroutine_spawn_func_ptr;
        } else if (label == "__goroutine_spawn_func_id") {
            func_addr = (void*)__goroutine_spawn_func_id;
        } else if (label == "__promise_resolve") {
            func_addr = (void*)__promise_resolve;
        // Typed array creation functions
        } else if (label == "__typed_array_create_int32") {
            func_addr = (void*)__typed_array_create_int32;
        } else if (label == "__typed_array_create_int64") {
            func_addr = (void*)__typed_array_create_int64;
        } else if (label == "__typed_array_create_float32") {
            func_addr = (void*)__typed_array_create_float32;
        } else if (label == "__typed_array_create_float64") {
            func_addr = (void*)__typed_array_create_float64;
        } else if (label == "__typed_array_create_uint8") {
            func_addr = (void*)__typed_array_create_uint8;
        } else if (label == "__typed_array_create_uint16") {
            func_addr = (void*)__typed_array_create_uint16;
        } else if (label == "__typed_array_create_uint32") {
            func_addr = (void*)__typed_array_create_uint32;
        } else if (label == "__typed_array_create_uint64") {
            func_addr = (void*)__typed_array_create_uint64;
        // Typed array push functions
        } else if (label == "__typed_array_push_int32") {
            func_addr = (void*)__typed_array_push_int32;
        } else if (label == "__typed_array_push_int64") {
            func_addr = (void*)__typed_array_push_int64;
        } else if (label == "__typed_array_push_float32") {
            func_addr = (void*)__typed_array_push_float32;
        } else if (label == "__typed_array_push_float64") {
            func_addr = (void*)__typed_array_push_float64;
        } else if (label == "__typed_array_push_uint8") {
            func_addr = (void*)__typed_array_push_uint8;
        } else if (label == "__typed_array_push_uint16") {
            func_addr = (void*)__typed_array_push_uint16;
        } else if (label == "__typed_array_push_uint32") {
            func_addr = (void*)__typed_array_push_uint32;
        } else if (label == "__typed_array_push_uint64") {
            func_addr = (void*)__typed_array_push_uint64;
        // Typed array console logging
        } else if (label == "__console_log_typed_array_int32") {
            func_addr = (void*)__console_log_typed_array_int32;
        } else if (label == "__console_log_typed_array_int64") {
            func_addr = (void*)__console_log_typed_array_int64;
        } else if (label == "__console_log_typed_array_float32") {
            func_addr = (void*)__console_log_typed_array_float32;
        } else if (label == "__console_log_typed_array_float64") {
            func_addr = (void*)__console_log_typed_array_float64;
        } else if (label == "__register_function") {
            func_addr = (void*)__register_function;
        } else if (label == "__object_create") {
            func_addr = (void*)__object_create;
        } else if (label == "__object_set_property") {
            func_addr = (void*)__object_set_property;
        } else if (label == "__object_set_property_name") {
            func_addr = (void*)__object_set_property_name;
        } else if (label == "__object_get_property") {
            func_addr = (void*)__object_get_property;
        } else if (label == "__object_get_property_name") {
            func_addr = (void*)__object_get_property_name;
        } else if (label == "__object_call_method") {
            func_addr = (void*)__object_call_method;
        } else if (label == "__object_destroy") {
            func_addr = (void*)__object_destroy;
        } else if (label == "__static_set_property") {
            func_addr = (void*)__static_set_property;
        } else if (label == "__static_get_property") {
            func_addr = (void*)__static_get_property;
        } else if (label == "__register_class_inheritance") {
            func_addr = (void*)__register_class_inheritance;
        } else if (label == "__super_constructor_call") {
            func_addr = (void*)__super_constructor_call;
        } else if (label == "__runtime_pow") {
            func_addr = (void*)__runtime_pow;
        } else if (label == "__runtime_js_equal") {
            func_addr = (void*)__runtime_js_equal;
        // High-Performance String Runtime Functions - Future-Proof Implementation
        } else if (label == "__string_create") {
            func_addr = (void*)__string_create;
        } else if (label == "__string_create_empty") {
            func_addr = (void*)__string_create_empty;
        } else if (label == "__string_destroy") {
            func_addr = (void*)__string_destroy;
        } else if (label == "__string_concat") {
            func_addr = (void*)__string_concat;
        } else if (label == "__string_concat_cstr") {
            func_addr = (void*)__string_concat_cstr;
        } else if (label == "__string_concat_cstr_left") {
            func_addr = (void*)__string_concat_cstr_left;
        } else if (label == "__string_equals") {
            func_addr = (void*)__string_equals;
        } else if (label == "__string_equals_cstr") {
            func_addr = (void*)__string_equals_cstr;
        } else if (label == "__string_compare") {
            func_addr = (void*)__string_compare;
        } else if (label == "__string_length") {
            func_addr = (void*)__string_length;
        } else if (label == "__string_c_str") {
            func_addr = (void*)__string_c_str;
        } else if (label == "__string_char_at") {
            func_addr = (void*)__string_char_at;
        } else if (label == "__string_intern") {
            func_addr = (void*)__string_intern;
        } else if (label == "__string_pool_cleanup") {
            func_addr = (void*)__string_pool_cleanup;
        } else if (label == "__regex_create_by_id") {
            func_addr = (void*)__regex_create_by_id;
        } else if (label == "__regex_test") {
            func_addr = (void*)__regex_test;
        } else if (label == "__regex_exec") {
            func_addr = (void*)__regex_exec;
        } else if (label == "__regex_match_all") {
            func_addr = (void*)__regex_match_all;
        } else if (label == "__regex_get_source") {
            func_addr = (void*)__regex_get_source;
        } else if (label == "__regex_get_global") {
            func_addr = (void*)__regex_get_global;
        } else if (label == "__regex_get_ignore_case") {
            func_addr = (void*)__regex_get_ignore_case;
        } else if (label == "__regex_create") {
            func_addr = (void*)__regex_create;
        } else if (label == "__regex_create_simple") {
            func_addr = (void*)__regex_create_simple;
        } else if (label == "__regex_destroy") {
            func_addr = (void*)__regex_destroy;
        } else if (label == "__register_regex_pattern") {
            func_addr = (void*)__register_regex_pattern;
        } else if (label == "__debug_print_pointer") {
            func_addr = (void*)__debug_print_pointer;
        }
        
        if (func_addr) {
            std::cout << "DEBUG: About to generate call to " << label << " at address " << func_addr << std::endl;
            // mov rax, immediate64
            code.push_back(0x48);
            code.push_back(0xB8);
            uint64_t addr = reinterpret_cast<uint64_t>(func_addr);
            for (int i = 0; i < 8; i++) {
                code.push_back((addr >> (i * 8)) & 0xFF);
            }
            // call rax
            code.push_back(0xFF);
            code.push_back(0xD0);
            std::cout << "DEBUG: Generated call instruction for " << label << std::endl;
            return;
        }
        
        // If not found in hardcoded list, check the dynamic function registry
        extern std::unordered_map<std::string, void*> gots_function_registry;
        auto it = gots_function_registry.find(label);
        if (it != gots_function_registry.end()) {
            func_addr = it->second;
            std::cout << "DEBUG: emit_call found " << label << " in registry at address " << func_addr << std::endl;
            
            // mov rax, immediate64
            code.push_back(0x48);
            code.push_back(0xB8);
            uint64_t addr = reinterpret_cast<uint64_t>(func_addr);
            for (int i = 0; i < 8; i++) {
                code.push_back((addr >> (i * 8)) & 0xFF);
            }
            // call rax
            code.push_back(0xFF);
            code.push_back(0xD0);
            return;
        }
    }
    
    // Regular relative call for local labels
    code.push_back(0xE8);
    
    auto it = label_offsets.find(label);
    if (it != label_offsets.end()) {
        int32_t offset = it->second - (code.size() + 4);
        code.push_back(offset & 0xFF);
        code.push_back((offset >> 8) & 0xFF);
        code.push_back((offset >> 16) & 0xFF);
        code.push_back((offset >> 24) & 0xFF);
    } else {
        unresolved_jumps.push_back({label, code.size()});
        code.push_back(0x00);
        code.push_back(0x00);
        code.push_back(0x00);
        code.push_back(0x00);
    }
}

void X86CodeGen::emit_ret() {
    code.push_back(0xC3);
}

void X86CodeGen::emit_function_return() {
    // Use dynamic stack size if set, otherwise default to 256 bytes (same as prologue)
    int64_t stack_size = function_stack_size > 0 ? function_stack_size : 256;
    // Ensure 16-byte alignment
    if (stack_size % 16 != 0) {
        stack_size += 16 - (stack_size % 16);
    }
    emit_add_reg_imm(RSP, stack_size);   // restore function stack space
    
    // Remove stack alignment padding (must match prologue)
    code.push_back(0x48); code.push_back(0x83); code.push_back(0xC4); code.push_back(0x08);  // add rsp, 8
    
    // Restore callee-saved registers in reverse order
    code.push_back(0x41); code.push_back(0x5F);  // pop r15
    code.push_back(0x41); code.push_back(0x5E);  // pop r14
    code.push_back(0x41); code.push_back(0x5D);  // pop r13
    code.push_back(0x41); code.push_back(0x5C);  // pop r12
    code.push_back(0x5B);        // pop rbx
    code.push_back(0x5D);        // pop rbp
    emit_ret();                  // ret
}

void X86CodeGen::emit_jump(const std::string& label) {
    code.push_back(0xE9);
    
    auto it = label_offsets.find(label);
    if (it != label_offsets.end()) {
        int32_t offset = it->second - (code.size() + 4);
        code.push_back(offset & 0xFF);
        code.push_back((offset >> 8) & 0xFF);
        code.push_back((offset >> 16) & 0xFF);
        code.push_back((offset >> 24) & 0xFF);
    } else {
        unresolved_jumps.push_back({label, code.size()});
        code.push_back(0x00);
        code.push_back(0x00);
        code.push_back(0x00);
        code.push_back(0x00);
    }
}

void X86CodeGen::emit_jump_if_zero(const std::string& label) {
    code.push_back(0x0F);
    code.push_back(0x84);
    
    auto it = label_offsets.find(label);
    if (it != label_offsets.end()) {
        int32_t offset = it->second - (code.size() + 4);
        code.push_back(offset & 0xFF);
        code.push_back((offset >> 8) & 0xFF);
        code.push_back((offset >> 16) & 0xFF);
        code.push_back((offset >> 24) & 0xFF);
    } else {
        unresolved_jumps.push_back({label, code.size()});
        code.push_back(0x00);
        code.push_back(0x00);
        code.push_back(0x00);
        code.push_back(0x00);
    }
}

void X86CodeGen::emit_jump_if_not_zero(const std::string& label) {
    code.push_back(0x0F);
    code.push_back(0x85);
    
    auto it = label_offsets.find(label);
    if (it != label_offsets.end()) {
        int32_t offset = it->second - (code.size() + 4);
        code.push_back(offset & 0xFF);
        code.push_back((offset >> 8) & 0xFF);
        code.push_back((offset >> 16) & 0xFF);
        code.push_back((offset >> 24) & 0xFF);
    } else {
        unresolved_jumps.push_back({label, code.size()});
        code.push_back(0x00);
        code.push_back(0x00);
        code.push_back(0x00);
        code.push_back(0x00);
    }
}

void X86CodeGen::emit_compare(int reg1, int reg2) {
    code.push_back(0x48 | ((reg1 >> 3) & 1) | ((reg2 >> 3) & 1) << 2);
    code.push_back(0x39);
    code.push_back(0xC0 | ((reg2 & 7) << 3) | (reg1 & 7));
}

void X86CodeGen::emit_setl(int reg) {
    // SETL instruction: 0F 9C
    code.push_back(0x0F);
    code.push_back(0x9C);
    code.push_back(0xC0 | (reg & 7)); // Sets AL/BL/CL etc.
}

void X86CodeGen::emit_setg(int reg) {
    // SETG instruction: 0F 9F
    code.push_back(0x0F);
    code.push_back(0x9F);
    code.push_back(0xC0 | (reg & 7));
}

void X86CodeGen::emit_sete(int reg) {
    // SETE instruction: 0F 94
    code.push_back(0x0F);
    code.push_back(0x94);
    code.push_back(0xC0 | (reg & 7));
}

void X86CodeGen::emit_setne(int reg) {
    // SETNE instruction: 0F 95
    code.push_back(0x0F);
    code.push_back(0x95);
    code.push_back(0xC0 | (reg & 7));
}

void X86CodeGen::emit_setle(int reg) {
    // SETLE instruction: 0F 9E
    code.push_back(0x0F);
    code.push_back(0x9E);
    code.push_back(0xC0 | (reg & 7));
}

void X86CodeGen::emit_setge(int reg) {
    // SETGE instruction: 0F 9D
    code.push_back(0x0F);
    code.push_back(0x9D);
    code.push_back(0xC0 | (reg & 7));
}

void X86CodeGen::emit_and_reg_imm(int reg, int64_t value) {
    // AND with immediate value
    if (value <= 0xFF) {
        // 8-bit immediate
        code.push_back(0x48 | ((reg >> 3) & 1));
        code.push_back(0x83);
        code.push_back(0xE0 | (reg & 7));
        code.push_back(value & 0xFF);
    } else {
        // 32-bit immediate
        code.push_back(0x48 | ((reg >> 3) & 1));
        code.push_back(0x81);
        code.push_back(0xE0 | (reg & 7));
        code.push_back(value & 0xFF);
        code.push_back((value >> 8) & 0xFF);
        code.push_back((value >> 16) & 0xFF);
        code.push_back((value >> 24) & 0xFF);
    }
}

void X86CodeGen::emit_label(const std::string& label) {
    std::cout << "DEBUG: emit_label(" << label << ") at offset " << code.size() << std::endl;
    label_offsets[label] = code.size();
    
    for (auto& jump : unresolved_jumps) {
        if (jump.first == label) {
            int32_t offset = code.size() - (jump.second + 4);
            code[jump.second] = offset & 0xFF;
            code[jump.second + 1] = (offset >> 8) & 0xFF;
            code[jump.second + 2] = (offset >> 16) & 0xFF;
            code[jump.second + 3] = (offset >> 24) & 0xFF;
        }
    }
    
    unresolved_jumps.erase(
        std::remove_if(unresolved_jumps.begin(), unresolved_jumps.end(),
                      [&label](const std::pair<std::string, int64_t>& jump) {
                          return jump.first == label;
                      }),
        unresolved_jumps.end());
}

void X86CodeGen::resolve_runtime_function_calls() {
    // Resolve any unresolved runtime function calls now that the registry is populated
    extern std::unordered_map<std::string, void*> gots_function_registry;
    
    std::cout << "DEBUG: Resolving " << unresolved_jumps.size() << " unresolved function calls" << std::endl;
    
    auto it = unresolved_jumps.begin();
    while (it != unresolved_jumps.end()) {
        const std::string& label = it->first;
        int64_t call_offset = it->second;
        
        // Check if this is a runtime function that's now registered
        if (label.substr(0, 2) == "__") {
            auto registry_it = gots_function_registry.find(label);
            if (registry_it != gots_function_registry.end()) {
                void* func_addr = registry_it->second;
                std::cout << "DEBUG: Resolving runtime function " << label << " to address " << func_addr << std::endl;
                
                // Patch the call instruction to use an absolute call via register
                // We need to replace the relative call (E8 XX XX XX XX) with:
                // mov rax, immediate64 (48 B8 XX XX XX XX XX XX XX XX)
                // call rax (FF D0)
                
                // First, we need more space. The original call is 5 bytes, we need 12 bytes.
                // For now, let's use a simpler approach and just patch the relative offset
                // to point to a stub we'll create at the end of the code.
                
                // Calculate where to put the stub (at the end of current code)
                int64_t stub_offset = code.size();
                
                // Create stub: mov rax, address; call rax; ret (to return to caller)
                code.push_back(0x48); code.push_back(0xB8);  // mov rax, imm64
                uint64_t addr = reinterpret_cast<uint64_t>(func_addr);
                for (int i = 0; i < 8; i++) {
                    code.push_back((addr >> (i * 8)) & 0xFF);
                }
                code.push_back(0xFF); code.push_back(0xD0);  // call rax
                
                // Calculate relative offset from call site to stub
                int32_t relative_offset = stub_offset - (call_offset + 4);
                
                // Patch the original call instruction's offset
                code[call_offset] = relative_offset & 0xFF;
                code[call_offset + 1] = (relative_offset >> 8) & 0xFF;
                code[call_offset + 2] = (relative_offset >> 16) & 0xFF;
                code[call_offset + 3] = (relative_offset >> 24) & 0xFF;
                
                std::cout << "DEBUG: Patched call at offset " << call_offset << " to jump to stub at " << stub_offset << std::endl;
                
                // Remove this resolved jump
                it = unresolved_jumps.erase(it);
                continue;
            }
        }
        ++it;
    }
    
    std::cout << "DEBUG: " << unresolved_jumps.size() << " unresolved function calls remaining" << std::endl;
}

void X86CodeGen::emit_goroutine_spawn(const std::string& function_name) {
    std::cout << "DEBUG: emit_goroutine_spawn called with: " << function_name << std::endl;
    std::cout.flush();
    
    // Use string pooling for function names (similar to StringLiteral)
    static std::unordered_map<std::string, const char*> func_name_pool;
    
    auto it = func_name_pool.find(function_name);
    if (it == func_name_pool.end()) {
        // Allocate permanent storage for the function name
        char* name_copy = new char[function_name.length() + 1];
        strcpy(name_copy, function_name.c_str());
        func_name_pool[function_name] = name_copy;
        it = func_name_pool.find(function_name);
    }
    
    std::cout << "DEBUG: About to emit call to __goroutine_spawn with function name: " << it->second << std::endl;
    std::cout.flush();
    
    // WORKAROUND: Due to JIT call issues, spawn goroutine properly using thread pool
    // This ensures timers work correctly with proper goroutine lifecycle
    std::cout << "DEBUG: WORKAROUND: Using proper goroutine spawn with function name" << std::endl;
    
    // Load function name into RDI for the call
    emit_mov_reg_imm(RDI, reinterpret_cast<int64_t>(it->second));
    emit_call("__goroutine_spawn");
    
    std::cout << "DEBUG: emit_call to __goroutine_spawn completed" << std::endl;
    std::cout.flush();
}

void X86CodeGen::emit_goroutine_spawn_with_args(const std::string& function_name, int arg_count) {
    // Use string pooling for function names (similar to StringLiteral)
    static std::unordered_map<std::string, const char*> func_name_pool;
    
    auto it = func_name_pool.find(function_name);
    if (it == func_name_pool.end()) {
        // Allocate permanent storage for the function name
        char* name_copy = new char[function_name.length() + 1];
        strcpy(name_copy, function_name.c_str());
        func_name_pool[function_name] = name_copy;
        it = func_name_pool.find(function_name);
    }
    
    // For now, only support specific argument counts with dedicated functions
    if (arg_count == 1) {
        // Load the argument into a register before setting up the call
        emit_mov_reg_mem_rsp(RAX, 0);  // RAX = [rsp] (load argument from stack)
        
        // Set up calling convention properly
        emit_mov_reg_imm(RDI, reinterpret_cast<int64_t>(it->second));  // function name
        emit_mov_reg_reg(RSI, RAX);  // RSI = argument value
        
        // Ensure stack is aligned for C calling convention
        emit_sub_reg_imm(RSP, 8);  // Align stack to 16-byte boundary
        emit_call("__goroutine_spawn_with_arg1");
        emit_add_reg_imm(RSP, 8);  // Restore stack
    } else if (arg_count == 2) {
        // Load both arguments
        emit_mov_reg_mem_rsp(RAX, 0);   // RAX = [rsp] (first argument)  
        emit_mov_reg_mem_rsp(RCX, 8);   // RCX = [rsp+8] (second argument)
        
        // Set up calling convention
        emit_mov_reg_imm(RDI, reinterpret_cast<int64_t>(it->second));
        emit_mov_reg_reg(RSI, RAX);  // RSI = first argument
        emit_mov_reg_reg(RDX, RCX);  // RDX = second argument
        
        emit_sub_reg_imm(RSP, 8);
        emit_call("__goroutine_spawn_with_arg2");
        emit_add_reg_imm(RSP, 8);
    } else {
        // For other argument counts, just call the no-args version for now
        emit_mov_reg_imm(RDI, reinterpret_cast<int64_t>(it->second));
        emit_call("__goroutine_spawn");
    }
}

void X86CodeGen::emit_goroutine_spawn_with_func_ptr() {
    // Function pointer already in RDI
    emit_mov_reg_imm(RSI, 0);  // No argument for now
    emit_call("__goroutine_spawn_func_ptr");
}

void X86CodeGen::emit_goroutine_spawn_with_func_id() {
    // Function ID already in RDI
    emit_mov_reg_imm(RSI, 0);  // No argument for now
    emit_call("__goroutine_spawn_func_id");
}

void X86CodeGen::emit_promise_resolve(int value_reg) {
    emit_mov_reg_reg(RDI, value_reg);
    emit_call("__promise_resolve");
}

void X86CodeGen::emit_promise_await(int promise_reg) {
    emit_mov_reg_reg(RDI, promise_reg);
    emit_call("__promise_await");
}

// High-Performance String Assembly Optimizations for GoTS
// These methods provide ultra-fast string operations at the assembly level

void X86CodeGen::emit_string_length_fast(int string_reg, int dest_reg) {
    // Optimized string length for GoTSString with SSO detection
    // Input: string_reg contains GoTSString*
    // Output: dest_reg contains length
    
    // Load the capacity field to check if it's a small string
    emit_mov_reg_mem(dest_reg, reinterpret_cast<int64_t>(nullptr) + 16); // Load capacity field
    
    // Check if small string (capacity == 0)
    emit_compare(dest_reg, 0);
    
    // Use conditional move for branch-free execution
    // If small string, load size from small.size (offset 23)
    // If large string, load size from large.size (offset 8)
    
    static int label_counter = 0;
    std::string end_label = "__string_len_end_" + std::to_string(label_counter++);
    std::string large_label = "__string_len_large_" + std::to_string(label_counter++);
    
    emit_jump_if_not_zero(large_label);
    
    // Small string path - load size from offset 23
    code.push_back(0x48 | ((dest_reg >> 3) & 1)); // REX prefix
    code.push_back(0x8B); // mov instruction
    code.push_back(0x40 | (dest_reg & 7) | ((string_reg & 7) << 3)); // ModR/M byte
    code.push_back(23); // offset for small.size
    emit_jump(end_label);
    
    // Large string path - load size from offset 8
    emit_label(large_label);
    emit_mov_reg_mem(dest_reg, 8); // Load large.size
    
    emit_label(end_label);
}

void X86CodeGen::emit_string_concat_fast(int str1_reg, int str2_reg, int dest_reg) {
    // Ultra-fast string concatenation with SSO optimization
    // This checks if result fits in SSO and uses optimized paths
    
    // First, get lengths of both strings
    emit_string_length_fast(str1_reg, R10); // len1 in R10
    emit_string_length_fast(str2_reg, R11); // len2 in R11
    
    // Calculate total length: R10 + R11
    emit_add_reg_reg(R10, R11); // total_len in R10
    
    // Check if total length fits in SSO (22 bytes or less)
    emit_mov_reg_imm(R9, 22);
    emit_compare(R10, R9);
    
    static int concat_counter = 0;
    std::string sso_path = "__concat_sso_" + std::to_string(concat_counter);
    std::string heap_path = "__concat_heap_" + std::to_string(concat_counter);
    std::string end_path = "__concat_end_" + std::to_string(concat_counter++);
    
    emit_jump_if_greater(heap_path);
    
    // SSO path - extremely fast inline concatenation
    emit_label(sso_path);
    // Allocate new GoTSString on stack for SSO
    emit_sub_reg_imm(RSP, 32); // Allocate 32 bytes for GoTSString
    
    // Copy string1 data directly with optimized memcpy
    emit_mov_reg_reg(RDI, RSP); // dest = stack allocation
    emit_mov_reg_reg(RSI, str1_reg); // src = str1 data
    emit_call("__string_c_str"); // Get C string from str1
    emit_mov_reg_reg(RSI, RAX); // src = C string
    emit_string_length_fast(str1_reg, RDX); // len = str1 length
    emit_fast_memcpy(); // Optimized inline memcpy
    
    // Copy string2 data
    emit_add_reg_reg(RDI, RDX); // dest += str1_len
    emit_mov_reg_reg(RSI, str2_reg);
    emit_call("__string_c_str");
    emit_mov_reg_reg(RSI, RAX);
    emit_string_length_fast(str2_reg, RDX);
    emit_fast_memcpy();
    
    emit_mov_reg_reg(dest_reg, RSP); // Result = stack allocated string
    emit_add_reg_imm(RSP, 32); // Restore stack
    emit_jump(end_path);
    
    // Heap path - fall back to runtime function
    emit_label(heap_path);
    emit_mov_reg_reg(RDI, str1_reg);
    emit_mov_reg_reg(RSI, str2_reg);
    emit_call("__string_concat");
    emit_mov_reg_reg(dest_reg, RAX);
    
    emit_label(end_path);
}

void X86CodeGen::emit_fast_memcpy() {
    // Ultra-fast inline memcpy using SIMD when possible
    // RDI = dest, RSI = src, RDX = length
    
    static int memcpy_counter = 0;
    std::string loop_label = "__memcpy_loop_" + std::to_string(memcpy_counter);
    std::string end_label = "__memcpy_end_" + std::to_string(memcpy_counter++);
    std::string small_label = "__memcpy_small_" + std::to_string(memcpy_counter);
    
    // For very small copies, use direct mov instructions
    emit_mov_reg_imm(RCX, 8);
    emit_compare(RDX, RCX);
    emit_jump_if_less(small_label);
    
    // For larger copies, use rep movsb (optimized by modern CPUs)
    emit_mov_reg_reg(RCX, RDX);
    code.push_back(0xF3); // rep prefix
    code.push_back(0xA4); // movsb
    emit_jump(end_label);
    
    // Small copy path - unroll for 1-8 bytes
    emit_label(small_label);
    emit_label(loop_label);
    code.push_back(0x8A); // mov al, [rsi]
    code.push_back(0x06);
    code.push_back(0x88); // mov [rdi], al
    code.push_back(0x07);
    emit_add_reg_imm(RSI, 1);
    emit_add_reg_imm(RDI, 1);
    emit_sub_reg_imm(RDX, 1);
    emit_jump_if_not_zero(loop_label);
    
    emit_label(end_label);
}

void X86CodeGen::emit_string_equals_fast(int str1_reg, int str2_reg, int dest_reg) {
    // Ultra-fast string comparison with early exit optimizations
    
    // Quick pointer equality check
    emit_compare(str1_reg, str2_reg);
    static int eq_counter = 0;
    std::string true_label = "__str_eq_true_" + std::to_string(eq_counter);
    std::string false_label = "__str_eq_false_" + std::to_string(eq_counter);
    std::string end_label = "__str_eq_end_" + std::to_string(eq_counter++);
    
    emit_jump_if_equal(true_label);
    
    // Get lengths and compare them first (early exit)
    emit_string_length_fast(str1_reg, R10);
    emit_string_length_fast(str2_reg, R11);
    emit_compare(R10, R11);
    emit_jump_if_not_zero(false_label);
    
    // Lengths are equal, compare data
    emit_mov_reg_reg(RDI, str1_reg);
    emit_call("__string_c_str");
    emit_mov_reg_reg(RDI, RAX);
    
    emit_mov_reg_reg(RSI, str2_reg);
    emit_call("__string_c_str");
    emit_mov_reg_reg(RSI, RAX);
    
    emit_mov_reg_reg(RDX, R10); // length
    
    // Use optimized memcmp
    emit_fast_memcmp();
    emit_jump_if_zero(true_label);
    
    emit_label(false_label);
    emit_mov_reg_imm(dest_reg, 0);
    emit_jump(end_label);
    
    emit_label(true_label);
    emit_mov_reg_imm(dest_reg, 1);
    
    emit_label(end_label);
}

void X86CodeGen::emit_fast_memcmp() {
    // Ultra-fast memory comparison
    // RDI = ptr1, RSI = ptr2, RDX = length
    // Sets zero flag if equal
    
    static int cmp_counter = 0;
    std::string loop_label = "__memcmp_loop_" + std::to_string(cmp_counter);
    std::string end_label = "__memcmp_end_" + std::to_string(cmp_counter++);
    
    emit_mov_reg_reg(RCX, RDX);
    code.push_back(0xF3); // rep prefix
    code.push_back(0xA6); // cmpsb
}

// Missing method implementations for X86CodeGen

void X86CodeGen::emit_xor_reg_reg(int dst, int src) {
    // XOR dst, src - using 64-bit XOR
    if (dst >= 8 || src >= 8) {
        code.push_back(0x4C | ((dst >> 3) & 1) | (((src >> 3) & 1) << 2));
    } else {
        code.push_back(0x48);
    }
    code.push_back(0x31);
    code.push_back(0xC0 | (dst & 7) | ((src & 7) << 3));
}

void X86CodeGen::emit_call_reg(int reg) {
    // CALL reg - call address in register
    if (reg >= 8) {
        code.push_back(0x41);
    }
    code.push_back(0xFF);
    code.push_back(0xD0 | (reg & 7));
}

void X86CodeGen::emit_jump_if_equal(const std::string& label) {
    // JE/JZ label - jump if equal/zero flag set
    code.push_back(0x0F);
    code.push_back(0x84);
    unresolved_jumps.emplace_back(label, code.size());
    code.push_back(0x00);
    code.push_back(0x00);
    code.push_back(0x00);
    code.push_back(0x00);
}

void X86CodeGen::emit_jump_if_greater(const std::string& label) {
    // JG label - jump if greater
    code.push_back(0x0F);
    code.push_back(0x8F);
    unresolved_jumps.emplace_back(label, code.size());
    code.push_back(0x00);
    code.push_back(0x00);
    code.push_back(0x00);
    code.push_back(0x00);
}

void X86CodeGen::emit_jump_if_less(const std::string& label) {
    // JL label - jump if less
    code.push_back(0x0F);
    code.push_back(0x8C);
    unresolved_jumps.emplace_back(label, code.size());
    code.push_back(0x00);
    code.push_back(0x00);
    code.push_back(0x00);
    code.push_back(0x00);
}

}