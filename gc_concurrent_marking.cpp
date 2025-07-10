#include "gc_concurrent_marking.h"
#include "gc_memory_manager.h"
#include <chrono>
#include <algorithm>
#include <random>
#include <iostream>
#include <atomic>

namespace gots {

// ============================================================================
// WORK STEALING MARK STACK IMPLEMENTATION
// ============================================================================

WorkStealingMarkStack::WorkStealingMarkStack(int num_workers) {
    worker_queues_.reserve(num_workers);
    for (int i = 0; i < num_workers; ++i) {
        worker_queues_.push_back(std::make_unique<WorkQueue>());
    }
}

void WorkStealingMarkStack::push_work(int worker_id, void* object, int depth) {
    if (worker_id < 0 || worker_id >= static_cast<int>(worker_queues_.size())) {
        // Push to overflow queue
        std::lock_guard<std::mutex> lock(overflow_mutex_);
        overflow_queue_.push({object, depth});
        return;
    }
    
    auto& queue = worker_queues_[worker_id];
    std::lock_guard<std::mutex> lock(queue->mutex);
    queue->tasks.push({object, depth});
    queue->has_work.store(true);
}

bool WorkStealingMarkStack::pop_work(int worker_id, MarkTask& task) {
    if (worker_id < 0 || worker_id >= static_cast<int>(worker_queues_.size())) {
        return false;
    }
    
    auto& queue = worker_queues_[worker_id];
    std::lock_guard<std::mutex> lock(queue->mutex);
    
    if (!queue->tasks.empty()) {
        task = queue->tasks.front();
        queue->tasks.pop();
        if (queue->tasks.empty()) {
            queue->has_work.store(false);
        }
        return true;
    }
    
    return false;
}

bool WorkStealingMarkStack::steal_work(int worker_id, MarkTask& task) {
    // Try to steal from other workers
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    
    std::vector<int> candidates;
    for (int i = 0; i < static_cast<int>(worker_queues_.size()); ++i) {
        if (i != worker_id && worker_queues_[i]->has_work.load()) {
            candidates.push_back(i);
        }
    }
    
    if (candidates.empty()) {
        // Try overflow queue
        std::lock_guard<std::mutex> lock(overflow_mutex_);
        if (!overflow_queue_.empty()) {
            task = overflow_queue_.front();
            overflow_queue_.pop();
            return true;
        }
        return false;
    }
    
    // Randomly pick a victim to steal from
    std::uniform_int_distribution<> dis(0, candidates.size() - 1);
    int victim = candidates[dis(gen)];
    
    auto& queue = worker_queues_[victim];
    std::lock_guard<std::mutex> lock(queue->mutex);
    
    if (!queue->tasks.empty()) {
        task = queue->tasks.front();
        queue->tasks.pop();
        if (queue->tasks.empty()) {
            queue->has_work.store(false);
        }
        return true;
    }
    
    return false;
}

bool WorkStealingMarkStack::is_marking_complete() const {
    if (!marking_done_.load()) return false;
    
    // Check if any worker has work
    for (const auto& queue : worker_queues_) {
        if (queue->has_work.load()) return false;
    }
    
    // Check overflow queue
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(overflow_mutex_));
    return overflow_queue_.empty();
}

void WorkStealingMarkStack::finish_marking() {
    marking_done_.store(true);
}

void WorkStealingMarkStack::reset() {
    marking_done_.store(false);
    active_workers_.store(0);
    
    for (auto& queue : worker_queues_) {
        std::lock_guard<std::mutex> lock(queue->mutex);
        while (!queue->tasks.empty()) {
            queue->tasks.pop();
        }
        queue->has_work.store(false);
    }
    
    std::lock_guard<std::mutex> lock(overflow_mutex_);
    while (!overflow_queue_.empty()) {
        overflow_queue_.pop();
    }
}

// ============================================================================
// CONCURRENT MARKER IMPLEMENTATION
// ============================================================================

ConcurrentMarker::ConcurrentMarker(int worker_id, WorkStealingMarkStack& mark_stack, GarbageCollector& gc)
    : worker_id_(worker_id), mark_stack_(mark_stack), gc_(gc) {}

void ConcurrentMarker::mark_loop() {
    mark_stack_.active_workers_.fetch_add(1);
    
    while (!should_stop_.load()) {
        WorkStealingMarkStack::MarkTask task;
        bool found_work = false;
        
        // Try to get work from own queue
        if (mark_stack_.pop_work(worker_id_, task)) {
            found_work = true;
        } 
        // Try to steal work
        else if (mark_stack_.steal_work(worker_id_, task)) {
            found_work = true;
            work_stolen_.fetch_add(1);
        }
        
        if (found_work) {
            mark_object_and_push_refs(task.object, task.depth);
            objects_marked_.fetch_add(1);
        } else {
            // No work available, check if marking is complete
            if (mark_stack_.is_marking_complete()) {
                break;
            }
            // Brief sleep to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
    
    mark_stack_.active_workers_.fetch_sub(1);
}

void ConcurrentMarker::mark_object_and_push_refs(void* obj, int depth) {
    if (!obj || !MarkingUtils::is_valid_object_pointer(obj)) return;
    
    // Mark the object atomically
    if (!MarkingUtils::mark_object_atomic(obj)) {
        return; // Already marked
    }
    
    // Get object header and type info
    ObjectHeader* header = reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(obj) - sizeof(ObjectHeader)
    );
    
    const TypeInfo* type_info = gc_.get_type_registry().get_type(header->type_id);
    if (!type_info) return;
    
    // Limit recursion depth to prevent stack overflow
    if (depth > 50) {
        return;
    }
    
    // Push references to work queue
    iterate_refs(obj, type_info, [this, depth](void* ref) {
        if (ref && MarkingUtils::is_valid_object_pointer(ref)) {
            mark_stack_.push_work(worker_id_, ref, depth + 1);
        }
    });
}

// ============================================================================
// CONCURRENT MARKING COORDINATOR IMPLEMENTATION
// ============================================================================

ConcurrentMarkingCoordinator::ConcurrentMarkingCoordinator(GarbageCollector& gc, int num_workers)
    : gc_(gc), num_workers_(num_workers) {
    
    mark_stack_ = std::make_unique<WorkStealingMarkStack>(num_workers);
    
    // Create marker workers
    markers_.reserve(num_workers);
    marker_threads_.reserve(num_workers);
    
    for (int i = 0; i < num_workers; ++i) {
        markers_.push_back(std::make_unique<ConcurrentMarker>(i, *mark_stack_, gc));
    }
    
    std::cout << "DEBUG: Created concurrent marking coordinator with " 
              << num_workers << " workers\n";
}

ConcurrentMarkingCoordinator::~ConcurrentMarkingCoordinator() {
    stop_marking();
}

void ConcurrentMarkingCoordinator::start_concurrent_marking() {
    if (marking_active_.load()) return;
    
    std::lock_guard<std::mutex> lock(marking_mutex_);
    
    if (marking_active_.load()) return; // Double-check
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Reset mark stack
    mark_stack_->reset();
    
    // Start worker threads
    for (int i = 0; i < num_workers_; ++i) {
        marker_threads_.emplace_back(&ConcurrentMarker::mark_loop, markers_[i].get());
    }
    
    marking_active_.store(true);
    
    std::cout << "DEBUG: Started concurrent marking with " << num_workers_ << " workers\n";
}

void ConcurrentMarkingCoordinator::wait_for_completion() {
    if (!marking_active_.load()) return;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Signal marking completion
    mark_stack_->finish_marking();
    
    // Wait for all workers to finish
    for (auto& thread : marker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    marker_threads_.clear();
    marking_active_.store(false);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    total_marking_time_ms_.fetch_add(duration);
    
    // Collect statistics
    size_t total_marked = 0;
    for (const auto& marker : markers_) {
        total_marked += marker->get_objects_marked();
    }
    total_objects_marked_.fetch_add(total_marked);
    
    std::cout << "DEBUG: Concurrent marking completed in " << duration 
              << "ms, marked " << total_marked << " objects\n";
}

void ConcurrentMarkingCoordinator::stop_marking() {
    if (!marking_active_.load()) return;
    
    // Stop all markers
    for (auto& marker : markers_) {
        marker->stop();
    }
    
    wait_for_completion();
}

void ConcurrentMarkingCoordinator::push_roots(const std::vector<void*>& roots) {
    for (size_t i = 0; i < roots.size(); ++i) {
        if (roots[i]) {
            int worker_id = i % num_workers_;
            mark_stack_->push_work(worker_id, roots[i], 0);
        }
    }
}

ConcurrentMarkingCoordinator::MarkingStats ConcurrentMarkingCoordinator::get_stats() const {
    MarkingStats stats;
    stats.total_objects_marked = total_objects_marked_.load();
    stats.total_time_ms = total_marking_time_ms_.load();
    stats.worker_count = num_workers_;
    
    for (const auto& marker : markers_) {
        stats.per_worker_marked.push_back(marker->get_objects_marked());
        stats.per_worker_stolen.push_back(marker->get_work_stolen());
    }
    
    return stats;
}

// ============================================================================
// INCREMENTAL MARKER IMPLEMENTATION
// ============================================================================

IncrementalMarker::IncrementalMarker(WorkStealingMarkStack& mark_stack, GarbageCollector& gc)
    : mark_stack_(mark_stack), gc_(gc) {}

void IncrementalMarker::start_incremental_marking() {
    incremental_active_.store(true);
    write_barrier_active_.store(true);
    std::cout << "DEBUG: Started incremental marking\n";
}

bool IncrementalMarker::perform_marking_increment() {
    if (!incremental_active_.load()) return false;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    size_t objects_marked = 0;
    size_t work_budget = work_budget_.load();
    size_t time_budget_us = time_budget_us_.load();
    
    WorkStealingMarkStack::MarkTask task;
    
    while (objects_marked < work_budget) {
        // Check time budget
        auto current_time = std::chrono::high_resolution_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
            current_time - start_time).count();
        
        if (elapsed_us >= static_cast<long>(time_budget_us)) {
            break;
        }
        
        // Try to get work
        if (mark_stack_.pop_work(0, task)) {
            // Mark object and push references
            if (MarkingUtils::mark_object_atomic(task.object)) {
                ObjectHeader* header = reinterpret_cast<ObjectHeader*>(
                    static_cast<uint8_t*>(task.object) - sizeof(ObjectHeader)
                );
                
                const TypeInfo* type_info = gc_.get_type_registry().get_type(header->type_id);
                if (type_info) {
                    iterate_refs(task.object, type_info, [this](void* ref) {
                        if (ref && MarkingUtils::is_valid_object_pointer(ref)) {
                            mark_stack_.push_work(0, ref, 0);
                        }
                    });
                }
                objects_marked++;
            }
        } else if (mark_stack_.is_marking_complete()) {
            // No more work, marking is complete
            complete_marking();
            return false;
        } else {
            // No work available now, but might be more later
            break;
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();
    
    std::cout << "DEBUG: Incremental marking: " << objects_marked 
              << " objects in " << duration_us << "μs\n";
    
    return true;
}

void IncrementalMarker::handle_write_barrier(void* obj, void* field, void* new_value) {
    if (!write_barrier_active_.load() || !new_value) return;
    
    // Add the new reference to be marked
    std::lock_guard<std::mutex> lock(write_barrier_mutex_);
    write_barrier_queue_.push(new_value);
}

void IncrementalMarker::complete_marking() {
    // Process any remaining write barrier objects
    std::lock_guard<std::mutex> lock(write_barrier_mutex_);
    while (!write_barrier_queue_.empty()) {
        void* obj = write_barrier_queue_.front();
        write_barrier_queue_.pop();
        mark_stack_.push_work(0, obj, 0);
    }
    
    incremental_active_.store(false);
    write_barrier_active_.store(false);
    
    std::cout << "DEBUG: Incremental marking completed\n";
}

// ============================================================================
// MARKING UTILITIES IMPLEMENTATION
// ============================================================================

bool MarkingUtils::is_young_object(void* obj) {
    // This would check if object is in young generation
    // Simplified implementation
    return true; // Assume all objects are young for now
}

bool MarkingUtils::is_old_object(void* obj) {
    ObjectHeader* header = reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(obj) - sizeof(ObjectHeader)
    );
    return header->flags & ObjectHeader::IN_OLD_GEN;
}

bool MarkingUtils::mark_object_atomic(void* obj) {
    if (!obj) return false;
    
    ObjectHeader* header = reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(obj) - sizeof(ObjectHeader)
    );
    
    // Validate object header is within heap bounds
    if (!is_valid_object_pointer(obj)) {
        return false;
    }
    
    // Atomic test-and-set for marked flag using compare-and-swap
    std::atomic<uint8_t>* atomic_flags = reinterpret_cast<std::atomic<uint8_t>*>(&header->flags);
    uint8_t old_flags = atomic_flags->load(std::memory_order_relaxed);
    
    while (true) {
        // Already marked?
        if (old_flags & ObjectHeader::MARKED) {
            return false; // Already marked by another thread
        }
        
        // Try to set marked flag atomically
        uint8_t new_flags = old_flags | ObjectHeader::MARKED;
        
        // Use compare-and-swap to atomically update flags
        if (atomic_flags->compare_exchange_weak(old_flags, new_flags, 
                                               std::memory_order_release, 
                                               std::memory_order_relaxed)) {
            return true; // Successfully marked
        }
        
        // CAS failed, old_flags now contains the current value, retry
    }
}

size_t MarkingUtils::get_object_total_size(void* obj) {
    ObjectHeader* header = reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(obj) - sizeof(ObjectHeader)
    );
    return sizeof(ObjectHeader) + header->size;
}

bool MarkingUtils::is_valid_object_pointer(void* obj) {
    if (!obj) return false;
    
    // Basic pointer validation
    uintptr_t addr = reinterpret_cast<uintptr_t>(obj);
    
    // Check alignment
    if (addr % GCConfig::OBJECT_ALIGNMENT != 0) {
        return false;
    }
    
    // Check if pointer is within reasonable range (not too low/high)
    if (addr < 0x1000 || addr > 0x7FFFFFFFFFFF) {
        return false;
    }
    
    // Get object header and validate it's accessible
    ObjectHeader* header = reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(obj) - sizeof(ObjectHeader)
    );
    
    // Basic header validation
    if (header->size == 0 || header->size > 0x1000000) { // Max 16MB objects
        return false;
    }
    
    // Check type_id is reasonable
    if (header->type_id == 0 || header->type_id > 0xFFFF) {
        return false;
    }
    
    // TODO: Add heap bounds checking when heap regions are available
    // This would require access to GenerationalHeap instance
    
    return true;
}

size_t MarkingUtils::count_references(void* obj) {
    ObjectHeader* header = reinterpret_cast<ObjectHeader*>(
        static_cast<uint8_t*>(obj) - sizeof(ObjectHeader)
    );
    
    // This would use type information to count references
    // Simplified implementation
    return 1;
}

// ============================================================================
// ADAPTIVE MARKING IMPLEMENTATION
// ============================================================================

AdaptiveMarking::AdaptiveMarking(ParallelMarkingConfig& config) : config_(config) {
    recent_marking_times_.reserve(measurement_window_);
    recent_efficiency_scores_.reserve(measurement_window_);
}

void AdaptiveMarking::record_marking_completion(size_t time_ms, 
                                               const ConcurrentMarkingCoordinator::MarkingStats& stats) {
    recent_marking_times_.push_back(time_ms);
    recent_efficiency_scores_.push_back(calculate_efficiency(stats));
    
    // Keep only recent measurements
    if (recent_marking_times_.size() > measurement_window_) {
        recent_marking_times_.erase(recent_marking_times_.begin());
        recent_efficiency_scores_.erase(recent_efficiency_scores_.begin());
    }
}

int AdaptiveMarking::suggest_worker_adjustment() {
    if (recent_marking_times_.size() < 3) {
        return 0; // Not enough data
    }
    
    double avg_time = 0;
    double avg_efficiency = 0;
    
    for (size_t i = 0; i < recent_marking_times_.size(); ++i) {
        avg_time += recent_marking_times_[i];
        avg_efficiency += recent_efficiency_scores_[i];
    }
    
    avg_time /= recent_marking_times_.size();
    avg_efficiency /= recent_efficiency_scores_.size();
    
    // If marking time is too high and efficiency is good, add workers
    if (avg_time > config_.target_marking_time_ms && 
        avg_efficiency > config_.worker_efficiency_threshold) {
        return 1; // Add one worker
    }
    
    // If efficiency is low, reduce workers
    if (avg_efficiency < config_.worker_efficiency_threshold * 0.7) {
        return -1; // Remove one worker
    }
    
    return 0; // No change needed
}

double AdaptiveMarking::calculate_efficiency(const ConcurrentMarkingCoordinator::MarkingStats& stats) {
    if (stats.worker_count == 0 || stats.total_objects_marked == 0) {
        return 0.0;
    }
    
    // Calculate work distribution efficiency
    size_t max_worker_objects = 0;
    size_t min_worker_objects = SIZE_MAX;
    
    for (size_t objects : stats.per_worker_marked) {
        max_worker_objects = std::max(max_worker_objects, objects);
        min_worker_objects = std::min(min_worker_objects, objects);
    }
    
    if (max_worker_objects == 0) return 0.0;
    
    // Efficiency is how evenly work was distributed
    double efficiency = static_cast<double>(min_worker_objects) / max_worker_objects;
    
    return efficiency;
}

} // namespace gots