#include "goroutine_aware_gc.h"
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <sstream>

namespace gots {

// ============================================================================
// JIT CODE GENERATION IMPLEMENTATION
// ============================================================================

class GoroutineJITCodeGen {
private:
    uint8_t* code_buffer_;
    size_t code_buffer_size_;
    size_t code_offset_;
    uint32_t current_goroutine_id_;
    
    // Code generation state
    std::unordered_map<std::string, size_t> label_locations_;
    std::vector<size_t> slow_path_locations_;
    std::unordered_map<size_t, ObjectOwnership> allocation_site_ownership_;
    
    // Platform-specific constants
    enum class Platform {
        X86_64,
        WASM32
    };
    
    Platform target_platform_;
    
    // Register allocation (simplified)
    enum class Register : uint8_t {
        RAX = 0, RCX = 1, RDX = 2, RBX = 3,
        RSP = 4, RBP = 5, RSI = 6, RDI = 7,
        R8 = 8, R9 = 9, R10 = 10, R11 = 11,
        R12 = 12, R13 = 13, R14 = 14, R15 = 15
    };
    
public:
    GoroutineJITCodeGen(uint8_t* buffer, size_t buffer_size, Platform platform = Platform::X86_64) 
        : code_buffer_(buffer), code_buffer_size_(buffer_size), code_offset_(0), 
          current_goroutine_id_(0), target_platform_(platform) {}
    
    void set_current_goroutine(uint32_t goroutine_id) {
        current_goroutine_id_ = goroutine_id;
    }
    
    void set_allocation_ownership(size_t allocation_site, ObjectOwnership ownership) {
        allocation_site_ownership_[allocation_site] = ownership;
    }
    
    // ============================================================================
    // STACK ALLOCATION CODE GENERATION
    // ============================================================================
    
    void emit_stack_allocation(size_t allocation_site, size_t size, uint32_t type_id, Register result_reg) {
        std::cout << "[JIT] Emitting stack allocation: site=" << allocation_site 
                  << ", size=" << size << ", type=" << type_id << "\n";
        
        if (target_platform_ == Platform::X86_64) {
            emit_x86_stack_allocation(size, type_id, result_reg);
        } else {
            emit_wasm_stack_allocation(size, type_id);
        }
    }
    
    void emit_x86_stack_allocation(size_t size, uint32_t type_id, Register result_reg) {
        size_t total_size = size + sizeof(GoroutineObjectHeader);
        total_size = align_to_16(total_size);
        
        // sub rsp, total_size
        emit_x86_rex_prefix(true, false, false, false);
        emit_byte(0x83);
        emit_byte(0xEC);
        emit_byte(total_size & 0xFF);
        
        // mov result_reg, rsp
        emit_x86_rex_prefix(true, false, false, result_reg >= Register::R8);
        emit_byte(0x89);
        emit_byte(0xE0 | (static_cast<uint8_t>(result_reg) & 0x07));
        
        // Initialize header
        // mov dword [result_reg], (size & 0xFFFFFF) | (type_id << 24)
        emit_x86_rex_prefix(false, false, false, result_reg >= Register::R8);
        emit_byte(0xC7);
        emit_byte(0x00 | (static_cast<uint8_t>(result_reg) & 0x07));
        emit_u32((size & 0xFFFFFF) | (type_id << 24));
        
        // mov dword [result_reg + 4], STACK_ALLOCATED | (goroutine_id << 16)
        emit_x86_rex_prefix(false, false, false, result_reg >= Register::R8);
        emit_byte(0xC7);
        emit_byte(0x40 | (static_cast<uint8_t>(result_reg) & 0x07));
        emit_byte(0x04);
        emit_u32(static_cast<uint32_t>(ObjectOwnership::STACK_LOCAL) | (current_goroutine_id_ << 16));
        
        // mov dword [result_reg + 8], 1 << (goroutine_id & 31)
        emit_x86_rex_prefix(false, false, false, result_reg >= Register::R8);
        emit_byte(0xC7);
        emit_byte(0x40 | (static_cast<uint8_t>(result_reg) & 0x07));
        emit_byte(0x08);
        emit_u32(1u << (current_goroutine_id_ & 31));
        
        // lea result_reg, [result_reg + sizeof(GoroutineObjectHeader)]
        emit_x86_rex_prefix(true, false, false, result_reg >= Register::R8);
        emit_byte(0x8D);
        emit_byte(0x40 | (static_cast<uint8_t>(result_reg) & 0x07));
        emit_byte(sizeof(GoroutineObjectHeader));
        
        std::cout << "[JIT] Generated " << (code_offset_ - 0) << " bytes of x86 stack allocation code\n";
    }
    
    void emit_wasm_stack_allocation(size_t size, uint32_t type_id) {
        // WebAssembly stack allocation using local.get/local.set
        
        // Get stack pointer
        emit_wasm_local_get(0); // Assume local 0 is stack pointer
        
        // Push size
        emit_wasm_i32_const(size + sizeof(GoroutineObjectHeader));
        
        // Subtract from stack pointer
        emit_wasm_i32_sub();
        
        // Store new stack pointer
        emit_wasm_local_tee(0);
        
        // Initialize header
        emit_wasm_local_get(0);
        emit_wasm_i32_const((size & 0xFFFFFF) | (type_id << 24));
        emit_wasm_i32_store(0, 0);
        
        emit_wasm_local_get(0);
        emit_wasm_i32_const(static_cast<uint32_t>(ObjectOwnership::STACK_LOCAL) | (current_goroutine_id_ << 16));
        emit_wasm_i32_store(0, 4);
        
        emit_wasm_local_get(0);
        emit_wasm_i32_const(1u << (current_goroutine_id_ & 31));
        emit_wasm_i32_store(0, 8);
        
        // Return object start
        emit_wasm_local_get(0);
        emit_wasm_i32_const(sizeof(GoroutineObjectHeader));
        emit_wasm_i32_add();
        
        std::cout << "[JIT] Generated WebAssembly stack allocation code\n";
    }
    
    // ============================================================================
    // GOROUTINE PRIVATE ALLOCATION CODE GENERATION
    // ============================================================================
    
    void emit_goroutine_private_allocation(size_t allocation_site, size_t size, uint32_t type_id, Register result_reg) {
        std::cout << "[JIT] Emitting goroutine private allocation: site=" << allocation_site 
                  << ", size=" << size << ", type=" << type_id << "\n";
        
        if (target_platform_ == Platform::X86_64) {
            emit_x86_private_allocation(size, type_id, result_reg);
        } else {
            emit_wasm_private_allocation(size, type_id);
        }
    }
    
    void emit_x86_private_allocation(size_t size, uint32_t type_id, Register result_reg) {
        size_t total_size = size + sizeof(GoroutineObjectHeader);
        total_size = align_to_16(total_size);
        
        // Load goroutine heap from thread-local storage
        // mov rdi, fs:[goroutine_heap_offset]
        emit_byte(0x64); // FS prefix
        emit_x86_rex_prefix(true, false, false, false);
        emit_byte(0x8B);
        emit_byte(0x3C);
        emit_byte(0x25);
        emit_u32(0x200); // Goroutine heap offset in TLS
        
        // Load TLAB current pointer
        // mov rax, [rdi + tlab_current_offset]
        emit_x86_rex_prefix(true, false, false, false);
        emit_byte(0x8B);
        emit_byte(0x47);
        emit_byte(0x10); // TLAB current offset
        
        // Calculate new current pointer
        // lea rdx, [rax + total_size]
        emit_x86_rex_prefix(true, false, false, false);
        emit_byte(0x8D);
        emit_byte(0x90);
        emit_u32(total_size);
        
        // Compare with TLAB end
        // cmp rdx, [rdi + tlab_end_offset]
        emit_x86_rex_prefix(true, false, false, false);
        emit_byte(0x3B);
        emit_byte(0x57);
        emit_byte(0x18); // TLAB end offset
        
        // Jump to slow path if allocation would overflow
        size_t slow_path_jump = code_offset_;
        emit_byte(0x0F);
        emit_byte(0x87);
        emit_u32(0); // Placeholder for slow path address
        slow_path_locations_.push_back(slow_path_jump + 2);
        
        // Update TLAB current pointer
        // mov [rdi + tlab_current_offset], rdx
        emit_x86_rex_prefix(true, false, false, false);
        emit_byte(0x89);
        emit_byte(0x57);
        emit_byte(0x10);
        
        // Initialize object header
        // mov dword [rax], (size & 0xFFFFFF) | (type_id << 24)
        emit_x86_rex_prefix(false, false, false, false);
        emit_byte(0xC7);
        emit_byte(0x00);
        emit_u32((size & 0xFFFFFF) | (type_id << 24));
        
        // mov dword [rax + 4], GOROUTINE_PRIVATE | (goroutine_id << 16)
        emit_x86_rex_prefix(false, false, false, false);
        emit_byte(0xC7);
        emit_byte(0x40);
        emit_byte(0x04);
        emit_u32(static_cast<uint32_t>(ObjectOwnership::GOROUTINE_PRIVATE) | (current_goroutine_id_ << 16));
        
        // mov dword [rax + 8], 1 << (goroutine_id & 31)
        emit_x86_rex_prefix(false, false, false, false);
        emit_byte(0xC7);
        emit_byte(0x40);
        emit_byte(0x08);
        emit_u32(1u << (current_goroutine_id_ & 31));
        
        // Move result to desired register and adjust for object start
        if (result_reg != Register::RAX) {
            // mov result_reg, rax
            emit_x86_rex_prefix(true, result_reg >= Register::R8, false, false);
            emit_byte(0x89);
            emit_byte(0xC0 | (static_cast<uint8_t>(result_reg) & 0x07));
        }
        
        // lea result_reg, [result_reg + sizeof(GoroutineObjectHeader)]
        emit_x86_rex_prefix(true, false, false, result_reg >= Register::R8);
        emit_byte(0x8D);
        emit_byte(0x40 | (static_cast<uint8_t>(result_reg) & 0x07));
        emit_byte(sizeof(GoroutineObjectHeader));
        
        std::cout << "[JIT] Generated " << (code_offset_ - 0) << " bytes of x86 private allocation code\n";
    }
    
    void emit_wasm_private_allocation(size_t size, uint32_t type_id) {
        size_t total_size = size + sizeof(GoroutineObjectHeader);
        
        // Load TLAB current from linear memory
        emit_wasm_i32_const(0x1000); // TLAB current address
        emit_wasm_i32_load(0, 0);
        
        // Calculate new current
        emit_wasm_local_tee(1); // Store current in local 1
        emit_wasm_i32_const(total_size);
        emit_wasm_i32_add();
        
        // Load TLAB end
        emit_wasm_i32_const(0x1008); // TLAB end address
        emit_wasm_i32_load(0, 0);
        
        // Compare
        emit_wasm_i32_gt_u();
        
        // Branch to slow path if overflow
        emit_wasm_br_if(0);
        
        // Update TLAB current
        emit_wasm_i32_const(0x1000);
        emit_wasm_local_get(1);
        emit_wasm_i32_const(total_size);
        emit_wasm_i32_add();
        emit_wasm_i32_store(0, 0);
        
        // Initialize header
        emit_wasm_local_get(1);
        emit_wasm_i32_const((size & 0xFFFFFF) | (type_id << 24));
        emit_wasm_i32_store(0, 0);
        
        emit_wasm_local_get(1);
        emit_wasm_i32_const(static_cast<uint32_t>(ObjectOwnership::GOROUTINE_PRIVATE) | (current_goroutine_id_ << 16));
        emit_wasm_i32_store(0, 4);
        
        emit_wasm_local_get(1);
        emit_wasm_i32_const(1u << (current_goroutine_id_ & 31));
        emit_wasm_i32_store(0, 8);
        
        // Return object start
        emit_wasm_local_get(1);
        emit_wasm_i32_const(sizeof(GoroutineObjectHeader));
        emit_wasm_i32_add();
        
        std::cout << "[JIT] Generated WebAssembly private allocation code\n";
    }
    
    // ============================================================================
    // SHARED ALLOCATION CODE GENERATION
    // ============================================================================
    
    void emit_shared_allocation(size_t allocation_site, size_t size, uint32_t type_id, ObjectOwnership ownership) {
        std::cout << "[JIT] Emitting shared allocation: site=" << allocation_site 
                  << ", size=" << size << ", ownership=" << static_cast<int>(ownership) << "\n";
        
        if (target_platform_ == Platform::X86_64) {
            emit_x86_shared_allocation(size, type_id, ownership);
        } else {
            emit_wasm_shared_allocation(size, type_id, ownership);
        }
    }
    
    void emit_x86_shared_allocation(size_t size, uint32_t type_id, ObjectOwnership ownership) {
        // For shared allocations, we need to call the slow path
        // This is too complex for inline generation
        
        // Push arguments
        emit_x86_rex_prefix(false, false, false, false);
        emit_byte(0x68); // push immediate
        emit_u32(size);
        
        emit_x86_rex_prefix(false, false, false, false);
        emit_byte(0x68);
        emit_u32(type_id);
        
        emit_x86_rex_prefix(false, false, false, false);
        emit_byte(0x68);
        emit_u32(static_cast<uint32_t>(ownership));
        
        emit_x86_rex_prefix(false, false, false, false);
        emit_byte(0x68);
        emit_u32(current_goroutine_id_);
        
        // Call allocation function
        emit_byte(0xE8); // call relative
        if (ownership == ObjectOwnership::GOROUTINE_SHARED) {
            emit_u32(reinterpret_cast<uintptr_t>(&__gc_alloc_goroutine_shared) - 
                    (reinterpret_cast<uintptr_t>(code_buffer_) + code_offset_ + 4));
        } else {
            emit_u32(reinterpret_cast<uintptr_t>(&__gc_alloc_global_shared) - 
                    (reinterpret_cast<uintptr_t>(code_buffer_) + code_offset_ + 4));
        }
        
        // Clean up stack
        emit_x86_rex_prefix(true, false, false, false);
        emit_byte(0x83);
        emit_byte(0xC4);
        emit_byte(0x10); // add rsp, 16
        
        std::cout << "[JIT] Generated " << (code_offset_ - 0) << " bytes of x86 shared allocation code\n";
    }
    
    void emit_wasm_shared_allocation(size_t size, uint32_t type_id, ObjectOwnership ownership) {
        // Call shared allocation function
        emit_wasm_i32_const(size);
        emit_wasm_i32_const(type_id);
        emit_wasm_i32_const(static_cast<uint32_t>(ownership));
        emit_wasm_i32_const(current_goroutine_id_);
        
        if (ownership == ObjectOwnership::GOROUTINE_SHARED) {
            emit_wasm_call(get_function_index("__gc_alloc_goroutine_shared"));
        } else {
            emit_wasm_call(get_function_index("__gc_alloc_global_shared"));
        }
        
        std::cout << "[JIT] Generated WebAssembly shared allocation code\n";
    }
    
    // ============================================================================
    // WRITE BARRIER CODE GENERATION
    // ============================================================================
    
    void emit_write_barrier(void* obj_reg, size_t field_offset, void* value_reg, bool may_be_cross_goroutine) {
        std::cout << "[JIT] Emitting write barrier: cross_goroutine=" << may_be_cross_goroutine << "\n";
        
        if (target_platform_ == Platform::X86_64) {
            emit_x86_write_barrier(obj_reg, field_offset, value_reg, may_be_cross_goroutine);
        } else {
            emit_wasm_write_barrier(field_offset, may_be_cross_goroutine);
        }
    }
    
    void emit_x86_write_barrier(void* obj_reg, size_t field_offset, void* value_reg, bool may_be_cross_goroutine) {
        Register obj = static_cast<Register>(reinterpret_cast<uintptr_t>(obj_reg));
        Register value = static_cast<Register>(reinterpret_cast<uintptr_t>(value_reg));
        
        if (!may_be_cross_goroutine) {
            // Fast path: same goroutine write
            emit_x86_fast_write_barrier(obj, field_offset, value);
        } else {
            // Slow path: potentially cross-goroutine write
            emit_x86_sync_write_barrier(obj, field_offset, value);
        }
    }
    
    void emit_x86_fast_write_barrier(Register obj, size_t field_offset, Register value) {
        // Do the write first
        // mov [obj + field_offset], value
        emit_x86_rex_prefix(true, false, false, obj >= Register::R8 || value >= Register::R8);
        emit_byte(0x89);
        emit_byte(0x80 | (static_cast<uint8_t>(value) & 0x07) << 3 | (static_cast<uint8_t>(obj) & 0x07));
        emit_u32(field_offset);
        
        // Check if generational barrier is needed
        // test byte [obj - sizeof(header) + flag_offset], IN_OLD_GEN
        emit_x86_rex_prefix(false, false, false, obj >= Register::R8);
        emit_byte(0xF6);
        emit_byte(0x40 | (static_cast<uint8_t>(obj) & 0x07));
        emit_byte(static_cast<uint8_t>(-(int)sizeof(GoroutineObjectHeader) + 5));
        emit_byte(0x10); // IN_OLD_GEN flag
        
        // Skip barrier if not old gen
        emit_byte(0x74); // jz
        emit_byte(0x15); // Skip next instructions
        
        // Check if value is young
        // test byte [value - sizeof(header) + flag_offset], IN_OLD_GEN
        emit_x86_rex_prefix(false, false, false, value >= Register::R8);
        emit_byte(0xF6);
        emit_byte(0x40 | (static_cast<uint8_t>(value) & 0x07));
        emit_byte(static_cast<uint8_t>(-(int)sizeof(GoroutineObjectHeader) + 5));
        emit_byte(0x10);
        
        // Skip if value is old
        emit_byte(0x75); // jnz
        emit_byte(0x0C); // Skip card marking
        
        // Mark card dirty
        emit_x86_card_marking(obj);
        
        std::cout << "[JIT] Generated fast write barrier code\n";
    }
    
    void emit_x86_sync_write_barrier(Register obj, size_t field_offset, Register value) {
        // Mark object as accessed by current goroutine
        // or dword [obj - sizeof(header) + accessing_goroutines_offset], goroutine_mask
        emit_x86_rex_prefix(false, false, false, obj >= Register::R8);
        emit_byte(0x81);
        emit_byte(0x48 | (static_cast<uint8_t>(obj) & 0x07));
        emit_byte(static_cast<uint8_t>(-(int)sizeof(GoroutineObjectHeader) + 8));
        emit_u32(1u << (current_goroutine_id_ & 31));
        
        // Memory fence for release semantics
        emit_byte(0x0F);
        emit_byte(0xAE);
        emit_byte(0xF0); // mfence
        
        // Atomic store with release ordering
        // mov [obj + field_offset], value (with lock prefix)
        emit_byte(0xF0); // lock prefix
        emit_x86_rex_prefix(true, false, false, obj >= Register::R8 || value >= Register::R8);
        emit_byte(0x89);
        emit_byte(0x80 | (static_cast<uint8_t>(value) & 0x07) << 3 | (static_cast<uint8_t>(obj) & 0x07));
        emit_u32(field_offset);
        
        // Generational barrier (similar to fast path)
        emit_x86_generational_barrier_check(obj, value);
        
        std::cout << "[JIT] Generated synchronized write barrier code\n";
    }
    
    void emit_x86_card_marking(Register obj) {
        // mov rcx, obj
        emit_x86_rex_prefix(true, false, false, obj >= Register::R8);
        emit_byte(0x89);
        emit_byte(0xC0 | (static_cast<uint8_t>(obj) & 0x07) << 3 | 1); // RCX = 1
        
        // shr rcx, 9 (divide by card size)
        emit_x86_rex_prefix(true, false, false, false);
        emit_byte(0xC1);
        emit_byte(0xE9);
        emit_byte(0x09);
        
        // mov byte [card_table + rcx], 1
        emit_x86_rex_prefix(true, false, false, false);
        emit_byte(0xC6);
        emit_byte(0x80 | 1); // RCX
        emit_u64(reinterpret_cast<uintptr_t>(get_card_table_address()));
        emit_byte(0x01);
    }
    
    void emit_x86_generational_barrier_check(Register obj, Register value) {
        // Similar to fast path generational check
        emit_x86_rex_prefix(false, false, false, obj >= Register::R8);
        emit_byte(0xF6);
        emit_byte(0x40 | (static_cast<uint8_t>(obj) & 0x07));
        emit_byte(static_cast<uint8_t>(-(int)sizeof(GoroutineObjectHeader) + 5));
        emit_byte(0x10);
        
        emit_byte(0x74);
        emit_byte(0x10);
        
        emit_x86_rex_prefix(false, false, false, value >= Register::R8);
        emit_byte(0xF6);
        emit_byte(0x40 | (static_cast<uint8_t>(value) & 0x07));
        emit_byte(static_cast<uint8_t>(-(int)sizeof(GoroutineObjectHeader) + 5));
        emit_byte(0x10);
        
        emit_byte(0x75);
        emit_byte(0x08);
        
        emit_x86_card_marking(obj);
    }
    
    void emit_wasm_write_barrier(size_t field_offset, bool may_be_cross_goroutine) {
        if (!may_be_cross_goroutine) {
            // Fast path: direct write
            emit_wasm_i32_store(0, field_offset);
        } else {
            // Slow path: call write barrier function
            emit_wasm_i32_const(field_offset);
            emit_wasm_i32_const(current_goroutine_id_);
            emit_wasm_call(get_function_index("__gc_write_barrier_sync"));
        }
    }
    
    // ============================================================================
    // SAFEPOINT CODE GENERATION
    // ============================================================================
    
    void emit_safepoint_poll() {
        std::cout << "[JIT] Emitting safepoint poll\n";
        
        if (target_platform_ == Platform::X86_64) {
            emit_x86_safepoint_poll();
        } else {
            emit_wasm_safepoint_poll();
        }
    }
    
    void emit_x86_safepoint_poll() {
        // Test safepoint page (will fault if protected)
        // mov rax, [safepoint_page]
        emit_x86_rex_prefix(true, false, false, false);
        emit_byte(0x8B);
        emit_byte(0x04);
        emit_byte(0x25);
        emit_u64(reinterpret_cast<uintptr_t>(get_safepoint_page()));
        
        // This will fault if safepoint is requested
        // The fault handler will call the safepoint slow path
    }
    
    void emit_wasm_safepoint_poll() {
        // Check global safepoint flag
        emit_wasm_global_get(get_global_index("safepoint_requested"));
        
        // Branch if safepoint requested
        emit_wasm_br_if(get_block_index("safepoint_slow"));
    }
    
    // ============================================================================
    // FUNCTION PROLOGUE/EPILOGUE
    // ============================================================================
    
    void emit_function_prologue(uint32_t goroutine_id, const std::vector<void*>& local_roots) {
        std::cout << "[JIT] Emitting function prologue for goroutine " << goroutine_id << "\n";
        
        if (target_platform_ == Platform::X86_64) {
            emit_x86_function_prologue(goroutine_id, local_roots);
        } else {
            emit_wasm_function_prologue(goroutine_id, local_roots);
        }
    }
    
    void emit_x86_function_prologue(uint32_t goroutine_id, const std::vector<void*>& local_roots) {
        // Standard prologue
        emit_byte(0x55); // push rbp
        emit_x86_rex_prefix(true, false, false, false);
        emit_byte(0x89);
        emit_byte(0xE5); // mov rbp, rsp
        
        // Set current goroutine
        current_goroutine_id_ = goroutine_id;
        
        // Register stack roots if any
        if (!local_roots.empty()) {
            // Push arguments for root registration
            emit_x86_rex_prefix(false, false, false, false);
            emit_byte(0x68);
            emit_u32(local_roots.size());
            
            emit_x86_rex_prefix(true, false, false, false);
            emit_byte(0x68);
            emit_u64(reinterpret_cast<uintptr_t>(local_roots.data()));
            
            emit_x86_rex_prefix(false, false, false, false);
            emit_byte(0x68);
            emit_u32(goroutine_id);
            
            // Call root registration
            emit_byte(0xE8);
            emit_u32(reinterpret_cast<uintptr_t>(&__gc_register_goroutine_roots) - 
                    (reinterpret_cast<uintptr_t>(code_buffer_) + code_offset_ + 4));
            
            // Clean up stack
            emit_x86_rex_prefix(true, false, false, false);
            emit_byte(0x83);
            emit_byte(0xC4);
            emit_byte(0x18); // add rsp, 24
        }
    }
    
    void emit_function_epilogue(uint32_t goroutine_id, const std::vector<void*>& local_roots) {
        std::cout << "[JIT] Emitting function epilogue for goroutine " << goroutine_id << "\n";
        
        if (target_platform_ == Platform::X86_64) {
            emit_x86_function_epilogue(goroutine_id, local_roots);
        } else {
            emit_wasm_function_epilogue(goroutine_id, local_roots);
        }
    }
    
    void emit_x86_function_epilogue(uint32_t goroutine_id, const std::vector<void*>& local_roots) {
        // Unregister stack roots if any
        if (!local_roots.empty()) {
            emit_x86_rex_prefix(false, false, false, false);
            emit_byte(0x68);
            emit_u32(local_roots.size());
            
            emit_x86_rex_prefix(true, false, false, false);
            emit_byte(0x68);
            emit_u64(reinterpret_cast<uintptr_t>(local_roots.data()));
            
            emit_x86_rex_prefix(false, false, false, false);
            emit_byte(0x68);
            emit_u32(goroutine_id);
            
            emit_byte(0xE8);
            emit_u32(reinterpret_cast<uintptr_t>(&__gc_unregister_goroutine_roots) - 
                    (reinterpret_cast<uintptr_t>(code_buffer_) + code_offset_ + 4));
            
            emit_x86_rex_prefix(true, false, false, false);
            emit_byte(0x83);
            emit_byte(0xC4);
            emit_byte(0x18);
        }
        
        // Standard epilogue
        emit_byte(0x5D); // pop rbp
        emit_byte(0xC3); // ret
    }
    
    void emit_wasm_function_prologue(uint32_t goroutine_id, const std::vector<void*>& local_roots) {
        // Set current goroutine
        current_goroutine_id_ = goroutine_id;
        
        // Register roots if any
        if (!local_roots.empty()) {
            emit_wasm_i32_const(local_roots.size());
            emit_wasm_i32_const(reinterpret_cast<uintptr_t>(local_roots.data()));
            emit_wasm_i32_const(goroutine_id);
            emit_wasm_call(get_function_index("__gc_register_goroutine_roots"));
        }
    }
    
    void emit_wasm_function_epilogue(uint32_t goroutine_id, const std::vector<void*>& local_roots) {
        // Unregister roots if any
        if (!local_roots.empty()) {
            emit_wasm_i32_const(local_roots.size());
            emit_wasm_i32_const(reinterpret_cast<uintptr_t>(local_roots.data()));
            emit_wasm_i32_const(goroutine_id);
            emit_wasm_call(get_function_index("__gc_unregister_goroutine_roots"));
        }
    }
    
    // ============================================================================
    // SLOW PATH GENERATION
    // ============================================================================
    
    void emit_slow_paths() {
        std::cout << "[JIT] Emitting slow paths\n";
        
        // Emit slow path for allocation failures
        for (size_t location : slow_path_locations_) {
            // Patch the jump address
            uint32_t slow_path_offset = code_offset_ - (location + 4);
            *reinterpret_cast<uint32_t*>(code_buffer_ + location) = slow_path_offset;
        }
        
        // Emit actual slow path code
        emit_allocation_slow_path();
        emit_safepoint_slow_path();
    }
    
    void emit_allocation_slow_path() {
        if (target_platform_ == Platform::X86_64) {
            // Call allocation slow path
            emit_byte(0xE8);
            emit_u32(reinterpret_cast<uintptr_t>(&__gc_alloc_slow_path) - 
                    (reinterpret_cast<uintptr_t>(code_buffer_) + code_offset_ + 4));
            
            // Return to caller
            emit_byte(0xC3);
        } else {
            // WebAssembly slow path
            emit_wasm_call(get_function_index("__gc_alloc_slow_path"));
        }
    }
    
    void emit_safepoint_slow_path() {
        if (target_platform_ == Platform::X86_64) {
            // Push goroutine ID
            emit_x86_rex_prefix(false, false, false, false);
            emit_byte(0x68);
            emit_u32(current_goroutine_id_);
            
            // Call safepoint handler
            emit_byte(0xE8);
            emit_u32(reinterpret_cast<uintptr_t>(&__gc_safepoint_handler) - 
                    (reinterpret_cast<uintptr_t>(code_buffer_) + code_offset_ + 4));
            
            // Clean up stack
            emit_x86_rex_prefix(true, false, false, false);
            emit_byte(0x83);
            emit_byte(0xC4);
            emit_byte(0x04);
            
            // Return to caller
            emit_byte(0xC3);
        } else {
            // WebAssembly safepoint
            emit_wasm_i32_const(current_goroutine_id_);
            emit_wasm_call(get_function_index("__gc_safepoint_handler"));
        }
    }
    
    // ============================================================================
    // CODE GENERATION STATISTICS
    // ============================================================================
    
    void print_code_generation_statistics() {
        std::cout << "\n=== CODE GENERATION STATISTICS ===\n";
        std::cout << "Generated code size: " << code_offset_ << " bytes\n";
        std::cout << "Slow path locations: " << slow_path_locations_.size() << "\n";
        std::cout << "Allocation sites: " << allocation_site_ownership_.size() << "\n";
        std::cout << "Target platform: " << (target_platform_ == Platform::X86_64 ? "x86-64" : "WebAssembly") << "\n";
        std::cout << "Current goroutine: " << current_goroutine_id_ << "\n";
        std::cout << "==================================\n\n";
    }
    
private:
    // ============================================================================
    // HELPER FUNCTIONS
    // ============================================================================
    
    void emit_byte(uint8_t byte) {
        if (code_offset_ < code_buffer_size_) {
            code_buffer_[code_offset_++] = byte;
        }
    }
    
    void emit_u32(uint32_t value) {
        if (code_offset_ + 4 <= code_buffer_size_) {
            *reinterpret_cast<uint32_t*>(code_buffer_ + code_offset_) = value;
            code_offset_ += 4;
        }
    }
    
    void emit_u64(uint64_t value) {
        if (code_offset_ + 8 <= code_buffer_size_) {
            *reinterpret_cast<uint64_t*>(code_buffer_ + code_offset_) = value;
            code_offset_ += 8;
        }
    }
    
    void emit_x86_rex_prefix(bool w, bool r, bool x, bool b) {
        uint8_t rex = 0x40;
        if (w) rex |= 0x08;
        if (r) rex |= 0x04;
        if (x) rex |= 0x02;
        if (b) rex |= 0x01;
        emit_byte(rex);
    }
    
    // WebAssembly helpers
    void emit_wasm_i32_const(uint32_t value) {
        emit_byte(0x41);
        emit_leb128_u32(value);
    }
    
    void emit_wasm_i32_add() { emit_byte(0x6A); }
    void emit_wasm_i32_sub() { emit_byte(0x6B); }
    void emit_wasm_i32_gt_u() { emit_byte(0x4B); }
    void emit_wasm_local_get(uint32_t index) { emit_byte(0x20); emit_leb128_u32(index); }
    void emit_wasm_local_set(uint32_t index) { emit_byte(0x21); emit_leb128_u32(index); }
    void emit_wasm_local_tee(uint32_t index) { emit_byte(0x22); emit_leb128_u32(index); }
    void emit_wasm_global_get(uint32_t index) { emit_byte(0x23); emit_leb128_u32(index); }
    void emit_wasm_i32_load(uint32_t align, uint32_t offset) { emit_byte(0x28); emit_leb128_u32(align); emit_leb128_u32(offset); }
    void emit_wasm_i32_store(uint32_t align, uint32_t offset) { emit_byte(0x36); emit_leb128_u32(align); emit_leb128_u32(offset); }
    void emit_wasm_call(uint32_t func_index) { emit_byte(0x10); emit_leb128_u32(func_index); }
    void emit_wasm_br_if(uint32_t depth) { emit_byte(0x0D); emit_leb128_u32(depth); }
    
    void emit_leb128_u32(uint32_t value) {
        do {
            uint8_t byte = value & 0x7F;
            value >>= 7;
            if (value != 0) byte |= 0x80;
            emit_byte(byte);
        } while (value != 0);
    }
    
    static constexpr size_t align_to_16(size_t size) {
        return (size + 15) & ~15;
    }
    
    // Platform-specific function lookups
    uint32_t get_function_index(const char* name) {
        // In a real implementation, this would lookup function indices
        static std::unordered_map<std::string, uint32_t> func_indices = {
            {"__gc_alloc_goroutine_shared", 0},
            {"__gc_alloc_global_shared", 1},
            {"__gc_write_barrier_sync", 2},
            {"__gc_register_goroutine_roots", 3},
            {"__gc_unregister_goroutine_roots", 4},
            {"__gc_alloc_slow_path", 5},
            {"__gc_safepoint_handler", 6}
        };
        
        auto it = func_indices.find(name);
        return it != func_indices.end() ? it->second : 0;
    }
    
    uint32_t get_global_index(const char* name) {
        static std::unordered_map<std::string, uint32_t> global_indices = {
            {"safepoint_requested", 0}
        };
        
        auto it = global_indices.find(name);
        return it != global_indices.end() ? it->second : 0;
    }
    
    uint32_t get_block_index(const char* name) {
        static std::unordered_map<std::string, uint32_t> block_indices = {
            {"safepoint_slow", 0}
        };
        
        auto it = block_indices.find(name);
        return it != block_indices.end() ? it->second : 0;
    }
    
    void* get_card_table_address() {
        // In a real implementation, this would return the actual card table address
        return nullptr;
    }
    
    void* get_safepoint_page() {
        // In a real implementation, this would return the actual safepoint page address
        return nullptr;
    }
};

// ============================================================================
// EXTERNAL C API FUNCTIONS (forward declarations)
// ============================================================================

extern "C" {
    void* __gc_alloc_goroutine_shared(size_t size, uint32_t type_id, uint32_t ownership, uint32_t goroutine_id);
    void* __gc_alloc_global_shared(size_t size, uint32_t type_id, uint32_t ownership, uint32_t goroutine_id);
    void __gc_write_barrier_sync(void* obj, void* field, void* new_value, uint32_t goroutine_id);
    void __gc_register_goroutine_roots(size_t count, void** roots, uint32_t goroutine_id);
    void __gc_unregister_goroutine_roots(size_t count, void** roots, uint32_t goroutine_id);
    void* __gc_alloc_slow_path(size_t size, uint32_t type_id, uint32_t ownership, uint32_t goroutine_id);
    void __gc_safepoint_handler(uint32_t goroutine_id);
}

// ============================================================================
// PUBLIC JIT INTEGRATION API
// ============================================================================

// Factory function to create JIT code generator
std::unique_ptr<GoroutineJITCodeGen> create_jit_code_generator(uint8_t* buffer, size_t buffer_size) {
    return std::make_unique<GoroutineJITCodeGen>(buffer, buffer_size);
}

// RAII wrapper for JIT code buffers
class CodeBufferRAII {
private:
    std::unique_ptr<uint8_t[]> buffer_;
    size_t size_;
    
public:
    CodeBufferRAII(size_t size) : size_(size) {
        buffer_ = std::make_unique<uint8_t[]>(size);
        if (!buffer_) {
            throw std::bad_alloc();
        }
    }
    
    uint8_t* get() { return buffer_.get(); }
    const uint8_t* get() const { return buffer_.get(); }
    size_t size() const { return size_; }
    
    // Prevent copying, allow moving
    CodeBufferRAII(const CodeBufferRAII&) = delete;
    CodeBufferRAII& operator=(const CodeBufferRAII&) = delete;
    CodeBufferRAII(CodeBufferRAII&&) = default;
    CodeBufferRAII& operator=(CodeBufferRAII&&) = default;
};

// Test function to demonstrate code generation
void test_jit_code_generation() {
    std::cout << "[JIT] Testing code generation...\n";
    
    try {
        // Allocate code buffer with RAII
        const size_t buffer_size = 64 * 1024; // 64KB
        CodeBufferRAII code_buffer_raii(buffer_size);
        uint8_t* code_buffer = code_buffer_raii.get();
        
        // Create code generator
        auto codegen = create_jit_code_generator(code_buffer, buffer_size);
        if (!codegen) {
            throw std::runtime_error("Failed to create JIT code generator");
        }
        
        // Test different allocation patterns
        codegen->set_current_goroutine(1);
        
        // Test stack allocation
        codegen->emit_stack_allocation(1000, 32, 42, GoroutineJITCodeGen::Register::RAX);
        
        // Test private allocation
        codegen->emit_goroutine_private_allocation(1001, 64, 43, GoroutineJITCodeGen::Register::RDX);
        
        // Test shared allocation
        codegen->emit_shared_allocation(1002, 128, 44, ObjectOwnership::GOROUTINE_SHARED);
        
        // Test write barriers
        codegen->emit_write_barrier(reinterpret_cast<void*>(0), 8, reinterpret_cast<void*>(1), false);
        codegen->emit_write_barrier(reinterpret_cast<void*>(0), 16, reinterpret_cast<void*>(1), true);
        
        // Test safepoint
        codegen->emit_safepoint_poll();
        
        // Test function prologue/epilogue
        std::vector<void*> roots = {reinterpret_cast<void*>(0x1000), reinterpret_cast<void*>(0x2000)};
        codegen->emit_function_prologue(1, roots);
        codegen->emit_function_epilogue(1, roots);
        
        // Generate slow paths
        codegen->emit_slow_paths();
        
        // Print statistics
        codegen->print_code_generation_statistics();
        
        // Buffer automatically cleaned up by RAII
        std::cout << "[JIT] Code generation test completed successfully\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[JIT] Code generation test failed: " << e.what() << "\n";
        throw;
    }
}

} // namespace gots