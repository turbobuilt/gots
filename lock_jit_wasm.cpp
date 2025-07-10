#include "compiler.h"
#include "lock_system.h"
#include <atomic>

namespace gots {

// WebAssembly opcodes for atomic operations (threads extension)
constexpr uint8_t WASM_ATOMIC_PREFIX = 0xFE;
constexpr uint8_t WASM_ATOMIC_WAKE = 0x01;
constexpr uint8_t WASM_ATOMIC_I32_WAIT = 0x02;
constexpr uint8_t WASM_ATOMIC_I64_WAIT = 0x03;
constexpr uint8_t WASM_ATOMIC_I32_LOAD = 0x10;
constexpr uint8_t WASM_ATOMIC_I64_LOAD = 0x11;
constexpr uint8_t WASM_ATOMIC_I32_STORE = 0x17;
constexpr uint8_t WASM_ATOMIC_I64_STORE = 0x18;
constexpr uint8_t WASM_ATOMIC_I32_RMW_CMPXCHG = 0x48;
constexpr uint8_t WASM_ATOMIC_I64_RMW_CMPXCHG = 0x49;
constexpr uint8_t WASM_ATOMIC_I32_RMW_ADD = 0x1E;
constexpr uint8_t WASM_ATOMIC_I64_RMW_ADD = 0x1F;

// Standard WebAssembly opcodes
constexpr uint8_t WASM_LOCAL_GET = 0x20;
constexpr uint8_t WASM_LOCAL_SET = 0x21;
constexpr uint8_t WASM_I32_CONST = 0x41;
constexpr uint8_t WASM_I64_CONST = 0x42;
constexpr uint8_t WASM_I32_EQZ = 0x45;
constexpr uint8_t WASM_I64_EQZ = 0x50;
constexpr uint8_t WASM_I32_EQ = 0x46;
constexpr uint8_t WASM_I64_EQ = 0x51;
constexpr uint8_t WASM_I32_ADD = 0x6A;
constexpr uint8_t WASM_I64_ADD = 0x7C;
constexpr uint8_t WASM_IF = 0x04;
constexpr uint8_t WASM_ELSE = 0x05;
constexpr uint8_t WASM_END = 0x0B;
constexpr uint8_t WASM_BR = 0x0C;
constexpr uint8_t WASM_BR_IF = 0x0D;
constexpr uint8_t WASM_CALL = 0x10;
constexpr uint8_t WASM_SELECT = 0x1B;

// Lock structure offsets in WebAssembly linear memory
constexpr uint32_t LOCK_IS_LOCKED_OFFSET = 0;
constexpr uint32_t LOCK_OWNER_ID_OFFSET = 8;
constexpr uint32_t LOCK_LOCK_COUNT_OFFSET = 16;

void WasmCodeGen::emit_lock_acquire(int lock_reg) {
    // Get current goroutine ID from global
    emit_opcode(WASM_CALL);
    emit_leb128(get_function_index("__get_current_goroutine_id"));
    
    // Store current goroutine ID in local variable
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(get_next_local_index()); // current_goroutine_id local
    int current_id_local = get_current_local_index();
    
    // Fast path: try atomic compare-exchange
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_IS_LOCKED_OFFSET);
    emit_opcode(WASM_I32_ADD); // Calculate address of is_locked field
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(0); // expected = false
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(1); // desired = true
    
    // i32.atomic.rmw.cmpxchg (ptr expected desired -> original)
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I32_RMW_CMPXCHG);
    emit_leb128(2); // alignment (4 bytes)
    emit_leb128(0); // offset
    
    // Check if we got the lock (original value was 0)
    emit_opcode(WASM_I32_EQZ); // true if original was 0 (we got lock)
    
    emit_opcode(WASM_IF);
    emit_type(WASM_TYPE_VOID);
    
    // Fast path success: set owner and count
    // Set owner_goroutine_id
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_OWNER_ID_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(current_id_local);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I64_STORE);
    emit_leb128(3); // alignment (8 bytes)
    emit_leb128(0); // offset
    
    // Set lock_count = 1
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_LOCK_COUNT_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(1);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I32_STORE);
    emit_leb128(2); // alignment (4 bytes)
    emit_leb128(0); // offset
    
    emit_opcode(WASM_ELSE);
    
    // Slow path: check for recursive locking or contention
    // Load current owner
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_OWNER_ID_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I64_LOAD);
    emit_leb128(3); // alignment
    emit_leb128(0); // offset
    
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(current_id_local);
    
    emit_opcode(WASM_I64_EQ); // Check if same owner
    
    emit_opcode(WASM_IF);
    emit_type(WASM_TYPE_VOID);
    
    // Recursive lock: increment count
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_LOCK_COUNT_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(1);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I32_RMW_ADD);
    emit_leb128(2); // alignment
    emit_leb128(0); // offset
    emit_opcode(WASM_DROP); // Drop the returned original value
    
    emit_opcode(WASM_ELSE);
    
    // Contended lock: call slow path
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_CALL);
    emit_leb128(get_function_index("__lock_acquire_slow"));
    
    emit_opcode(WASM_END); // End recursive check
    emit_opcode(WASM_END); // End fast path check
}

void WasmCodeGen::emit_lock_release(int lock_reg) {
    // Get current goroutine ID
    emit_opcode(WASM_CALL);
    emit_leb128(get_function_index("__get_current_goroutine_id"));
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(get_next_local_index());
    int current_id_local = get_current_local_index();
    
    // Verify ownership
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_OWNER_ID_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I64_LOAD);
    emit_leb128(3); // alignment
    emit_leb128(0); // offset
    
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(current_id_local);
    
    emit_opcode(WASM_I64_EQ);
    
    emit_opcode(WASM_IF);
    emit_type(WASM_TYPE_VOID);
    
    // Owner verification passed, check lock count
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_LOCK_COUNT_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I32_LOAD);
    emit_leb128(2); // alignment
    emit_leb128(0); // offset
    
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(get_next_local_index());
    int count_local = get_current_local_index();
    
    // Check if count > 1 (recursive unlock)
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(count_local);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(1);
    emit_opcode(WASM_I32_GT_S); // Signed greater than
    
    emit_opcode(WASM_IF);
    emit_type(WASM_TYPE_VOID);
    
    // Recursive unlock: decrement count
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_LOCK_COUNT_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(-1); // Subtract 1
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I32_RMW_ADD);
    emit_leb128(2); // alignment
    emit_leb128(0); // offset
    emit_opcode(WASM_DROP);
    
    emit_opcode(WASM_ELSE);
    
    // Final unlock: clear owner and release lock
    // Clear owner (-1)
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_OWNER_ID_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_I64_CONST);
    emit_leb128(-1);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I64_STORE);
    emit_leb128(3); // alignment
    emit_leb128(0); // offset
    
    // Clear count
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_LOCK_COUNT_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(0);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I32_STORE);
    emit_leb128(2); // alignment
    emit_leb128(0); // offset
    
    // Release the lock
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_IS_LOCKED_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(0);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I32_STORE);
    emit_leb128(2); // alignment
    emit_leb128(0); // offset
    
    // Wake waiting threads
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_IS_LOCKED_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(1); // Wake one thread
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_WAKE);
    emit_leb128(2); // alignment
    emit_leb128(0); // offset
    emit_opcode(WASM_DROP); // Drop wake count result
    
    emit_opcode(WASM_END); // End count check
    emit_opcode(WASM_ELSE);
    
    // Not owner: call error handler
    emit_opcode(WASM_CALL);
    emit_leb128(get_function_index("__lock_release_error"));
    
    emit_opcode(WASM_END); // End ownership check
}

void WasmCodeGen::emit_lock_try_acquire(int lock_reg, int result_reg) {
    // Get current goroutine ID
    emit_opcode(WASM_CALL);
    emit_leb128(get_function_index("__get_current_goroutine_id"));
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(get_next_local_index());
    int current_id_local = get_current_local_index();
    
    // Check for recursive locking first
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_OWNER_ID_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I64_LOAD);
    emit_leb128(3); // alignment
    emit_leb128(0); // offset
    
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(current_id_local);
    
    emit_opcode(WASM_I64_EQ);
    
    emit_opcode(WASM_IF);
    emit_type(WASM_TYPE_I32);
    
    // Recursive try lock: increment count and return success
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_LOCK_COUNT_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(1);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I32_RMW_ADD);
    emit_leb128(2); // alignment
    emit_leb128(0); // offset
    emit_opcode(WASM_DROP);
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(1); // Return success
    
    emit_opcode(WASM_ELSE);
    
    // Try atomic compare-exchange
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_IS_LOCKED_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(0); // expected = false
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(1); // desired = true
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I32_RMW_CMPXCHG);
    emit_leb128(2); // alignment
    emit_leb128(0); // offset
    
    // Check if successful (original was 0)
    emit_opcode(WASM_I32_EQZ);
    
    emit_opcode(WASM_IF);
    emit_type(WASM_TYPE_I32);
    
    // Success: set owner and count
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_OWNER_ID_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(current_id_local);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I64_STORE);
    emit_leb128(3); // alignment
    emit_leb128(0); // offset
    
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_I32_CONST);
    emit_leb128(LOCK_LOCK_COUNT_OFFSET);
    emit_opcode(WASM_I32_ADD);
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(1);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I32_STORE);
    emit_leb128(2); // alignment
    emit_leb128(0); // offset
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(1); // Return success
    
    emit_opcode(WASM_ELSE);
    
    emit_opcode(WASM_I32_CONST);
    emit_leb128(0); // Return failure
    
    emit_opcode(WASM_END); // End success check
    emit_opcode(WASM_END); // End recursive check
    
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(result_reg);
}

void WasmCodeGen::emit_lock_try_acquire_timeout(int lock_reg, int timeout_reg, int result_reg) {
    // For timeout version, call runtime function
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(lock_reg);
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(timeout_reg);
    
    emit_opcode(WASM_CALL);
    emit_leb128(get_function_index("__lock_try_acquire_timeout"));
    
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(result_reg);
}

void WasmCodeGen::emit_atomic_compare_exchange(int ptr_reg, int expected_reg, int desired_reg, int result_reg) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(ptr_reg);
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(expected_reg);
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(desired_reg);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I64_RMW_CMPXCHG);
    emit_leb128(3); // alignment (8 bytes)
    emit_leb128(0); // offset
    
    // Check if result equals expected (success)
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(expected_reg);
    emit_opcode(WASM_I64_EQ);
    
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(result_reg);
}

void WasmCodeGen::emit_atomic_fetch_add(int ptr_reg, int value_reg, int result_reg) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(ptr_reg);
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(value_reg);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I64_RMW_ADD);
    emit_leb128(3); // alignment
    emit_leb128(0); // offset
    
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(result_reg);
}

void WasmCodeGen::emit_atomic_store(int ptr_reg, int value_reg, int memory_order) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(ptr_reg);
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(value_reg);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I64_STORE);
    emit_leb128(3); // alignment
    emit_leb128(0); // offset
}

void WasmCodeGen::emit_atomic_load(int ptr_reg, int result_reg, int memory_order) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(ptr_reg);
    
    emit_opcode(WASM_ATOMIC_PREFIX);
    emit_opcode(WASM_ATOMIC_I64_LOAD);
    emit_leb128(3); // alignment
    emit_leb128(0); // offset
    
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(result_reg);
}

void WasmCodeGen::emit_memory_fence(int fence_type) {
    // WebAssembly doesn't have explicit fence instructions
    // Atomic operations provide necessary ordering guarantees
    // This is a no-op for WebAssembly
}

// Helper functions for WebAssembly bytecode emission
void WasmCodeGen::emit_opcode(uint8_t opcode) {
    code.push_back(opcode);
}

void WasmCodeGen::emit_leb128(uint64_t value) {
    while (value >= 0x80) {
        code.push_back((value & 0x7F) | 0x80);
        value >>= 7;
    }
    code.push_back(value & 0x7F);
}

void WasmCodeGen::emit_type(uint8_t type) {
    code.push_back(type);
}

// Function index management (would be part of larger WASM module management)
uint32_t WasmCodeGen::get_function_index(const std::string& name) {
    // This would lookup function indices in the WebAssembly module
    // For now, return a placeholder
    static std::unordered_map<std::string, uint32_t> function_indices = {
        {"__get_current_goroutine_id", 0},
        {"__lock_acquire_slow", 1},
        {"__lock_release_error", 2},
        {"__lock_try_acquire_timeout", 3}
    };
    
    auto it = function_indices.find(name);
    return (it != function_indices.end()) ? it->second : 0;
}

uint32_t WasmCodeGen::get_next_local_index() {
    return next_local_index_++;
}

uint32_t WasmCodeGen::get_current_local_index() {
    return next_local_index_ - 1;
}

} // namespace gots