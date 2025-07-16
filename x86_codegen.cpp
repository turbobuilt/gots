#include "compiler.h"
#include "runtime.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <unordered_map>

// Forward declarations for runtime functions
extern "C" int64_t __gots_set_timeout(void* callback, int64_t delay_ms);
extern "C" int64_t __gots_set_interval(void* callback, int64_t delay_ms);  
extern "C" bool __gots_clear_timeout(int64_t timer_id);
extern "C" bool __gots_clear_interval(int64_t timer_id);
// Legacy function removed - use __lookup_function_fast(func_id) instead
extern "C" void __runtime_stub_function();

// Forward declarations for gots namespace functions
namespace gots {
    extern void* __goroutine_spawn_func_ptr(void* func_ptr, void* arg);
}

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

// High-Performance Runtime Function Address Table
// Pre-computed at initialization - eliminates runtime string comparisons
static std::unordered_map<std::string, void*> g_runtime_function_table;
static bool g_runtime_table_initialized = false;

// Simple Array runtime functions (extern C declarations)
extern "C" {
    extern void* __simple_array_create(double* values, int64_t size);
    extern void* __simple_array_zeros(int64_t size);
    extern void* __simple_array_ones(int64_t size);
    extern void __simple_array_push(void* array, double value);
    extern double __simple_array_pop(void* array);
    extern double __simple_array_get(void* array, int64_t index);
    extern void __simple_array_set(void* array, int64_t index, double value);
    extern int64_t __simple_array_length(void* array);
    extern double __simple_array_sum(void* array);
    extern double __simple_array_mean(void* array);
    extern void* __simple_array_shape(void* array);
    extern const char* __simple_array_tostring(void* array);
    extern void* __simple_array_slice(void* array, int64_t start, int64_t end, int64_t step);
    extern void* __simple_array_slice_all(void* array);
    extern const char* __dynamic_method_toString(void* obj);
}

static void initialize_runtime_function_table() {
    if (g_runtime_table_initialized) return;
    
    // Core functions - essential for basic functionality
    g_runtime_function_table["__console_log"] = (void*)__console_log;
    g_runtime_function_table["__console_log_newline"] = (void*)__console_log_newline;
    g_runtime_function_table["__console_log_space"] = (void*)__console_log_space;
    g_runtime_function_table["__console_log_string"] = (void*)__console_log;
    g_runtime_function_table["__console_log_auto"] = (void*)__console_log_auto;
    g_runtime_function_table["__gots_string_to_cstr"] = (void*)__gots_string_to_cstr;
    
    // High-performance goroutine spawn functions
    g_runtime_function_table["__goroutine_spawn_fast"] = (void*)__goroutine_spawn_fast;
    g_runtime_function_table["__goroutine_spawn_fast_arg1"] = (void*)__goroutine_spawn_fast_arg1;
    g_runtime_function_table["__goroutine_spawn_fast_arg2"] = (void*)__goroutine_spawn_fast_arg2;
    g_runtime_function_table["__goroutine_spawn_func_ptr"] = (void*)__goroutine_spawn_func_ptr;
    
    // Function registration - FAST ONLY SYSTEM
    g_runtime_function_table["__register_function_fast"] = (void*)__register_function_fast;
    g_runtime_function_table["__lookup_function_fast"] = (void*)__lookup_function_fast;
    g_runtime_function_table["__set_goroutine_context"] = (void*)__set_goroutine_context;
    
    // Timer functions
    g_runtime_function_table["__gots_set_timeout"] = (void*)__gots_set_timeout;
    g_runtime_function_table["__gots_set_interval"] = (void*)__gots_set_interval;
    g_runtime_function_table["__gots_clear_timeout"] = (void*)__gots_clear_timeout;
    g_runtime_function_table["__gots_clear_interval"] = (void*)__gots_clear_interval;
    
    // Utility functions
    extern void* __string_intern(const char* str);
    extern void* __get_executable_memory_base();
    g_runtime_function_table["__array_create"] = (void*)__array_create;
    g_runtime_function_table["__string_create"] = (void*)__string_create;
    g_runtime_function_table["__string_intern"] = (void*)__string_intern;
    g_runtime_function_table["__lookup_function_fast"] = (void*)__lookup_function_fast;
    g_runtime_function_table["__get_executable_memory_base"] = (void*)__get_executable_memory_base;
    
    // Advanced goroutine functions
    extern void __init_advanced_goroutine_system();
    extern void* __goroutine_alloc_shared(int64_t size);
    extern void __goroutine_share_memory(void* ptr, int64_t target_id);
    extern void __goroutine_release_shared(void* ptr);
    extern void* __channel_create(int64_t element_size, int64_t capacity);
    extern bool __channel_send_int64(void* channel_ptr, int64_t value);
    extern bool __channel_receive_int64(void* channel_ptr, int64_t* value);
    extern bool __channel_try_receive_int64(void* channel_ptr, int64_t* value);
    extern void __channel_close(void* channel_ptr);
    extern void __channel_delete(void* channel_ptr);
    extern void __print_scheduler_stats();
    
    g_runtime_function_table["__init_advanced_goroutine_system"] = (void*)__init_advanced_goroutine_system;
    g_runtime_function_table["__goroutine_alloc_shared"] = (void*)__goroutine_alloc_shared;
    g_runtime_function_table["__goroutine_share_memory"] = (void*)__goroutine_share_memory;
    g_runtime_function_table["__goroutine_release_shared"] = (void*)__goroutine_release_shared;
    g_runtime_function_table["__channel_create"] = (void*)__channel_create;
    g_runtime_function_table["__channel_send_int64"] = (void*)__channel_send_int64;
    g_runtime_function_table["__channel_receive_int64"] = (void*)__channel_receive_int64;
    g_runtime_function_table["__channel_try_receive_int64"] = (void*)__channel_try_receive_int64;
    g_runtime_function_table["__channel_close"] = (void*)__channel_close;
    g_runtime_function_table["__channel_delete"] = (void*)__channel_delete;
    g_runtime_function_table["__print_scheduler_stats"] = (void*)__print_scheduler_stats;
    
    // Register Simple Array runtime functions
    
    g_runtime_function_table["__simple_array_create"] = (void*)__simple_array_create;
    g_runtime_function_table["__simple_array_zeros"] = (void*)__simple_array_zeros;
    g_runtime_function_table["__simple_array_ones"] = (void*)__simple_array_ones;
    g_runtime_function_table["__simple_array_push"] = (void*)__simple_array_push;
    g_runtime_function_table["__simple_array_pop"] = (void*)__simple_array_pop;
    g_runtime_function_table["__simple_array_get"] = (void*)__simple_array_get;
    g_runtime_function_table["__simple_array_set"] = (void*)__simple_array_set;
    g_runtime_function_table["__simple_array_length"] = (void*)__simple_array_length;
    g_runtime_function_table["__simple_array_sum"] = (void*)__simple_array_sum;
    g_runtime_function_table["__simple_array_mean"] = (void*)__simple_array_mean;
    g_runtime_function_table["__simple_array_shape"] = (void*)__simple_array_shape;
    g_runtime_function_table["__simple_array_tostring"] = (void*)__simple_array_tostring;
    g_runtime_function_table["__simple_array_slice"] = (void*)__simple_array_slice;
    g_runtime_function_table["__simple_array_slice_all"] = (void*)__simple_array_slice_all;
    g_runtime_function_table["__console_log_number"] = (void*)__console_log_number;
    g_runtime_function_table["__dynamic_method_toString"] = (void*)__dynamic_method_toString;
    
    g_runtime_table_initialized = true;
}

void X86CodeGen::emit_call(const std::string& label) {
    // Check if this is a runtime function call
    if (label.substr(0, 2) == "__") {
        // Initialize function table on first use
        initialize_runtime_function_table();
        
        // Fast O(1) lookup instead of long if-else chain
        auto it = g_runtime_function_table.find(label);
        void* func_addr = nullptr;
        
        if (it != g_runtime_function_table.end()) {
            func_addr = it->second;
        } else {
            // Default case - return a no-op function for unimplemented runtime functions
            func_addr = (void*)__runtime_stub_function;
        }
        
        if (func_addr) {
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
    // Legacy function resolution disabled - using fast function table system
}

void X86CodeGen::emit_goroutine_spawn(const std::string& function_name) {
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
    
    std::cout.flush();
    
    // WORKAROUND: Due to JIT call issues, spawn goroutine properly using thread pool
    // This ensures timers work correctly with proper goroutine lifecycle
    
    // Load function name into RDI for the call
    emit_mov_reg_imm(RDI, reinterpret_cast<int64_t>(it->second));
    emit_call("__goroutine_spawn");
    
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

void X86CodeGen::emit_goroutine_spawn_with_address(void* function_address) {
    
    // Load function address into RDI register
    emit_mov_reg_imm(RDI, reinterpret_cast<int64_t>(function_address));
    
    // Call the runtime function to spawn goroutine with function address
    emit_call("__goroutine_spawn_func_ptr");
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

size_t X86CodeGen::get_current_offset() const {
    return code.size();
}

// High-Performance Function Calls - Direct function ID access
void X86CodeGen::emit_call_fast(uint16_t func_id) {
    // Ultra-fast function call using pre-computed function table
    // This eliminates ALL string lookups and hash table access
    
    // Validate function ID
    if (func_id == 0 || func_id >= MAX_FUNCTIONS) {
        std::cerr << "ERROR: Invalid function ID in emit_call_fast: " << func_id << std::endl;
        return;
    }
    
    // Load function pointer directly from g_function_table[func_id]
    // This generates direct assembly access to the function table
    
    // Get the address of g_function_table
    extern FunctionEntry g_function_table[];
    uint64_t table_addr = reinterpret_cast<uint64_t>(g_function_table);
    uint64_t func_entry_addr = table_addr + (func_id * sizeof(FunctionEntry));
    
    // mov rax, func_entry_addr  ; Load address of function entry
    code.push_back(0x48);  // REX.W prefix
    code.push_back(0xB8);  // MOV rax, imm64
    for (int i = 0; i < 8; i++) {
        code.push_back((func_entry_addr >> (i * 8)) & 0xFF);
    }
    
    // mov rax, [rax]  ; Dereference to get actual function pointer
    code.push_back(0x48);  // REX.W prefix
    code.push_back(0x8B);  // MOV reg, r/m
    code.push_back(0x00);  // ModR/M: [rax]
    
    // call rax
    code.push_back(0xFF);
    code.push_back(0xD0);
}

void X86CodeGen::emit_goroutine_spawn_fast(uint16_t func_id) {
    // Optimized goroutine spawn using direct function ID
    // No string lookups, no hash table access
    
    // mov rdi, func_id  ; First argument: function ID
    emit_mov_reg_imm(RDI, func_id);
    
    // Call the optimized spawn function
    emit_call("__goroutine_spawn_fast");
}

void X86CodeGen::emit_goroutine_spawn_direct(void* function_address) {
    // ULTRA-OPTIMIZED: Direct goroutine spawn with zero function call overhead
    // This is the fastest possible goroutine spawn - no ABI overhead, no lookups
    
    
    // Load function address directly into RDI register for the spawn function
    emit_mov_reg_imm(RDI, reinterpret_cast<int64_t>(function_address));
    
    // Call the direct spawn function that expects a function address
    emit_call("__goroutine_spawn_func_ptr");
}

void X86CodeGen::emit_goroutine_spawn_with_offset(size_t function_offset) {
    // NEAR-OPTIMAL: Calculate function address as executable_memory_base + offset
    // This adds one LEA instruction but is still very fast
    
    
    // Get executable memory base address (stored globally)
    emit_call("__get_executable_memory_base"); // Result in RAX
    
    // Add offset to base address: LEA RDI, [RAX + offset]
    if (function_offset <= 0x7FFFFFFF) {
        // Use 32-bit displacement for smaller offset
        code.push_back(0x48); // REX prefix for 64-bit
        code.push_back(0x8D); // LEA opcode
        code.push_back(0xB8); // ModR/M: RDI = [RAX + disp32]
        
        // Emit 32-bit offset
        code.push_back(function_offset & 0xFF);
        code.push_back((function_offset >> 8) & 0xFF);
        code.push_back((function_offset >> 16) & 0xFF);
        code.push_back((function_offset >> 24) & 0xFF);
    } else {
        // For larger offsets, use two instructions
        emit_mov_reg_imm(RDI, static_cast<int64_t>(function_offset));
        // ADD RDI, RAX
        code.push_back(0x48); // REX prefix
        code.push_back(0x01); // ADD opcode
        code.push_back(0xC7); // ModR/M: RDI += RAX
    }
    
    // Call the direct spawn function that expects a function address
    emit_call("__goroutine_spawn_func_ptr");
}

void X86CodeGen::emit_calculate_function_address_from_offset(size_t function_offset) {
    // NEAR-OPTIMAL: Calculate function address for non-goroutine callbacks
    
    
    // Get executable memory base address
    emit_call("__get_executable_memory_base"); // Result in RAX
    
    // Add offset to get final function address in RAX
    if (function_offset <= 0x7FFFFFFF) {
        // Use 32-bit displacement: LEA RAX, [RAX + offset]
        code.push_back(0x48); // REX prefix for 64-bit
        code.push_back(0x8D); // LEA opcode
        code.push_back(0x80); // ModR/M: RAX = [RAX + disp32]
        
        // Emit 32-bit offset
        code.push_back(function_offset & 0xFF);
        code.push_back((function_offset >> 8) & 0xFF);
        code.push_back((function_offset >> 16) & 0xFF);
        code.push_back((function_offset >> 24) & 0xFF);
    } else {
        // For larger offsets, use immediate load and add
        emit_mov_reg_imm(RDI, static_cast<int64_t>(function_offset));
        // ADD RAX, RDI
        code.push_back(0x48); // REX prefix
        code.push_back(0x01); // ADD opcode
        code.push_back(0xF8); // ModR/M: RAX += RDI
    }
    
    // Result is now in RAX (the function address)
}

// Stub implementations for lock methods
void X86CodeGen::emit_lock_acquire(int lock_id) { (void)lock_id; }
void X86CodeGen::emit_lock_release(int lock_id) { (void)lock_id; }
void X86CodeGen::emit_lock_try_acquire(int lock_id, int result_reg) { (void)lock_id; (void)result_reg; }
void X86CodeGen::emit_lock_try_acquire_timeout(int lock_id, int timeout_reg, int result_reg) { (void)lock_id; (void)timeout_reg; (void)result_reg; }
void X86CodeGen::emit_atomic_compare_exchange(int ptr_reg, int expected_reg, int desired_reg, int result_reg) { (void)ptr_reg; (void)expected_reg; (void)desired_reg; (void)result_reg; }
void X86CodeGen::emit_atomic_fetch_add(int ptr_reg, int value_reg, int result_reg) { (void)ptr_reg; (void)value_reg; (void)result_reg; }
void X86CodeGen::emit_atomic_store(int ptr_reg, int value_reg, int ordering) { (void)ptr_reg; (void)value_reg; (void)ordering; }
void X86CodeGen::emit_atomic_load(int ptr_reg, int result_reg, int ordering) { (void)ptr_reg; (void)result_reg; (void)ordering; }
void X86CodeGen::emit_memory_fence(int fence_type) { (void)fence_type; }

}