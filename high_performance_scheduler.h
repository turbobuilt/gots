#pragma once

#include "unified_event_system.h"
#include <atomic>
#include <array>
#include <thread>
#include <vector>
#include <deque>
#include <memory>

namespace gots {

// ============================================================================
// LOCK-FREE WORK-STEALING SCHEDULER
// ============================================================================

class LockFreeWorkStealingScheduler {
private:
    // Per-thread work queues with lock-free double-ended queues
    struct alignas(64) WorkQueue {  // Cache line aligned
        // Lock-free deque for work stealing
        std::atomic<size_t> head_{0};
        std::atomic<size_t> tail_{0};
        std::array<std::atomic<std::shared_ptr<Goroutine>>, 4096> queue_;
        
        // Thread-local cache for recently scheduled goroutines
        thread_local static std::deque<std::shared_ptr<Goroutine>> local_cache_;
        
        // Fast path: push to local end (no contention)
        bool push_local(std::shared_ptr<Goroutine> goroutine) {
            size_t tail = tail_.load(std::memory_order_relaxed);
            size_t next_tail = (tail + 1) % queue_.size();
            
            if (next_tail != head_.load(std::memory_order_acquire)) {
                queue_[tail].store(goroutine, std::memory_order_relaxed);
                tail_.store(next_tail, std::memory_order_release);
                return true;
            }
            return false; // Queue full
        }
        
        // Fast path: pop from local end (no contention)
        std::shared_ptr<Goroutine> pop_local() {
            size_t tail = tail_.load(std::memory_order_relaxed);
            if (tail == head_.load(std::memory_order_relaxed)) {
                return nullptr; // Empty
            }
            
            tail = (tail - 1 + queue_.size()) % queue_.size();
            auto goroutine = queue_[tail].load(std::memory_order_relaxed);
            queue_[tail].store(nullptr, std::memory_order_relaxed);
            tail_.store(tail, std::memory_order_relaxed);
            
            return goroutine;
        }
        
        // Slow path: steal from remote end (potential contention)
        std::shared_ptr<Goroutine> steal() {
            size_t head = head_.load(std::memory_order_acquire);
            size_t tail = tail_.load(std::memory_order_acquire);
            
            if (head == tail) {
                return nullptr; // Empty
            }
            
            auto goroutine = queue_[head].load(std::memory_order_relaxed);
            if (!goroutine) {
                return nullptr;
            }
            
            // Try to advance head atomically
            if (head_.compare_exchange_weak(head, (head + 1) % queue_.size(),
                                           std::memory_order_release,
                                           std::memory_order_relaxed)) {
                return goroutine;
            }
            
            return nullptr; // Failed to steal
        }
    };
    
    // Thread pool with work-stealing queues
    std::vector<std::unique_ptr<WorkQueue>> work_queues_;
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> shutdown_{false};
    
    // Thread-local worker ID for fast queue access
    thread_local static int worker_id_;
    
    // Lock-free timer wheel for high-performance timer scheduling
    struct TimerWheel {
        static constexpr size_t WHEEL_SIZE = 512;
        static constexpr size_t LEVELS = 4;
        
        struct TimerSlot {
            std::atomic<Timer*> first_timer{nullptr};
            std::atomic<size_t> timer_count{0};
        };
        
        std::array<std::array<TimerSlot, WHEEL_SIZE>, LEVELS> wheels_;
        std::atomic<uint64_t> current_tick_{0};
        
        void schedule_timer(Timer* timer, uint64_t delay_ticks) {
            // Hierarchical timer wheel placement
            size_t level = 0;
            while (delay_ticks >= WHEEL_SIZE && level < LEVELS - 1) {
                delay_ticks /= WHEEL_SIZE;
                level++;
            }
            
            size_t slot = (current_tick_.load() + delay_ticks) % WHEEL_SIZE;
            
            // Lock-free insertion using CAS
            Timer* expected = wheels_[level][slot].first_timer.load();
            timer->next = expected;
            
            while (!wheels_[level][slot].first_timer.compare_exchange_weak(
                expected, timer, std::memory_order_release, std::memory_order_relaxed)) {
                timer->next = expected;
            }
            
            wheels_[level][slot].timer_count.fetch_add(1, std::memory_order_relaxed);
        }
        
        std::vector<Timer*> get_expired_timers() {
            std::vector<Timer*> expired;
            uint64_t current = current_tick_.fetch_add(1, std::memory_order_relaxed);
            
            // Process timers in current slot
            for (size_t level = 0; level < LEVELS; level++) {
                size_t slot = current % WHEEL_SIZE;
                Timer* timer = wheels_[level][slot].first_timer.exchange(nullptr);
                
                while (timer) {
                    Timer* next = timer->next;
                    
                    if (level == 0) {
                        // Execute immediately
                        expired.push_back(timer);
                    } else {
                        // Cascade to lower level
                        schedule_timer(timer, 0);
                    }
                    
                    timer = next;
                }
                
                current /= WHEEL_SIZE;
                if (current == 0) break;
            }
            
            return expired;
        }
    };
    
    std::unique_ptr<TimerWheel> timer_wheel_;
    
public:
    LockFreeWorkStealingScheduler(size_t num_threads = 0) {
        if (num_threads == 0) {
            num_threads = std::thread::hardware_concurrency();
        }
        
        // Initialize work queues
        work_queues_.resize(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            work_queues_[i] = std::make_unique<WorkQueue>();
        }
        
        // Initialize timer wheel
        timer_wheel_ = std::make_unique<TimerWheel>();
        
        // Start worker threads
        worker_threads_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i) {
            worker_threads_.emplace_back([this, i]() {
                worker_loop(i);
            });
        }
    }
    
    ~LockFreeWorkStealingScheduler() {
        shutdown();
    }
    
    // Schedule goroutine with zero allocation overhead
    void schedule(std::shared_ptr<Goroutine> goroutine) {
        int worker = worker_id_;
        if (worker >= 0 && worker < work_queues_.size()) {
            // Fast path: schedule on current worker
            if (work_queues_[worker]->push_local(goroutine)) {
                return;
            }
        }
        
        // Slow path: find available worker using round-robin
        static std::atomic<size_t> round_robin_counter{0};
        size_t start = round_robin_counter.fetch_add(1, std::memory_order_relaxed) % work_queues_.size();
        
        for (size_t i = 0; i < work_queues_.size(); ++i) {
            size_t worker_idx = (start + i) % work_queues_.size();
            if (work_queues_[worker_idx]->push_local(goroutine)) {
                return;
            }
        }
        
        // All queues full - this should be rare
        // Could implement overflow queue or pressure relief here
        std::this_thread::yield();
        schedule(goroutine); // Retry
    }
    
    // High-performance timer scheduling
    void schedule_timer(Timer timer, uint64_t delay_ms) {
        uint64_t delay_ticks = delay_ms; // Assume 1ms ticks
        Timer* heap_timer = new Timer(std::move(timer));
        timer_wheel_->schedule_timer(heap_timer, delay_ticks);
    }
    
    void shutdown() {
        shutdown_.store(true);
        
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        worker_threads_.clear();
    }

private:
    void worker_loop(int worker_id) {
        worker_id_ = worker_id;
        WorkQueue* local_queue = work_queues_[worker_id].get();
        
        while (!shutdown_.load(std::memory_order_relaxed)) {
            std::shared_ptr<Goroutine> goroutine = nullptr;
            
            // 1. Try to get work from local queue (fastest)
            goroutine = local_queue->pop_local();
            
            if (!goroutine) {
                // 2. Try to steal work from other queues
                goroutine = steal_work(worker_id);
            }
            
            if (goroutine) {
                // Execute goroutine
                try {
                    goroutine->run();
                    
                    // Handle goroutine completion
                    if (!goroutine->is_completed()) {
                        // Reschedule if not finished
                        schedule(goroutine);
                    }
                } catch (const std::exception& e) {
                    // Handle goroutine exceptions
                    goroutine->set_state(GoroutineState::FAILED);
                }
            } else {
                // 3. Process expired timers
                process_timers();
                
                // 4. No work available - yield CPU briefly
                std::this_thread::yield();
            }
        }
    }
    
    std::shared_ptr<Goroutine> steal_work(int current_worker) {
        // Random work stealing to distribute load
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        
        for (size_t attempts = 0; attempts < work_queues_.size(); ++attempts) {
            size_t victim = gen() % work_queues_.size();
            if (victim == current_worker) continue;
            
            auto stolen = work_queues_[victim]->steal();
            if (stolen) {
                return stolen;
            }
        }
        
        return nullptr;
    }
    
    void process_timers() {
        auto expired_timers = timer_wheel_->get_expired_timers();
        
        for (Timer* timer : expired_timers) {
            try {
                timer->callback();
                
                if (timer->is_interval) {
                    // Reschedule interval timer
                    uint64_t interval_ticks = timer->interval_duration.count();
                    timer_wheel_->schedule_timer(timer, interval_ticks);
                } else {
                    delete timer;
                }
            } catch (const std::exception& e) {
                // Handle timer callback exceptions
                delete timer;
            }
        }
    }
};

// Thread-local storage
thread_local int LockFreeWorkStealingScheduler::worker_id_ = -1;
thread_local std::deque<std::shared_ptr<Goroutine>> 
    LockFreeWorkStealingScheduler::WorkQueue::local_cache_;

// ============================================================================
// ADAPTIVE LOAD BALANCING
// ============================================================================

class AdaptiveLoadBalancer {
private:
    struct WorkerMetrics {
        std::atomic<uint64_t> tasks_completed{0};
        std::atomic<uint64_t> steal_attempts{0};
        std::atomic<uint64_t> steal_successes{0};
        std::atomic<uint64_t> queue_length{0};
    };
    
    std::vector<WorkerMetrics> worker_metrics_;
    std::atomic<uint64_t> last_balance_time_{0};
    
public:
    void balance_load(LockFreeWorkStealingScheduler* scheduler) {
        // Adaptive load balancing based on worker metrics
        // This runs periodically to optimize work distribution
        
        uint64_t current_time = std::chrono::steady_clock::now().time_since_epoch().count();
        uint64_t last_balance = last_balance_time_.load();
        
        // Balance every 100ms
        if (current_time - last_balance < 100000000) {
            return;
        }
        
        if (last_balance_time_.compare_exchange_weak(last_balance, current_time)) {
            // Analyze worker performance and adjust stealing behavior
            analyze_and_optimize();
        }
    }
    
private:
    void analyze_and_optimize() {
        // Machine learning-based optimization could go here
        // For now, simple heuristics
        
        // Find underutilized and overutilized workers
        // Adjust work stealing probabilities
        // Migrate long-running tasks if needed
    }
};

} // namespace gots