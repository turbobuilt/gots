#include "goroutine_aware_gc.h"
#include <thread>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace gots {

// ============================================================================
// GLOBAL GC COORDINATOR STATE
// ============================================================================

static GoroutineCoordinatedGC* g_gc_coordinator = nullptr;
static std::mutex g_gc_coordinator_mutex;

// Safepoint mechanism using protected page
static void* g_safepoint_page = nullptr;
static std::atomic<bool> g_safepoint_requested{false};

// ============================================================================
// SAFEPOINT IMPLEMENTATION
// ============================================================================

static void setup_safepoint_page() {
    // Allocate a page for safepoint polling
    g_safepoint_page = mmap(nullptr, getpagesize(), PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (g_safepoint_page == MAP_FAILED) {
        throw std::runtime_error("Failed to allocate safepoint page");
    }
    
    std::cout << "[GC] Setup safepoint page at " << g_safepoint_page << "\n";
}

static void cleanup_safepoint_page() {
    if (g_safepoint_page && g_safepoint_page != MAP_FAILED) {
        munmap(g_safepoint_page, getpagesize());
        g_safepoint_page = nullptr;
    }
}

static void protect_safepoint_page() {
    if (g_safepoint_page && g_safepoint_page != MAP_FAILED) {
        // Protect the page to cause faults on access
        int result = mprotect(g_safepoint_page, getpagesize(), PROT_NONE);
        if (result != 0) {
            std::cerr << "[GC] ERROR: Failed to protect safepoint page: " << strerror(errno) << "\n";
            
            // Critical security fix: Handle protection failure securely
            switch (errno) {
                case ENOMEM:
                    std::cerr << "[GC] Out of memory for memory protection\n";
                    throw std::runtime_error("Failed to protect safepoint page: out of memory");
                    
                case EACCES:
                    std::cerr << "[GC] Access denied for memory protection\n";
                    throw std::runtime_error("Failed to protect safepoint page: access denied");
                    
                case EINVAL:
                    std::cerr << "[GC] Invalid memory protection parameters\n";
                    // Cleanup and fallback to atomic polling
                    cleanup_safepoint_page();
                    g_safepoint_page = nullptr;
                    break;
                    
                default:
                    std::cerr << "[GC] Unknown memory protection error: " << errno << "\n";
                    throw std::runtime_error("Failed to protect safepoint page: unknown error");
            }
            
            // Fallback to atomic polling only for recoverable errors
            std::cout << "[GC] Falling back to atomic safepoint polling\n";
            g_safepoint_requested.store(true, std::memory_order_release);
        } else {
            g_safepoint_requested.store(true, std::memory_order_release);
            std::cout << "[GC] Protected safepoint page for coordinated GC\n";
        }
    } else {
        // No safepoint page available, use atomic polling
        std::cout << "[GC] Using atomic safepoint polling (no protected page)\n";
        g_safepoint_requested.store(true, std::memory_order_release);
    }
}

static void unprotect_safepoint_page() {
    if (g_safepoint_page && g_safepoint_page != MAP_FAILED) {
        // Unprotect the page with proper error handling
        int result = mprotect(g_safepoint_page, getpagesize(), PROT_READ | PROT_WRITE);
        if (result != 0) {
            std::cerr << "[GC] ERROR: Failed to unprotect safepoint page: " << strerror(errno) << "\n";
            
            // Handle unprotection failure - this is serious
            switch (errno) {
                case ENOMEM:
                    std::cerr << "[GC] Out of memory for memory protection\n";
                    // Continue with atomic release but log the issue
                    break;
                    
                case EACCES:
                    std::cerr << "[GC] Access denied for memory protection\n";
                    // This could indicate a security issue
                    break;
                    
                case EINVAL:
                    std::cerr << "[GC] Invalid memory protection parameters\n";
                    // Page might be corrupted, cleanup
                    cleanup_safepoint_page();
                    g_safepoint_page = nullptr;
                    break;
                    
                default:
                    std::cerr << "[GC] Unknown memory protection error: " << errno << "\n";
                    break;
            }
            
            // Even on failure, we must release the safepoint to avoid deadlock
            std::cout << "[GC] Forcing safepoint release despite unprotect failure\n";
        } else {
            std::cout << "[GC] Unprotected safepoint page\n";
        }
    }
    
    // Always release the safepoint flag
    g_safepoint_requested.store(false, std::memory_order_release);
}

// ============================================================================
// GOROUTINE INFO IMPLEMENTATION
// ============================================================================

struct GoroutineInfoImpl {
    uint32_t id;
    std::thread::id thread_id;
    std::atomic<bool> at_safepoint{false};
    std::atomic<bool> gc_requested{false};
    std::atomic<bool> active{true};
    
    // Stack roots
    void** stack_roots;
    size_t stack_root_count;
    std::mutex roots_mutex;
    
    // Allocation statistics
    std::atomic<size_t> private_allocated{0};
    std::atomic<size_t> shared_allocated{0};
    std::atomic<size_t> allocations_since_gc{0};
    
    // Timing statistics
    std::atomic<size_t> safepoint_count{0};
    std::atomic<size_t> total_safepoint_time_us{0};
    
    GoroutineInfoImpl(uint32_t goroutine_id) : id(goroutine_id) {
        thread_id = std::this_thread::get_id();
        stack_roots = nullptr;
        stack_root_count = 0;
        
        std::cout << "[GC] Created goroutine info for " << id << "\n";
    }
    
    ~GoroutineInfoImpl() {
        if (stack_roots) {
            delete[] stack_roots;
        }
        std::cout << "[GC] Destroyed goroutine info for " << id << "\n";
    }
    
    void set_stack_roots(void** roots, size_t count) {
        std::lock_guard<std::mutex> lock(roots_mutex);
        
        if (stack_roots) {
            delete[] stack_roots;
        }
        
        if (count > 0) {
            stack_roots = new void*[count];
            std::copy(roots, roots + count, stack_roots);
            stack_root_count = count;
        } else {
            stack_roots = nullptr;
            stack_root_count = 0;
        }
        
        std::cout << "[GC] Set " << count << " stack roots for goroutine " << id << "\n";
    }
    
    std::vector<void*> get_stack_roots() {
        std::lock_guard<std::mutex> lock(roots_mutex);
        std::vector<void*> roots;
        
        for (size_t i = 0; i < stack_root_count; ++i) {
            if (stack_roots[i]) {
                roots.push_back(stack_roots[i]);
            }
        }
        
        return roots;
    }
    
    void enter_safepoint() {
        auto start_time = std::chrono::high_resolution_clock::now();
        
        at_safepoint.store(true, std::memory_order_release);
        safepoint_count.fetch_add(1, std::memory_order_relaxed);
        
        // Wait for GC to complete
        while (gc_requested.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        
        at_safepoint.store(false, std::memory_order_release);
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        total_safepoint_time_us.fetch_add(duration.count(), std::memory_order_relaxed);
        
        std::cout << "[GC] Goroutine " << id << " completed safepoint in " 
                  << duration.count() << " μs\n";
    }
    
    void print_statistics() const {
        std::cout << "[GC] Goroutine " << id << " statistics:\n";
        std::cout << "  Private allocated: " << private_allocated.load() << " bytes\n";
        std::cout << "  Shared allocated: " << shared_allocated.load() << " bytes\n";
        std::cout << "  Allocations since GC: " << allocations_since_gc.load() << "\n";
        std::cout << "  Safepoint count: " << safepoint_count.load() << "\n";
        std::cout << "  Total safepoint time: " << total_safepoint_time_us.load() << " μs\n";
        
        auto avg_safepoint_time = safepoint_count.load() > 0 ? 
            total_safepoint_time_us.load() / safepoint_count.load() : 0;
        std::cout << "  Average safepoint time: " << avg_safepoint_time << " μs\n";
    }
};

// ============================================================================
// GOROUTINE COORDINATED GC IMPLEMENTATION
// ============================================================================

GoroutineCoordinatedGC::GoroutineCoordinatedGC() {
    setup_safepoint_page();
    std::cout << "[GC] Created goroutine coordinated GC\n";
}

GoroutineCoordinatedGC::~GoroutineCoordinatedGC() {
    shutdown();
    cleanup_safepoint_page();
    std::cout << "[GC] Destroyed goroutine coordinated GC\n";
}

void GoroutineCoordinatedGC::initialize() {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    
    running_.store(true);
    
    // Start GC threads
    private_gc_thread_ = std::thread(&GoroutineCoordinatedGC::private_gc_thread_loop, this);
    shared_gc_thread_ = std::thread(&GoroutineCoordinatedGC::shared_gc_thread_loop, this);
    
    std::cout << "[GC] Initialized coordinated GC with " << std::thread::hardware_concurrency() 
              << " cores\n";
}

void GoroutineCoordinatedGC::shutdown() {
    running_.store(false);
    
    // Wake up GC threads
    safepoint_cv_.notify_all();
    gc_cv_.notify_all();
    
    // Wait for GC threads to finish
    if (private_gc_thread_.joinable()) {
        private_gc_thread_.join();
    }
    if (shared_gc_thread_.joinable()) {
        shared_gc_thread_.join();
    }
    
    // Print final statistics
    print_all_statistics();
    
    std::cout << "[GC] Shutdown coordinated GC\n";
}

GoroutineCoordinatedGC& GoroutineCoordinatedGC::instance() {
    std::lock_guard<std::mutex> lock(g_gc_coordinator_mutex);
    if (!g_gc_coordinator) {
        g_gc_coordinator = new GoroutineCoordinatedGC();
        g_gc_coordinator->initialize();
    }
    return *g_gc_coordinator;
}

void GoroutineCoordinatedGC::register_goroutine(uint32_t goroutine_id) {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    
    if (goroutines_.find(goroutine_id) == goroutines_.end()) {
        goroutines_[goroutine_id] = std::make_unique<GoroutineInfoImpl>(goroutine_id);
        total_goroutines_.fetch_add(1, std::memory_order_relaxed);
        
        std::cout << "[GC] Registered goroutine " << goroutine_id 
                  << " (total: " << total_goroutines_.load() << ")\n";
    }
}

void GoroutineCoordinatedGC::unregister_goroutine(uint32_t goroutine_id) {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    
    auto it = goroutines_.find(goroutine_id);
    if (it != goroutines_.end()) {
        it->second->print_statistics();
        goroutines_.erase(it);
        total_goroutines_.fetch_sub(1, std::memory_order_relaxed);
        
        std::cout << "[GC] Unregistered goroutine " << goroutine_id 
                  << " (total: " << total_goroutines_.load() << ")\n";
    }
}

void GoroutineCoordinatedGC::set_goroutine_stack_roots(uint32_t goroutine_id, void** roots, size_t count) {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    
    auto it = goroutines_.find(goroutine_id);
    if (it != goroutines_.end()) {
        it->second->set_stack_roots(roots, count);
    }
}

void GoroutineCoordinatedGC::safepoint_poll(uint32_t goroutine_id) {
    if (g_safepoint_requested.load(std::memory_order_acquire)) {
        safepoint_slow(goroutine_id);
    }
}

void GoroutineCoordinatedGC::safepoint_slow(uint32_t goroutine_id) {
    auto& gc = instance();
    
    std::lock_guard<std::mutex> lock(gc.goroutines_mutex_);
    auto it = gc.goroutines_.find(goroutine_id);
    if (it != gc.goroutines_.end()) {
        it->second->enter_safepoint();
    }
}

void GoroutineCoordinatedGC::request_gc(GCType type) {
    std::lock_guard<std::mutex> lock(gc_mutex_);
    
    if (gc_in_progress_.load()) {
        std::cout << "[GC] GC already in progress, ignoring request\n";
        return;
    }
    
    gc_requested_type_ = type;
    gc_cv_.notify_all();
    
    std::cout << "[GC] Requested " << (type == GCType::PRIVATE ? "private" : "shared") 
              << " garbage collection\n";
}

void GoroutineCoordinatedGC::wait_for_all_safepoints() {
    std::cout << "[GC] Waiting for all goroutines to reach safepoint...\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    constexpr auto SAFEPOINT_TIMEOUT = std::chrono::seconds(30); // 30 second timeout
    constexpr auto WARNING_TIMEOUT = std::chrono::seconds(5);    // Warn after 5 seconds
    
    // Request safepoint from all goroutines
    std::vector<uint32_t> active_goroutines;
    {
        std::lock_guard<std::mutex> lock(goroutines_mutex_);
        for (const auto& [goroutine_id, info] : goroutines_) {
            if (info->active.load()) {
                info->gc_requested.store(true, std::memory_order_release);
                active_goroutines.push_back(goroutine_id);
            }
        }
    }
    
    std::cout << "[GC] Requesting safepoint from " << active_goroutines.size() << " active goroutines\n";
    
    // Protect safepoint page to trigger faults
    protect_safepoint_page();
    
    // Wait for all goroutines to reach safepoint with timeout
    bool all_at_safepoint = false;
    bool warning_issued = false;
    
    while (!all_at_safepoint && running_.load()) {
        auto current_time = std::chrono::high_resolution_clock::now();
        auto elapsed = current_time - start_time;
        
        // Check for timeout
        if (elapsed > SAFEPOINT_TIMEOUT) {
            std::cerr << "[GC] CRITICAL: Safepoint timeout after " 
                      << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() 
                      << " seconds!\n";
            
            // Log which goroutines are still not at safepoint
            {
                std::lock_guard<std::mutex> lock(goroutines_mutex_);
                for (const auto& [goroutine_id, info] : goroutines_) {
                    if (info->active.load() && !info->at_safepoint.load()) {
                        std::cerr << "[GC] Goroutine " << goroutine_id 
                                  << " failed to reach safepoint\n";
                    }
                }
            }
            
            // Force termination to prevent system deadlock
            std::cerr << "[GC] Forcing safepoint release to prevent deadlock\n";
            break;
        }
        
        // Issue warning after 5 seconds
        if (!warning_issued && elapsed > WARNING_TIMEOUT) {
            std::cout << "[GC] WARNING: Safepoint taking longer than expected ("
                      << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                      << " ms)\n";
            warning_issued = true;
            
            // Log slow goroutines
            {
                std::lock_guard<std::mutex> lock(goroutines_mutex_);
                for (const auto& [goroutine_id, info] : goroutines_) {
                    if (info->active.load() && !info->at_safepoint.load()) {
                        std::cout << "[GC] Waiting for goroutine " << goroutine_id << "\n";
                    }
                }
            }
        }
        
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        
        // Check if all goroutines are at safepoint
        {
            std::lock_guard<std::mutex> lock(goroutines_mutex_);
            all_at_safepoint = true;
            
            for (const auto& [goroutine_id, info] : goroutines_) {
                if (info->active.load() && !info->at_safepoint.load()) {
                    all_at_safepoint = false;
                    break;
                }
            }
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    
    if (all_at_safepoint) {
        std::cout << "[GC] All goroutines reached safepoint in " << duration.count() << " μs\n";
    } else {
        std::cout << "[GC] Safepoint coordination incomplete after " << duration.count() << " μs\n";
    }
}

void GoroutineCoordinatedGC::release_all_safepoints() {
    std::cout << "[GC] Releasing all goroutines from safepoint...\n";
    
    // Unprotect safepoint page
    unprotect_safepoint_page();
    
    // Release all goroutines
    {
        std::lock_guard<std::mutex> lock(goroutines_mutex_);
        for (const auto& [goroutine_id, info] : goroutines_) {
            info->gc_requested.store(false, std::memory_order_release);
        }
    }
    
    std::cout << "[GC] Released all goroutines from safepoint\n";
}

void GoroutineCoordinatedGC::collect_goroutine_private() {
    std::cout << "[GC] Starting private goroutine collection...\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Collect each goroutine's private heap separately
    std::vector<uint32_t> goroutine_ids;
    {
        std::lock_guard<std::mutex> lock(goroutines_mutex_);
        for (const auto& [goroutine_id, info] : goroutines_) {
            if (info->active.load()) {
                goroutine_ids.push_back(goroutine_id);
            }
        }
    }
    
    // Collect private heaps in parallel
    std::vector<std::thread> collection_threads;
    for (uint32_t goroutine_id : goroutine_ids) {
        collection_threads.emplace_back([this, goroutine_id]() {
            collect_single_goroutine_private(goroutine_id);
        });
    }
    
    // Wait for all collections to complete
    for (auto& thread : collection_threads) {
        thread.join();
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    private_collections_.fetch_add(1, std::memory_order_relaxed);
    total_private_pause_time_ms_.fetch_add(duration.count(), std::memory_order_relaxed);
    
    std::cout << "[GC] Completed private collection in " << duration.count() << " ms\n";
}

void GoroutineCoordinatedGC::collect_goroutine_shared() {
    std::cout << "[GC] Starting shared goroutine collection...\n";
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // This requires coordination across all goroutines
    wait_for_all_safepoints();
    
    // Phase 1: Mark all shared objects
    mark_shared_objects();
    
    // Phase 2: Sweep unreachable shared objects
    sweep_shared_objects();
    
    // Phase 3: Compact shared heap (optional)
    compact_shared_heap();
    
    release_all_safepoints();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
    
    shared_collections_.fetch_add(1, std::memory_order_relaxed);
    total_shared_pause_time_ms_.fetch_add(duration.count(), std::memory_order_relaxed);
    
    std::cout << "[GC] Completed shared collection in " << duration.count() << " ms\n";
}

void GoroutineCoordinatedGC::collect_single_goroutine_private(uint32_t goroutine_id) {
    std::cout << "[GC] Collecting private heap for goroutine " << goroutine_id << "...\n";
    
    // Get goroutine info
    GoroutineInfoImpl* info;
    {
        std::lock_guard<std::mutex> lock(goroutines_mutex_);
        auto it = goroutines_.find(goroutine_id);
        if (it == goroutines_.end()) return;
        info = it->second.get();
    }
    
    // Get stack roots
    std::vector<void*> roots = info->get_stack_roots();
    
    // Get private heap objects
    std::vector<void*> private_objects = GoroutineAwareHeap::instance().get_goroutine_objects(goroutine_id);
    
    // Simple mark-and-sweep for private objects
    std::unordered_set<void*> marked_objects;
    
    // Mark phase: mark all reachable objects from roots
    for (void* root : roots) {
        mark_object_and_children(root, marked_objects);
    }
    
    // Sweep phase: collect unmarked objects
    size_t collected_count = 0;
    size_t collected_bytes = 0;
    
    for (void* obj : private_objects) {
        if (marked_objects.find(obj) == marked_objects.end()) {
            // Object is unreachable, collect it
            GoroutineObjectHeader* header = get_goroutine_header(obj);
            if (header) {
                collected_bytes += header->size;
                collected_count++;
                
                // In a real implementation, this would free the object
                // For now, just mark it as collected
                header->flags |= ObjectHeader::MARKED; // Reuse flag for "collected"
            }
        }
    }
    
    // Reset allocation counts
    info->allocations_since_gc.store(0, std::memory_order_relaxed);
    
    std::cout << "[GC] Collected " << collected_count << " objects (" 
              << collected_bytes << " bytes) from goroutine " << goroutine_id << "\n";
}

void GoroutineCoordinatedGC::mark_shared_objects() {
    std::cout << "[GC] Marking shared objects...\n";
    
    // Get all shared objects
    std::vector<void*> shared_objects = GoroutineAwareHeap::instance().get_shared_objects();
    std::vector<void*> global_objects = GoroutineAwareHeap::instance().get_global_objects();
    
    std::unordered_set<void*> marked_objects;
    
    // Mark from all goroutine roots
    {
        std::lock_guard<std::mutex> lock(goroutines_mutex_);
        for (const auto& [goroutine_id, info] : goroutines_) {
            if (info->active.load()) {
                std::vector<void*> roots = info->get_stack_roots();
                for (void* root : roots) {
                    mark_object_and_children(root, marked_objects);
                }
            }
        }
    }
    
    // Mark from card table (cross-generational references)
    GoroutineWriteBarrier::scan_dirty_cards([&](void* card_addr, size_t card_size) {
        // Scan objects in this card for references
        // This is a simplified implementation
        for (void* obj : shared_objects) {
            if (obj >= card_addr && obj < static_cast<uint8_t*>(card_addr) + card_size) {
                mark_object_and_children(obj, marked_objects);
            }
        }
    });
    
    shared_objects_marked_ = marked_objects.size();
    std::cout << "[GC] Marked " << marked_objects.size() << " shared objects\n";
}

void GoroutineCoordinatedGC::sweep_shared_objects() {
    std::cout << "[GC] Sweeping shared objects...\n";
    
    // Get all shared objects
    std::vector<void*> shared_objects = GoroutineAwareHeap::instance().get_shared_objects();
    std::vector<void*> global_objects = GoroutineAwareHeap::instance().get_global_objects();
    
    size_t swept_count = 0;
    size_t swept_bytes = 0;
    
    // Sweep shared objects
    for (void* obj : shared_objects) {
        GoroutineObjectHeader* header = get_goroutine_header(obj);
        if (header && !header->is_marked()) {
            swept_bytes += header->size;
            swept_count++;
            
            // In a real implementation, this would free the object
            header->flags |= ObjectHeader::MARKED; // Mark as swept
        }
    }
    
    // Sweep global objects
    for (void* obj : global_objects) {
        GoroutineObjectHeader* header = get_goroutine_header(obj);
        if (header && !header->is_marked()) {
            swept_bytes += header->size;
            swept_count++;
            
            // In a real implementation, this would free the object
            header->flags |= ObjectHeader::MARKED; // Mark as swept
        }
    }
    
    shared_objects_swept_ = swept_count;
    shared_bytes_freed_ += swept_bytes;
    
    std::cout << "[GC] Swept " << swept_count << " objects (" 
              << swept_bytes << " bytes)\n";
}

void GoroutineCoordinatedGC::compact_shared_heap() {
    std::cout << "[GC] Compacting shared heap...\n";
    
    // In a real implementation, this would compact the heap
    // For now, just clear the card table
    GoroutineWriteBarrier::clear_cards();
    
    std::cout << "[GC] Completed heap compaction\n";
}

void GoroutineCoordinatedGC::mark_object_and_children(void* obj, std::unordered_set<void*>& marked) {
    if (!obj || marked.find(obj) != marked.end()) return;
    
    GoroutineObjectHeader* header = get_goroutine_header(obj);
    if (!header) return;
    
    // Mark this object
    marked.insert(obj);
    header->set_marked(true);
    
    // In a real implementation, this would traverse object references
    // For now, just mark the object itself
}

void GoroutineCoordinatedGC::private_gc_thread_loop() {
    std::cout << "[GC] Started private GC thread\n";
    
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(gc_mutex_);
        
        // Wait for GC request or timeout
        gc_cv_.wait_for(lock, std::chrono::seconds(1), [this] {
            return gc_requested_type_ == GCType::PRIVATE || !running_.load();
        });
        
        if (!running_.load()) break;
        
        if (gc_requested_type_ == GCType::PRIVATE) {
            gc_in_progress_.store(true);
            gc_requested_type_ = GCType::NONE;
            
            lock.unlock();
            
            collect_goroutine_private();
            
            gc_in_progress_.store(false);
        }
    }
    
    std::cout << "[GC] Stopped private GC thread\n";
}

void GoroutineCoordinatedGC::shared_gc_thread_loop() {
    std::cout << "[GC] Started shared GC thread\n";
    
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(gc_mutex_);
        
        // Wait for GC request or timeout
        gc_cv_.wait_for(lock, std::chrono::seconds(5), [this] {
            return gc_requested_type_ == GCType::SHARED || !running_.load();
        });
        
        if (!running_.load()) break;
        
        if (gc_requested_type_ == GCType::SHARED) {
            gc_in_progress_.store(true);
            gc_requested_type_ = GCType::NONE;
            
            lock.unlock();
            
            collect_goroutine_shared();
            
            gc_in_progress_.store(false);
        }
    }
    
    std::cout << "[GC] Stopped shared GC thread\n";
}

GoroutineCoordinatedGC::GoroutineGCStats GoroutineCoordinatedGC::get_stats() const {
    GoroutineGCStats stats;
    stats.total_goroutines = total_goroutines_.load();
    stats.private_collections = private_collections_.load();
    stats.shared_collections = shared_collections_.load();
    stats.cross_goroutine_references = cross_goroutine_references_.load();
    stats.sync_operations = sync_operations_.load();
    
    size_t total_safepoint_time = 0;
    size_t total_safepoints = 0;
    
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(goroutines_mutex_));
        for (const auto& [goroutine_id, info] : goroutines_) {
            total_safepoint_time += info->total_safepoint_time_us.load();
            total_safepoints += info->safepoint_count.load();
        }
    }
    
    stats.avg_safepoint_time_us = total_safepoints > 0 ? total_safepoint_time / total_safepoints : 0;
    
    return stats;
}

void GoroutineCoordinatedGC::print_all_statistics() const {
    std::cout << "\n=== COORDINATED GC STATISTICS ===\n";
    
    GoroutineGCStats stats = get_stats();
    
    std::cout << "Total goroutines: " << stats.total_goroutines << "\n";
    std::cout << "Private collections: " << stats.private_collections << "\n";
    std::cout << "Shared collections: " << stats.shared_collections << "\n";
    std::cout << "Cross-goroutine references: " << stats.cross_goroutine_references << "\n";
    std::cout << "Sync operations: " << stats.sync_operations << "\n";
    std::cout << "Average safepoint time: " << stats.avg_safepoint_time_us << " μs\n";
    
    std::cout << "\nCollection timing:\n";
    std::cout << "Total private pause time: " << total_private_pause_time_ms_.load() << " ms\n";
    std::cout << "Total shared pause time: " << total_shared_pause_time_ms_.load() << " ms\n";
    
    if (stats.private_collections > 0) {
        std::cout << "Average private pause: " << 
                     (total_private_pause_time_ms_.load() / stats.private_collections) << " ms\n";
    }
    if (stats.shared_collections > 0) {
        std::cout << "Average shared pause: " << 
                     (total_shared_pause_time_ms_.load() / stats.shared_collections) << " ms\n";
    }
    
    std::cout << "\nMemory statistics:\n";
    std::cout << "Shared objects marked: " << shared_objects_marked_ << "\n";
    std::cout << "Shared objects swept: " << shared_objects_swept_ << "\n";
    std::cout << "Shared bytes freed: " << shared_bytes_freed_ << "\n";
    
    std::cout << "\nPer-goroutine statistics:\n";
    {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(goroutines_mutex_));
        for (const auto& [goroutine_id, info] : goroutines_) {
            info->print_statistics();
        }
    }
    
    std::cout << "=================================\n\n";
}

} // namespace gots