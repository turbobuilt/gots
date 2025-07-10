#pragma once

#include "gc_memory_manager.h"
#include <atomic>
#include <thread>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <condition_variable>

// Performance optimization: Conditional debug output
#ifdef DEBUG
    #define GC_DEBUG_LOG(msg) std::cout << msg << std::endl
#else
    #define GC_DEBUG_LOG(msg) do {} while(0)
#endif

namespace gots {

// ============================================================================
// GOROUTINE-AWARE MEMORY ALLOCATION STRATEGY
// ============================================================================

enum class ObjectOwnership {
    STACK_LOCAL,        // Stack allocated, single goroutine
    GOROUTINE_PRIVATE,  // Heap allocated, single goroutine access
    GOROUTINE_SHARED,   // Heap allocated, multiple goroutine access
    GLOBAL_SHARED       // Globally accessible objects
};

// Enhanced object header with goroutine ownership tracking
struct GoroutineObjectHeader : public ObjectHeader {
    union {
        struct {
            uint32_t owner_goroutine_id : 16;  // Which goroutine owns this
            uint32_t ownership_type : 2;       // ObjectOwnership enum
            uint32_t ref_goroutine_count : 6;  // How many goroutines reference this
            uint32_t needs_sync : 1;           // Requires synchronization
            uint32_t reserved : 7;
        };
        uint32_t goroutine_flags;
    };
    
    mutable std::atomic<uint64_t> accessing_goroutines{0}; // Support up to 64 goroutines
    
    bool is_shared() const { 
        return ownership_type == static_cast<uint32_t>(ObjectOwnership::GOROUTINE_SHARED) ||
               ownership_type == static_cast<uint32_t>(ObjectOwnership::GLOBAL_SHARED);
    }
    
    bool is_goroutine_private() const {
        return ownership_type == static_cast<uint32_t>(ObjectOwnership::GOROUTINE_PRIVATE);
    }
    
    bool is_stack_local() const {
        return ownership_type == static_cast<uint32_t>(ObjectOwnership::STACK_LOCAL);
    }
    
    void add_accessing_goroutine(uint32_t goroutine_id) {
        if (goroutine_id < 64) {  // Fast path for common case
            uint64_t mask = 1ull << goroutine_id;
            accessing_goroutines.fetch_or(mask, std::memory_order_relaxed);
        }
        // For >64 goroutines, fall back to shared object handling
    }
    
    bool is_accessed_by_goroutine(uint32_t goroutine_id) const {
        if (goroutine_id < 64) {
            uint64_t mask = 1ull << goroutine_id;
            return accessing_goroutines.load(std::memory_order_relaxed) & mask;
        }
        return true; // Conservative for >64 goroutines
    }
};

// ============================================================================
// GOROUTINE-AWARE ESCAPE ANALYSIS
// ============================================================================

class GoroutineEscapeAnalyzer {
public:
    struct GoroutineAnalysisResult {
        ObjectOwnership ownership = ObjectOwnership::STACK_LOCAL;
        std::vector<uint32_t> accessing_goroutines;
        bool needs_synchronization = false;
        
        // Escape reasons
        bool captured_by_goroutine = false;
        bool accessed_across_goroutines = false;
        bool returned_from_goroutine = false;
        bool stored_in_shared_object = false;
        bool passed_to_channel = false;
    };
    
    // Track goroutine creation and variable capture
    static void register_goroutine_spawn(
        uint32_t parent_goroutine_id,
        uint32_t child_goroutine_id,
        const std::vector<size_t>& captured_vars
    );
    
    // Track cross-goroutine variable access
    static void register_cross_goroutine_access(
        uint32_t accessing_goroutine_id,
        size_t var_id,
        size_t allocation_site,
        bool is_write
    );
    
    // Enhanced allocation analysis
    static GoroutineAnalysisResult analyze_goroutine_allocation(
        const void* jit_context,
        size_t allocation_site,
        size_t allocation_size,
        uint32_t type_id,
        uint32_t current_goroutine_id
    );
    
    // Check if variable is captured by any goroutine
    static bool is_captured_by_goroutine(size_t var_id);
    
    // Get all goroutines that access a variable
    static std::vector<uint32_t> get_accessing_goroutines(size_t var_id);
    
private:
    static thread_local struct {
        uint32_t current_goroutine_id = 0;
        std::unordered_map<size_t, GoroutineAnalysisResult> allocation_results;
        std::unordered_map<size_t, std::vector<uint32_t>> var_goroutine_access;
        std::unordered_map<uint32_t, std::vector<size_t>> goroutine_captured_vars;
    } g_goroutine_data;
};

// ============================================================================
// DUAL-HEAP ALLOCATION STRATEGY
// ============================================================================

class GoroutineAwareHeap {
private:
    // Private heap per goroutine (fast allocation)
    struct GoroutineHeap {
        uint32_t goroutine_id;
        uint8_t* tlab_start;
        std::atomic<uint8_t*> tlab_current;
        uint8_t* tlab_end;
        std::atomic<size_t> allocated_bytes{0};
        
        // Small private heap for goroutine-local objects
        uint8_t* private_heap_start;
        std::atomic<uint8_t*> private_heap_current;
        uint8_t* private_heap_end;
        
        GoroutineHeap(uint32_t id) : goroutine_id(id) {
            tlab_current.store(nullptr);
            private_heap_current.store(nullptr);
        }
    };
    
    // Shared heap for cross-goroutine objects
    struct SharedHeap {
        uint8_t* start;
        std::atomic<uint8_t*> current;
        uint8_t* end;
        std::mutex allocation_mutex;
        
        // Separate regions for different sharing levels
        uint8_t* goroutine_shared_start;  // Objects shared between specific goroutines
        uint8_t* global_shared_start;     // Objects accessible to all goroutines
    };
    
    // Per-goroutine heaps
    std::unordered_map<uint32_t, std::unique_ptr<GoroutineHeap>> goroutine_heaps_;
    std::mutex goroutine_heaps_mutex_;
    
    // Shared heap
    SharedHeap shared_heap_;
    
    // Current goroutine tracking
    static thread_local uint32_t current_goroutine_id_;
    
public:
    void initialize();
    void shutdown();
    
    // Register new goroutine
    void register_goroutine(uint32_t goroutine_id);
    void unregister_goroutine(uint32_t goroutine_id);
    
    // Allocation based on ownership
    static inline void* allocate_by_ownership(
        size_t size,
        uint32_t type_id,
        ObjectOwnership ownership,
        uint32_t goroutine_id = 0
    ) {
        switch (ownership) {
            case ObjectOwnership::STACK_LOCAL:
                return allocate_stack_local(size, type_id);
                
            case ObjectOwnership::GOROUTINE_PRIVATE:
                return allocate_goroutine_private(size, type_id, goroutine_id);
                
            case ObjectOwnership::GOROUTINE_SHARED:
                return allocate_goroutine_shared(size, type_id);
                
            case ObjectOwnership::GLOBAL_SHARED:
                return allocate_global_shared(size, type_id);
        }
        return nullptr;
    }
    
    // Fast path: stack allocation (unchanged)
    static inline void* allocate_stack_local(size_t size, uint32_t type_id) {
        // JIT emits stack allocation directly
        return reinterpret_cast<void*>(0xDEADBEEF); // Marker
    }
    
    // Fast path: goroutine-private TLAB allocation
    static inline void* allocate_goroutine_private(size_t size, uint32_t type_id, uint32_t goroutine_id) {
        GoroutineHeap* heap = get_goroutine_heap(goroutine_id);
        if (!heap) return allocate_slow(size, type_id, ObjectOwnership::GOROUTINE_PRIVATE);
        
        size_t total_size = size + sizeof(GoroutineObjectHeader);
        total_size = align_size(total_size);
        
        // Atomic TLAB allocation with compare-and-swap
        uint8_t* current = heap->tlab_current.load(std::memory_order_relaxed);
        uint8_t* new_current;
        
        do {
            new_current = current + total_size;
            if (new_current > heap->tlab_end) {
                return allocate_slow(size, type_id, ObjectOwnership::GOROUTINE_PRIVATE);
            }
        } while (!heap->tlab_current.compare_exchange_weak(
            current, new_current, std::memory_order_relaxed));
        
        // Initialize header
        GoroutineObjectHeader* header = reinterpret_cast<GoroutineObjectHeader*>(current);
        header->size = size;
        header->type_id = type_id;
        header->flags = 0;
        header->owner_goroutine_id = goroutine_id;
        header->ownership_type = static_cast<uint32_t>(ObjectOwnership::GOROUTINE_PRIVATE);
        header->ref_goroutine_count = 1;
        header->needs_sync = 0;
        // Use safe goroutine ID for bit mask - already fixed in header
        
        return reinterpret_cast<uint8_t*>(header) + sizeof(GoroutineObjectHeader);
    }
    
    // Slow path: shared heap allocation
    static void* allocate_goroutine_shared(size_t size, uint32_t type_id);
    static void* allocate_global_shared(size_t size, uint32_t type_id);
    
private:
    static GoroutineHeap* get_goroutine_heap(uint32_t goroutine_id);
    static void* allocate_slow(size_t size, uint32_t type_id, ObjectOwnership ownership);
    static constexpr size_t align_size(size_t size) {
        return (size + 15) & ~15;
    }
};

// ============================================================================
// GOROUTINE-AWARE WRITE BARRIERS
// ============================================================================

class GoroutineWriteBarrier {
public:
    // Enhanced write barrier with goroutine synchronization
    static inline void write_ref_with_sync(
        void* obj,
        void* field,
        void* new_value,
        uint32_t writing_goroutine_id
    ) {
        // Get object headers
        GoroutineObjectHeader* obj_header = get_goroutine_header(obj);
        GoroutineObjectHeader* value_header = get_goroutine_header(new_value);
        
        if (!obj_header) {
            // Fall back to simple write for objects without headers
            *reinterpret_cast<void**>(field) = new_value;
            return;
        }
        
        // value_header can be null for primitives or stack objects
        
        // Check if this is a cross-goroutine write
        bool is_cross_goroutine = 
            obj_header->owner_goroutine_id != writing_goroutine_id ||
            obj_header->is_shared();
        
        if (is_cross_goroutine) {
            // Mark object as accessed by this goroutine
            obj_header->add_accessing_goroutine(writing_goroutine_id);
            
            // Ensure memory ordering for cross-goroutine access
            std::atomic_thread_fence(std::memory_order_release);
            
            // Perform the write with synchronization
            std::atomic<void*>* atomic_field = reinterpret_cast<std::atomic<void*>*>(field);
            atomic_field->store(new_value, std::memory_order_release);
            
            // Generational barrier (if applicable)
            if (needs_generational_barrier(obj_header, value_header)) {
                mark_card_dirty(obj);
            }
        } else {
            // Fast path: same goroutine, no sync needed
            *reinterpret_cast<void**>(field) = new_value;
            
            // Still need generational barrier
            if (needs_generational_barrier(obj_header, value_header)) {
                mark_card_dirty(obj);
            }
        }
    }
    
    // Read barrier for cross-goroutine reads
    static inline void* read_ref_with_sync(
        void* obj,
        void* field,
        uint32_t reading_goroutine_id
    ) {
        GoroutineObjectHeader* obj_header = get_goroutine_header(obj);
        
        if (!obj_header) {
            return *reinterpret_cast<void**>(field);
        }
        
        // Check if this is a cross-goroutine read
        bool is_cross_goroutine = 
            obj_header->owner_goroutine_id != reading_goroutine_id ||
            obj_header->is_shared();
        
        if (is_cross_goroutine) {
            // Mark object as accessed by this goroutine
            obj_header->add_accessing_goroutine(reading_goroutine_id);
            
            // Synchronized read
            std::atomic<void*>* atomic_field = reinterpret_cast<std::atomic<void*>*>(field);
            return atomic_field->load(std::memory_order_acquire);
        } else {
            // Fast path: same goroutine
            return *reinterpret_cast<void**>(field);
        }
    }
    
private:
    static GoroutineObjectHeader* get_goroutine_header(void* obj);
    static bool needs_generational_barrier(GoroutineObjectHeader* obj, GoroutineObjectHeader* value);
    static void mark_card_dirty(void* obj);
};

// ============================================================================
// COORDINATED GARBAGE COLLECTION
// ============================================================================

class GoroutineCoordinatedGC {
private:
    // Goroutine registry
    struct GoroutineInfo {
        uint32_t id;
        std::thread::id thread_id;
        std::atomic<bool> at_safepoint{false};
        std::atomic<bool> gc_requested{false};
        void** stack_roots;
        size_t stack_root_count;
        
        // Goroutine-specific allocation statistics
        std::atomic<size_t> private_allocated{0};
        std::atomic<size_t> shared_allocated{0};
    };
    
    std::unordered_map<uint32_t, std::unique_ptr<GoroutineInfo>> goroutines_;
    std::mutex goroutines_mutex_;
    
    // Global GC coordination
    std::atomic<bool> gc_in_progress_{false};
    std::atomic<uint32_t> goroutines_at_safepoint_{0};
    std::atomic<uint32_t> total_goroutines_{0};
    std::condition_variable safepoint_cv_;
    std::mutex safepoint_mutex_;
    
    // Separate collection strategies
    std::thread private_gc_thread_;
    std::thread shared_gc_thread_;
    
public:
    void initialize();
    void shutdown();
    
    // Goroutine lifecycle
    void register_goroutine(uint32_t goroutine_id);
    void unregister_goroutine(uint32_t goroutine_id);
    
    // Enhanced safepoint coordination
    static inline void safepoint_poll(uint32_t goroutine_id) {
        GoroutineInfo* info = get_goroutine_info(goroutine_id);
        if (info && info->gc_requested.load(std::memory_order_acquire)) {
            safepoint_slow(goroutine_id);
        }
    }
    
    // Dual GC strategies
    void collect_goroutine_private();  // Fast, per-goroutine collection
    void collect_goroutine_shared();   // Slower, coordinated collection
    
    // Root scanning across all goroutines
    void scan_all_goroutine_roots();
    
    // Statistics
    struct GoroutineGCStats {
        size_t total_goroutines;
        size_t private_collections;
        size_t shared_collections;
        size_t cross_goroutine_references;
        size_t sync_operations;
        size_t avg_safepoint_time_us;
    };
    
    GoroutineGCStats get_stats() const;
    
private:
    static void safepoint_slow(uint32_t goroutine_id);
    static GoroutineInfo* get_goroutine_info(uint32_t goroutine_id);
    void wait_for_all_safepoints();
    void release_all_safepoints();
    
    void private_gc_thread_loop();
    void shared_gc_thread_loop();
};

// ============================================================================
// JIT INTEGRATION FOR GOROUTINE-AWARE ALLOCATION
// ============================================================================

extern "C" {
    // Enhanced allocation functions
    void* __gc_alloc_by_ownership(size_t size, uint32_t type_id, uint32_t ownership, uint32_t goroutine_id);
    
    // Goroutine-aware write barriers
    void __gc_write_barrier_sync(void* obj, void* field, void* new_value, uint32_t goroutine_id);
    void* __gc_read_barrier_sync(void* obj, void* field, uint32_t goroutine_id);
    
    // Goroutine lifecycle
    void __gc_register_goroutine(uint32_t goroutine_id);
    void __gc_unregister_goroutine(uint32_t goroutine_id);
    
    // Enhanced safepoint
    void __gc_safepoint_goroutine(uint32_t goroutine_id);
    
    // Goroutine variable tracking
    void __gc_goroutine_capture_vars(uint32_t goroutine_id, void** vars, size_t count);
    void __gc_cross_goroutine_access(uint32_t goroutine_id, void* obj, uint32_t is_write);
}

} // namespace gots