#include "gc_memory_manager.h"
#include "gc_concurrent_marking.h"
#include <sys/mman.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stack>
#include <unordered_map>
#include <cassert>
#include <cstring>
#include <chrono>
#include <deque>

namespace gots {

// ============================================================================
// GLOBAL INSTANCES
// ============================================================================

static GarbageCollector* g_gc_instance = nullptr;
thread_local TLAB* GenerationalHeap::tlab_ = nullptr;
thread_local GarbageCollector::ThreadRootCleanup* GarbageCollector::thread_root_cleanup_ = nullptr;
thread_local int GarbageCollector::thread_deque_index_ = -1;
uint8_t* WriteBarrier::card_table_ = nullptr;
size_t WriteBarrier::card_table_size_ = 0;

// Thread-local escape analysis data with bounded cache and LRU eviction
thread_local struct {
    std::vector<std::pair<size_t, size_t>> scope_stack; // (scope_id, depth)
    std::unordered_map<size_t, EscapeAnalyzer::AnalysisResult> allocation_sites;
    std::unordered_map<size_t, std::vector<size_t>> var_to_sites;
    std::unordered_map<size_t, size_t> var_scope;
    
    // LRU tracking for allocation sites
    std::unordered_map<size_t, size_t> site_access_time;
    std::unordered_map<size_t, size_t> var_access_time;
    
    size_t current_scope = 0;
    size_t access_counter = 0;
    
    static constexpr size_t MAX_ALLOCATION_SITES = 5000;  // Reduced limit
    static constexpr size_t MAX_VARIABLES = 2000;         // Limit variables too
    static constexpr size_t CLEANUP_THRESHOLD = 500;      // More frequent cleanup
    static constexpr size_t FORCE_CLEANUP_THRESHOLD = 100; // Force cleanup interval
    
    void cleanup_old_data() {
        access_counter++;
        
        // Force periodic cleanup even if under limits
        bool force_cleanup = (access_counter % FORCE_CLEANUP_THRESHOLD == 0);
        
        if (!force_cleanup && allocation_sites.size() < MAX_ALLOCATION_SITES && 
            var_to_sites.size() < MAX_VARIABLES) {
            return;
        }
        
        size_t cleaned_sites = 0;
        size_t cleaned_vars = 0;
        
        // First, remove variables no longer in scope
        std::vector<size_t> vars_to_remove;
        for (const auto& [var_id, scope_id] : var_scope) {
            bool in_current_scope = false;
            for (const auto& [sid, depth] : scope_stack) {
                if (sid == scope_id) {
                    in_current_scope = true;
                    break;
                }
            }
            if (!in_current_scope) {
                vars_to_remove.push_back(var_id);
            }
        }
        
        // Remove out-of-scope variables and their sites
        for (size_t var_id : vars_to_remove) {
            auto it = var_to_sites.find(var_id);
            if (it != var_to_sites.end()) {
                for (size_t site : it->second) {
                    allocation_sites.erase(site);
                    site_access_time.erase(site);
                    cleaned_sites++;
                }
                var_to_sites.erase(it);
                cleaned_vars++;
            }
            var_scope.erase(var_id);
            var_access_time.erase(var_id);
        }
        
        // If still over limits, do LRU eviction
        if (allocation_sites.size() > MAX_ALLOCATION_SITES || force_cleanup) {
            evict_lru_allocation_sites();
        }
        
        if (var_to_sites.size() > MAX_VARIABLES || force_cleanup) {
            evict_lru_variables();
        }
        
        if (cleaned_sites > 0 || cleaned_vars > 0) {
            std::cout << "[GC] Escape analysis cleanup: removed " << cleaned_sites 
                      << " sites, " << cleaned_vars << " variables\n";
        }
    }
    
    void evict_lru_allocation_sites() {
        if (allocation_sites.size() <= MAX_ALLOCATION_SITES / 2) return;
        
        // Build vector of (site_id, last_access_time) and sort by access time
        std::vector<std::pair<size_t, size_t>> sites_by_time;
        for (const auto& [site_id, _] : allocation_sites) {
            size_t last_access = site_access_time.count(site_id) ? 
                                site_access_time[site_id] : 0;
            sites_by_time.emplace_back(site_id, last_access);
        }
        
        // Sort by access time (oldest first)
        std::sort(sites_by_time.begin(), sites_by_time.end(),
                 [](const auto& a, const auto& b) { return a.second < b.second; });
        
        // Remove oldest half
        size_t to_remove = allocation_sites.size() / 2;
        for (size_t i = 0; i < to_remove && i < sites_by_time.size(); ++i) {
            size_t site_id = sites_by_time[i].first;
            allocation_sites.erase(site_id);
            site_access_time.erase(site_id);
        }
    }
    
    void evict_lru_variables() {
        if (var_to_sites.size() <= MAX_VARIABLES / 2) return;
        
        // Build vector of (var_id, last_access_time)
        std::vector<std::pair<size_t, size_t>> vars_by_time;
        for (const auto& [var_id, _] : var_to_sites) {
            size_t last_access = var_access_time.count(var_id) ? 
                                var_access_time[var_id] : 0;
            vars_by_time.emplace_back(var_id, last_access);
        }
        
        // Sort by access time (oldest first)
        std::sort(vars_by_time.begin(), vars_by_time.end(),
                 [](const auto& a, const auto& b) { return a.second < b.second; });
        
        // Remove oldest third
        size_t to_remove = var_to_sites.size() / 3;
        for (size_t i = 0; i < to_remove && i < vars_by_time.size(); ++i) {
            size_t var_id = vars_by_time[i].first;
            
            // Remove all allocation sites for this variable
            auto it = var_to_sites.find(var_id);
            if (it != var_to_sites.end()) {
                for (size_t site : it->second) {
                    allocation_sites.erase(site);
                    site_access_time.erase(site);
                }
                var_to_sites.erase(it);
            }
            var_scope.erase(var_id);
            var_access_time.erase(var_id);
        }
    }
    
    void touch_allocation_site(size_t site_id) {
        site_access_time[site_id] = access_counter;
    }
    
    void touch_variable(size_t var_id) {
        var_access_time[var_id] = access_counter;
    }
    
} g_escape_data;

// ============================================================================
// ESCAPE ANALYZER IMPLEMENTATION
// ============================================================================

EscapeAnalyzer::AnalysisResult EscapeAnalyzer::analyze_allocation(
    const void* jit_context,
    size_t allocation_site,
    size_t allocation_size,
    uint32_t type_id
) {
    AnalysisResult result;
    
    // Periodic cleanup
    if (++g_escape_data.access_counter % g_escape_data.CLEANUP_THRESHOLD == 0) {
        g_escape_data.cleanup_old_data();
    }
    
    // Check size constraints
    if (allocation_size > GCConfig::MAX_STACK_ALLOC_SIZE) {
        result.size_too_large = true;
        return result;
    }
    
    // Check if we have analysis data for this site
    auto it = g_escape_data.allocation_sites.find(allocation_site);
    if (it != g_escape_data.allocation_sites.end()) {
        g_escape_data.touch_allocation_site(allocation_site);
        return it->second;
    }
    
    // Conservative: if no analysis data, assume it escapes
    result.escapes_to_heap = true;
    return result;
}

void EscapeAnalyzer::register_scope_entry(size_t scope_id) {
    g_escape_data.scope_stack.push_back({scope_id, g_escape_data.current_scope++});
}

void EscapeAnalyzer::register_scope_exit(size_t scope_id) {
    if (!g_escape_data.scope_stack.empty() && 
        g_escape_data.scope_stack.back().first == scope_id) {
        g_escape_data.scope_stack.pop_back();
        if (!g_escape_data.scope_stack.empty()) {
            g_escape_data.current_scope = g_escape_data.scope_stack.back().second;
        }
    }
}

void EscapeAnalyzer::register_variable_def(size_t var_id, size_t scope_id, size_t allocation_site) {
    g_escape_data.var_to_sites[var_id].push_back(allocation_site);
    g_escape_data.var_scope[var_id] = scope_id;
    g_escape_data.touch_variable(var_id);
    
    // Initialize analysis for this allocation site
    if (g_escape_data.allocation_sites.find(allocation_site) == g_escape_data.allocation_sites.end()) {
        AnalysisResult result;
        result.max_lifetime_scope = scope_id;
        result.can_stack_allocate = true; // Optimistic
        g_escape_data.allocation_sites[allocation_site] = result;
        g_escape_data.touch_allocation_site(allocation_site);
    }
}

void EscapeAnalyzer::register_assignment(size_t from_var, size_t to_var) {
    // If from_var escapes to to_var, propagate escape information
    auto from_sites = g_escape_data.var_to_sites[from_var];
    auto to_scope = g_escape_data.var_scope[to_var];
    
    for (size_t site : from_sites) {
        auto& result = g_escape_data.allocation_sites[site];
        if (to_scope < result.max_lifetime_scope) {
            // Variable outlives its allocation scope
            result.escapes_to_heap = true;
            result.can_stack_allocate = false;
            result.escape_points.push_back(to_var);
        }
    }
}

void EscapeAnalyzer::register_return(size_t var_id) {
    auto sites = g_escape_data.var_to_sites[var_id];
    for (size_t site : sites) {
        auto& result = g_escape_data.allocation_sites[site];
        result.escapes_to_return = true;
        result.can_stack_allocate = false;
        result.escape_points.push_back(0); // 0 = return
    }
}

void EscapeAnalyzer::register_closure_capture(size_t var_id) {
    auto sites = g_escape_data.var_to_sites[var_id];
    for (size_t site : sites) {
        auto& result = g_escape_data.allocation_sites[site];
        result.escapes_to_closure = true;
        result.can_stack_allocate = false;
    }
}

// ============================================================================
// GENERATIONAL HEAP IMPLEMENTATION
// ============================================================================

void GenerationalHeap::initialize() {
    // Allocate young generation
    size_t young_size = GCConfig::YOUNG_GEN_SIZE;
    young_.eden_start = static_cast<uint8_t*>(
        mmap(nullptr, young_size, PROT_READ | PROT_WRITE, 
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
    );
    
    if (young_.eden_start == MAP_FAILED) {
        throw std::runtime_error("Failed to allocate young generation");
    }
    
    // Split young gen: 80% eden, 10% survivor1, 10% survivor2
    size_t eden_size = (young_size * 8) / 10;
    size_t survivor_size = young_size / 10;
    
    young_.eden_current = young_.eden_start;
    young_.eden_end = young_.eden_start + eden_size;
    
    young_.survivor1_start = young_.eden_end;
    young_.survivor1_end = young_.survivor1_start + survivor_size;
    
    young_.survivor2_start = young_.survivor1_end;
    young_.survivor2_end = young_.survivor2_start + survivor_size;
    
    young_.active_survivor = young_.survivor1_start;
    
    // Allocate old generation
    old_.start = static_cast<uint8_t*>(
        mmap(nullptr, GCConfig::OLD_GEN_SIZE, PROT_READ | PROT_WRITE, 
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)
    );
    
    if (old_.start == MAP_FAILED) {
        munmap(young_.eden_start, young_size);
        throw std::runtime_error("Failed to allocate old generation");
    }
    
    old_.current = old_.start;
    old_.end = old_.start + GCConfig::OLD_GEN_SIZE;
    
    // Initialize card table
    WriteBarrier::card_table_size_ = GCConfig::OLD_GEN_SIZE / GCConfig::CARD_SIZE;
    WriteBarrier::card_table_ = new uint8_t[WriteBarrier::card_table_size_]();
}

void GenerationalHeap::shutdown() {
    // Clean up TLABs
    {
        std::lock_guard<std::mutex> lock(tlabs_mutex_);
        all_tlabs_.clear();
    }
    
    // Unmap memory
    if (young_.eden_start != MAP_FAILED) {
        munmap(young_.eden_start, GCConfig::YOUNG_GEN_SIZE);
    }
    
    if (old_.start != MAP_FAILED) {
        munmap(old_.start, GCConfig::OLD_GEN_SIZE);
    }
    
    delete[] WriteBarrier::card_table_;
    WriteBarrier::card_table_ = nullptr;
}

void* GenerationalHeap::allocate_slow(size_t size, uint32_t type_id, bool is_array) {
    // Get or create TLAB for this thread with double-checked locking
    TLAB* local_tlab = tlab_;
    if (!local_tlab) {
        std::lock_guard<std::mutex> lock(GarbageCollector::instance().tlabs_mutex_);
        
        // Double-check after acquiring lock
        local_tlab = tlab_;
        if (!local_tlab) {
            // Allocate new TLAB from eden
            if (young_.eden_current + GCConfig::TLAB_SIZE <= young_.eden_end) {
                auto new_tlab = std::make_unique<TLAB>(
                    young_.eden_current, 
                    GCConfig::TLAB_SIZE,
                    std::hash<std::thread::id>{}(std::this_thread::get_id())
                );
                
                young_.eden_current += GCConfig::TLAB_SIZE;
                local_tlab = new_tlab.get();
                GarbageCollector::instance().all_tlabs_.push_back(std::move(new_tlab));
                
                // Use atomic store to ensure visibility
                std::atomic_thread_fence(std::memory_order_release);
                tlab_ = local_tlab;
            } else {
                // Eden full, trigger GC
                GarbageCollector::instance().request_gc(false);
                
                // Retry after GC
                if (young_.eden_current + GCConfig::TLAB_SIZE <= young_.eden_end) {
                    return allocate_slow(size, type_id, is_array);
                }
                
                // Still no space, allocate directly in old gen
                return allocate_large_slow(size, type_id, is_array);
            }
        }
    }
    
    // Try allocation in new TLAB
    void* result = allocate_fast(size, type_id, is_array);
    if (result) return result;
    
    // Object too large for TLAB
    return allocate_large_slow(size, type_id, is_array);
}

void* GenerationalHeap::allocate_large_slow(size_t size, uint32_t type_id, bool is_array) {
    size_t total_size = size + sizeof(ObjectHeader);
    total_size = (total_size + GCConfig::OBJECT_ALIGNMENT - 1) & ~(GCConfig::OBJECT_ALIGNMENT - 1);
    
    std::lock_guard<std::mutex> lock(GarbageCollector::instance().heap_mutex_);
    
    // Try old generation
    if (old_.current + total_size <= old_.end) {
        void* mem = old_.current;
        old_.current += total_size;
        
        // Initialize header
        ObjectHeader* header = reinterpret_cast<ObjectHeader*>(mem);
        header->size = size;
        header->flags = ObjectHeader::IN_OLD_GEN;
        header->type_id = type_id;
        header->forward_ptr = nullptr;
        if (is_array) header->flags |= ObjectHeader::IS_ARRAY;
        
        return header->get_object_start();
    }
    
    // Need full GC
    GarbageCollector::instance().request_gc(true);
    
    // Retry
    if (old_.current + total_size <= old_.end) {
        return allocate_large_slow(size, type_id, is_array);
    }
    
    // Out of memory
    throw std::bad_alloc();
}

// ============================================================================
// GARBAGE COLLECTOR IMPLEMENTATION
// ============================================================================

GarbageCollector::GarbageCollector() {
    heap_.initialize();
    
    // Initialize work-stealing deques based on hardware concurrency
    int num_deques = std::thread::hardware_concurrency();
    mark_deques_.reserve(num_deques);
    for (int i = 0; i < num_deques; ++i) {
        mark_deques_.push_back(std::make_unique<MarkDeque>());
    }
}

GarbageCollector::~GarbageCollector() {
    shutdown();
}

void GarbageCollector::initialize() {
    initialize_thread_cleanup_system();
    type_registry_.register_common_types();
    
    // Initialize optimized write barriers
    OptimizedWriteBarrier::initialize(
        heap_.young_.eden_start, 
        GCConfig::YOUNG_GEN_SIZE + GCConfig::OLD_GEN_SIZE, 
        GCConfig::CARD_SIZE
    );
    AdaptiveWriteBarriers::initialize();
    
    concurrent_marker_ = std::make_unique<ConcurrentMarkingCoordinator>(*this);
    gc_thread_ = std::thread(&GarbageCollector::gc_thread_loop, this);
}

void GarbageCollector::shutdown() {
    running_.store(false);
    gc_cv_.notify_all();
    
    if (gc_thread_.joinable()) {
        gc_thread_.join();
    }
    
    heap_.shutdown();
    OptimizedWriteBarrier::shutdown();
    shutdown_thread_cleanup_system();
}

GarbageCollector& GarbageCollector::instance() {
    if (!g_gc_instance) {
        g_gc_instance = new GarbageCollector();
        g_gc_instance->initialize();
    }
    return *g_gc_instance;
}

void GarbageCollector::gc_thread_loop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(gc_mutex_);
        
        // Wait for GC request
        gc_cv_.wait(lock, [this] {
            return gc_requested_.load() || !running_.load();
        });
        
        if (!running_.load()) break;
        
        if (gc_requested_.exchange(false)) {
            lock.unlock();
            
            // Determine which GC to perform
            size_t young_used = heap_.young_used();
            size_t old_used = heap_.old_used();
            
            if (young_used > GCConfig::YOUNG_GEN_SIZE * 0.8) {
                perform_young_gc();
            } else if (old_used > GCConfig::OLD_GEN_SIZE * 0.8) {
                perform_old_gc();
            }
        }
    }
}

void GarbageCollector::perform_young_gc() {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Request safe point
    wait_for_safepoint();
    
    // Phase 1: Concurrent marking
    current_phase_.store(Phase::MARKING);
    start_concurrent_marking();
    wait_for_concurrent_marking();
    
    // Phase 2: Copy live objects
    current_phase_.store(Phase::RELOCATING);
    copy_young_survivors();
    
    // Phase 3: Update references
    current_phase_.store(Phase::UPDATING_REFS);
    update_references();
    
    // Reset eden
    heap_.young_.eden_current = heap_.young_.eden_start;
    
    // Swap survivor spaces
    std::swap(heap_.young_.active_survivor, 
              heap_.young_.active_survivor == heap_.young_.survivor1_start ? 
              heap_.young_.survivor2_start : heap_.young_.survivor1_start);
    
    // Clear card table
    WriteBarrier::clear_cards();
    
    // Release threads
    release_safepoint();
    current_phase_.store(Phase::IDLE);
    
    heap_.young_.collections.fetch_add(1);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto pause_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    total_pause_time_ms_.fetch_add(pause_time);
    max_pause_time_ms_.store(std::max(max_pause_time_ms_.load(), pause_time));
}

void GarbageCollector::mark_roots() {
    std::lock_guard<std::mutex> lock(roots_.roots_mutex);
    
    // Mark stack roots
    for (void** root : roots_.stack_roots) {
        if (*root) mark_object(*root);
    }
    
    // Mark global roots
    for (void** root : roots_.global_roots) {
        if (*root) mark_object(*root);
    }
    
    // Mark register roots (from safepoint)
    for (void** root : roots_.register_roots) {
        if (*root) mark_object(*root);
    }
}

void GarbageCollector::mark_object(void* obj) {
    if (!obj) return;
    
    ObjectHeader* header = reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(obj) - sizeof(ObjectHeader)
    );
    
    // Already marked?
    if (header->is_marked()) return;
    
    // Mark it
    header->set_marked(true);
    
    // Add to thread-local deque for processing
    int deque_idx = get_thread_deque_index();
    auto& deque = mark_deques_[deque_idx];
    {
        std::lock_guard<std::mutex> lock(deque->mutex);
        deque->deque.push_back(obj);
        deque->size.fetch_add(1, std::memory_order_relaxed);
    }
}

void GarbageCollector::process_mark_stack() {
    int deque_idx = get_thread_deque_index();
    process_mark_deque(deque_idx);
}

void GarbageCollector::process_mark_deque(int deque_index) {
    auto& my_deque = mark_deques_[deque_index];
    int steal_attempts = 0;
    const int max_steal_attempts = mark_deques_.size() * 2;
    
    while (true) {
        void* obj = nullptr;
        
        // Try to get work from own deque
        {
            std::lock_guard<std::mutex> lock(my_deque->mutex);
            if (!my_deque->deque.empty()) {
                obj = my_deque->deque.back();
                my_deque->deque.pop_back();
                my_deque->size.fetch_sub(1, std::memory_order_relaxed);
            }
        }
        
        // If no work, try to steal from others
        if (!obj && steal_attempts < max_steal_attempts) {
            steal_attempts++;
            for (size_t i = 0; i < mark_deques_.size(); ++i) {
                if (i != deque_index && steal_work(i, obj)) {
                    steal_attempts = 0;  // Reset on successful steal
                    break;
                }
            }
        }
        
        // If still no work, we're done
        if (!obj) {
            break;
        }
        
        // Process the object
        ObjectHeader* header = reinterpret_cast<ObjectHeader*>(
            static_cast<uint8_t*>(obj) - sizeof(ObjectHeader)
        );
        
        // Get type info to find references
        TypeInfo* type_info = type_registry_.get_type(header->type_id);
        if (!type_info) continue;
        
        // Process all reference fields
        for (size_t offset : type_info->ref_offsets) {
            void** field = reinterpret_cast<void**>(
                static_cast<uint8_t*>(obj) + offset
            );
            if (*field) mark_object(*field);
        }
    }
}

bool GarbageCollector::steal_work(int from_deque, void*& obj) {
    auto& deque = mark_deques_[from_deque];
    
    // Only steal if they have enough work
    if (deque->size.load(std::memory_order_relaxed) < 2) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(deque->mutex);
    if (!deque->deque.empty()) {
        // Steal from front (opposite end from owner)
        obj = deque->deque.front();
        deque->deque.pop_front();
        deque->size.fetch_sub(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}

int GarbageCollector::get_thread_deque_index() {
    if (thread_deque_index_ == -1) {
        thread_deque_index_ = next_deque_.fetch_add(1) % mark_deques_.size();
    }
    return thread_deque_index_;
}

// Copy survivors from young generation
void GarbageCollector::copy_young_survivors() {
    // Simplified implementation - just promote marked objects
    uint8_t* scan = heap_.young_.eden_start;
    uint8_t* eden_end = heap_.young_.eden_current;
    
    while (scan < eden_end) {
        // Bounds check: ensure we have at least space for a header
        if (scan + sizeof(ObjectHeader) > eden_end) {
            std::cerr << "ERROR: Incomplete object header at end of eden space\n";
            break;
        }
        
        ObjectHeader* header = reinterpret_cast<ObjectHeader*>(scan);
        
        // Validate object size to prevent overflow
        if (header->size > GCConfig::YOUNG_GEN_SIZE || 
            header->size < sizeof(void*)) {
            std::cerr << "ERROR: Invalid object size " << header->size << " at " << scan << "\n";
            break;
        }
        
        // Validate object header integrity
        if (header->type_id == 0 || header->type_id > 10000) {
            std::cerr << "ERROR: Invalid type_id " << header->type_id << " at " << scan << "\n";
            break;
        }
        
        // Validate object flags
        if ((header->flags & ~(ObjectHeader::IS_MARKED | ObjectHeader::IS_ARRAY | 
                              ObjectHeader::IN_OLD_GEN | ObjectHeader::STACK_ALLOCATED)) != 0) {
            std::cerr << "ERROR: Invalid object flags " << std::hex << header->flags 
                      << " at " << scan << "\n";
            break;
        }
        
        // Calculate next object position with overflow check
        size_t total_object_size = sizeof(ObjectHeader) + header->size;
        
        // Check for integer overflow
        if (total_object_size < header->size) {
            std::cerr << "ERROR: Object size overflow detected\n";
            break;
        }
        
        // Ensure we don't read beyond eden space
        if (scan + total_object_size > eden_end) {
            std::cerr << "ERROR: Object extends beyond eden space\n";
            break;
        }
        
        if (header->is_marked()) {
            // Promote to old generation
            void* new_location = copy_object(header->get_object_start(), true);
            if (new_location) {
                header->forward_ptr = new_location;
            }
        }
        
        // Move to next object with alignment
        scan += total_object_size;
        scan = reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(scan) + GCConfig::OBJECT_ALIGNMENT - 1) & ~(GCConfig::OBJECT_ALIGNMENT - 1));
        
        // Final bounds check after alignment
        if (scan > eden_end) {
            break;
        }
    }
}

// Copy object to new location
void* GarbageCollector::copy_object(void* obj, bool to_old_gen) {
    ObjectHeader* header = reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(obj) - sizeof(ObjectHeader)
    );
    
    size_t total_size = sizeof(ObjectHeader) + header->size;
    total_size = (total_size + GCConfig::OBJECT_ALIGNMENT - 1) & ~(GCConfig::OBJECT_ALIGNMENT - 1);
    
    void* new_location = nullptr;
    
    if (to_old_gen) {
        std::lock_guard<std::mutex> lock(heap_.heap_mutex_);
        if (heap_.old_.current + total_size <= heap_.old_.end) {
            new_location = heap_.old_.current;
            heap_.old_.current += total_size;
            memcpy(new_location, header, total_size);
            
            ObjectHeader* new_header = reinterpret_cast<ObjectHeader*>(new_location);
            new_header->flags |= ObjectHeader::IN_OLD_GEN;
        }
    }
    
    return new_location ? reinterpret_cast<ObjectHeader*>(new_location)->get_object_start() : nullptr;
}

// Update all references to point to new locations
void GarbageCollector::update_references() {
    std::lock_guard<std::mutex> lock(roots_.roots_mutex);
    
    auto update_ref = [](void** ref_ptr) {
        if (*ref_ptr) {
            ObjectHeader* header = reinterpret_cast<ObjectHeader*>(
                static_cast<uint8_t*>(*ref_ptr) - sizeof(ObjectHeader)
            );
            if (header->forward_ptr != nullptr) {
                *ref_ptr = header->forward_ptr;
            }
        }
    };
    
    for (void** root : roots_.stack_roots) {
        update_ref(root);
    }
    
    for (void** root : roots_.global_roots) {
        update_ref(root);
    }
}

// Safepoint implementation
void GarbageCollector::wait_for_safepoint() {
    safepoint_requested_.store(true, std::memory_order_release);
    while (threads_at_safepoint_.load() < total_threads_) {
        std::this_thread::yield();
    }
}

void GarbageCollector::release_safepoint() {
    threads_at_safepoint_.store(0);
    safepoint_requested_.store(false, std::memory_order_release);
}

void GarbageCollector::safepoint_slow() {
    instance().threads_at_safepoint_.fetch_add(1);
    while (instance().safepoint_requested_.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    instance().threads_at_safepoint_.fetch_sub(1);
}

// Root management
void GarbageCollector::add_stack_root(void** root) {
    // Initialize thread-local cleanup if needed
    if (!thread_root_cleanup_) {
        thread_root_cleanup_ = new ThreadRootCleanup(this);
        
        // Register with global thread cleanup system
        ThreadLocalCleanup::register_thread();
        
        // Add this cleanup to the thread's cleanup list
        auto* thread_data = ThreadLocalCleanup::get_thread_data();
        if (thread_data) {
            thread_data->root_cleanup = static_cast<void*>(thread_root_cleanup_);
        }
        
        // Register cleanup callback for thread exit
        register_thread_cleanup_callback([](void* arg) {
            auto* cleanup = static_cast<ThreadRootCleanup*>(arg);
            if (cleanup) {
                cleanup->cleanup_all_roots();
                delete cleanup;
            }
        }, thread_root_cleanup_);
    }
    
    std::lock_guard<std::mutex> lock(roots_.roots_mutex);
    roots_.stack_roots.push_back(root);
    
    // Also add to thread-local cleanup tracking
    if (thread_root_cleanup_) {
        thread_root_cleanup_->add_root(root);
    }
}

void GarbageCollector::remove_stack_root(void** root) {
    std::lock_guard<std::mutex> lock(roots_.roots_mutex);
    auto it = std::find(roots_.stack_roots.begin(), roots_.stack_roots.end(), root);
    if (it != roots_.stack_roots.end()) {
        roots_.stack_roots.erase(it);
    }
    
    // Also remove from thread-local cleanup tracking
    if (thread_root_cleanup_) {
        thread_root_cleanup_->remove_root(root);
    }
}

void GarbageCollector::add_global_root(void** root) {
    std::lock_guard<std::mutex> lock(roots_.roots_mutex);
    roots_.global_roots.push_back(root);
}

void GarbageCollector::remove_global_root(void** root) {
    std::lock_guard<std::mutex> lock(roots_.roots_mutex);
    auto it = std::find(roots_.global_roots.begin(), roots_.global_roots.end(), root);
    if (it != roots_.global_roots.end()) {
        roots_.global_roots.erase(it);
    }
}

// GC triggers
void GarbageCollector::request_gc(bool full) {
    gc_requested_.store(true);
    gc_cv_.notify_one();
}

void GarbageCollector::perform_old_gc() {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    wait_for_safepoint();
    current_phase_.store(Phase::MARKING);
    mark_roots();
    process_mark_stack();
    
    // Compact memory and decommit unused pages
    decommit_old_generation_tail();
    
    release_safepoint();
    current_phase_.store(Phase::IDLE);
    
    heap_.old_.collections.fetch_add(1);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto pause_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    total_pause_time_ms_.fetch_add(pause_time);
    max_pause_time_ms_.store(std::max(max_pause_time_ms_.load(), static_cast<size_t>(pause_time)));
}

void GarbageCollector::perform_full_gc() {
    wait_for_safepoint();
    current_phase_.store(Phase::MARKING);
    mark_roots();
    process_mark_stack();
    heap_.young_.eden_current = heap_.young_.eden_start;
    release_safepoint();
    current_phase_.store(Phase::IDLE);
}

// Statistics
GarbageCollector::Stats GarbageCollector::get_stats() const {
    Stats stats;
    stats.young_collections = heap_.young_.collections.load();
    stats.old_collections = heap_.old_.collections.load();
    stats.total_pause_time_ms = total_pause_time_ms_.load();
    stats.max_pause_time_ms = max_pause_time_ms_.load();
    stats.total_allocated = heap_.total_allocated();
    stats.total_freed = 0;
    stats.live_objects = 0;
    return stats;
}

// Concurrent marking methods
void GarbageCollector::start_concurrent_marking() {
    if (concurrent_marker_) {
        // Collect roots
        std::vector<void*> roots;
        {
            std::lock_guard<std::mutex> lock(roots_.roots_mutex);
            for (void** root : roots_.stack_roots) {
                if (*root) roots.push_back(*root);
            }
            for (void** root : roots_.global_roots) {
                if (*root) roots.push_back(*root);
            }
        }
        
        concurrent_marker_->push_roots(roots);
        concurrent_marker_->start_concurrent_marking();
    }
}

void GarbageCollector::wait_for_concurrent_marking() {
    if (concurrent_marker_) {
        concurrent_marker_->wait_for_completion();
    }
}

// Heap usage functions
size_t GenerationalHeap::young_used() const {
    return young_.eden_current - young_.eden_start;
}

size_t GenerationalHeap::old_used() const {
    return old_.current - old_.start;
}

size_t GenerationalHeap::total_allocated() const {
    return young_used() + old_used();
}

void GenerationalHeap::decommit_unused_memory() {
    // Calculate unused memory at the end of old generation
    size_t old_size_used = old_used();
    size_t old_total_size = old_.end - old_.start;
    size_t unused_size = old_total_size - old_size_used;
    
    // Only decommit if we have significant unused memory (at least 1MB)
    const size_t MIN_DECOMMIT_SIZE = 1024 * 1024;
    if (unused_size < MIN_DECOMMIT_SIZE) {
        return;
    }
    
    // Round down to page boundary
    size_t page_size = 4096;
    size_t decommit_start = reinterpret_cast<uintptr_t>(old_.current);
    decommit_start = (decommit_start + page_size - 1) & ~(page_size - 1);
    
    size_t decommit_end = reinterpret_cast<uintptr_t>(old_.end);
    
    if (decommit_start < decommit_end) {
        // Use madvise to tell the OS it can reclaim these pages
        void* addr = reinterpret_cast<void*>(decommit_start);
        size_t len = decommit_end - decommit_start;
        
        if (madvise(addr, len, MADV_DONTNEED) == 0) {
            // Successfully decommitted memory
            // Note: The virtual address space is still reserved, but physical
            // memory can be reclaimed by the OS
        }
    }
}

size_t GenerationalHeap::get_unused_memory() const {
    size_t young_unused = (young_.eden_end - young_.eden_current);
    size_t old_unused = (old_.end - old_.current);
    return young_unused + old_unused;
}

// ============================================================================
// C API IMPLEMENTATION
// ============================================================================

extern "C" {

void* __gc_alloc_fast(size_t size, uint32_t type_id) {
    return GenerationalHeap::allocate_fast(size, type_id, false);
}

void* __gc_alloc_array_fast(size_t element_size, size_t count, uint32_t type_id) {
    size_t total_size = sizeof(size_t) + element_size * count; // length prefix
    return GenerationalHeap::allocate_fast(total_size, type_id, true);
}

void* __gc_alloc_stack(size_t size, uint32_t type_id) {
    // This is a marker - actual stack allocation is done by JIT
    return reinterpret_cast<void*>(0xDEADBEEF);
}

void __gc_write_barrier(void* obj, void* field, void* new_value) {
    WriteBarrier::write_ref(obj, field, new_value);
}

void __gc_safepoint() {
    GarbageCollector::safepoint_poll();
}

void __gc_register_roots(void** roots, size_t count) {
    auto& gc = GarbageCollector::instance();
    // Use thread-local cleanup to ensure automatic cleanup
    if (!GarbageCollector::thread_root_cleanup_) {
        GarbageCollector::thread_root_cleanup_ = new GarbageCollector::ThreadRootCleanup(&gc);
        register_thread_cleanup_callback([](void* arg) {
            auto* cleanup = static_cast<GarbageCollector::ThreadRootCleanup*>(arg);
            delete cleanup;
        }, GarbageCollector::thread_root_cleanup_);
    }
    
    for (size_t i = 0; i < count; ++i) {
        GarbageCollector::thread_root_cleanup_->add_root(&roots[i]);
    }
}

void __gc_unregister_roots(void** roots, size_t count) {
    if (GarbageCollector::thread_root_cleanup_) {
        for (size_t i = 0; i < count; ++i) {
            GarbageCollector::thread_root_cleanup_->remove_root(&roots[i]);
        }
    }
}

void __gc_register_type(uint32_t type_id, size_t size, void* vtable, 
                       uint32_t ref_offsets[], size_t ref_count) {
    // Register type information for GC
    TypeInfo info;
    info.type_id = type_id;
    info.size = size;
    info.vtable = vtable;
    for (size_t i = 0; i < ref_count; ++i) {
        info.ref_offsets.push_back(ref_offsets[i]);
    }
    
    GarbageCollector::instance().type_registry_.register_type(info);
}

} // extern "C"

// ============================================================================
// WRITE BARRIER IMPLEMENTATION
// ============================================================================

void WriteBarrier::scan_dirty_cards(std::function<void(void*, void*)> callback) {
    auto& gc = GarbageCollector::instance();
    auto& heap = gc.heap_;
    
    OptimizedWriteBarrier::scan_dirty_cards_simd([&callback, &heap](uintptr_t card_start) {
        uintptr_t card_end = card_start + GCConfig::CARD_SIZE;
        
        // Only scan if card is in old generation range
        if (card_start < reinterpret_cast<uintptr_t>(heap.old_.start) ||
            card_start >= reinterpret_cast<uintptr_t>(heap.old_.current)) {
            return;
        }
        
        // Walk objects in card with proper bounds checking
        uintptr_t addr = card_start;
        while (addr < card_end && addr < reinterpret_cast<uintptr_t>(heap.old_.current)) {
            // Ensure we have space for header
            if (addr + sizeof(ObjectHeader) > reinterpret_cast<uintptr_t>(heap.old_.current)) {
                break;
            }
            
            ObjectHeader* header = reinterpret_cast<ObjectHeader*>(addr);
            
            // Validate header
            if (header->size == 0 || header->size > GCConfig::OLD_GEN_SIZE) {
                break; // Invalid object, stop scanning this card
            }
            
            if (header->flags & ObjectHeader::IN_OLD_GEN) {
                callback(header->get_object_start(), nullptr);
            }
            
            // Move to next object with alignment
            size_t total_size = sizeof(ObjectHeader) + header->size;
            total_size = (total_size + GCConfig::OBJECT_ALIGNMENT - 1) & ~(GCConfig::OBJECT_ALIGNMENT - 1);
            addr += total_size;
        }
    });
}

void WriteBarrier::clear_cards() {
    // Clear the entire card table
    if (card_table_ && card_table_size_ > 0) {
        memset(card_table_, 0, card_table_size_);
    }
    OptimizedWriteBarrier::clear_cards_batch();
}

void GarbageCollector::decommit_old_generation_tail() {
    // Compact memory by finding highest used address
    uint8_t* highest_used = heap_.old_.start;
    
    // Scan old generation to find highest live object
    uint8_t* scan = heap_.old_.start;
    while (scan < heap_.old_.current) {
        if (scan + sizeof(ObjectHeader) > heap_.old_.current) {
            break;
        }
        
        ObjectHeader* header = reinterpret_cast<ObjectHeader*>(scan);
        
        // Validate header
        if (header->size == 0 || header->size > GCConfig::OLD_GEN_SIZE) {
            break;
        }
        
        if (header->is_marked()) {
            // This object is live, update highest used
            highest_used = scan + sizeof(ObjectHeader) + header->size;
        }
        
        // Clear mark bit for next GC
        header->set_marked(false);
        
        // Move to next object
        size_t total_size = sizeof(ObjectHeader) + header->size;
        total_size = (total_size + GCConfig::OBJECT_ALIGNMENT - 1) & ~(GCConfig::OBJECT_ALIGNMENT - 1);
        scan += total_size;
    }
    
    // Update current pointer to just after highest live object
    highest_used = reinterpret_cast<uint8_t*>(
        (reinterpret_cast<uintptr_t>(highest_used) + GCConfig::OBJECT_ALIGNMENT - 1) & 
        ~(GCConfig::OBJECT_ALIGNMENT - 1)
    );
    
    if (highest_used < heap_.old_.current) {
        heap_.old_.current = highest_used;
    }
    
    // Decommit unused memory
    heap_.decommit_unused_memory();
}

} // namespace gots