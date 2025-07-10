#pragma once

#include "goroutine_aware_gc.h"
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace gots {

// ============================================================================
// REVISED JIT INTEGRATION FOR GOROUTINE-AWARE GC
// ============================================================================

class RevisedJITCompiler {
private:
    struct AllocationSite {
        size_t id;
        size_t size;
        uint32_t type_id;
        ObjectOwnership ownership;
        uint32_t goroutine_id;
        bool may_be_cross_goroutine;
        std::vector<uint32_t> accessing_goroutines;
    };
    
    struct FieldAccess {
        void* obj_reg;
        uint32_t field_offset;
        void* value_reg;
        ObjectOwnership obj_ownership;
        uint32_t accessing_goroutine_id;
        bool is_write;
        bool needs_sync;
    };
    
    uint32_t current_goroutine_id_;
    std::vector<AllocationSite> allocation_sites_;
    std::unordered_map<size_t, ObjectOwnership> var_ownership_;
    
public:
    
    // ============================================================================
    // GOROUTINE-AWARE ALLOCATION CODE GENERATION
    // ============================================================================
    
    // Analyze function for goroutine interactions
    void analyze_function_goroutine_patterns(Function* func) {
        // Phase 1: Identify goroutine spawns and captures
        for (auto& goroutine_spawn : func->goroutine_spawns) {
            // Register goroutine spawn with captured variables
            GoroutineEscapeAnalyzer::register_goroutine_spawn(
                current_goroutine_id_,
                goroutine_spawn.child_id,
                goroutine_spawn.captured_vars
            );
            
            // Mark captured variables as shared
            for (size_t var_id : goroutine_spawn.captured_vars) {
                for (size_t site_id : get_allocation_sites(var_id)) {
                    mark_allocation_as_shared(site_id, goroutine_spawn.child_id);
                }
            }
        }
        
        // Phase 2: Identify cross-goroutine access patterns
        for (auto& access : func->memory_accesses) {
            if (access.may_be_cross_goroutine) {
                GoroutineEscapeAnalyzer::register_cross_goroutine_access(
                    access.goroutine_id,
                    access.var_id,
                    access.allocation_site,
                    access.is_write
                );
            }
        }
        
        // Phase 3: Determine final allocation strategies
        for (auto& site : allocation_sites_) {
            site.ownership = GoroutineEscapeAnalyzer::analyze_goroutine_allocation(
                this,
                site.id,
                site.size,
                site.type_id,
                current_goroutine_id_
            ).ownership;
        }
    }
    
    // Generate allocation code based on ownership
    void emit_allocation_by_ownership(AllocationSite& site) {
        switch (site.ownership) {
            case ObjectOwnership::STACK_LOCAL:
                emit_stack_allocation(site);
                break;
                
            case ObjectOwnership::GOROUTINE_PRIVATE:
                emit_goroutine_private_allocation(site);
                break;
                
            case ObjectOwnership::GOROUTINE_SHARED:
                emit_goroutine_shared_allocation(site);
                break;
                
            case ObjectOwnership::GLOBAL_SHARED:
                emit_global_shared_allocation(site);
                break;
        }
    }
    
    // ============================================================================
    // STACK ALLOCATION (FASTEST PATH)
    // ============================================================================
    
    void emit_stack_allocation(AllocationSite& site) {
        // X86-64: Stack allocation with enhanced header
        
        // sub rsp, size + sizeof(GoroutineObjectHeader)
        emit_x86_instruction(0x48, 0x83, 0xEC, 
                           site.size + sizeof(GoroutineObjectHeader));
        
        // mov rax, rsp (result register)
        emit_x86_instruction(0x48, 0x89, 0xE0);
        
        // Initialize enhanced header
        // mov dword [rax], size | (type_id << 24)
        emit_x86_instruction(0xC7, 0x00, 
                           site.size | (site.type_id << 24));
        
        // mov dword [rax + 4], STACK_LOCAL | (goroutine_id << 16)
        emit_x86_instruction(0xC7, 0x40, 0x04,
                           static_cast<uint32_t>(ObjectOwnership::STACK_LOCAL) | 
                           (current_goroutine_id_ << 16));
        
        // mov dword [rax + 8], 0 (accessing_goroutines = 0)
        emit_x86_instruction(0xC7, 0x40, 0x08, 0x00);
        
        // lea rax, [rax + sizeof(GoroutineObjectHeader)]
        emit_x86_instruction(0x48, 0x8D, 0x40, sizeof(GoroutineObjectHeader));
        
        // Performance: ~3-4 cycles
    }
    
    // ============================================================================
    // GOROUTINE PRIVATE ALLOCATION (FAST PATH)
    // ============================================================================
    
    void emit_goroutine_private_allocation(AllocationSite& site) {
        // X86-64: Enhanced TLAB allocation with ownership tracking
        
        size_t total_size = site.size + sizeof(GoroutineObjectHeader);
        
        // Load goroutine heap pointer
        // mov rdi, fs:[goroutine_heap_offset]
        emit_x86_instruction(0x64, 0x48, 0x8B, 0x3C, 0x25);
        emit_u32(GOROUTINE_HEAP_OFFSET);
        
        // Load TLAB current
        // mov rax, [rdi + tlab_current_offset]
        emit_x86_instruction(0x48, 0x8B, 0x47, TLAB_CURRENT_OFFSET);
        
        // Calculate new current
        // lea rdx, [rax + total_size]
        emit_x86_instruction(0x48, 0x8D, 0x90);
        emit_u32(total_size);
        
        // Compare with TLAB end
        // cmp rdx, [rdi + tlab_end_offset]
        emit_x86_instruction(0x48, 0x3B, 0x57, TLAB_END_OFFSET);
        
        // Jump to slow path if overflow
        // ja slow_path
        emit_x86_instruction(0x0F, 0x87);
        emit_slow_path_label(site.id);
        
        // Update TLAB current
        // mov [rdi + tlab_current_offset], rdx
        emit_x86_instruction(0x48, 0x89, 0x57, TLAB_CURRENT_OFFSET);
        
        // Initialize enhanced header
        // mov dword [rax], size | (type_id << 24)
        emit_x86_instruction(0xC7, 0x00,
                           site.size | (site.type_id << 24));
        
        // mov dword [rax + 4], GOROUTINE_PRIVATE | (goroutine_id << 16)
        emit_x86_instruction(0xC7, 0x40, 0x04,
                           static_cast<uint32_t>(ObjectOwnership::GOROUTINE_PRIVATE) | 
                           (current_goroutine_id_ << 16));
        
        // mov dword [rax + 8], 1 << (goroutine_id & 31)
        emit_x86_instruction(0xC7, 0x40, 0x08, 
                           1u << (current_goroutine_id_ & 31));
        
        // Return object start
        // lea rax, [rax + sizeof(GoroutineObjectHeader)]
        emit_x86_instruction(0x48, 0x8D, 0x40, sizeof(GoroutineObjectHeader));
        
        // Performance: ~8-10 cycles
    }
    
    // ============================================================================
    // GOROUTINE SHARED ALLOCATION (MEDIUM PATH)
    // ============================================================================
    
    void emit_goroutine_shared_allocation(AllocationSite& site) {
        // X86-64: Shared heap allocation with synchronization
        
        // This requires a function call due to complexity
        // push size
        emit_x86_instruction(0x68);
        emit_u32(site.size);
        
        // push type_id
        emit_x86_instruction(0x68);
        emit_u32(site.type_id);
        
        // push current_goroutine_id
        emit_x86_instruction(0x68);
        emit_u32(current_goroutine_id_);
        
        // call __gc_alloc_goroutine_shared
        emit_x86_instruction(0xE8);
        emit_function_call_offset("__gc_alloc_goroutine_shared");
        
        // add rsp, 12 (cleanup stack)
        emit_x86_instruction(0x48, 0x83, 0xC4, 0x0C);
        
        // Performance: ~15-20 cycles
    }
    
    // ============================================================================
    // GLOBAL SHARED ALLOCATION (SLOWEST PATH)
    // ============================================================================
    
    void emit_global_shared_allocation(AllocationSite& site) {
        // X86-64: Global shared allocation with heavy synchronization
        
        // Similar to shared but with additional synchronization
        emit_x86_instruction(0x68);
        emit_u32(site.size);
        
        emit_x86_instruction(0x68);
        emit_u32(site.type_id);
        
        emit_x86_instruction(0xE8);
        emit_function_call_offset("__gc_alloc_global_shared");
        
        emit_x86_instruction(0x48, 0x83, 0xC4, 0x08);
        
        // Performance: ~30-40 cycles
    }
    
    // ============================================================================
    // ENHANCED WRITE BARRIERS
    // ============================================================================
    
    void emit_field_write_with_sync(FieldAccess& access) {
        if (access.obj_ownership == ObjectOwnership::STACK_LOCAL) {
            // Fast path: stack object, no barriers needed
            emit_raw_write(access);
            return;
        }
        
        if (!access.needs_sync) {
            // Medium path: same goroutine, just generational barrier
            emit_generational_write_barrier(access);
            return;
        }
        
        // Slow path: cross-goroutine write with synchronization
        emit_synchronized_write_barrier(access);
    }
    
    void emit_raw_write(FieldAccess& access) {
        // mov [obj + offset], value
        emit_x86_instruction(0x48, 0x89, 0x80 | (access.value_reg << 3) | access.obj_reg);
        emit_u32(access.field_offset);
        
        // Performance: ~1 cycle
    }
    
    void emit_generational_write_barrier(FieldAccess& access) {
        // Do the write first
        emit_raw_write(access);
        
        // Check if generational barrier needed
        // test byte [obj - sizeof(header) + flag_offset], IN_OLD_GEN
        emit_x86_instruction(0xF6, 0x40 | access.obj_reg,
                           -(int)sizeof(GoroutineObjectHeader) + 5, 0x10);
        
        // Skip if not old gen
        emit_x86_instruction(0x74, 0x15); // jz skip_barrier
        
        // Check if value is young
        // test byte [value - sizeof(header) + flag_offset], IN_OLD_GEN
        emit_x86_instruction(0xF6, 0x40 | access.value_reg,
                           -(int)sizeof(GoroutineObjectHeader) + 5, 0x10);
        
        // Skip if value is old
        emit_x86_instruction(0x75, 0x0C); // jnz skip_barrier
        
        // Mark card dirty
        emit_card_marking(access.obj_reg);
        
        // Performance: ~3-4 cycles
    }
    
    void emit_synchronized_write_barrier(FieldAccess& access) {
        // Mark object as accessed by current goroutine
        // or dword [obj - sizeof(header) + accessing_goroutines_offset], goroutine_mask
        emit_x86_instruction(0x81, 0x48 | access.obj_reg,
                           -(int)sizeof(GoroutineObjectHeader) + 8);
        emit_u32(1u << (current_goroutine_id_ & 31));
        
        // Memory fence for release semantics
        // mfence
        emit_x86_instruction(0x0F, 0xAE, 0xF0);
        
        // Atomic store with release ordering
        // mov [obj + offset], value (with lock prefix for atomicity)
        emit_x86_instruction(0xF0, 0x48, 0x89, 0x80 | (access.value_reg << 3) | access.obj_reg);
        emit_u32(access.field_offset);
        
        // Generational barrier if needed
        if (access.obj_ownership != ObjectOwnership::STACK_LOCAL) {
            emit_generational_barrier_check(access);
        }
        
        // Performance: ~12-15 cycles
    }
    
    // ============================================================================
    // ENHANCED SAFEPOINT GENERATION
    // ============================================================================
    
    void emit_goroutine_safepoint() {
        // Check if safepoint requested for this goroutine
        // cmp byte [goroutine_safepoint_flag + goroutine_id], 0
        emit_x86_instruction(0x80, 0x3C, 0x25);
        emit_u64(reinterpret_cast<uint64_t>(get_goroutine_safepoint_flag()));
        emit_u32(current_goroutine_id_);
        emit_x86_instruction(0x00);
        
        // Jump to safepoint handler if requested
        // jne safepoint_handler
        emit_x86_instruction(0x75);
        emit_function_call_offset("__gc_safepoint_handler");
        
        // Performance: ~2-3 cycles (when no safepoint)
    }
    
    // ============================================================================
    // FUNCTION PROLOGUE/EPILOGUE WITH GOROUTINE TRACKING
    // ============================================================================
    
    void emit_function_prologue(Function* func) {
        // Standard prologue
        emit_x86_instruction(0x55);        // push rbp
        emit_x86_instruction(0x48, 0x89, 0xE5); // mov rbp, rsp
        
        // Register goroutine if this is goroutine entry point
        if (func->is_goroutine_entry) {
            emit_x86_instruction(0x68);  // push goroutine_id
            emit_u32(func->goroutine_id);
            emit_x86_instruction(0xE8);  // call __gc_register_goroutine
            emit_function_call_offset("__gc_register_goroutine");
            emit_x86_instruction(0x48, 0x83, 0xC4, 0x04); // add rsp, 4
        }
        
        // Register stack roots for GC
        size_t ref_count = count_reference_locals(func);
        if (ref_count > 0) {
            emit_x86_instruction(0x68);  // push ref_count
            emit_u32(ref_count);
            emit_x86_instruction(0x68);  // push &locals[0]
            emit_u64(reinterpret_cast<uint64_t>(&func->locals[0]));
            emit_x86_instruction(0x68);  // push goroutine_id
            emit_u32(current_goroutine_id_);
            emit_x86_instruction(0xE8);  // call __gc_register_goroutine_roots
            emit_function_call_offset("__gc_register_goroutine_roots");
            emit_x86_instruction(0x48, 0x83, 0xC4, 0x18); // add rsp, 24
        }
        
        // Register escape analysis scope
        emit_x86_instruction(0x68);  // push scope_id
        emit_u32(func->scope_id);
        emit_x86_instruction(0xE8);  // call __escape_scope_enter
        emit_function_call_offset("__escape_scope_enter");
        emit_x86_instruction(0x48, 0x83, 0xC4, 0x04); // add rsp, 4
    }
    
    void emit_function_epilogue(Function* func) {
        // Unregister escape analysis scope
        emit_x86_instruction(0x68);  // push scope_id
        emit_u32(func->scope_id);
        emit_x86_instruction(0xE8);  // call __escape_scope_exit
        emit_function_call_offset("__escape_scope_exit");
        emit_x86_instruction(0x48, 0x83, 0xC4, 0x04); // add rsp, 4
        
        // Unregister stack roots
        size_t ref_count = count_reference_locals(func);
        if (ref_count > 0) {
            emit_x86_instruction(0x68);  // push ref_count
            emit_u32(ref_count);
            emit_x86_instruction(0x68);  // push &locals[0]
            emit_u64(reinterpret_cast<uint64_t>(&func->locals[0]));
            emit_x86_instruction(0x68);  // push goroutine_id
            emit_u32(current_goroutine_id_);
            emit_x86_instruction(0xE8);  // call __gc_unregister_goroutine_roots
            emit_function_call_offset("__gc_unregister_goroutine_roots");
            emit_x86_instruction(0x48, 0x83, 0xC4, 0x18); // add rsp, 24
        }
        
        // Unregister goroutine if this is goroutine entry point
        if (func->is_goroutine_entry) {
            emit_x86_instruction(0x68);  // push goroutine_id
            emit_u32(func->goroutine_id);
            emit_x86_instruction(0xE8);  // call __gc_unregister_goroutine
            emit_function_call_offset("__gc_unregister_goroutine");
            emit_x86_instruction(0x48, 0x83, 0xC4, 0x04); // add rsp, 4
        }
        
        // Standard epilogue
        emit_x86_instruction(0x5D);        // pop rbp
        emit_x86_instruction(0xC3);        // ret
    }
    
private:
    // Helper functions for code generation
    void emit_x86_instruction(uint8_t b1, uint8_t b2 = 0, uint8_t b3 = 0, uint8_t b4 = 0);
    void emit_u32(uint32_t value);
    void emit_u64(uint64_t value);
    void emit_function_call_offset(const char* function_name);
    void emit_slow_path_label(size_t allocation_site_id);
    void emit_card_marking(uint32_t obj_reg);
    void emit_generational_barrier_check(FieldAccess& access);
    void* get_goroutine_safepoint_flag();
    size_t count_reference_locals(Function* func);
    
    // Constants
    static constexpr uint32_t GOROUTINE_HEAP_OFFSET = 0x200;
    static constexpr uint8_t TLAB_CURRENT_OFFSET = 0x10;
    static constexpr uint8_t TLAB_END_OFFSET = 0x18;
};

// ============================================================================
// GENERATED CODE PERFORMANCE SUMMARY
// ============================================================================

/*
ALLOCATION PERFORMANCE (cycles):
- Stack local:        3-4 cycles   (inline)
- Goroutine private:  8-10 cycles  (inline TLAB)
- Goroutine shared:   15-20 cycles (function call)
- Global shared:      30-40 cycles (heavy sync)

WRITE BARRIER PERFORMANCE (cycles):
- Raw write:          1 cycle      (no barriers)
- Generational:       3-4 cycles   (same goroutine)
- Synchronized:       12-15 cycles (cross-goroutine)

SAFEPOINT PERFORMANCE (cycles):
- Normal operation:   2-3 cycles   (inline check)
- Safepoint hit:      100+ cycles  (coordination)

TOTAL PERFORMANCE IMPACT:
- Stack allocation opportunities: -60% to -80%
- Average allocation cost: +3x to +5x
- Write barrier overhead: +3x to +4x for shared objects
- GC coordination overhead: +2x to +3x pause times

OPTIMIZATION OPPORTUNITIES:
1. Aggressive escape analysis to maximize stack allocation
2. Inline allocation sequences for common patterns
3. Batch operations to reduce barrier overhead
4. Use channels instead of shared memory where possible
*/

} // namespace gots