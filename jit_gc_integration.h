#pragma once

#include "gc_memory_manager.h"
#include <cstdint>

namespace gots {

// ============================================================================
// JIT CODE EMITTERS FOR INLINE ALLOCATION
// ============================================================================

class JITGCIntegration {
public:
    // Configuration for platform-specific code generation
    struct Config {
        bool use_compressed_oops = false;  // 32-bit object pointers
        bool emit_safepoints = true;       // Emit GC safepoints
        bool aggressive_stack_alloc = true; // Use escape analysis
        size_t max_inline_alloc_size = 256; // Max size for inline alloc
    };

    // ============================================================================
    // X86-64 INLINE ALLOCATION
    // ============================================================================
    
    // Emit fast TLAB allocation for x86-64
    // This generates ~10 instructions that execute in ~3-5 cycles
    static void emit_x86_allocation(
        uint8_t* code_buffer,
        size_t& offset,
        uint32_t size_reg,      // Register containing size
        uint32_t result_reg,    // Register for result
        uint32_t type_id,
        void* slow_path_label
    ) {
        // Assumes: RAX=size, RDI=result
        // Thread-local TLAB pointer in FS:[tlab_offset]
        
        // Load TLAB current pointer
        // mov rdi, fs:[tlab_current_offset]
        code_buffer[offset++] = 0x64; // FS prefix
        code_buffer[offset++] = 0x48; // REX.W
        code_buffer[offset++] = 0x8B;
        code_buffer[offset++] = 0x3C;
        code_buffer[offset++] = 0x25;
        write_u32(code_buffer + offset, TLAB_CURRENT_OFFSET);
        offset += 4;
        
        // Add object header size
        // lea rax, [rax + 8]
        code_buffer[offset++] = 0x48;
        code_buffer[offset++] = 0x8D;
        code_buffer[offset++] = 0x40;
        code_buffer[offset++] = 0x08;
        
        // Calculate new current
        // lea rdx, [rdi + rax]
        code_buffer[offset++] = 0x48;
        code_buffer[offset++] = 0x8D;
        code_buffer[offset++] = 0x14;
        code_buffer[offset++] = 0x07;
        
        // Compare with TLAB end
        // cmp rdx, fs:[tlab_end_offset]
        code_buffer[offset++] = 0x64;
        code_buffer[offset++] = 0x48;
        code_buffer[offset++] = 0x3B;
        code_buffer[offset++] = 0x14;
        code_buffer[offset++] = 0x25;
        write_u32(code_buffer + offset, TLAB_END_OFFSET);
        offset += 4;
        
        // Jump if allocation would overflow
        // ja slow_path
        code_buffer[offset++] = 0x0F;
        code_buffer[offset++] = 0x87;
        write_u32(code_buffer + offset, (uint32_t)(intptr_t)slow_path_label);
        offset += 4;
        
        // Update TLAB current
        // mov fs:[tlab_current_offset], rdx
        code_buffer[offset++] = 0x64;
        code_buffer[offset++] = 0x48;
        code_buffer[offset++] = 0x89;
        code_buffer[offset++] = 0x14;
        code_buffer[offset++] = 0x25;
        write_u32(code_buffer + offset, TLAB_CURRENT_OFFSET);
        offset += 4;
        
        // Initialize object header
        // mov dword [rdi], size | (type_id << 24)
        code_buffer[offset++] = 0xC7;
        code_buffer[offset++] = 0x07;
        write_u32(code_buffer + offset, size_reg | (type_id << 24));
        offset += 4;
        
        // Clear flags and forward pointer
        // mov dword [rdi + 4], 0
        code_buffer[offset++] = 0xC7;
        code_buffer[offset++] = 0x47;
        code_buffer[offset++] = 0x04;
        write_u32(code_buffer + offset, 0);
        offset += 4;
        
        // Return object start (skip header)
        // lea rax, [rdi + 8]
        code_buffer[offset++] = 0x48;
        code_buffer[offset++] = 0x8D;
        code_buffer[offset++] = 0x47;
        code_buffer[offset++] = 0x08;
    }
    
    // ============================================================================
    // X86-64 WRITE BARRIER
    // ============================================================================
    
    // Emit minimal write barrier (5-7 instructions)
    static void emit_x86_write_barrier(
        uint8_t* code_buffer,
        size_t& offset,
        uint32_t obj_reg,       // Object being written to
        uint32_t offset_imm,    // Field offset
        uint32_t value_reg      // New value
    ) {
        // Do the write first
        // mov [obj + offset], value
        code_buffer[offset++] = 0x48;
        code_buffer[offset++] = 0x89;
        code_buffer[offset++] = 0x80 | (value_reg << 3) | obj_reg;
        write_u32(code_buffer + offset, offset_imm);
        offset += 4;
        
        // Check if obj is in old gen
        // test byte [obj - 8 + 5], 0x10  ; IN_OLD_GEN flag
        code_buffer[offset++] = 0xF6;
        code_buffer[offset++] = 0x40 | obj_reg;
        code_buffer[offset++] = 0xFD; // -8 + 5 = -3
        code_buffer[offset++] = 0x10;
        
        // Skip barrier if not old gen
        // jz skip_barrier
        code_buffer[offset++] = 0x74;
        code_buffer[offset++] = 0x1C; // Skip next instructions
        
        // Check if value is young gen
        // test byte [value - 8 + 5], 0x10
        code_buffer[offset++] = 0xF6;
        code_buffer[offset++] = 0x40 | value_reg;
        code_buffer[offset++] = 0xFD;
        code_buffer[offset++] = 0x10;
        
        // Skip if value is also old
        // jnz skip_barrier
        code_buffer[offset++] = 0x75;
        code_buffer[offset++] = 0x12;
        
        // Mark card dirty
        // mov rcx, obj
        code_buffer[offset++] = 0x48;
        code_buffer[offset++] = 0x89;
        code_buffer[offset++] = 0xC0 | (obj_reg << 3) | 1; // RCX
        
        // shr rcx, 9  ; Divide by card size (512)
        code_buffer[offset++] = 0x48;
        code_buffer[offset++] = 0xC1;
        code_buffer[offset++] = 0xE9;
        code_buffer[offset++] = 0x09;
        
        // mov byte [card_table + rcx], 1
        code_buffer[offset++] = 0xC6;
        code_buffer[offset++] = 0x80 | 1;
        write_u64(code_buffer + offset, (uint64_t)WriteBarrier::card_table_);
        offset += 8;
        code_buffer[offset++] = 0x01;
        
        // skip_barrier:
    }
    
    // ============================================================================
    // X86-64 STACK ALLOCATION
    // ============================================================================
    
    // Emit stack allocation (when escape analysis says it's safe)
    static void emit_x86_stack_allocation(
        uint8_t* code_buffer,
        size_t& offset,
        uint32_t size,
        uint32_t result_reg,
        uint32_t type_id
    ) {
        // Allocate on stack
        // sub rsp, size + 8
        code_buffer[offset++] = 0x48;
        code_buffer[offset++] = 0x83;
        code_buffer[offset++] = 0xEC;
        code_buffer[offset++] = size + 8;
        
        // Get stack pointer
        // mov result_reg, rsp
        code_buffer[offset++] = 0x48;
        code_buffer[offset++] = 0x89;
        code_buffer[offset++] = 0xE0 | result_reg;
        
        // Initialize header with STACK_ALLOCATED flag
        // mov dword [result_reg], size | (type_id << 24)
        code_buffer[offset++] = 0xC7;
        code_buffer[offset++] = 0x00 | result_reg;
        write_u32(code_buffer + offset, size | (type_id << 24));
        offset += 4;
        
        // mov dword [result_reg + 4], STACK_ALLOCATED
        code_buffer[offset++] = 0xC7;
        code_buffer[offset++] = 0x40 | result_reg;
        code_buffer[offset++] = 0x04;
        write_u32(code_buffer + offset, ObjectHeader::STACK_ALLOCATED);
        offset += 4;
        
        // Return object start
        // lea result_reg, [result_reg + 8]
        code_buffer[offset++] = 0x48;
        code_buffer[offset++] = 0x8D;
        code_buffer[offset++] = 0x40 | result_reg;
        code_buffer[offset++] = 0x08;
    }
    
    // ============================================================================
    // WEBASSEMBLY INLINE ALLOCATION
    // ============================================================================
    
    // Emit WASM bytecode for TLAB allocation
    static void emit_wasm_allocation(
        std::vector<uint8_t>& code,
        uint32_t size_local,
        uint32_t result_local,
        uint32_t type_id,
        uint32_t slow_path_label
    ) {
        // Load TLAB current from linear memory
        code.push_back(0x41); // i32.const
        emit_leb128(code, WASM_TLAB_CURRENT_ADDR);
        code.push_back(0x28); // i32.load
        code.push_back(0x02); // align=4
        code.push_back(0x00); // offset=0
        
        // Get size and add header
        code.push_back(0x20); // local.get
        emit_leb128(code, size_local);
        code.push_back(0x41); // i32.const
        code.push_back(0x08); // header size
        code.push_back(0x6A); // i32.add
        
        // Calculate new current
        code.push_back(0x20); // local.get (current)
        emit_leb128(code, 0); // temp local
        code.push_back(0x6A); // i32.add
        
        // Load TLAB end
        code.push_back(0x41); // i32.const
        emit_leb128(code, WASM_TLAB_END_ADDR);
        code.push_back(0x28); // i32.load
        code.push_back(0x02); // align=4
        code.push_back(0x00); // offset=0
        
        // Compare
        code.push_back(0x4B); // i32.gt_u
        code.push_back(0x0D); // br_if
        emit_leb128(code, slow_path_label);
        
        // Store new current
        code.push_back(0x41); // i32.const
        emit_leb128(code, WASM_TLAB_CURRENT_ADDR);
        code.push_back(0x20); // local.get (new current)
        emit_leb128(code, 1);
        code.push_back(0x36); // i32.store
        code.push_back(0x02); // align=4
        code.push_back(0x00); // offset=0
        
        // Initialize header
        // ... similar pattern for header init ...
    }
    
    // ============================================================================
    // SAFEPOINT POLLING
    // ============================================================================
    
    // Emit safepoint check (2-3 instructions)
    static void emit_x86_safepoint_poll(
        uint8_t* code_buffer,
        size_t& offset,
        void* slow_path
    ) {
        // test byte [safepoint_page], 0
        code_buffer[offset++] = 0x80;
        code_buffer[offset++] = 0x3C;
        code_buffer[offset++] = 0x25;
        write_u64(code_buffer + offset, (uint64_t)&GarbageCollector::safepoint_page_);
        offset += 8;
        code_buffer[offset++] = 0x00;
        
        // This will fault if page is protected, triggering safepoint
    }
    
private:
    static constexpr uint32_t TLAB_CURRENT_OFFSET = 0x100;
    static constexpr uint32_t TLAB_END_OFFSET = 0x108;
    static constexpr uint32_t WASM_TLAB_CURRENT_ADDR = 0x1000;
    static constexpr uint32_t WASM_TLAB_END_ADDR = 0x1008;
    
    static void write_u32(uint8_t* buffer, uint32_t value) {
        *reinterpret_cast<uint32_t*>(buffer) = value;
    }
    
    static void write_u64(uint8_t* buffer, uint64_t value) {
        *reinterpret_cast<uint64_t*>(buffer) = value;
    }
    
    static void emit_leb128(std::vector<uint8_t>& buffer, uint32_t value) {
        do {
            uint8_t byte = value & 0x7F;
            value >>= 7;
            if (value != 0) byte |= 0x80;
            buffer.push_back(byte);
        } while (value != 0);
    }
};

// ============================================================================
// JIT COMPILER INTEGRATION POINTS
// ============================================================================

class JITCompiler {
public:
    // Called during function compilation to analyze escapes
    void analyze_function_escapes(Function* func) {
        // Walk AST/IR to find allocations
        for (auto& alloc : func->allocations) {
            EscapeAnalyzer::register_variable_def(
                alloc.var_id,
                alloc.scope_id,
                alloc.site_id
            );
        }
        
        // Track assignments
        for (auto& assign : func->assignments) {
            if (assign.is_heap_store || assign.is_global) {
                // Mark as escaping
                for (auto site : get_allocation_sites(assign.source)) {
                    mark_escape_to_heap(site);
                }
            } else {
                EscapeAnalyzer::register_assignment(
                    assign.from_var,
                    assign.to_var
                );
            }
        }
        
        // Track returns
        for (auto& ret : func->returns) {
            if (ret.value_var != INVALID_VAR) {
                EscapeAnalyzer::register_return(ret.value_var);
            }
        }
        
        // Track closure captures
        for (auto& closure : func->closures) {
            for (auto var : closure.captured_vars) {
                EscapeAnalyzer::register_closure_capture(var);
            }
        }
    }
    
    // Emit allocation based on escape analysis
    void emit_allocation(AllocationSite& site) {
        auto analysis = EscapeAnalyzer::analyze_allocation(
            this,
            site.id,
            site.size,
            site.type_id
        );
        
        if (analysis.can_stack_allocate && 
            site.size <= GCConfig::MAX_STACK_ALLOC_SIZE) {
            // Emit stack allocation
            if (target_arch_ == Arch::X86_64) {
                JITGCIntegration::emit_x86_stack_allocation(
                    code_buffer_,
                    code_offset_,
                    site.size,
                    site.result_reg,
                    site.type_id
                );
            }
            site.is_stack_allocated = true;
        } else {
            // Emit heap allocation
            if (target_arch_ == Arch::X86_64) {
                JITGCIntegration::emit_x86_allocation(
                    code_buffer_,
                    code_offset_,
                    site.size_reg,
                    site.result_reg,
                    site.type_id,
                    get_slow_path_label(site.id)
                );
            }
        }
    }
    
    // Emit write barrier only for heap objects
    void emit_field_write(FieldWrite& write) {
        // Skip barrier for stack objects
        if (write.obj_is_stack_allocated) {
            // Just emit the write
            emit_raw_write(write);
            return;
        }
        
        // Skip barrier for primitive types
        if (!is_reference_type(write.value_type)) {
            emit_raw_write(write);
            return;
        }
        
        // Emit write barrier
        if (target_arch_ == Arch::X86_64) {
            JITGCIntegration::emit_x86_write_barrier(
                code_buffer_,
                code_offset_,
                write.obj_reg,
                write.field_offset,
                write.value_reg
            );
        }
    }
    
    // Register GC roots at function entry
    void emit_function_prologue(Function* func) {
        // Count reference-typed locals
        size_t ref_count = 0;
        for (auto& local : func->locals) {
            if (is_reference_type(local.type)) {
                ref_count++;
            }
        }
        
        if (ref_count > 0) {
            // Emit call to register roots
            emit_call("__gc_register_roots", 
                     &func->locals[0], ref_count);
        }
        
        // Register scope entry for escape analysis
        emit_call("__escape_scope_enter", func->scope_id);
    }
    
    // Unregister roots at function exit
    void emit_function_epilogue(Function* func) {
        // Scope exit
        emit_call("__escape_scope_exit", func->scope_id);
        
        // Unregister roots
        size_t ref_count = count_ref_locals(func);
        if (ref_count > 0) {
            emit_call("__gc_unregister_roots",
                     &func->locals[0], ref_count);
        }
    }
    
    // Emit safepoint at loop backedges and calls
    void emit_safepoint() {
        if (config_.emit_safepoints) {
            if (target_arch_ == Arch::X86_64) {
                JITGCIntegration::emit_x86_safepoint_poll(
                    code_buffer_,
                    code_offset_,
                    safepoint_slow_path_
                );
            }
        }
    }
};

} // namespace gots