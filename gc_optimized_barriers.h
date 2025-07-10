#pragma once

#include <atomic>
#include <cstdint>
#include <immintrin.h>  // For SIMD intrinsics
#include "gc_memory_manager.h"

namespace gots {

// ============================================================================
// OPTIMIZED WRITE BARRIER - High performance write barriers
// ============================================================================

class OptimizedWriteBarrier {
private:
    // Card table with optimized layout
    static uint8_t* card_table_;
    static size_t card_table_size_;
    static size_t card_shift_;  // Log2 of card size for fast division
    
    // Write barrier configuration
    static std::atomic<bool> barrier_enabled_;
    static std::atomic<bool> concurrent_marking_active_;
    
    // Statistics
    static std::atomic<size_t> barrier_hits_;
    static std::atomic<size_t> barrier_misses_;
    static std::atomic<size_t> false_positives_;
    
public:
    static void initialize(uint8_t* heap_start, size_t heap_size, size_t card_size);
    static void shutdown();
    
    // Ultra-fast inline write barrier with maximum optimization
    static inline void write_barrier_fast(void* obj, void** field, void* new_value) {
        // Do the write first - this is the common case and most important
        *field = new_value;
        
        // Early exit if barriers are disabled globally
        if (unlikely(!barrier_enabled_.load(std::memory_order_relaxed))) {
            return;
        }
        
        // Null pointer write never needs barrier
        if (unlikely(!new_value)) {
            return;
        }
        
        // Ultra-fast generation check using bit manipulation
        if (likely(same_generation_ultra_fast(obj, new_value))) {
            // No barrier needed - most common case
            return;
        }
        
        // Slow path: need to mark card (only for old->young references)
        mark_card_fast(obj);
    }
    
    // Ultra-fast generation checking using pointer arithmetic
    static inline bool same_generation_ultra_fast(void* obj1, void* obj2) {
        // Use pointer arithmetic to check if objects are in same region
        // This assumes heap layout: young gen followed by old gen
        const uintptr_t YOUNG_GEN_MASK = 0x7FFFFFF;  // 128MB young gen
        uintptr_t addr1 = reinterpret_cast<uintptr_t>(obj1);
        uintptr_t addr2 = reinterpret_cast<uintptr_t>(obj2);
        
        // Fast check: if both addresses have same high bits, same generation
        return ((addr1 ^ addr2) & ~YOUNG_GEN_MASK) == 0;
    }
    
    // Ultra-fast card marking without atomic operations
    static inline void mark_card_fast(void* obj) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
        size_t card_index = addr >> card_shift_;
        
        // Direct write - no atomic needed for card marking
        // Multiple threads marking same card is benign
        if (likely(card_index < card_table_size_)) {
            card_table_[card_index] = 1;
        }
    }
    
    // SIMD-optimized card scanning
    static void scan_dirty_cards_simd(std::function<void(uintptr_t)> callback);
    
    // Batch card clearing
    static void clear_cards_batch();
    
    // Enable/disable barriers
    static void enable_barriers() { barrier_enabled_.store(true); }
    static void disable_barriers() { barrier_enabled_.store(false); }
    
    // Concurrent marking support
    static void set_concurrent_marking(bool active) { 
        concurrent_marking_active_.store(active); 
    }
    
    // Statistics
    struct BarrierStats {
        size_t hits;
        size_t misses;
        size_t false_positives;
        double hit_rate;
    };
    static BarrierStats get_stats();
    
private:
    static void write_barrier_slow(void* obj, void** field, void* new_value);
    
    // Fast generation check with improved accuracy
    static inline bool same_generation_fast(void* obj1, void* obj2) {
        if (!obj1 || !obj2) return true;
        
        // Get object headers to check actual generation flags
        ObjectHeader* header1 = reinterpret_cast<ObjectHeader*>(
            static_cast<uint8_t*>(obj1) - sizeof(ObjectHeader)
        );
        ObjectHeader* header2 = reinterpret_cast<ObjectHeader*>(
            static_cast<uint8_t*>(obj2) - sizeof(ObjectHeader)
        );
        
        // Validate headers are reasonable before dereferencing
        uintptr_t addr1 = reinterpret_cast<uintptr_t>(obj1);
        uintptr_t addr2 = reinterpret_cast<uintptr_t>(obj2);
        
        // Basic bounds check to prevent segfaults
        if (addr1 < 0x1000 || addr2 < 0x1000 || 
            addr1 > 0x7FFFFFFFFFFF || addr2 > 0x7FFFFFFFFFFF) {
            // Fallback to address-based heuristic for invalid pointers
            return (addr1 ^ addr2) < (1ULL << 25);
        }
        
        // Check actual generation flags if headers seem valid
        try {
            bool obj1_old = header1->flags & ObjectHeader::IN_OLD_GEN;
            bool obj2_old = header2->flags & ObjectHeader::IN_OLD_GEN;
            
            // Same generation if both old or both young
            return obj1_old == obj2_old;
        } catch (...) {
            // If header access fails, fall back to heuristic
            return (addr1 ^ addr2) < (1ULL << 25);
        }
    }
    
    // Mark card with optimizations
    static inline void mark_card_optimized(void* obj) {
        uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
        size_t card_index = addr >> card_shift_;
        
        // Bounds check
        if (likely(card_index < card_table_size_)) {
            // Use atomic relaxed store - don't need ordering guarantees
            card_table_[card_index] = 1;
        }
    }
};

// ============================================================================
// LOCK-FREE REMEMBERED SET - Alternative to card table
// ============================================================================

class LockFreeRememberedSet {
private:
    struct RememberedEntry {
        std::atomic<void*> object;
        std::atomic<size_t> field_offset;
        std::atomic<RememberedEntry*> next;
    };
    
    // Hash table of remembered entries
    static constexpr size_t TABLE_SIZE = 65536;  // 64K entries
    std::atomic<RememberedEntry*> table_[TABLE_SIZE];
    
    // Memory pool for entries
    RememberedEntry* entry_pool_;
    std::atomic<size_t> pool_index_;
    static constexpr size_t POOL_SIZE = 1024 * 1024;  // 1M entries
    
public:
    LockFreeRememberedSet();
    ~LockFreeRememberedSet();
    
    // Add entry to remembered set
    void add_entry(void* obj, size_t field_offset);
    
    // Process all entries
    void process_entries(std::function<void(void*, size_t)> callback);
    
    // Clear all entries
    void clear();
    
private:
    size_t hash_object(void* obj) const {
        uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
        return (addr >> 3) & (TABLE_SIZE - 1);  // Simple hash
    }
    
    RememberedEntry* allocate_entry();
    RememberedEntry* recycle_entry();
};

// ============================================================================
// ADAPTIVE WRITE BARRIERS - Self-tuning barriers
// ============================================================================

class AdaptiveWriteBarriers {
private:
    // Adaptive configuration
    static std::atomic<int> barrier_mode_;  // 0=off, 1=simple, 2=full
    static std::atomic<size_t> adaptation_interval_;
    static std::atomic<size_t> operations_count_;
    
    // Performance metrics
    static std::atomic<size_t> barrier_overhead_ns_;
    static std::atomic<size_t> gc_pause_reduction_ms_;
    
public:
    enum BarrierMode {
        DISABLED = 0,
        SIMPLE = 1,      // Only young->old barriers
        FULL = 2,        // All cross-generational barriers
        CONCURRENT = 3   // Barriers for concurrent marking
    };
    
    static void initialize();
    
    // Adaptive write barrier that chooses best strategy
    static inline void adaptive_write_barrier(void* obj, void** field, void* new_value) {
        operations_count_.fetch_add(1, std::memory_order_relaxed);
        
        int mode = barrier_mode_.load(std::memory_order_relaxed);
        switch (mode) {
            case DISABLED:
                *field = new_value;
                break;
            case SIMPLE:
                simple_write_barrier(obj, field, new_value);
                break;
            case FULL:
            case CONCURRENT:
                OptimizedWriteBarrier::write_barrier_fast(obj, field, new_value);
                break;
        }
        
        // Check if adaptation is needed
        if (unlikely(operations_count_.load() % adaptation_interval_.load() == 0)) {
            adapt_barrier_strategy();
        }
    }
    
    // Force adaptation check
    static void adapt_barrier_strategy();
    
private:
    static void simple_write_barrier(void* obj, void** field, void* new_value);
    static void measure_barrier_overhead();
};

// ============================================================================
// SPECIALIZED BARRIERS - Type-specific optimizations
// ============================================================================

class SpecializedBarriers {
public:
    // Write barrier for arrays (batch processing)
    static void array_write_barrier(void* array, size_t start_index, size_t count, void** new_values);
    
    // Write barrier for object field updates
    static inline void field_write_barrier(void* obj, size_t field_offset, void* new_value) {
        void** field = reinterpret_cast<void**>(
            static_cast<uint8_t*>(obj) + field_offset);
        OptimizedWriteBarrier::write_barrier_fast(obj, field, new_value);
    }
    
    // Write barrier for weak references (special handling)
    static void weak_ref_write_barrier(void* obj, void** field, void* new_value);
    
    // Bulk update with optimized barriers
    static void bulk_update_barrier(void* obj, const std::vector<size_t>& field_offsets, 
                                   const std::vector<void*>& new_values);
};

// ============================================================================
// COMPILER INTRINSICS - Architecture-specific optimizations
// ============================================================================

class BarrierIntrinsics {
public:
    // Prefetch memory for better cache performance
    static inline void prefetch_for_write(void* addr) {
        #ifdef __x86_64__
        _mm_prefetch(static_cast<const char*>(addr), _MM_HINT_T0);
        #elif defined(__aarch64__)
        __builtin_prefetch(addr, 1, 3);  // Write, high locality
        #endif
    }
    
    // Fast memory fence for write ordering
    static inline void write_fence() {
        #ifdef __x86_64__
        _mm_sfence();
        #elif defined(__aarch64__)
        __asm__ volatile("dmb st" ::: "memory");
        #else
        std::atomic_thread_fence(std::memory_order_release);
        #endif
    }
    
    // Branch prediction hints
    static inline bool likely(bool condition) {
        #ifdef __GNUC__
        return __builtin_expect(condition, 1);
        #else
        return condition;
        #endif
    }
    
    static inline bool unlikely(bool condition) {
        #ifdef __GNUC__
        return __builtin_expect(condition, 0);
        #else
        return condition;
        #endif
    }
};

// ============================================================================
// JIT BARRIER TEMPLATES - Code generation templates
// ============================================================================

class JITBarrierTemplates {
public:
    // X86-64 assembly template for write barrier
    static constexpr const char* X86_64_WRITE_BARRIER = R"(
        # Fast write barrier for x86-64
        # Input: %rdi = obj, %rsi = field, %rdx = new_value
        
        # Check if barrier is enabled
        testb $1, barrier_enabled(%rip)
        jz .Lno_barrier
        
        # Check if same generation (fast heuristic)
        mov %rdi, %rax
        xor %rdx, %rax
        shr $25, %rax
        jnz .Lslow_barrier
        
        # Fast path: same generation
        mov %rdx, (%rsi)
        ret
        
    .Lslow_barrier:
        # Slow path: mark card
        mov %rdi, %rax
        shr $9, %rax        # card_shift
        movb $1, card_table(%rax)
        mov %rdx, (%rsi)
        ret
        
    .Lno_barrier:
        # No barrier needed
        mov %rdx, (%rsi)
        ret
    )";
    
    // WebAssembly template for write barrier
    static constexpr const char* WASM_WRITE_BARRIER = R"(
        ;; Fast write barrier for WebAssembly
        ;; Input: obj, field_ptr, new_value on stack
        
        (func $write_barrier (param $obj i32) (param $field i32) (param $value i32)
            ;; Check if barrier enabled
            global.get $barrier_enabled
            i32.eqz
            if
                ;; No barrier
                local.get $field
                local.get $value
                i32.store
                return
            end
            
            ;; Check same generation
            local.get $obj
            local.get $value
            i32.xor
            i32.const 33554432  ;; 1 << 25
            i32.lt_u
            if
                ;; Same generation
                local.get $field
                local.get $value
                i32.store
                return
            end
            
            ;; Mark card and store
            local.get $obj
            i32.const 9
            i32.shr_u
            i32.const 1
            call $mark_card
            
            local.get $field
            local.get $value
            i32.store
        )
    )";
};

// Global optimization flags
#ifndef likely
#define likely(x)   BarrierIntrinsics::likely(x)
#define unlikely(x) BarrierIntrinsics::unlikely(x)
#endif

} // namespace gots