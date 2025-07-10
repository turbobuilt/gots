#include "goroutine_aware_gc.h"
#include <atomic>
#include <iostream>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace gots {

// ============================================================================
// WRITE BARRIER GLOBAL STATE
// ============================================================================

// Card table for generational GC - using smart pointer for safety
static std::unique_ptr<uint8_t[]> g_card_table = nullptr;
static size_t g_card_table_size = 0;
static std::atomic<bool> g_card_table_initialized{false};

// Write barrier statistics
static std::atomic<size_t> g_fast_writes{0};
static std::atomic<size_t> g_sync_writes{0};
static std::atomic<size_t> g_barrier_hits{0};
static std::atomic<size_t> g_card_marks{0};

// Synchronization for write barrier initialization
static std::mutex g_barrier_mutex;

// ============================================================================
// CARD TABLE MANAGEMENT
// ============================================================================

static void initialize_card_table() {
    if (g_card_table_initialized.load()) return;
    
    std::lock_guard<std::mutex> lock(g_barrier_mutex);
    if (g_card_table_initialized.load()) return;
    
    // Calculate card table size based on total heap size
    size_t total_heap_size = GCConfig::YOUNG_GEN_SIZE + GCConfig::OLD_GEN_SIZE;
    g_card_table_size = (total_heap_size + GCConfig::CARD_SIZE - 1) / GCConfig::CARD_SIZE;
    
    // Allocate card table using smart pointer
    g_card_table = std::make_unique<uint8_t[]>(g_card_table_size);
    std::memset(g_card_table.get(), 0, g_card_table_size);
    
    g_card_table_initialized.store(true);
    
    std::cout << "[BARRIER] Initialized card table: " << g_card_table_size 
              << " cards for " << total_heap_size << " bytes\n";
}

static void cleanup_card_table() {
    if (!g_card_table_initialized.load()) return;
    
    std::lock_guard<std::mutex> lock(g_barrier_mutex);
    if (!g_card_table_initialized.load()) return;
    
    g_card_table.reset();
    g_card_table_size = 0;
    g_card_table_initialized.store(false);
    
    std::cout << "[BARRIER] Cleaned up card table\n";
}

static inline void mark_card_dirty(void* addr) {
    if (!g_card_table_initialized.load()) return;
    
    uintptr_t address = reinterpret_cast<uintptr_t>(addr);
    size_t card_index = address / GCConfig::CARD_SIZE;
    
    if (card_index < g_card_table_size) {
        g_card_table.get()[card_index] = 1;
        g_card_marks.fetch_add(1, std::memory_order_relaxed);
    }
}

static inline bool is_card_dirty(void* addr) {
    if (!g_card_table_initialized.load()) return false;
    
    uintptr_t address = reinterpret_cast<uintptr_t>(addr);
    size_t card_index = address / GCConfig::CARD_SIZE;
    
    return (card_index < g_card_table_size) && (g_card_table.get()[card_index] != 0);
}

static void clear_card_table() {
    if (!g_card_table_initialized.load()) return;
    
    std::memset(g_card_table.get(), 0, g_card_table_size);
    std::cout << "[BARRIER] Cleared card table\n";
}

// ============================================================================
// WRITE BARRIER IMPLEMENTATION
// ============================================================================

void GoroutineWriteBarrier::initialize() {
    initialize_card_table();
    std::cout << "[BARRIER] Initialized goroutine write barriers\n";
}

void GoroutineWriteBarrier::cleanup() {
    cleanup_card_table();
    std::cout << "[BARRIER] Cleaned up goroutine write barriers\n";
}

void GoroutineWriteBarrier::write_ref_with_sync(
    void* obj,
    void* field,
    void* new_value,
    uint32_t writing_goroutine_id
) {
    if (!obj || !field) {
        // Simple write for null objects
        *reinterpret_cast<void**>(field) = new_value;
        return;
    }
    
    // Get object headers
    GoroutineObjectHeader* obj_header = get_goroutine_header(obj);
    GoroutineObjectHeader* value_header = get_goroutine_header(new_value);
    
    if (!obj_header) {
        // No header, assume it's a simple write
        *reinterpret_cast<void**>(field) = new_value;
        g_fast_writes.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    
    // Check if this is a cross-goroutine write
    bool is_cross_goroutine = 
        (obj_header->owner_goroutine_id != writing_goroutine_id) ||
        obj_header->is_shared();
    
    if (is_cross_goroutine) {
        // Synchronized write path
        perform_synchronized_write(obj, field, new_value, writing_goroutine_id, 
                                 obj_header, value_header);
    } else {
        // Fast path for same-goroutine writes
        perform_fast_write(obj, field, new_value, obj_header, value_header);
    }
}

void GoroutineWriteBarrier::perform_fast_write(
    void* obj,
    void* field,
    void* new_value,
    GoroutineObjectHeader* obj_header,
    GoroutineObjectHeader* value_header
) {
    // Do the write
    *reinterpret_cast<void**>(field) = new_value;
    
    // Check if we need generational barrier
    if (needs_generational_barrier(obj_header, value_header)) {
        mark_card_dirty(obj);
        g_barrier_hits.fetch_add(1, std::memory_order_relaxed);
    }
    
    g_fast_writes.fetch_add(1, std::memory_order_relaxed);
}

void GoroutineWriteBarrier::perform_synchronized_write(
    void* obj,
    void* field,
    void* new_value,
    uint32_t writing_goroutine_id,
    GoroutineObjectHeader* obj_header,
    GoroutineObjectHeader* value_header
) {
    // Mark object as accessed by this goroutine
    obj_header->add_accessing_goroutine(writing_goroutine_id);
    
    // Ensure memory ordering for cross-goroutine access
    std::atomic_thread_fence(std::memory_order_release);
    
    // Perform atomic write
    std::atomic<void*>* atomic_field = reinterpret_cast<std::atomic<void*>*>(field);
    atomic_field->store(new_value, std::memory_order_release);
    
    // Generational barrier (if applicable)
    if (needs_generational_barrier(obj_header, value_header)) {
        mark_card_dirty(obj);
        g_barrier_hits.fetch_add(1, std::memory_order_relaxed);
    }
    
    g_sync_writes.fetch_add(1, std::memory_order_relaxed);
    
    GC_DEBUG_LOG("[BARRIER] Synchronized write by goroutine " << writing_goroutine_id 
              << " to object owned by " << obj_header->owner_goroutine_id);
}

void* GoroutineWriteBarrier::read_ref_with_sync(
    void* obj,
    void* field,
    uint32_t reading_goroutine_id
) {
    if (!obj || !field) {
        return *reinterpret_cast<void**>(field);
    }
    
    GoroutineObjectHeader* obj_header = get_goroutine_header(obj);
    if (!obj_header) {
        return *reinterpret_cast<void**>(field);
    }
    
    // Check if this is a cross-goroutine read
    bool is_cross_goroutine = 
        (obj_header->owner_goroutine_id != reading_goroutine_id) ||
        obj_header->is_shared();
    
    if (is_cross_goroutine) {
        // Mark object as accessed by this goroutine
        obj_header->add_accessing_goroutine(reading_goroutine_id);
        
        // Synchronized read with acquire semantics
        std::atomic<void*>* atomic_field = reinterpret_cast<std::atomic<void*>*>(field);
        void* result = atomic_field->load(std::memory_order_acquire);
        
        GC_DEBUG_LOG("[BARRIER] Synchronized read by goroutine " << reading_goroutine_id 
                  << " from object owned by " << obj_header->owner_goroutine_id);
        
        return result;
    } else {
        // Fast path for same-goroutine reads
        return *reinterpret_cast<void**>(field);
    }
}

bool GoroutineWriteBarrier::needs_generational_barrier(
    GoroutineObjectHeader* obj_header,
    GoroutineObjectHeader* value_header
) {
    if (!obj_header || !value_header) return false;
    
    // Check if this is an old-to-young reference
    bool obj_is_old = (obj_header->flags & ObjectHeader::IN_OLD_GEN) != 0;
    bool value_is_young = (value_header->flags & ObjectHeader::IN_OLD_GEN) == 0;
    
    return obj_is_old && value_is_young;
}

// ============================================================================
// BULK WRITE OPERATIONS
// ============================================================================

void GoroutineWriteBarrier::bulk_write_refs(
    void* obj,
    void** fields,
    void** new_values,
    size_t count,
    uint32_t writing_goroutine_id
) {
    if (!obj || !fields || !new_values || count == 0) return;
    
    GoroutineObjectHeader* obj_header = get_goroutine_header(obj);
    if (!obj_header) {
        // Simple bulk write
        for (size_t i = 0; i < count; ++i) {
            *fields[i] = new_values[i];
        }
        return;
    }
    
    bool is_cross_goroutine = 
        (obj_header->owner_goroutine_id != writing_goroutine_id) ||
        obj_header->is_shared();
    
    if (is_cross_goroutine) {
        // Mark object as accessed by this goroutine once
        obj_header->add_accessing_goroutine(writing_goroutine_id);
        
        // Memory fence for ordering
        std::atomic_thread_fence(std::memory_order_release);
        
        // Perform atomic writes
        for (size_t i = 0; i < count; ++i) {
            std::atomic<void*>* atomic_field = reinterpret_cast<std::atomic<void*>*>(fields[i]);
            atomic_field->store(new_values[i], std::memory_order_release);
        }
        
        // Check generational barrier for all writes - OPTIMIZED
        bool needs_barrier = false;
        for (size_t i = 0; i < count; ++i) {
            GoroutineObjectHeader* value_header = get_goroutine_header(new_values[i]);
            if (needs_generational_barrier(obj_header, value_header)) {
                needs_barrier = true;
                break; // Early exit optimization - already present
            }
        }
        
        if (needs_barrier) {
            mark_card_dirty(obj);
            g_barrier_hits.fetch_add(1, std::memory_order_relaxed);
        }
        
        g_sync_writes.fetch_add(count, std::memory_order_relaxed);
        
        std::cout << "[BARRIER] Bulk synchronized write of " << count << " fields by goroutine " 
                  << writing_goroutine_id << "\n";
    } else {
        // Fast bulk write
        for (size_t i = 0; i < count; ++i) {
            *fields[i] = new_values[i];
        }
        
        // Check generational barrier - OPTIMIZED WITH EARLY EXIT
        bool needs_barrier = false;
        for (size_t i = 0; i < count; ++i) {
            GoroutineObjectHeader* value_header = get_goroutine_header(new_values[i]);
            if (needs_generational_barrier(obj_header, value_header)) {
                needs_barrier = true;
                break; // Early exit optimization added
            }
        }
        
        if (needs_barrier) {
            mark_card_dirty(obj);
            g_barrier_hits.fetch_add(1, std::memory_order_relaxed);
        }
        
        g_fast_writes.fetch_add(count, std::memory_order_relaxed);
    }
}

// ============================================================================
// ARRAY WRITE BARRIERS
// ============================================================================

void GoroutineWriteBarrier::array_write_ref(
    void* array,
    size_t index,
    void* new_value,
    uint32_t writing_goroutine_id
) {
    if (!array) return;
    
    GoroutineObjectHeader* array_header = get_goroutine_header(array);
    if (!array_header || !(array_header->flags & ObjectHeader::IS_ARRAY)) {
        // Not an array, treat as regular write
        void** field = static_cast<void**>(array) + index;
        write_ref_with_sync(array, field, new_value, writing_goroutine_id);
        return;
    }
    
    // Array-specific write
    void** element = static_cast<void**>(array) + index;
    write_ref_with_sync(array, element, new_value, writing_goroutine_id);
}

void GoroutineWriteBarrier::array_bulk_write_refs(
    void* array,
    size_t start_index,
    void** new_values,
    size_t count,
    uint32_t writing_goroutine_id
) {
    if (!array || !new_values || count == 0) return;
    
    GoroutineObjectHeader* array_header = get_goroutine_header(array);
    if (!array_header || !(array_header->flags & ObjectHeader::IS_ARRAY)) {
        return;
    }
    
    // Prepare field pointers for bulk write
    void** fields = static_cast<void**>(array) + start_index;
    bulk_write_refs(array, fields, new_values, count, writing_goroutine_id);
}

// ============================================================================
// CARD TABLE SCANNING
// ============================================================================

void GoroutineWriteBarrier::scan_dirty_cards(std::function<void(void*, size_t)> callback) {
    if (!g_card_table_initialized.load()) return;
    
    size_t dirty_cards = 0;
    
    for (size_t i = 0; i < g_card_table_size; ++i) {
        if (g_card_table.get()[i]) {
            dirty_cards++;
            
            // Calculate address range for this card
            void* card_start = reinterpret_cast<void*>(i * GCConfig::CARD_SIZE);
            size_t card_size = GCConfig::CARD_SIZE;
            
            // Adjust for last card
            if (i == g_card_table_size - 1) {
                size_t total_heap_size = GCConfig::YOUNG_GEN_SIZE + GCConfig::OLD_GEN_SIZE;
                card_size = total_heap_size - (i * GCConfig::CARD_SIZE);
            }
            
            callback(card_start, card_size);
        }
    }
    
    std::cout << "[BARRIER] Scanned " << dirty_cards << " dirty cards out of " 
              << g_card_table_size << " total cards\n";
}

void GoroutineWriteBarrier::clear_cards() {
    clear_card_table();
}

std::vector<void*> GoroutineWriteBarrier::get_dirty_card_addresses() {
    std::vector<void*> addresses;
    
    if (!g_card_table_initialized.load()) return addresses;
    
    for (size_t i = 0; i < g_card_table_size; ++i) {
        if (g_card_table.get()[i]) {
            void* card_start = reinterpret_cast<void*>(i * GCConfig::CARD_SIZE);
            addresses.push_back(card_start);
        }
    }
    
    return addresses;
}

// ============================================================================
// WRITE BARRIER STATISTICS
// ============================================================================

GoroutineWriteBarrier::Statistics GoroutineWriteBarrier::get_statistics() {
    Statistics stats;
    stats.fast_writes = g_fast_writes.load();
    stats.sync_writes = g_sync_writes.load();
    stats.barrier_hits = g_barrier_hits.load();
    stats.card_marks = g_card_marks.load();
    stats.total_writes = stats.fast_writes + stats.sync_writes;
    
    if (stats.total_writes > 0) {
        stats.sync_write_percentage = (double)stats.sync_writes / stats.total_writes * 100.0;
        stats.barrier_hit_percentage = (double)stats.barrier_hits / stats.total_writes * 100.0;
    }
    
    return stats;
}

void GoroutineWriteBarrier::print_statistics() {
    Statistics stats = get_statistics();
    
    std::cout << "\n=== WRITE BARRIER STATISTICS ===\n";
    std::cout << "Total writes: " << stats.total_writes << "\n";
    std::cout << "Fast writes: " << stats.fast_writes << " (" 
              << (100.0 - stats.sync_write_percentage) << "%)\n";
    std::cout << "Sync writes: " << stats.sync_writes << " (" 
              << stats.sync_write_percentage << "%)\n";
    std::cout << "Barrier hits: " << stats.barrier_hits << " (" 
              << stats.barrier_hit_percentage << "%)\n";
    std::cout << "Card marks: " << stats.card_marks << "\n";
    std::cout << "Card table size: " << g_card_table_size << " cards\n";
    std::cout << "================================\n\n";
}

void GoroutineWriteBarrier::reset_statistics() {
    g_fast_writes.store(0);
    g_sync_writes.store(0);
    g_barrier_hits.store(0);
    g_card_marks.store(0);
    std::cout << "[BARRIER] Reset statistics\n";
}

// ============================================================================
// DEBUGGING AND TESTING
// ============================================================================

bool GoroutineWriteBarrier::is_object_in_card(void* obj, size_t card_index) {
    if (!obj || card_index >= g_card_table_size) return false;
    
    uintptr_t obj_addr = reinterpret_cast<uintptr_t>(obj);
    uintptr_t card_start = card_index * GCConfig::CARD_SIZE;
    uintptr_t card_end = card_start + GCConfig::CARD_SIZE;
    
    return (obj_addr >= card_start && obj_addr < card_end);
}

size_t GoroutineWriteBarrier::get_object_card_index(void* obj) {
    if (!obj) return SIZE_MAX;
    
    uintptr_t obj_addr = reinterpret_cast<uintptr_t>(obj);
    return obj_addr / GCConfig::CARD_SIZE;
}

void GoroutineWriteBarrier::force_mark_card(void* obj) {
    if (obj) {
        mark_card_dirty(obj);
    }
}

void GoroutineWriteBarrier::test_write_barriers() {
    std::cout << "[BARRIER] Testing write barriers...\n";
    
    // Test data
    int dummy_data = 42;
    void* test_obj = &dummy_data;
    void* test_field = &test_obj;
    void* test_value = &dummy_data;
    
    // Test fast write
    perform_fast_write(test_obj, test_field, test_value, nullptr, nullptr);
    
    // Test synchronized write
    perform_synchronized_write(test_obj, test_field, test_value, 1, nullptr, nullptr);
    
    // Test bulk write
    void* fields[3] = {test_field, test_field, test_field};
    void* values[3] = {test_value, test_value, test_value};
    bulk_write_refs(test_obj, fields, values, 3, 1);
    
    // Test array write
    array_write_ref(test_obj, 0, test_value, 1);
    
    std::cout << "[BARRIER] Write barrier tests completed\n";
    print_statistics();
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

GoroutineObjectHeader* GoroutineWriteBarrier::get_goroutine_header(void* obj) {
    if (!obj) return nullptr;
    return reinterpret_cast<GoroutineObjectHeader*>(
        static_cast<uint8_t*>(obj) - sizeof(GoroutineObjectHeader)
    );
}

bool GoroutineWriteBarrier::is_same_goroutine_access(void* obj, uint32_t goroutine_id) {
    GoroutineObjectHeader* header = get_goroutine_header(obj);
    if (!header) return false;
    
    return !header->is_shared() && (header->owner_goroutine_id == goroutine_id);
}

bool GoroutineWriteBarrier::requires_synchronization(void* obj, uint32_t goroutine_id) {
    GoroutineObjectHeader* header = get_goroutine_header(obj);
    if (!header) return true; // Conservative
    
    return header->is_shared() || (header->owner_goroutine_id != goroutine_id);
}

} // namespace gots