// Demonstration of advanced goroutine features
// This shows the core concepts implemented

#include <iostream>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>
#include <deque>
#include <functional>
#include <mutex>
#include <chrono>

namespace gots {

// ============================================================================
// 1. SHARED MEMORY POOL - Zero-copy sharing between goroutines
// ============================================================================

class SimpleSharedMemoryPool {
private:
    struct MemoryBlock {
        std::atomic<int> ref_count{0};
        size_t size;
        void* data;
        
        MemoryBlock(size_t s) : size(s) {
            data = std::aligned_alloc(64, s); // 64-byte aligned
        }
        
        ~MemoryBlock() {
            std::free(data);
        }
    };
    
    std::vector<std::unique_ptr<MemoryBlock>> blocks_;
    std::mutex mutex_;
    
public:
    void* allocate(size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto block = std::make_unique<MemoryBlock>(size);
        block->ref_count.store(1);
        void* ptr = block->data;
        
        blocks_.push_back(std::move(block));
        std::cout << "SHARED MEMORY: Allocated " << size << " bytes at " << ptr << std::endl;
        return ptr;
    }
    
    void add_ref(void* ptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& block : blocks_) {
            if (block->data == ptr) {
                int old_count = block->ref_count.fetch_add(1);
                std::cout << "SHARED MEMORY: Reference count increased from " << old_count 
                          << " to " << (old_count + 1) << std::endl;
                return;
            }
        }
    }
    
    void release(void* ptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& block : blocks_) {
            if (block->data == ptr) {
                int old_count = block->ref_count.fetch_sub(1);
                std::cout << "SHARED MEMORY: Reference count decreased from " << old_count 
                          << " to " << (old_count - 1) << std::endl;
                
                if (old_count == 1) {
                    std::cout << "SHARED MEMORY: Block freed (last reference)" << std::endl;
                }
                return;
            }
        }
    }
};

// ============================================================================
// 2. LOCK-FREE QUEUE - For work stealing and communication
// ============================================================================

template<typename T>
class SimpleLockFreeQueue {
private:
    std::atomic<T*> items_[1024];
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
    
public:
    SimpleLockFreeQueue() {
        for (size_t i = 0; i < 1024; ++i) {
            items_[i].store(nullptr);
        }
    }
    
    bool enqueue(T item) {
        size_t tail = tail_.load();
        size_t next_tail = (tail + 1) % 1024;
        
        if (next_tail == head_.load()) {
            return false; // Queue full
        }
        
        T* ptr = new T(std::move(item));
        items_[tail].store(ptr);
        tail_.store(next_tail);
        
        return true;
    }
    
    bool dequeue(T& result) {
        size_t head = head_.load();
        
        if (head == tail_.load()) {
            return false; // Queue empty
        }
        
        T* ptr = items_[head].exchange(nullptr);
        if (ptr) {
            result = std::move(*ptr);
            delete ptr;
            head_.store((head + 1) % 1024);
            return true;
        }
        
        return false;
    }
};

// ============================================================================
// 3. WORK STEALING SCHEDULER - Balance load across cores
// ============================================================================

class SimpleWorkStealingScheduler {
private:
    struct Worker {
        std::deque<std::function<void()>> local_queue;
        std::mutex mutex;
        std::thread thread;
        std::atomic<bool> active{true};
        std::atomic<size_t> tasks_executed{0};
        size_t worker_id;
        
        Worker(size_t id) : worker_id(id) {}
    };
    
    std::vector<std::unique_ptr<Worker>> workers_;
    SimpleLockFreeQueue<std::function<void()>> global_queue_;
    std::atomic<bool> shutdown_{false};
    std::atomic<size_t> steals_{0};
    
    void worker_loop(Worker* worker) {
        std::cout << "WORK STEALING: Worker " << worker->worker_id << " started" << std::endl;
        
        while (!shutdown_.load()) {
            std::function<void()> task;
            bool found = false;
            
            // 1. Try local queue
            {
                std::lock_guard<std::mutex> lock(worker->mutex);
                if (!worker->local_queue.empty()) {
                    task = std::move(worker->local_queue.front());
                    worker->local_queue.pop_front();
                    found = true;
                }
            }
            
            // 2. Try global queue
            if (!found) {
                found = global_queue_.dequeue(task);
            }
            
            // 3. Try stealing
            if (!found) {
                found = try_steal(worker);
                if (found) {
                    std::lock_guard<std::mutex> lock(worker->mutex);
                    if (!worker->local_queue.empty()) {
                        task = std::move(worker->local_queue.front());
                        worker->local_queue.pop_front();
                    }
                }
            }
            
            // 4. Execute task
            if (found && task) {
                task();
                worker->tasks_executed.fetch_add(1);
            } else {
                std::this_thread::yield();
            }
        }
        
        std::cout << "WORK STEALING: Worker " << worker->worker_id << " executed " 
                  << worker->tasks_executed.load() << " tasks" << std::endl;
    }
    
    bool try_steal(Worker* thief) {
        if (workers_.size() <= 1) return false;
        
        size_t victim_id = (thief->worker_id + 1) % workers_.size();
        Worker* victim = workers_[victim_id].get();
        
        std::lock_guard<std::mutex> victim_lock(victim->mutex);
        if (victim->local_queue.size() > 1) {
            std::lock_guard<std::mutex> thief_lock(thief->mutex);
            
            // Steal half of the tasks
            size_t steal_count = victim->local_queue.size() / 2;
            for (size_t i = 0; i < steal_count; ++i) {
                thief->local_queue.push_back(std::move(victim->local_queue.back()));
                victim->local_queue.pop_back();
            }
            
            steals_.fetch_add(steal_count);
            std::cout << "WORK STEALING: Worker " << thief->worker_id << " stole " 
                      << steal_count << " tasks from worker " << victim->worker_id << std::endl;
            return true;
        }
        
        return false;
    }
    
public:
    SimpleWorkStealingScheduler(size_t num_workers = 0) {
        if (num_workers == 0) {
            num_workers = std::thread::hardware_concurrency();
        }
        
        std::cout << "WORK STEALING: Starting scheduler with " << num_workers << " workers" << std::endl;
        
        for (size_t i = 0; i < num_workers; ++i) {
            auto worker = std::make_unique<Worker>(i);
            worker->thread = std::thread(&SimpleWorkStealingScheduler::worker_loop, this, worker.get());
            workers_.push_back(std::move(worker));
        }
    }
    
    ~SimpleWorkStealingScheduler() {
        shutdown_.store(true);
        for (auto& worker : workers_) {
            if (worker->thread.joinable()) {
                worker->thread.join();
            }
        }
        
        std::cout << "WORK STEALING: Scheduler shutdown. Total steals: " << steals_.load() << std::endl;
    }
    
    void schedule(std::function<void()> task) {
        if (!global_queue_.enqueue(std::move(task))) {
            std::cout << "WORK STEALING: Global queue full!" << std::endl;
        }
    }
};

// ============================================================================
// 4. GOROUTINE POOL - Reuse goroutine contexts
// ============================================================================

class SimpleGoroutinePool {
private:
    struct PooledGoroutine {
        std::function<void()> task;
        std::atomic<bool> in_use{false};
        std::thread thread;
        std::atomic<bool> should_exit{false};
        
        void run() {
            while (!should_exit.load()) {
                if (in_use.load() && task) {
                    task();
                    in_use.store(false);
                    task = nullptr;
                }
                std::this_thread::yield();
            }
        }
    };
    
    std::vector<std::unique_ptr<PooledGoroutine>> pool_;
    std::mutex mutex_;
    size_t max_size_;
    std::atomic<size_t> reuses_{0};
    
public:
    SimpleGoroutinePool(size_t max_size = 10) : max_size_(max_size) {
        std::cout << "GOROUTINE POOL: Initialized with max size " << max_size << std::endl;
    }
    
    ~SimpleGoroutinePool() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& pooled : pool_) {
            pooled->should_exit.store(true);
            if (pooled->thread.joinable()) {
                pooled->thread.join();
            }
        }
    }
    
    bool execute(std::function<void()> task) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Find available goroutine
        for (auto& pooled : pool_) {
            bool expected = false;
            if (pooled->in_use.compare_exchange_strong(expected, true)) {
                pooled->task = std::move(task);
                reuses_.fetch_add(1);
                std::cout << "GOROUTINE POOL: Reused goroutine (total reuses: " << reuses_.load() << ")" << std::endl;
                return true;
            }
        }
        
        // Create new goroutine if under limit
        if (pool_.size() < max_size_) {
            auto pooled = std::make_unique<PooledGoroutine>();
            pooled->task = std::move(task);
            pooled->in_use.store(true);
            pooled->thread = std::thread(&PooledGoroutine::run, pooled.get());
            
            pool_.push_back(std::move(pooled));
            std::cout << "GOROUTINE POOL: Created new goroutine (pool size: " << pool_.size() << ")" << std::endl;
            return true;
        }
        
        return false; // Pool full
    }
};

} // namespace gots

// ============================================================================
// DEMONSTRATION
// ============================================================================

int main() {
    std::cout << "\n=== ADVANCED GOROUTINE FEATURES DEMONSTRATION ===" << std::endl;
    
    // 1. Shared Memory Pool Demo
    std::cout << "\n--- 1. SHARED MEMORY POOL ---" << std::endl;
    {
        gots::SimpleSharedMemoryPool pool;
        
        void* mem1 = pool.allocate(1024);
        void* mem2 = pool.allocate(2048);
        
        // Simulate sharing between goroutines
        pool.add_ref(mem1);
        pool.add_ref(mem1);
        
        // Simulate releases
        pool.release(mem1);
        pool.release(mem1);
        pool.release(mem1); // Last reference
        
        pool.release(mem2);
    }
    
    // 2. Lock-Free Queue Demo
    std::cout << "\n--- 2. LOCK-FREE QUEUE ---" << std::endl;
    {
        gots::SimpleLockFreeQueue<int> queue;
        
        // Producer
        std::thread producer([&queue]() {
            for (int i = 0; i < 10; ++i) {
                queue.enqueue(i);
                std::cout << "LOCK-FREE QUEUE: Enqueued " << i << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        
        // Consumer
        std::thread consumer([&queue]() {
            int value;
            int received = 0;
            while (received < 10) {
                if (queue.dequeue(value)) {
                    std::cout << "LOCK-FREE QUEUE: Dequeued " << value << std::endl;
                    received++;
                } else {
                    std::this_thread::yield();
                }
            }
        });
        
        producer.join();
        consumer.join();
    }
    
    // 3. Work Stealing Scheduler Demo
    std::cout << "\n--- 3. WORK STEALING SCHEDULER ---" << std::endl;
    {
        gots::SimpleWorkStealingScheduler scheduler(2);
        
        // Schedule many tasks
        for (int i = 0; i < 20; ++i) {
            scheduler.schedule([i]() {
                std::cout << "TASK " << i << " executing on worker thread" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            });
        }
        
        // Wait for tasks to complete
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    // 4. Goroutine Pool Demo
    std::cout << "\n--- 4. GOROUTINE POOL ---" << std::endl;
    {
        gots::SimpleGoroutinePool pool(3);
        
        // Execute multiple tasks
        for (int i = 0; i < 10; ++i) {
            pool.execute([i]() {
                std::cout << "GOROUTINE POOL: Task " << i << " executing" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        
        // Wait for tasks to complete
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    std::cout << "\n=== DEMONSTRATION COMPLETE ===" << std::endl;
    return 0;
}