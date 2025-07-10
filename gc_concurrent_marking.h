#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include "gc_memory_manager.h"

namespace gots {

// Forward declarations
class GarbageCollector;
class ObjectHeader;

// ============================================================================
// WORK STEALING MARK STACK - Lock-free parallel marking
// ============================================================================

class WorkStealingMarkStack {
private:
    struct MarkTask {
        void* object;
        int depth;
    };
    
    // Per-thread work queues
    struct WorkQueue {
        std::queue<MarkTask> tasks;
        std::mutex mutex;
        std::atomic<bool> has_work{false};
    };
    
    std::vector<std::unique_ptr<WorkQueue>> worker_queues_;
    std::atomic<int> active_workers_{0};
    std::atomic<bool> marking_done_{false};
    
    // Global overflow queue for load balancing
    std::queue<MarkTask> overflow_queue_;
    std::mutex overflow_mutex_;
    
public:
    WorkStealingMarkStack(int num_workers);
    ~WorkStealingMarkStack() = default;
    
    // Add work to a specific worker queue
    void push_work(int worker_id, void* object, int depth = 0);
    
    // Try to steal work from another queue
    bool steal_work(int worker_id, MarkTask& task);
    
    // Get work from own queue
    bool pop_work(int worker_id, MarkTask& task);
    
    // Check if all workers are done
    bool is_marking_complete() const;
    
    // Signal that marking is done
    void finish_marking();
    
    // Reset for new marking phase
    void reset();
    
    // Get number of workers
    int get_worker_count() const { return worker_queues_.size(); }
};

// ============================================================================
// CONCURRENT MARKER - Parallel marking worker
// ============================================================================

class ConcurrentMarker {
private:
    int worker_id_;
    WorkStealingMarkStack& mark_stack_;
    GarbageCollector& gc_;
    std::atomic<bool> should_stop_{false};
    
    // Statistics
    std::atomic<size_t> objects_marked_{0};
    std::atomic<size_t> work_stolen_{0};
    
public:
    ConcurrentMarker(int worker_id, WorkStealingMarkStack& mark_stack, GarbageCollector& gc);
    
    // Main marking loop
    void mark_loop();
    
    // Mark a single object and push its references
    void mark_object_and_push_refs(void* obj, int depth);
    
    // Stop the marker
    void stop() { should_stop_.store(true); }
    
    // Get statistics
    size_t get_objects_marked() const { return objects_marked_.load(); }
    size_t get_work_stolen() const { return work_stolen_.load(); }
};

// ============================================================================
// CONCURRENT MARKING COORDINATOR - Manages parallel marking
// ============================================================================

class ConcurrentMarkingCoordinator {
private:
    std::vector<std::unique_ptr<ConcurrentMarker>> markers_;
    std::vector<std::thread> marker_threads_;
    std::unique_ptr<WorkStealingMarkStack> mark_stack_;
    
    GarbageCollector& gc_;
    int num_workers_;
    
    // Synchronization
    std::atomic<bool> marking_active_{false};
    std::condition_variable marking_cv_;
    std::mutex marking_mutex_;
    
    // Statistics
    std::atomic<size_t> total_objects_marked_{0};
    std::atomic<size_t> total_marking_time_ms_{0};
    
public:
    ConcurrentMarkingCoordinator(GarbageCollector& gc, int num_workers = std::thread::hardware_concurrency());
    ~ConcurrentMarkingCoordinator();
    
    // Start concurrent marking from roots
    void start_concurrent_marking();
    
    // Wait for marking to complete
    void wait_for_completion();
    
    // Stop all marking threads
    void stop_marking();
    
    // Push initial roots to be marked
    void push_roots(const std::vector<void*>& roots);
    
    // Check if marking is active
    bool is_marking_active() const { return marking_active_.load(); }
    
    // Get statistics
    struct MarkingStats {
        size_t total_objects_marked;
        size_t total_time_ms;
        size_t worker_count;
        std::vector<size_t> per_worker_marked;
        std::vector<size_t> per_worker_stolen;
    };
    
    MarkingStats get_stats() const;
};

// ============================================================================
// INCREMENTAL MARKING - For low-latency GC
// ============================================================================

class IncrementalMarker {
private:
    WorkStealingMarkStack& mark_stack_;
    GarbageCollector& gc_;
    
    // Incremental marking state
    std::atomic<bool> incremental_active_{false};
    std::atomic<size_t> work_budget_{1000};  // Objects to mark per increment
    std::atomic<size_t> time_budget_us_{500}; // Microseconds per increment
    
    // Write barrier support
    std::atomic<bool> write_barrier_active_{false};
    std::queue<void*> write_barrier_queue_;
    std::mutex write_barrier_mutex_;
    
public:
    IncrementalMarker(WorkStealingMarkStack& mark_stack, GarbageCollector& gc);
    
    // Start incremental marking
    void start_incremental_marking();
    
    // Perform one increment of marking
    bool perform_marking_increment();
    
    // Handle write barrier during concurrent marking
    void handle_write_barrier(void* obj, void* field, void* new_value);
    
    // Complete incremental marking
    void complete_marking();
    
    // Set budgets
    void set_work_budget(size_t objects) { work_budget_.store(objects); }
    void set_time_budget_us(size_t microseconds) { time_budget_us_.store(microseconds); }
    
    // Check if incremental marking is active
    bool is_active() const { return incremental_active_.load(); }
};

// ============================================================================
// MARKING UTILITIES - Helper functions for marking
// ============================================================================

class MarkingUtils {
public:
    // Check if object is in young generation
    static bool is_young_object(void* obj);
    
    // Check if object is in old generation
    static bool is_old_object(void* obj);
    
    // Mark object atomically (returns true if newly marked)
    static bool mark_object_atomic(void* obj);
    
    // Get object size including header
    static size_t get_object_total_size(void* obj);
    
    // Verify object pointer is valid
    static bool is_valid_object_pointer(void* obj);
    
    // Count references in object
    static size_t count_references(void* obj);
};

// ============================================================================
// PARALLEL MARKING CONFIGURATION
// ============================================================================

struct ParallelMarkingConfig {
    int num_workers = std::thread::hardware_concurrency();
    size_t work_steal_attempts = 3;
    size_t min_work_chunk_size = 10;
    bool enable_incremental = true;
    bool enable_write_barriers = true;
    size_t incremental_work_budget = 1000;
    size_t incremental_time_budget_us = 500;
    
    // Adaptive configuration
    bool enable_adaptive_workers = true;
    double target_marking_time_ms = 10.0;  // Target marking time
    double worker_efficiency_threshold = 0.8;  // When to add/remove workers
};

// ============================================================================
// ADAPTIVE MARKING - Dynamically adjust worker count
// ============================================================================

class AdaptiveMarking {
private:
    ParallelMarkingConfig& config_;
    std::vector<double> recent_marking_times_;
    std::vector<double> recent_efficiency_scores_;
    size_t measurement_window_ = 10;
    
public:
    AdaptiveMarking(ParallelMarkingConfig& config);
    
    // Record marking completion
    void record_marking_completion(size_t time_ms, const ConcurrentMarkingCoordinator::MarkingStats& stats);
    
    // Suggest worker count adjustment
    int suggest_worker_adjustment();
    
    // Calculate worker efficiency
    double calculate_efficiency(const ConcurrentMarkingCoordinator::MarkingStats& stats);
};

} // namespace gots