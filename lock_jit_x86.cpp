#include "compiler.h"
#include "lock_system.h"
#include <atomic>
#include <immintrin.h>

namespace gots {

// X86-64 register definitions
enum class X86Register : int {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3, RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8 = 8, R9 = 9, R10 = 10, R11 = 11, R12 = 12, R13 = 13, R14 = 14, R15 = 15
};

// Memory ordering constants for atomic operations
enum class MemoryOrder : int {
    RELAXED = 0,
    ACQUIRE = 1,
    RELEASE = 2,
    ACQ_REL = 3,
    SEQ_CST = 4
};

// Lock structure offsets (must match lock_system.h)
constexpr int LOCK_IS_LOCKED_OFFSET = 0;
constexpr int LOCK_OWNER_ID_OFFSET = 8;
constexpr int LOCK_LOCK_COUNT_OFFSET = 16;
constexpr int LOCK_MUTEX_OFFSET = 24;

void X86CodeGen::emit_lock_acquire(int lock_reg) {
    // Get current goroutine ID
    // mov r10, fs:[current_goroutine_offset]  ; Load current goroutine from TLS
    emit_byte(0x65); // FS segment prefix
    emit_byte(0x4C); // REX.WR
    emit_byte(0x8B); // MOV
    emit_byte(0x14); // r10, [disp32]
    emit_byte(0x25);
    emit_u32(0); // TLS offset for current_goroutine (to be resolved at runtime)
    
    // Load goroutine ID from current goroutine
    // mov r11, [r10 + goroutine_id_offset]
    emit_byte(0x4D); // REX.WR
    emit_byte(0x8B); // MOV
    emit_byte(0x5A); // r11, [r10 + disp8]
    emit_byte(8);    // goroutine ID offset
    
    // Fast path: try atomic compare-exchange on is_locked flag
    // xor eax, eax                    ; expected = false (0)
    emit_byte(0x31);
    emit_byte(0xC0);
    
    // mov edx, 1                      ; desired = true (1)
    emit_byte(0xBA);
    emit_u32(1);
    
    // lock cmpxchg byte [lock_reg + LOCK_IS_LOCKED_OFFSET], dl
    emit_byte(0xF0); // LOCK prefix
    emit_byte(0x0F);
    emit_byte(0xB0); // CMPXCHG byte
    emit_byte(0x80 | (lock_reg & 7)); // [lock_reg + disp32]
    emit_u32(LOCK_IS_LOCKED_OFFSET);
    
    // jne slow_path                   ; If lock was already held, go to slow path
    emit_byte(0x75);
    emit_byte(0x15); // Jump 21 bytes ahead to slow_path
    
    // Fast path success: set owner and count
    // mov [lock_reg + LOCK_OWNER_ID_OFFSET], r11  ; Set owner_goroutine_id
    emit_byte(0x4C); // REX.W
    emit_byte(0x89);
    emit_byte(0x98 | (lock_reg & 7)); // [lock_reg + disp32], r11
    emit_u32(LOCK_OWNER_ID_OFFSET);
    
    // mov dword [lock_reg + LOCK_LOCK_COUNT_OFFSET], 1  ; Set lock_count = 1
    emit_byte(0xC7);
    emit_byte(0x80 | (lock_reg & 7));
    emit_u32(LOCK_LOCK_COUNT_OFFSET);
    emit_u32(1);
    
    // jmp end                         ; Skip slow path
    emit_byte(0xEB);
    emit_byte(0x0C); // Jump 12 bytes ahead to end
    
    // slow_path:
    // Check for recursive locking
    // cmp [lock_reg + LOCK_OWNER_ID_OFFSET], r11
    emit_byte(0x4C); // REX.W
    emit_byte(0x39);
    emit_byte(0x98 | (lock_reg & 7)); // cmp [lock_reg + disp32], r11
    emit_u32(LOCK_OWNER_ID_OFFSET);
    
    // je recursive_lock               ; Same owner = recursive lock
    emit_byte(0x74);
    emit_byte(0x08); // Jump 8 bytes ahead to recursive_lock
    
    // Call slow lock path (contended lock)
    // push lock_reg                   ; Save lock pointer
    emit_byte(0x50 | (lock_reg & 7));
    
    // call __lock_acquire_slow
    emit_byte(0xE8);
    emit_u32(0); // Offset to __lock_acquire_slow (to be resolved)
    
    // pop lock_reg                    ; Restore lock pointer
    emit_byte(0x58 | (lock_reg & 7));
    
    // jmp end
    emit_byte(0xEB);
    emit_byte(0x06); // Jump 6 bytes ahead to end
    
    // recursive_lock:
    // inc dword [lock_reg + LOCK_LOCK_COUNT_OFFSET]
    emit_byte(0xFF);
    emit_byte(0x80 | (lock_reg & 7));
    emit_u32(LOCK_LOCK_COUNT_OFFSET);
    
    // end:
    // Lock acquisition complete
}

void X86CodeGen::emit_lock_release(int lock_reg) {
    // Get current goroutine ID
    // mov r10, fs:[current_goroutine_offset]
    emit_byte(0x65); // FS segment prefix
    emit_byte(0x4C); // REX.WR
    emit_byte(0x8B); // MOV
    emit_byte(0x14);
    emit_byte(0x25);
    emit_u32(0); // TLS offset (to be resolved)
    
    // mov r11, [r10 + 8]              ; Load goroutine ID
    emit_byte(0x4D);
    emit_byte(0x8B);
    emit_byte(0x5A);
    emit_byte(8);
    
    // Verify ownership
    // cmp [lock_reg + LOCK_OWNER_ID_OFFSET], r11
    emit_byte(0x4C); // REX.W
    emit_byte(0x39);
    emit_byte(0x98 | (lock_reg & 7));
    emit_u32(LOCK_OWNER_ID_OFFSET);
    
    // jne error                       ; Not owner = error
    emit_byte(0x75);
    emit_byte(0x30); // Jump to error handling
    
    // Check lock count
    // mov eax, [lock_reg + LOCK_LOCK_COUNT_OFFSET]
    emit_byte(0x8B);
    emit_byte(0x80 | (lock_reg & 7));
    emit_u32(LOCK_LOCK_COUNT_OFFSET);
    
    // test eax, eax                   ; Check if count is 0
    emit_byte(0x85);
    emit_byte(0xC0);
    
    // je error                        ; Count = 0 = error
    emit_byte(0x74);
    emit_byte(0x26); // Jump to error
    
    // cmp eax, 1                      ; Check if final unlock
    emit_byte(0x83);
    emit_byte(0xF8);
    emit_byte(1);
    
    // jg recursive_unlock             ; Count > 1 = recursive unlock
    emit_byte(0x7F);
    emit_byte(0x0E); // Jump to recursive unlock
    
    // Final unlock: clear owner and release lock
    // mov qword [lock_reg + LOCK_OWNER_ID_OFFSET], -1
    emit_byte(0x48); // REX.W
    emit_byte(0xC7);
    emit_byte(0x80 | (lock_reg & 7));
    emit_u32(LOCK_OWNER_ID_OFFSET);
    emit_u32(0xFFFFFFFF); // -1 (low 32 bits)
    
    // mov dword [lock_reg + LOCK_LOCK_COUNT_OFFSET], 0
    emit_byte(0xC7);
    emit_byte(0x80 | (lock_reg & 7));
    emit_u32(LOCK_LOCK_COUNT_OFFSET);
    emit_u32(0);
    
    // xchg byte [lock_reg + LOCK_IS_LOCKED_OFFSET], al  ; Atomic release with full fence
    emit_byte(0x86);
    emit_byte(0x80 | (lock_reg & 7));
    emit_u32(LOCK_IS_LOCKED_OFFSET);
    
    // jmp end
    emit_byte(0xEB);
    emit_byte(0x08); // Jump to end
    
    // recursive_unlock:
    // dec dword [lock_reg + LOCK_LOCK_COUNT_OFFSET]
    emit_byte(0xFF);
    emit_byte(0x88 | (lock_reg & 7)); // DEC with displacement
    emit_u32(LOCK_LOCK_COUNT_OFFSET);
    
    // jmp end
    emit_byte(0xEB);
    emit_byte(0x02); // Jump to end
    
    // error:
    // int3                            ; Breakpoint for debugging
    emit_byte(0xCC);
    
    // end:
    // Release complete
}

void X86CodeGen::emit_lock_try_acquire(int lock_reg, int result_reg) {
    // Get current goroutine ID
    // mov r10, fs:[current_goroutine_offset]
    emit_byte(0x65);
    emit_byte(0x4C);
    emit_byte(0x8B);
    emit_byte(0x14);
    emit_byte(0x25);
    emit_u32(0);
    
    // mov r11, [r10 + 8]
    emit_byte(0x4D);
    emit_byte(0x8B);
    emit_byte(0x5A);
    emit_byte(8);
    
    // Check for recursive locking
    // cmp [lock_reg + LOCK_OWNER_ID_OFFSET], r11
    emit_byte(0x4C);
    emit_byte(0x39);
    emit_byte(0x98 | (lock_reg & 7));
    emit_u32(LOCK_OWNER_ID_OFFSET);
    
    // je recursive_try_lock
    emit_byte(0x74);
    emit_byte(0x18); // Jump to recursive handling
    
    // Try atomic compare-exchange
    // xor eax, eax                    ; expected = false
    emit_byte(0x31);
    emit_byte(0xC0);
    
    // mov edx, 1                      ; desired = true
    emit_byte(0xBA);
    emit_u32(1);
    
    // lock cmpxchg byte [lock_reg + LOCK_IS_LOCKED_OFFSET], dl
    emit_byte(0xF0);
    emit_byte(0x0F);
    emit_byte(0xB0);
    emit_byte(0x80 | (lock_reg & 7));
    emit_u32(LOCK_IS_LOCKED_OFFSET);
    
    // sete al                         ; Set AL to 1 if successful
    emit_byte(0x0F);
    emit_byte(0x94);
    emit_byte(0xC0);
    
    // movzx result_reg, al            ; Zero-extend result to full register
    emit_byte(0x0F);
    emit_byte(0xB6);
    emit_byte(0xC0 | ((result_reg & 7) << 3));
    
    // test al, al                     ; Check if we got the lock
    emit_byte(0x84);
    emit_byte(0xC0);
    
    // je failed                       ; If failed, skip owner setting
    emit_byte(0x74);
    emit_byte(0x0C); // Jump to failed
    
    // Set owner and count on success
    // mov [lock_reg + LOCK_OWNER_ID_OFFSET], r11
    emit_byte(0x4C);
    emit_byte(0x89);
    emit_byte(0x98 | (lock_reg & 7));
    emit_u32(LOCK_OWNER_ID_OFFSET);
    
    // mov dword [lock_reg + LOCK_LOCK_COUNT_OFFSET], 1
    emit_byte(0xC7);
    emit_byte(0x80 | (lock_reg & 7));
    emit_u32(LOCK_LOCK_COUNT_OFFSET);
    emit_u32(1);
    
    // jmp end
    emit_byte(0xEB);
    emit_byte(0x0A); // Jump to end
    
    // recursive_try_lock:
    // inc dword [lock_reg + LOCK_LOCK_COUNT_OFFSET]
    emit_byte(0xFF);
    emit_byte(0x80 | (lock_reg & 7));
    emit_u32(LOCK_LOCK_COUNT_OFFSET);
    
    // mov result_reg, 1               ; Return success
    emit_byte(0xB8 | (result_reg & 7));
    emit_u32(1);
    
    // jmp end
    emit_byte(0xEB);
    emit_byte(0x05); // Jump to end
    
    // failed:
    // mov result_reg, 0               ; Return failure
    emit_byte(0xB8 | (result_reg & 7));
    emit_u32(0);
    
    // end:
    // Try acquire complete
}

void X86CodeGen::emit_lock_try_acquire_timeout(int lock_reg, int timeout_reg, int result_reg) {
    // For timeout version, we call a runtime function that handles the complex timing logic
    // push timeout_reg
    emit_byte(0x50 | (timeout_reg & 7));
    
    // push lock_reg
    emit_byte(0x50 | (lock_reg & 7));
    
    // call __lock_try_acquire_timeout
    emit_byte(0xE8);
    emit_u32(0); // Offset to be resolved
    
    // add rsp, 16                     ; Clean up stack
    emit_byte(0x48);
    emit_byte(0x83);
    emit_byte(0xC4);
    emit_byte(16);
    
    // mov result_reg, eax             ; Return result from function
    emit_byte(0x89);
    emit_byte(0xC0 | ((result_reg & 7) << 3));
}

void X86CodeGen::emit_atomic_compare_exchange(int ptr_reg, int expected_reg, int desired_reg, int result_reg) {
    // mov rax, expected_reg           ; CMPXCHG uses RAX for expected value
    emit_byte(0x48); // REX.W
    emit_byte(0x89);
    emit_byte(0xC0 | ((expected_reg & 7) << 3)); // mov rax, expected_reg
    
    // lock cmpxchg [ptr_reg], desired_reg
    emit_byte(0xF0); // LOCK prefix
    emit_byte(0x48); // REX.W for 64-bit operation
    emit_byte(0x0F);
    emit_byte(0xB1); // CMPXCHG
    emit_byte(0x00 | ((desired_reg & 7) << 3) | (ptr_reg & 7));
    
    // sete result_reg                 ; Set result to 1 if equal (success)
    emit_byte(0x0F);
    emit_byte(0x94);
    emit_byte(0xC0 | (result_reg & 7));
    
    // movzx result_reg, result_reg    ; Zero-extend to full register
    emit_byte(0x0F);
    emit_byte(0xB6);
    emit_byte(0xC0 | ((result_reg & 7) << 3) | (result_reg & 7));
}

void X86CodeGen::emit_atomic_fetch_add(int ptr_reg, int value_reg, int result_reg) {
    // mov result_reg, value_reg       ; Copy value to result (will be exchanged)
    emit_byte(0x48); // REX.W
    emit_byte(0x89);
    emit_byte(0xC0 | ((value_reg & 7) << 3) | (result_reg & 7));
    
    // lock xadd [ptr_reg], result_reg ; Atomic exchange and add
    emit_byte(0xF0); // LOCK prefix
    emit_byte(0x48); // REX.W
    emit_byte(0x0F);
    emit_byte(0xC1); // XADD
    emit_byte(0x00 | ((result_reg & 7) << 3) | (ptr_reg & 7));
    
    // result_reg now contains the original value at [ptr_reg]
}

void X86CodeGen::emit_atomic_store(int ptr_reg, int value_reg, int memory_order) {
    if (memory_order >= static_cast<int>(MemoryOrder::RELEASE)) {
        // For release semantics, use XCHG (implicit lock and full fence)
        // xchg [ptr_reg], value_reg
        emit_byte(0x48); // REX.W
        emit_byte(0x87);
        emit_byte(0x00 | ((value_reg & 7) << 3) | (ptr_reg & 7));
    } else {
        // For relaxed ordering, regular MOV is sufficient on x86-64
        // mov [ptr_reg], value_reg
        emit_byte(0x48); // REX.W
        emit_byte(0x89);
        emit_byte(0x00 | ((value_reg & 7) << 3) | (ptr_reg & 7));
    }
}

void X86CodeGen::emit_atomic_load(int ptr_reg, int result_reg, int memory_order) {
    // mov result_reg, [ptr_reg]       ; Regular load (atomic on aligned data)
    emit_byte(0x48); // REX.W
    emit_byte(0x8B);
    emit_byte(0x00 | ((result_reg & 7) << 3) | (ptr_reg & 7));
    
    if (memory_order >= static_cast<int>(MemoryOrder::ACQUIRE)) {
        // Add load-acquire fence
        // lfence                       ; Load fence for acquire semantics
        emit_byte(0x0F);
        emit_byte(0xAE);
        emit_byte(0xE8);
    }
}

void X86CodeGen::emit_memory_fence(int fence_type) {
    switch (fence_type) {
        case static_cast<int>(MemoryOrder::ACQUIRE):
            // lfence                   ; Load fence
            emit_byte(0x0F);
            emit_byte(0xAE);
            emit_byte(0xE8);
            break;
        case static_cast<int>(MemoryOrder::RELEASE):
            // sfence                   ; Store fence
            emit_byte(0x0F);
            emit_byte(0xAE);
            emit_byte(0xF8);
            break;
        case static_cast<int>(MemoryOrder::SEQ_CST):
        default:
            // mfence                   ; Full memory fence
            emit_byte(0x0F);
            emit_byte(0xAE);
            emit_byte(0xF0);
            break;
    }
}

// Helper functions for emitting immediate values
void X86CodeGen::emit_byte(uint8_t byte) {
    code.push_back(byte);
}

void X86CodeGen::emit_u32(uint32_t value) {
    code.push_back(value & 0xFF);
    code.push_back((value >> 8) & 0xFF);
    code.push_back((value >> 16) & 0xFF);
    code.push_back((value >> 24) & 0xFF);
}

} // namespace gots