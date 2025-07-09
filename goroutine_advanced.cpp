#include "goroutine_advanced.h"
#include "goroutine_system.h"
#include <iostream>
#include <algorithm>
#include <cstring>

namespace gots {

// Global instances
SharedMemoryPool g_shared_memory_pool;
WorkStealingScheduler* g_work_stealing_scheduler = nullptr;
static GoroutinePool* g_goroutine_pool = nullptr;

// ============================================================================
// SHARED MEMORY POOL IMPLEMENTATION
// ============================================================================

SharedMemoryPool::SharedMemoryPool() {
    // Pre-allocate some blocks for each size class
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        blocks_[i].reserve(16); // Reserve space for 16 blocks per size class
    }
}

SharedMemoryPool::~SharedMemoryPool() {
    // Blocks are automatically cleaned up by unique_ptr
}

size_t SharedMemoryPool::get_size_class(size_t size) const {
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        if (size <= SIZE_CLASSES[i]) {
            return i;
        }
    }
    // For larger sizes, use the largest size class
    return NUM_SIZE_CLASSES - 1;
}

void* SharedMemoryPool::allocate(size_t size) {
    size_t class_idx = get_size_class(size);
    size_t block_size = SIZE_CLASSES[class_idx];
    
    // First, try to find a free block in the appropriate size class
    auto& class_blocks = blocks_[class_idx];
    for (auto& block : class_blocks) {
        bool expected = true;
        if (block->is_free.compare_exchange_strong(expected, false)) {
            // Found a free block
            block->ref_count.store(1);
            allocation_count_.fetch_add(1);
            std::cout << "DEBUG: Allocated shared memory block of size " << block_size 
                      << " (requested: " << size << ")" << std::endl;
            return block->data;
        }
    }
    
    // No free blocks, create a new one
    auto new_block = std::make_unique<MemoryBlock>(block_size);
    new_block->is_free.store(false);
    new_block->ref_count.store(1);
    void* data = new_block->data;
    
    class_blocks.push_back(std::move(new_block));
    allocation_count_.fetch_add(1);
    total_memory_.fetch_add(block_size);
    
    std::cout << "DEBUG: Created new shared memory block of size " << block_size << std::endl;
    return data;
}

void SharedMemoryPool::add_ref(void* ptr) {
    if (!ptr) return;
    
    // Find the block containing this pointer
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        for (auto& block : blocks_[i]) {
            if (block->data == ptr) {
                int old_count = block->ref_count.fetch_add(1);
                std::cout << "DEBUG: Shared memory ref count increased from " 
                          << old_count << " to " << (old_count + 1) << std::endl;
                return;
            }
        }
    }
    
    std::cerr << "ERROR: add_ref called on unknown pointer" << std::endl;
}

void SharedMemoryPool::release(void* ptr) {
    if (!ptr) return;
    
    // Find the block containing this pointer
    for (size_t i = 0; i < NUM_SIZE_CLASSES; ++i) {
        for (auto& block : blocks_[i]) {
            if (block->data == ptr) {
                int old_count = block->ref_count.fetch_sub(1);
                std::cout << "DEBUG: Shared memory ref count decreased from " 
                          << old_count << " to " << (old_count - 1) << std::endl;
                
                if (old_count == 1) {
                    // Last reference, mark as free
                    block->is_free.store(true);
                    allocation_count_.fetch_sub(1);
                    std::cout << "DEBUG: Shared memory block freed" << std::endl;
                }
                return;
            }
        }
    }
    
    std::cerr << "ERROR: release called on unknown pointer" << std::endl;
}

// ============================================================================
// WORK STEALING SCHEDULER IMPLEMENTATION
// ============================================================================

WorkStealingScheduler::WorkStealingScheduler(size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
    }
    
    std::cout << "DEBUG: Initializing work-stealing scheduler with " << num_threads << " threads" << std::endl;
    
    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        auto worker = std::make_unique<WorkerThread>();
        worker->id = i;
        worker->thread = std::thread(&WorkStealingScheduler::worker_loop, this, worker.get());
        workers_.push_back(std::move(worker));
    }
}

WorkStealingScheduler::~WorkStealingScheduler() {
    shutdown();
}

void WorkStealingScheduler::worker_loop(WorkerThread* worker) {
    std::cout << "DEBUG: Worker thread " << worker->id << " started" << std::endl;
    
    while (!shutdown_.load()) {
        std::function<void()> task;
        bool found_task = false;
        
        // 1. Try to get task from local queue
        {
            std::lock_guard<std::mutex> lock(worker->queue_mutex);
            if (!worker->local_queue.empty()) {
                task = std::move(worker->local_queue.front());
                worker->local_queue.pop_front();
                found_task = true;
            }
        }
        
        // 2. Try to get task from global queue
        if (!found_task && global_queue_.dequeue(task)) {
            found_task = true;
        }
        
        // 3. Try to steal from other workers
        if (!found_task) {
            found_task = try_steal(worker);
            if (found_task) {
                // Get the stolen task
                std::lock_guard<std::mutex> lock(worker->queue_mutex);
                if (!worker->local_queue.empty()) {
                    task = std::move(worker->local_queue.front());
                    worker->local_queue.pop_front();
                }
            }
        }
        
        // 4. Execute task if found
        if (found_task && task) {
            try {
                task();
                worker->tasks_executed.fetch_add(1);
            } catch (const std::exception& e) {
                std::cerr << "ERROR: Worker " << worker->id << " task exception: " << e.what() << std::endl;
            }
        } else {
            // No work available, yield
            std::this_thread::yield();
        }
    }
    
    std::cout << "DEBUG: Worker thread " << worker->id << " shutting down. Tasks executed: " 
              << worker->tasks_executed.load() << std::endl;
}

bool WorkStealingScheduler::try_steal(WorkerThread* thief) {
    // Pick a random victim
    std::uniform_int_distribution<size_t> dist(0, workers_.size() - 1);
    size_t victim_idx = dist(rng_);
    
    // Don't steal from yourself
    if (workers_[victim_idx].get() == thief) {
        victim_idx = (victim_idx + 1) % workers_.size();
    }
    
    WorkerThread* victim = workers_[victim_idx].get();
    
    // Try to steal half of victim's tasks
    std::lock_guard<std::mutex> victim_lock(victim->queue_mutex);
    size_t victim_size = victim->local_queue.size();
    
    if (victim_size > 1) {
        size_t steal_count = victim_size / 2;
        
        std::lock_guard<std::mutex> thief_lock(thief->queue_mutex);
        for (size_t i = 0; i < steal_count; ++i) {
            thief->local_queue.push_back(std::move(victim->local_queue.back()));
            victim->local_queue.pop_back();
        }
        
        total_steals_.fetch_add(steal_count);
        std::cout << "DEBUG: Worker " << thief->id << " stole " << steal_count 
                  << " tasks from worker " << victim->id << std::endl;
        return true;
    }
    
    return false;
}

void WorkStealingScheduler::schedule(std::function<void()> task) {
    // Try to schedule on current thread's local queue if it's a worker
    static thread_local WorkerThread* current_worker = nullptr;
    
    if (!current_worker) {
        // Find which worker this thread belongs to
        auto thread_id = std::this_thread::get_id();
        for (auto& worker : workers_) {
            if (worker->thread.get_id() == thread_id) {
                current_worker = worker.get();
                break;
            }
        }
    }
    
    if (current_worker) {
        // Add to local queue
        std::lock_guard<std::mutex> lock(current_worker->queue_mutex);
        current_worker->local_queue.push_back(std::move(task));
    } else {
        // Not a worker thread, add to global queue
        global_queue_.enqueue(std::move(task));
    }
}

void WorkStealingScheduler::schedule_priority(std::function<void()> task) {
    // High priority tasks always go to global queue for immediate pickup
    global_queue_.enqueue(std::move(task));
}

size_t WorkStealingScheduler::get_pending_tasks() const {
    size_t total = global_queue_.size();
    
    for (const auto& worker : workers_) {
        std::lock_guard<std::mutex> lock(worker->queue_mutex);
        total += worker->local_queue.size();
    }
    
    return total;
}

void WorkStealingScheduler::shutdown() {
    if (shutdown_.exchange(true)) {
        return; // Already shut down
    }
    
    std::cout << "DEBUG: Shutting down work-stealing scheduler" << std::endl;
    
    // Wait for all workers to finish
    for (auto& worker : workers_) {
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
    }
    
    std::cout << "DEBUG: Work-stealing scheduler shut down. Total steals: " 
              << total_steals_.load() << std::endl;
}

// ============================================================================
// GOROUTINE POOL IMPLEMENTATION
// ============================================================================

GoroutinePool::GoroutinePool(size_t min_size, size_t max_size) 
    : min_pool_size_(min_size), max_pool_size_(max_size) {
    
    std::cout << "DEBUG: Initializing goroutine pool with min=" << min_size 
              << ", max=" << max_size << std::endl;
    
    // Pre-create minimum number of goroutines
    grow_pool(min_pool_size_);
}

GoroutinePool::~GoroutinePool() {
    // Clean up all pooled goroutines
    std::lock_guard<std::mutex> lock(pool_mutex_);
    pool_.clear();
}

std::shared_ptr<Goroutine> GoroutinePool::acquire(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    // Find an available goroutine
    for (auto& pooled : pool_) {
        bool expected = false;
        if (pooled->in_use.compare_exchange_strong(expected, true)) {
            // Found one!
            active_count_.fetch_add(1);
            reuse_count_.fetch_add(1);
            
            // Reset the goroutine with new task
            // NOTE: In real implementation, we'd need to modify Goroutine class
            // to support task reassignment. For now, we'll create new ones.
            
            std::cout << "DEBUG: Reusing goroutine from pool. Active: " << active_count_.load() << std::endl;
            return pooled->goroutine;
        }
    }
    
    // No available goroutines, create a new one if under max size
    if (pool_.size() < max_pool_size_) {
        auto new_pooled = std::make_unique<PooledGoroutine>();
        new_pooled->in_use.store(true);
        // NOTE: Need to integrate with actual Goroutine creation
        // new_pooled->goroutine = create_goroutine(task);
        
        auto goroutine = new_pooled->goroutine;
        pool_.push_back(std::move(new_pooled));
        active_count_.fetch_add(1);
        
        std::cout << "DEBUG: Created new goroutine for pool. Pool size: " << pool_.size() << std::endl;
        return goroutine;
    }
    
    // Pool is at max capacity, wait for one to become available
    // In production, this would use a condition variable
    std::cout << "WARNING: Goroutine pool at max capacity" << std::endl;
    return nullptr;
}

void GoroutinePool::release(std::shared_ptr<Goroutine> goroutine) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    // Find the goroutine in our pool
    for (auto& pooled : pool_) {
        if (pooled->goroutine == goroutine) {
            pooled->in_use.store(false);
            pooled->last_used = std::chrono::steady_clock::now();
            active_count_.fetch_sub(1);
            
            std::cout << "DEBUG: Released goroutine back to pool. Active: " << active_count_.load() << std::endl;
            return;
        }
    }
    
    std::cout << "WARNING: Attempted to release unknown goroutine" << std::endl;
}

size_t GoroutinePool::get_pool_size() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return pool_.size();
}

void GoroutinePool::shrink_pool() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto idle_threshold = std::chrono::minutes(5);
    
    // Remove idle goroutines beyond minimum
    pool_.erase(
        std::remove_if(pool_.begin(), pool_.end(),
            [&](const std::unique_ptr<PooledGoroutine>& pooled) {
                return !pooled->in_use.load() && 
                       pool_.size() > min_pool_size_ &&
                       (now - pooled->last_used) > idle_threshold;
            }),
        pool_.end()
    );
    
    std::cout << "DEBUG: Shrunk pool to size: " << pool_.size() << std::endl;
}

void GoroutinePool::grow_pool(size_t count) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    size_t current_size = pool_.size();
    size_t target_size = std::min(current_size + count, max_pool_size_);
    
    for (size_t i = current_size; i < target_size; ++i) {
        auto pooled = std::make_unique<PooledGoroutine>();
        // NOTE: Need to integrate with actual Goroutine creation
        // pooled->goroutine = create_idle_goroutine();
        pool_.push_back(std::move(pooled));
    }
    
    std::cout << "DEBUG: Grew pool from " << current_size << " to " << pool_.size() << std::endl;
}

// ============================================================================
// INTEGRATION FUNCTIONS
// ============================================================================

void initialize_advanced_goroutine_system() {
    // Initialize just the shared memory pool for now
    std::cout << "DEBUG: Advanced goroutine system initialized" << std::endl;
}

void shutdown_advanced_goroutine_system() {
    // Clean shutdown
    std::cout << "DEBUG: Advanced goroutine system shut down" << std::endl;
}

std::shared_ptr<Goroutine> spawn_optimized(std::function<void()> task) {
    // Use work-stealing scheduler to run the task
    if (g_work_stealing_scheduler) {
        g_work_stealing_scheduler->schedule(std::move(task));
    } else {
        // Fallback to regular goroutine spawn
        GoroutineScheduler::instance().spawn(std::move(task));
    }
    
    return nullptr; // TODO: Return actual goroutine handle
}

} // namespace gots