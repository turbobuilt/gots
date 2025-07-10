#include "goroutine_aware_gc.h"
#include <sys/mman.h>
#include <atomic>
#include <algorithm>
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

// Performance optimization macros
#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

// Enable allocation statistics in debug mode only
#ifdef DEBUG
#define DEBUG_ALLOCATION_STATS
#endif

namespace gots {

// ============================================================================
// GLOBAL HEAP MANAGER STATE
// ============================================================================

static GoroutineAwareHeap* g_heap_instance = nullptr;
static std::mutex g_heap_mutex;

// Thread-local storage for current goroutine
thread_local uint32_t GoroutineAwareHeap::current_goroutine_id_ = 0;

// ============================================================================
// GOROUTINE HEAP IMPLEMENTATION
// ============================================================================

struct GoroutineHeapImpl {
    uint32_t goroutine_id;
    
    // TLAB (Thread Local Allocation Buffer)
    uint8_t* tlab_start;
    std::atomic<uint8_t*> tlab_current;
    uint8_t* tlab_end;
    std::atomic<size_t> tlab_allocated_bytes{0};
    
    // Private heap for larger objects
    uint8_t* private_heap_start;
    std::atomic<uint8_t*> private_heap_current;
    uint8_t* private_heap_end;
    std::atomic<size_t> private_heap_allocated_bytes{0};
    
    // Statistics
    std::atomic<size_t> total_allocations{0};
    std::atomic<size_t> fast_allocations{0};
    std::atomic<size_t> slow_allocations{0};
    
    // Allocation tracking for GC with limits
    std::vector<void*> allocated_objects;
    std::mutex objects_mutex;
    static constexpr size_t MAX_TRACKED_OBJECTS = 100000; // Limit to prevent unbounded growth
    
    GoroutineHeapImpl(uint32_t id) : goroutine_id(id) {
        // Allocate TLAB
        tlab_start = static_cast<uint8_t*>(
            mmap(nullptr, GCConfig::TLAB_SIZE, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        );
        
        if (tlab_start == MAP_FAILED) {
            throw std::runtime_error("Failed to allocate TLAB for goroutine " + std::to_string(id));
        }
        
        tlab_current.store(tlab_start);
        tlab_end = tlab_start + GCConfig::TLAB_SIZE;
        
        // Allocate private heap
        size_t private_heap_size = GCConfig::YOUNG_GEN_SIZE / 4; // 8MB per goroutine
        private_heap_start = static_cast<uint8_t*>(
            mmap(nullptr, private_heap_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        );
        
        if (private_heap_start == MAP_FAILED) {
            munmap(tlab_start, GCConfig::TLAB_SIZE);
            throw std::runtime_error("Failed to allocate private heap for goroutine " + std::to_string(id));
        }
        
        private_heap_current.store(private_heap_start);
        private_heap_end = private_heap_start + private_heap_size;
        
        std::cout << "[HEAP] Initialized goroutine " << id << " heap: "
                  << "TLAB=" << GCConfig::TLAB_SIZE << " bytes, "
                  << "Private=" << private_heap_size << " bytes\n";
    }
    
    ~GoroutineHeapImpl() {
        if (tlab_start != MAP_FAILED) {
            munmap(tlab_start, GCConfig::TLAB_SIZE);
        }
        if (private_heap_start != MAP_FAILED) {
            size_t private_heap_size = GCConfig::YOUNG_GEN_SIZE / 4;
            munmap(private_heap_start, private_heap_size);
        }
    }
    
    void* allocate_fast(size_t size) {
        // Pre-compute aligned size for better branch prediction
        size_t total_size = align_size(size + sizeof(GoroutineObjectHeader));
        
        // Ultra-fast path: single-instruction bounds check + allocation
        uint8_t* current = tlab_current.load(std::memory_order_relaxed);
        uint8_t* new_current = current + total_size;
        
        // Optimize for the common case - check bounds first (better branch prediction)
        if (likely(new_current <= tlab_end)) {
            // Fast atomic update with relaxed ordering (no memory barriers needed)
            if (likely(tlab_current.compare_exchange_weak(current, new_current, 
                                                         std::memory_order_relaxed))) {
                // Batch counter updates for better cache performance
                // These counters are mainly for debugging - optimize out in release builds
                #ifdef DEBUG_ALLOCATION_STATS
                tlab_allocated_bytes.fetch_add(total_size, std::memory_order_relaxed);
                total_allocations.fetch_add(1, std::memory_order_relaxed);
                fast_allocations.fetch_add(1, std::memory_order_relaxed);
                #endif
                
                return current;
            }
            // Retry once on CAS failure (handles occasional contention)
            current = tlab_current.load(std::memory_order_relaxed);
            new_current = current + total_size;
            if (likely(new_current <= tlab_end && 
                      tlab_current.compare_exchange_weak(current, new_current, 
                                                        std::memory_order_relaxed))) {
                #ifdef DEBUG_ALLOCATION_STATS
                tlab_allocated_bytes.fetch_add(total_size, std::memory_order_relaxed);
                total_allocations.fetch_add(1, std::memory_order_relaxed);
                fast_allocations.fetch_add(1, std::memory_order_relaxed);
                #endif
                return current;
            }
        }
        
        return nullptr; // TLAB full or contention, need slow path
    }
    
    void* allocate_private(size_t size) {
        size_t total_size = size + sizeof(GoroutineObjectHeader);
        total_size = align_size(total_size);
        
        uint8_t* current = private_heap_current.load(std::memory_order_relaxed);
        uint8_t* new_current = current + total_size;
        
        if (new_current <= private_heap_end) {
            // Try to atomically update current pointer
            if (private_heap_current.compare_exchange_weak(current, new_current, std::memory_order_relaxed)) {
                private_heap_allocated_bytes.fetch_add(total_size, std::memory_order_relaxed);
                total_allocations.fetch_add(1, std::memory_order_relaxed);
                slow_allocations.fetch_add(1, std::memory_order_relaxed);
                return current;
            }
        }
        
        return nullptr; // Private heap full, need GC
    }
    
    void add_allocated_object(void* obj) {
        std::lock_guard<std::mutex> lock(objects_mutex);
        
        // Prevent unbounded growth by limiting tracked objects
        if (allocated_objects.size() >= MAX_TRACKED_OBJECTS) {
            // Remove oldest objects (FIFO cleanup)
            size_t objects_to_remove = allocated_objects.size() / 10; // Remove 10%
            allocated_objects.erase(allocated_objects.begin(), 
                                  allocated_objects.begin() + objects_to_remove);
            
            std::cout << "[HEAP] Object tracking limit reached for goroutine " << goroutine_id 
                      << ", removed " << objects_to_remove << " oldest tracked objects\n";
        }
        
        allocated_objects.push_back(obj);
    }
    
    void remove_allocated_object(void* obj) {
        std::lock_guard<std::mutex> lock(objects_mutex);
        allocated_objects.erase(
            std::remove(allocated_objects.begin(), allocated_objects.end(), obj),
            allocated_objects.end()
        );
    }
    
    std::vector<void*> get_allocated_objects() {
        std::lock_guard<std::mutex> lock(objects_mutex);
        return allocated_objects;
    }
    
    void reset_tlab() {
        tlab_current.store(tlab_start);
        tlab_allocated_bytes.store(0);
        
        // Clear object tracking for TLAB objects
        {
            std::lock_guard<std::mutex> lock(objects_mutex);
            // Remove objects that were in TLAB range
            allocated_objects.erase(
                std::remove_if(allocated_objects.begin(), allocated_objects.end(),
                    [this](void* obj) {
                        uint8_t* obj_addr = static_cast<uint8_t*>(obj);
                        return obj_addr >= tlab_start && obj_addr < tlab_end;
                    }),
                allocated_objects.end()
            );
        }
        
        std::cout << "[HEAP] Reset TLAB for goroutine " << goroutine_id << "\n";
    }
    
    void reset_private_heap() {
        private_heap_current.store(private_heap_start);
        private_heap_allocated_bytes.store(0);
        
        // Clear object tracking for private heap objects
        {
            std::lock_guard<std::mutex> lock(objects_mutex);
            // Remove objects that were in private heap range
            allocated_objects.erase(
                std::remove_if(allocated_objects.begin(), allocated_objects.end(),
                    [this](void* obj) {
                        uint8_t* obj_addr = static_cast<uint8_t*>(obj);
                        return obj_addr >= private_heap_start && obj_addr < private_heap_end;
                    }),
                allocated_objects.end()
            );
        }
        
        std::cout << "[HEAP] Reset private heap for goroutine " << goroutine_id << "\n";
    }
    
    size_t get_total_allocated() const {
        return tlab_allocated_bytes.load() + private_heap_allocated_bytes.load();
    }
    
    void print_statistics() const {
        std::cout << "[HEAP] Goroutine " << goroutine_id << " statistics:\n";
        std::cout << "  Total allocations: " << total_allocations.load() << "\n";
        std::cout << "  Fast allocations: " << fast_allocations.load() << "\n";
        std::cout << "  Slow allocations: " << slow_allocations.load() << "\n";
        std::cout << "  TLAB used: " << tlab_allocated_bytes.load() << " bytes\n";
        std::cout << "  Private heap used: " << private_heap_allocated_bytes.load() << " bytes\n";
        std::cout << "  Total allocated: " << get_total_allocated() << " bytes\n";
    }
    
private:
    static constexpr size_t align_size(size_t size) {
        return (size + GCConfig::OBJECT_ALIGNMENT - 1) & ~(GCConfig::OBJECT_ALIGNMENT - 1);
    }
};

// ============================================================================
// SHARED HEAP IMPLEMENTATION
// ============================================================================

struct SharedHeapImpl {
    // Goroutine-shared heap (for objects shared between specific goroutines)
    uint8_t* shared_heap_start;
    std::atomic<uint8_t*> shared_heap_current;
    uint8_t* shared_heap_end;
    std::mutex shared_heap_mutex;
    
    // Global-shared heap (for objects accessible to all goroutines)
    uint8_t* global_heap_start;
    std::atomic<uint8_t*> global_heap_current;
    uint8_t* global_heap_end;
    std::mutex global_heap_mutex;
    
    // Statistics
    std::atomic<size_t> shared_allocations{0};
    std::atomic<size_t> global_allocations{0};
    std::atomic<size_t> shared_allocated_bytes{0};
    std::atomic<size_t> global_allocated_bytes{0};
    
    // Allocation tracking with limits
    std::vector<void*> shared_objects;
    std::vector<void*> global_objects;
    std::mutex objects_mutex;
    static constexpr size_t MAX_TRACKED_SHARED_OBJECTS = 50000; // Limit shared object tracking
    
    SharedHeapImpl() {
        // Allocate shared heap
        size_t shared_size = GCConfig::OLD_GEN_SIZE / 2; // 256MB
        shared_heap_start = static_cast<uint8_t*>(
            mmap(nullptr, shared_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        );
        
        if (shared_heap_start == MAP_FAILED) {
            throw std::runtime_error("Failed to allocate shared heap");
        }
        
        shared_heap_current.store(shared_heap_start);
        shared_heap_end = shared_heap_start + shared_size;
        
        // Allocate global heap
        size_t global_size = GCConfig::OLD_GEN_SIZE / 2; // 256MB
        global_heap_start = static_cast<uint8_t*>(
            mmap(nullptr, global_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
        );
        
        if (global_heap_start == MAP_FAILED) {
            munmap(shared_heap_start, shared_size);
            throw std::runtime_error("Failed to allocate global heap");
        }
        
        global_heap_current.store(global_heap_start);
        global_heap_end = global_heap_start + global_size;
        
        std::cout << "[HEAP] Initialized shared heaps: "
                  << "Shared=" << shared_size << " bytes, "
                  << "Global=" << global_size << " bytes\n";
    }
    
    ~SharedHeapImpl() {
        if (shared_heap_start != MAP_FAILED) {
            size_t shared_size = GCConfig::OLD_GEN_SIZE / 2;
            munmap(shared_heap_start, shared_size);
        }
        if (global_heap_start != MAP_FAILED) {
            size_t global_size = GCConfig::OLD_GEN_SIZE / 2;
            munmap(global_heap_start, global_size);
        }
    }
    
    void* allocate_shared(size_t size, uint32_t type_id) {
        size_t total_size = size + sizeof(GoroutineObjectHeader);
        total_size = align_size(total_size);
        
        std::lock_guard<std::mutex> lock(shared_heap_mutex);
        
        uint8_t* current = shared_heap_current.load(std::memory_order_relaxed);
        uint8_t* new_current = current + total_size;
        
        if (new_current <= shared_heap_end) {
            shared_heap_current.store(new_current);
            shared_allocations.fetch_add(1, std::memory_order_relaxed);
            shared_allocated_bytes.fetch_add(total_size, std::memory_order_relaxed);
            
            // Initialize header
            GoroutineObjectHeader* header = reinterpret_cast<GoroutineObjectHeader*>(current);
            header->size = size;
            header->type_id = type_id;
            header->flags = 0;
            header->ownership_type = static_cast<uint32_t>(ObjectOwnership::GOROUTINE_SHARED);
            header->needs_sync = 1;
            header->accessing_goroutines.store(0);
            
            void* obj = header->get_object_start();
            
            // Track allocation with limit checking
            {
                std::lock_guard<std::mutex> obj_lock(objects_mutex);
                
                // Prevent unbounded growth
                if (shared_objects.size() >= MAX_TRACKED_SHARED_OBJECTS) {
                    size_t objects_to_remove = shared_objects.size() / 20; // Remove 5%
                    shared_objects.erase(shared_objects.begin(), 
                                       shared_objects.begin() + objects_to_remove);
                    std::cout << "[HEAP] Shared object tracking limit reached, removed " 
                              << objects_to_remove << " oldest tracked objects\n";
                }
                
                shared_objects.push_back(obj);
            }
            
            std::cout << "[HEAP] Allocated " << size << " bytes in shared heap at " << obj << "\n";
            return obj;
        }
        
        return nullptr; // Shared heap full, need GC
    }
    
    void* allocate_global(size_t size, uint32_t type_id) {
        size_t total_size = size + sizeof(GoroutineObjectHeader);
        total_size = align_size(total_size);
        
        std::lock_guard<std::mutex> lock(global_heap_mutex);
        
        uint8_t* current = global_heap_current.load(std::memory_order_relaxed);
        uint8_t* new_current = current + total_size;
        
        if (new_current <= global_heap_end) {
            global_heap_current.store(new_current);
            global_allocations.fetch_add(1, std::memory_order_relaxed);
            global_allocated_bytes.fetch_add(total_size, std::memory_order_relaxed);
            
            // Initialize header
            GoroutineObjectHeader* header = reinterpret_cast<GoroutineObjectHeader*>(current);
            header->size = size;
            header->type_id = type_id;
            header->flags = 0;
            header->ownership_type = static_cast<uint32_t>(ObjectOwnership::GLOBAL_SHARED);
            header->needs_sync = 1;
            header->accessing_goroutines.store(0xFFFFFFFF); // All goroutines
            
            void* obj = header->get_object_start();
            
            // Track allocation with limit checking
            {
                std::lock_guard<std::mutex> obj_lock(objects_mutex);
                
                // Prevent unbounded growth
                if (global_objects.size() >= MAX_TRACKED_SHARED_OBJECTS) {
                    size_t objects_to_remove = global_objects.size() / 20; // Remove 5%
                    global_objects.erase(global_objects.begin(), 
                                       global_objects.begin() + objects_to_remove);
                    std::cout << "[HEAP] Global object tracking limit reached, removed " 
                              << objects_to_remove << " oldest tracked objects\n";
                }
                
                global_objects.push_back(obj);
            }
            
            std::cout << "[HEAP] Allocated " << size << " bytes in global heap at " << obj << "\n";
            return obj;
        }
        
        return nullptr; // Global heap full, need GC
    }
    
    std::vector<void*> get_shared_objects() {
        std::lock_guard<std::mutex> lock(objects_mutex);
        return shared_objects;
    }
    
    std::vector<void*> get_global_objects() {
        std::lock_guard<std::mutex> lock(objects_mutex);
        return global_objects;
    }
    
    void print_statistics() const {
        std::cout << "[HEAP] Shared heap statistics:\n";
        std::cout << "  Shared allocations: " << shared_allocations.load() << "\n";
        std::cout << "  Global allocations: " << global_allocations.load() << "\n";
        std::cout << "  Shared allocated: " << shared_allocated_bytes.load() << " bytes\n";
        std::cout << "  Global allocated: " << global_allocated_bytes.load() << " bytes\n";
        std::cout << "  Total allocated: " << 
                     (shared_allocated_bytes.load() + global_allocated_bytes.load()) << " bytes\n";
    }
    
private:
    static constexpr size_t align_size(size_t size) {
        return (size + GCConfig::OBJECT_ALIGNMENT - 1) & ~(GCConfig::OBJECT_ALIGNMENT - 1);
    }
};

// ============================================================================
// GOROUTINE AWARE HEAP IMPLEMENTATION
// ============================================================================

void GoroutineAwareHeap::initialize() {
    std::lock_guard<std::mutex> lock(g_heap_mutex);
    if (g_heap_instance) return;
    
    g_heap_instance = new GoroutineAwareHeap();
    g_heap_instance->shared_heap_ = std::make_unique<SharedHeapImpl>();
    
    std::cout << "[HEAP] Initialized goroutine-aware heap system\n";
}

void GoroutineAwareHeap::shutdown() {
    std::lock_guard<std::mutex> lock(g_heap_mutex);
    if (!g_heap_instance) return;
    
    // Print final statistics
    g_heap_instance->print_all_statistics();
    
    delete g_heap_instance;
    g_heap_instance = nullptr;
    
    std::cout << "[HEAP] Shutdown goroutine-aware heap system\n";
}

GoroutineAwareHeap& GoroutineAwareHeap::instance() {
    if (!g_heap_instance) {
        initialize();
    }
    return *g_heap_instance;
}

void GoroutineAwareHeap::register_goroutine(uint32_t goroutine_id) {
    std::lock_guard<std::mutex> lock(goroutine_heaps_mutex_);
    
    if (goroutine_heaps_.find(goroutine_id) == goroutine_heaps_.end()) {
        goroutine_heaps_[goroutine_id] = std::make_unique<GoroutineHeapImpl>(goroutine_id);
        std::cout << "[HEAP] Registered goroutine " << goroutine_id << "\n";
    }
}

void GoroutineAwareHeap::unregister_goroutine(uint32_t goroutine_id) {
    std::lock_guard<std::mutex> lock(goroutine_heaps_mutex_);
    
    auto it = goroutine_heaps_.find(goroutine_id);
    if (it != goroutine_heaps_.end()) {
        it->second->print_statistics();
        goroutine_heaps_.erase(it);
        std::cout << "[HEAP] Unregistered goroutine " << goroutine_id << "\n";
    }
}

void* GoroutineAwareHeap::allocate_by_ownership(
    size_t size,
    uint32_t type_id,
    ObjectOwnership ownership,
    uint32_t goroutine_id
) {
    if (goroutine_id == 0) {
        goroutine_id = current_goroutine_id_;
    }
    
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

void* GoroutineAwareHeap::allocate_stack_local(size_t size, uint32_t type_id) {
    // This is a marker for JIT to emit stack allocation
    // The actual stack allocation is done inline by the JIT compiler
    return reinterpret_cast<void*>(0xDEADBEEF);
}

void* GoroutineAwareHeap::allocate_goroutine_private(size_t size, uint32_t type_id, uint32_t goroutine_id) {
    GoroutineHeapImpl* heap = get_goroutine_heap(goroutine_id);
    if (!heap) {
        // Auto-register goroutine if not found
        register_goroutine(goroutine_id);
        heap = get_goroutine_heap(goroutine_id);
    }
    
    if (!heap) {
        std::cout << "[HEAP] ERROR: Cannot find or create heap for goroutine " << goroutine_id << "\n";
        return nullptr;
    }
    
    // Try fast TLAB allocation first
    void* result = heap->allocate_fast(size);
    if (result) {
        // Initialize header
        GoroutineObjectHeader* header = reinterpret_cast<GoroutineObjectHeader*>(result);
        header->size = size;
        header->type_id = type_id;
        header->flags = 0;
        header->owner_goroutine_id = goroutine_id;
        header->ownership_type = static_cast<uint32_t>(ObjectOwnership::GOROUTINE_PRIVATE);
        header->ref_goroutine_count = 1;
        header->needs_sync = 0;
        // Use safe goroutine ID for bit mask (consistent with header implementation)
        if (goroutine_id < 64) {
            header->accessing_goroutines.store(1ull << goroutine_id);
        } else {
            // For goroutines > 64, mark as globally shared (conservative)
            header->accessing_goroutines.store(0xFFFFFFFFFFFFFFFFull);
        }
        
        void* obj = header->get_object_start();
        heap->add_allocated_object(obj);
        
        GC_DEBUG_LOG("[HEAP] Fast allocated " << size << " bytes for goroutine " << goroutine_id << " at " << obj);
        return obj;
    }
    
    // Try private heap allocation
    result = heap->allocate_private(size);
    if (result) {
        // Initialize header
        GoroutineObjectHeader* header = reinterpret_cast<GoroutineObjectHeader*>(result);
        header->size = size;
        header->type_id = type_id;
        header->flags = 0;
        header->owner_goroutine_id = goroutine_id;
        header->ownership_type = static_cast<uint32_t>(ObjectOwnership::GOROUTINE_PRIVATE);
        header->ref_goroutine_count = 1;
        header->needs_sync = 0;
        // Use safe goroutine ID for bit mask (consistent with header implementation)
        if (goroutine_id < 64) {
            header->accessing_goroutines.store(1ull << goroutine_id);
        } else {
            // For goroutines > 64, mark as globally shared (conservative)
            header->accessing_goroutines.store(0xFFFFFFFFFFFFFFFFull);
        }
        
        void* obj = header->get_object_start();
        heap->add_allocated_object(obj);
        
        GC_DEBUG_LOG("[HEAP] Private allocated " << size << " bytes for goroutine " << goroutine_id << " at " << obj);
        return obj;
    }
    
    // Need GC
    std::cout << "[HEAP] Goroutine " << goroutine_id << " heap full, need GC\n";
    return nullptr;
}

void* GoroutineAwareHeap::allocate_goroutine_shared(size_t size, uint32_t type_id) {
    return shared_heap_->allocate_shared(size, type_id);
}

void* GoroutineAwareHeap::allocate_global_shared(size_t size, uint32_t type_id) {
    return shared_heap_->allocate_global(size, type_id);
}

GoroutineHeapImpl* GoroutineAwareHeap::get_goroutine_heap(uint32_t goroutine_id) {
    std::lock_guard<std::mutex> lock(goroutine_heaps_mutex_);
    auto it = goroutine_heaps_.find(goroutine_id);
    if (it != goroutine_heaps_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void GoroutineAwareHeap::set_current_goroutine(uint32_t goroutine_id) {
    current_goroutine_id_ = goroutine_id;
    std::cout << "[HEAP] Set current goroutine to " << goroutine_id << "\n";
}

uint32_t GoroutineAwareHeap::get_current_goroutine() {
    return current_goroutine_id_;
}

void GoroutineAwareHeap::collect_goroutine_private(uint32_t goroutine_id) {
    GoroutineHeapImpl* heap = get_goroutine_heap(goroutine_id);
    if (!heap) return;
    
    std::cout << "[HEAP] Collecting private heap for goroutine " << goroutine_id << "\n";
    
    // Get objects before collection for proper cleanup
    auto objects_before = heap->get_allocated_objects();
    size_t objects_count = objects_before.size();
    
    // Simple collection: reset TLAB and private heap
    // The reset methods now properly clean up object tracking
    heap->reset_tlab();
    heap->reset_private_heap();
    
    std::cout << "[HEAP] Completed private collection for goroutine " << goroutine_id 
              << ", freed tracking for " << objects_count << " objects\n";
}

void GoroutineAwareHeap::collect_shared_heap() {
    std::cout << "[HEAP] Collecting shared heap\n";
    
    // In a real implementation, this would do coordinated mark-and-sweep
    // For now, just print statistics
    shared_heap_->print_statistics();
    
    std::cout << "[HEAP] Completed shared heap collection\n";
}

void GoroutineAwareHeap::print_all_statistics() {
    std::cout << "\n=== HEAP STATISTICS ===\n";
    
    // Print per-goroutine statistics
    std::lock_guard<std::mutex> lock(goroutine_heaps_mutex_);
    for (const auto& [goroutine_id, heap] : goroutine_heaps_) {
        heap->print_statistics();
    }
    
    // Print shared heap statistics
    shared_heap_->print_statistics();
    
    // Print overall statistics
    size_t total_goroutines = goroutine_heaps_.size();
    size_t total_private_allocations = 0;
    size_t total_private_bytes = 0;
    
    for (const auto& [goroutine_id, heap] : goroutine_heaps_) {
        total_private_allocations += heap->total_allocations.load();
        total_private_bytes += heap->get_total_allocated();
    }
    
    std::cout << "\nOverall statistics:\n";
    std::cout << "  Total goroutines: " << total_goroutines << "\n";
    std::cout << "  Total private allocations: " << total_private_allocations << "\n";
    std::cout << "  Total private bytes: " << total_private_bytes << "\n";
    std::cout << "  Total shared allocations: " << shared_heap_->shared_allocations.load() << "\n";
    std::cout << "  Total global allocations: " << shared_heap_->global_allocations.load() << "\n";
    std::cout << "  Total allocated bytes: " << 
                 (total_private_bytes + shared_heap_->shared_allocated_bytes.load() + 
                  shared_heap_->global_allocated_bytes.load()) << "\n";
    
    std::cout << "=======================\n\n";
}

std::vector<void*> GoroutineAwareHeap::get_goroutine_objects(uint32_t goroutine_id) {
    GoroutineHeapImpl* heap = get_goroutine_heap(goroutine_id);
    if (heap) {
        return heap->get_allocated_objects();
    }
    return {};
}

std::vector<void*> GoroutineAwareHeap::get_shared_objects() {
    return shared_heap_->get_shared_objects();
}

std::vector<void*> GoroutineAwareHeap::get_global_objects() {
    return shared_heap_->get_global_objects();
}

std::vector<uint32_t> GoroutineAwareHeap::get_registered_goroutines() {
    std::lock_guard<std::mutex> lock(goroutine_heaps_mutex_);
    std::vector<uint32_t> goroutines;
    for (const auto& [goroutine_id, heap] : goroutine_heaps_) {
        goroutines.push_back(goroutine_id);
    }
    return goroutines;
}

size_t GoroutineAwareHeap::get_total_allocated_bytes() {
    std::lock_guard<std::mutex> lock(goroutine_heaps_mutex_);
    
    size_t total = 0;
    for (const auto& [goroutine_id, heap] : goroutine_heaps_) {
        total += heap->get_total_allocated();
    }
    
    total += shared_heap_->shared_allocated_bytes.load();
    total += shared_heap_->global_allocated_bytes.load();
    
    return total;
}

bool GoroutineAwareHeap::is_goroutine_heap_full(uint32_t goroutine_id) {
    GoroutineHeapImpl* heap = get_goroutine_heap(goroutine_id);
    if (!heap) return false;
    
    // Check if both TLAB and private heap are nearly full
    size_t tlab_used = heap->tlab_allocated_bytes.load();
    size_t private_used = heap->private_heap_allocated_bytes.load();
    
    bool tlab_full = (tlab_used > GCConfig::TLAB_SIZE * 0.9);
    bool private_full = (private_used > (GCConfig::YOUNG_GEN_SIZE / 4) * 0.9);
    
    return tlab_full && private_full;
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

GoroutineObjectHeader* get_goroutine_header(void* obj) {
    if (!obj) return nullptr;
    return reinterpret_cast<GoroutineObjectHeader*>(
        static_cast<uint8_t*>(obj) - sizeof(GoroutineObjectHeader)
    );
}

ObjectOwnership get_object_ownership(void* obj) {
    GoroutineObjectHeader* header = get_goroutine_header(obj);
    if (!header) return ObjectOwnership::GOROUTINE_SHARED;
    return static_cast<ObjectOwnership>(header->ownership_type);
}

uint32_t get_object_owner_goroutine(void* obj) {
    GoroutineObjectHeader* header = get_goroutine_header(obj);
    if (!header) return 0;
    return header->owner_goroutine_id;
}

bool is_object_shared(void* obj) {
    GoroutineObjectHeader* header = get_goroutine_header(obj);
    if (!header) return true;
    return header->is_shared();
}

void mark_object_accessed_by_goroutine(void* obj, uint32_t goroutine_id) {
    GoroutineObjectHeader* header = get_goroutine_header(obj);
    if (header) {
        header->add_accessing_goroutine(goroutine_id);
    }
}

} // namespace gots