#include "gc_optimized_barriers.h"
#include <cstring>
#include <algorithm>
#include <chrono>
#include <iostream>

// Optimization macros for branch prediction
#ifndef likely
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)
#endif

namespace gots {

// ============================================================================
// FAST HEADER VALIDATION - No exceptions
// ============================================================================

// Fast header validation without exception handling
static inline bool is_valid_header_fast(ObjectHeader* header) {
    // Validate pointer alignment first
    uintptr_t addr = reinterpret_cast<uintptr_t>(header);
    if (addr & 0x7) return false; // Must be 8-byte aligned
    
    // Basic range check for reasonable heap addresses
    if (addr < 0x1000 || addr > 0x7FFFFFFFFFFF) return false;
    
    // Use volatile access to prevent optimization and check for segfault
    volatile ObjectHeader* vol_header = header;
    
    // Try to read type_id with minimal risk
    uint32_t type_id = vol_header->type_id;
    if (type_id == 0 || type_id > 0xFFFF) return false;
    
    // Try to read size field
    uint32_t size = vol_header->size;
    if (size == 0 || size > 0x10000000) return false; // Max 256MB object
    
    return true;
}

// ============================================================================
// STATIC MEMBER DEFINITIONS
// ============================================================================

uint8_t* OptimizedWriteBarrier::card_table_ = nullptr;
size_t OptimizedWriteBarrier::card_table_size_ = 0;
size_t OptimizedWriteBarrier::card_shift_ = 9;  // 512-byte cards by default

std::atomic<bool> OptimizedWriteBarrier::barrier_enabled_{true};
std::atomic<bool> OptimizedWriteBarrier::concurrent_marking_active_{false};

std::atomic<size_t> OptimizedWriteBarrier::barrier_hits_{0};
std::atomic<size_t> OptimizedWriteBarrier::barrier_misses_{0};
std::atomic<size_t> OptimizedWriteBarrier::false_positives_{0};

std::atomic<int> AdaptiveWriteBarriers::barrier_mode_{AdaptiveWriteBarriers::SIMPLE};
std::atomic<size_t> AdaptiveWriteBarriers::adaptation_interval_{10000};
std::atomic<size_t> AdaptiveWriteBarriers::operations_count_{0};
std::atomic<size_t> AdaptiveWriteBarriers::barrier_overhead_ns_{0};
std::atomic<size_t> AdaptiveWriteBarriers::gc_pause_reduction_ms_{0};

// ============================================================================
// OPTIMIZED WRITE BARRIER IMPLEMENTATION
// ============================================================================

void OptimizedWriteBarrier::initialize(uint8_t* heap_start, size_t heap_size, size_t card_size) {
    card_shift_ = __builtin_ctzl(card_size);  // Log2 of card size
    card_table_size_ = heap_size >> card_shift_;
    
    // Allocate card table with alignment for SIMD operations
    card_table_ = static_cast<uint8_t*>(aligned_alloc(64, card_table_size_));
    if (!card_table_) {
        throw std::bad_alloc();
    }
    
    // Initialize to clean
    std::memset(card_table_, 0, card_table_size_);
    
    std::cout << "DEBUG: Initialized optimized write barriers\n";
    std::cout << "  Card table size: " << card_table_size_ << " bytes\n";
    std::cout << "  Card shift: " << card_shift_ << "\n";
}

void OptimizedWriteBarrier::shutdown() {
    if (card_table_) {
        free(card_table_);
        card_table_ = nullptr;
        card_table_size_ = 0;
    }
}

void OptimizedWriteBarrier::write_barrier_slow(void* obj, void** field, void* new_value) {
    // Validate inputs before proceeding (fast path)
    if (unlikely(!obj || !field)) {
        return; // Silent fail for invalid inputs to avoid console spam
    }
    
    // Conditional statistics collection only in debug builds
    #ifdef GC_COLLECT_BARRIER_STATS
    barrier_hits_.fetch_add(1, std::memory_order_relaxed);
    #endif
    
    // Perform the write first
    *field = new_value;
    
    if (!new_value) return;  // No need to track null references
    
    // Validate object pointers before dereferencing headers
    uintptr_t obj_addr = reinterpret_cast<uintptr_t>(obj);
    uintptr_t value_addr = reinterpret_cast<uintptr_t>(new_value);
    
    if (unlikely(obj_addr < 0x1000 || value_addr < 0x1000)) {
        // Invalid pointers, skip barrier to avoid segfault
        #ifdef GC_COLLECT_BARRIER_STATS
        false_positives_.fetch_add(1, std::memory_order_relaxed);
        #endif
        return;
    }
    
    // Check if we need to mark the card
    ObjectHeader* obj_header = reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(obj) - sizeof(ObjectHeader)
    );
    ObjectHeader* value_header = reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(new_value) - sizeof(ObjectHeader)
    );
    
    bool needs_barrier = false;
    
    // Replace try-catch with safe pointer validation
    if (unlikely(concurrent_marking_active_.load(std::memory_order_relaxed))) {
        // During concurrent marking, track all pointer stores
        needs_barrier = true;
    } else {
        // Normal generational barrier: old -> young references
        // Fast header validation without exceptions
        if (likely(is_valid_header_fast(obj_header) && is_valid_header_fast(value_header))) {
            needs_barrier = (obj_header->flags & ObjectHeader::IN_OLD_GEN) &&
                           !(value_header->flags & ObjectHeader::IN_OLD_GEN);
        } else {
            // Invalid headers, conservatively mark card
            needs_barrier = true;
            #ifdef GC_COLLECT_BARRIER_STATS
            false_positives_.fetch_add(1, std::memory_order_relaxed);
            #endif
        }
    }
    
    if (unlikely(needs_barrier)) {
        mark_card_optimized(obj);
    }
}

void OptimizedWriteBarrier::scan_dirty_cards_simd(std::function<void(uintptr_t)> callback) {
    constexpr size_t SIMD_WIDTH = 32;  // Process 32 bytes at a time with AVX2
    
    size_t simd_end = (card_table_size_ / SIMD_WIDTH) * SIMD_WIDTH;
    
    #ifdef __AVX2__
    __m256i zero = _mm256_setzero_si256();
    
    for (size_t i = 0; i < simd_end; i += SIMD_WIDTH) {
        __m256i cards = _mm256_load_si256(reinterpret_cast<const __m256i*>(&card_table_[i]));
        
        // Compare with zero to find dirty cards
        __m256i mask = _mm256_cmpeq_epi8(cards, zero);
        uint32_t dirty_mask = ~_mm256_movemask_epi8(mask);
        
        // Process each dirty card
        while (dirty_mask) {
            int bit = __builtin_ctz(dirty_mask);
            size_t card_index = i + bit;
            
            uintptr_t card_addr = card_index << card_shift_;
            callback(card_addr);
            
            dirty_mask &= dirty_mask - 1;  // Clear lowest set bit
        }
    }
    #endif
    
    // Process remaining cards
    for (size_t i = simd_end; i < card_table_size_; ++i) {
        if (card_table_[i]) {
            uintptr_t card_addr = i << card_shift_;
            callback(card_addr);
        }
    }
}

void OptimizedWriteBarrier::clear_cards_batch() {
    #ifdef __AVX2__
    constexpr size_t SIMD_WIDTH = 32;
    size_t simd_end = (card_table_size_ / SIMD_WIDTH) * SIMD_WIDTH;
    
    __m256i zero = _mm256_setzero_si256();
    
    // Clear cards in batches of 32 bytes
    for (size_t i = 0; i < simd_end; i += SIMD_WIDTH) {
        _mm256_store_si256(reinterpret_cast<__m256i*>(&card_table_[i]), zero);
    }
    
    // Clear remaining bytes
    std::memset(&card_table_[simd_end], 0, card_table_size_ - simd_end);
    #else
    std::memset(card_table_, 0, card_table_size_);
    #endif
}

OptimizedWriteBarrier::BarrierStats OptimizedWriteBarrier::get_stats() {
    BarrierStats stats;
    stats.hits = barrier_hits_.load();
    stats.misses = barrier_misses_.load();
    stats.false_positives = false_positives_.load();
    
    size_t total = stats.hits + stats.misses;
    stats.hit_rate = total > 0 ? static_cast<double>(stats.hits) / total : 0.0;
    
    return stats;
}

// ============================================================================
// LOCK-FREE REMEMBERED SET IMPLEMENTATION
// ============================================================================

LockFreeRememberedSet::LockFreeRememberedSet() : pool_index_(0) {
    // Initialize hash table
    for (size_t i = 0; i < TABLE_SIZE; ++i) {
        table_[i].store(nullptr, std::memory_order_relaxed);
    }
    
    // Allocate entry pool
    entry_pool_ = new RememberedEntry[POOL_SIZE];
    for (size_t i = 0; i < POOL_SIZE; ++i) {
        entry_pool_[i].object.store(nullptr);
        entry_pool_[i].field_offset.store(0);
        entry_pool_[i].next.store(nullptr);
    }
}

LockFreeRememberedSet::~LockFreeRememberedSet() {
    delete[] entry_pool_;
}

void LockFreeRememberedSet::add_entry(void* obj, size_t field_offset) {
    size_t hash = hash_object(obj);
    RememberedEntry* new_entry = allocate_entry();
    
    if (!new_entry) return;  // Pool exhausted
    
    new_entry->object.store(obj);
    new_entry->field_offset.store(field_offset);
    
    // Lock-free insertion into hash table
    RememberedEntry* head = table_[hash].load(std::memory_order_acquire);
    do {
        new_entry->next.store(head);
    } while (!table_[hash].compare_exchange_weak(head, new_entry, 
                                                std::memory_order_release, 
                                                std::memory_order_acquire));
}

void LockFreeRememberedSet::process_entries(std::function<void(void*, size_t)> callback) {
    for (size_t i = 0; i < TABLE_SIZE; ++i) {
        RememberedEntry* entry = table_[i].load(std::memory_order_acquire);
        while (entry) {
            void* obj = entry->object.load();
            size_t offset = entry->field_offset.load();
            
            if (obj) {
                callback(obj, offset);
            }
            
            entry = entry->next.load();
        }
    }
}

void LockFreeRememberedSet::clear() {
    for (size_t i = 0; i < TABLE_SIZE; ++i) {
        table_[i].store(nullptr, std::memory_order_release);
    }
    pool_index_.store(0, std::memory_order_release);
}

LockFreeRememberedSet::RememberedEntry* LockFreeRememberedSet::allocate_entry() {
    // Try to get an entry from the pool
    size_t current_index = pool_index_.load(std::memory_order_relaxed);
    
    // If pool is exhausted, try to recycle from the hash table
    if (current_index >= POOL_SIZE) {
        return recycle_entry();
    }
    
    // Try to atomically increment and get an entry
    size_t index = pool_index_.fetch_add(1, std::memory_order_relaxed);
    if (index < POOL_SIZE) {
        RememberedEntry* entry = &entry_pool_[index];
        entry->object.store(nullptr);
        entry->field_offset.store(0);
        entry->next.store(nullptr);
        return entry;
    }
    
    // Pool exhausted during allocation, try recycling
    return recycle_entry();
}

// Add recycling mechanism to reuse entries
LockFreeRememberedSet::RememberedEntry* LockFreeRememberedSet::recycle_entry() {
    // Find a chain with multiple entries and steal one
    for (size_t i = 0; i < TABLE_SIZE; i += 16) { // Sample every 16th bucket
        RememberedEntry* head = table_[i].load(std::memory_order_acquire);
        if (head && head->next.load()) {
            // Try to steal the second entry in the chain
            RememberedEntry* second = head->next.load();
            if (second && head->next.compare_exchange_weak(second, second->next.load())) {
                // Successfully recycled an entry
                second->object.store(nullptr);
                second->field_offset.store(0);
                second->next.store(nullptr);
                return second;
            }
        }
    }
    
    // If recycling fails, allocate from system (emergency fallback)
    static std::atomic<int> emergency_pools{0};
    if (emergency_pools.load() < 4) { // Limit emergency allocations
        emergency_pools.fetch_add(1);
        return new RememberedEntry{};
    }
    
    return nullptr; // Pool truly exhausted
}

// ============================================================================
// ADAPTIVE WRITE BARRIERS IMPLEMENTATION
// ============================================================================

void AdaptiveWriteBarriers::initialize() {
    // Start with simple barriers
    barrier_mode_.store(SIMPLE);
    operations_count_.store(0);
    
    std::cout << "DEBUG: Initialized adaptive write barriers\n";
}

void AdaptiveWriteBarriers::adapt_barrier_strategy() {
    static auto last_adaptation = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto time_since_last = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_adaptation).count();
    
    if (time_since_last < 1000) return;  // Don't adapt too frequently
    
    // Measure current performance
    measure_barrier_overhead();
    
    int current_mode = barrier_mode_.load();
    size_t overhead = barrier_overhead_ns_.load();
    size_t pause_reduction = gc_pause_reduction_ms_.load();
    
    // Simple adaptation logic
    if (overhead > 1000 && pause_reduction < 10) {
        // High overhead, low benefit - reduce barrier complexity
        if (current_mode > DISABLED) {
            barrier_mode_.store(current_mode - 1);
            std::cout << "DEBUG: Reduced barrier mode to " << (current_mode - 1) << "\n";
        }
    } else if (overhead < 500 && pause_reduction > 50) {
        // Low overhead, high benefit - increase barrier complexity
        if (current_mode < CONCURRENT) {
            barrier_mode_.store(current_mode + 1);
            std::cout << "DEBUG: Increased barrier mode to " << (current_mode + 1) << "\n";
        }
    }
    
    last_adaptation = now;
}

void AdaptiveWriteBarriers::simple_write_barrier(void* obj, void** field, void* new_value) {
    // Simple barrier: only check for old->young references
    *field = new_value;
    
    if (!new_value) return;
    
    ObjectHeader* obj_header = reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(obj) - sizeof(ObjectHeader)
    );
    ObjectHeader* value_header = reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(new_value) - sizeof(ObjectHeader)
    );
    
    if ((obj_header->flags & ObjectHeader::IN_OLD_GEN) &&
        !(value_header->flags & ObjectHeader::IN_OLD_GEN)) {
        OptimizedWriteBarrier::mark_card_optimized(obj);
    }
}

void AdaptiveWriteBarriers::measure_barrier_overhead() {
    // Simple overhead measurement
    constexpr int SAMPLE_SIZE = 1000;
    auto start = std::chrono::high_resolution_clock::now();
    
    // Simulate barrier operations
    volatile void* dummy_obj = reinterpret_cast<void*>(0x1000);
    volatile void** dummy_field = reinterpret_cast<void**>(0x2000);
    volatile void* dummy_value = reinterpret_cast<void*>(0x3000);
    
    for (int i = 0; i < SAMPLE_SIZE; ++i) {
        // Simulate the fast path check
        bool same_gen = OptimizedWriteBarrier::same_generation_fast(
            const_cast<void*>(dummy_obj), const_cast<void*>(dummy_value));
        (void)same_gen;  // Prevent optimization
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        end - start).count();
    
    barrier_overhead_ns_.store(duration / SAMPLE_SIZE);
}

// ============================================================================
// SPECIALIZED BARRIERS IMPLEMENTATION
// ============================================================================

void SpecializedBarriers::array_write_barrier(void* array, size_t start_index, 
                                              size_t count, void** new_values) {
    if (!OptimizedWriteBarrier::barrier_enabled_.load()) {
        // No barriers needed, just copy values
        void** array_data = static_cast<void**>(array);
        std::memcpy(&array_data[start_index], new_values, count * sizeof(void*));
        return;
    }
    
    // Batch processing for better cache performance
    constexpr size_t BATCH_SIZE = 64;  // Process in cache-line sized batches
    
    void** array_data = static_cast<void**>(array);
    bool need_card_mark = false;
    
    for (size_t i = 0; i < count; i += BATCH_SIZE) {
        size_t batch_end = std::min(i + BATCH_SIZE, count);
        
        // Check if any value in batch needs barrier
        for (size_t j = i; j < batch_end; ++j) {
            if (new_values[j] && 
                !OptimizedWriteBarrier::same_generation_fast(array, new_values[j])) {
                need_card_mark = true;
                break;
            }
        }
        
        // Copy the batch
        std::memcpy(&array_data[start_index + i], &new_values[i], 
                   (batch_end - i) * sizeof(void*));
        
        // Mark card if needed
        if (need_card_mark) {
            OptimizedWriteBarrier::mark_card_optimized(array);
            need_card_mark = false;  // Only mark once per object
        }
    }
}

void SpecializedBarriers::weak_ref_write_barrier(void* obj, void** field, void* new_value) {
    // Weak references don't prevent GC, so simpler barrier logic
    *field = new_value;
    
    // Only mark card during concurrent marking
    if (OptimizedWriteBarrier::concurrent_marking_active_.load() && new_value) {
        OptimizedWriteBarrier::mark_card_optimized(obj);
    }
}

void SpecializedBarriers::bulk_update_barrier(void* obj, const std::vector<size_t>& field_offsets,
                                              const std::vector<void*>& new_values) {
    if (field_offsets.size() != new_values.size()) {
        return;  // Mismatched arrays
    }
    
    bool need_card_mark = false;
    uint8_t* obj_bytes = static_cast<uint8_t*>(obj);
    
    // Perform all updates first, then mark card once if needed
    for (size_t i = 0; i < field_offsets.size(); ++i) {
        void** field = reinterpret_cast<void**>(obj_bytes + field_offsets[i]);
        *field = new_values[i];
        
        if (!need_card_mark && new_values[i] &&
            !OptimizedWriteBarrier::same_generation_fast(obj, new_values[i])) {
            need_card_mark = true;
        }
    }
    
    if (need_card_mark) {
        OptimizedWriteBarrier::mark_card_optimized(obj);
    }
}

} // namespace gots