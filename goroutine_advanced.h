#pragma once

#include <atomic>
#include <vector>
#include <queue>
#include <thread>
#include <memory>
#include <functional>
#include <unordered_map>
#include <deque>
#include <random>
#include <mutex>
#include <condition_variable>

namespace gots {

// Forward declarations
class Goroutine;
class WorkStealingScheduler;

// ============================================================================
// SHARED MEMORY POOL - Zero-copy memory sharing between goroutines
// ============================================================================

class SharedMemoryPool {
private:
    struct MemoryBlock {
        std::atomic<int> ref_count;
        size_t size;
        void* data;
        std::atomic<bool> is_free;
        
        MemoryBlock(size_t s) : ref_count(0), size(s), is_free(true) {
            data = std::aligned_alloc(64, s); // 64-byte aligned for cache lines
        }
        
        ~MemoryBlock() {
            std::free(data);
        }
    };
    
    // Lock-free memory blocks organized by size classes
    static constexpr size_t SIZE_CLASSES[] = {
        64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536
    };
    static constexpr size_t NUM_SIZE_CLASSES = sizeof(SIZE_CLASSES) / sizeof(SIZE_CLASSES[0]);
    
    std::vector<std::unique_ptr<MemoryBlock>> blocks_[NUM_SIZE_CLASSES];
    std::atomic<size_t> allocation_count_{0};
    std::atomic<size_t> total_memory_{0};
    
    size_t get_size_class(size_t size) const;
    
public:
    SharedMemoryPool();
    ~SharedMemoryPool();
    
    // Allocate shared memory that can be accessed by any goroutine
    void* allocate(size_t size);
    
    // Increase reference count (called when sharing with another goroutine)
    void add_ref(void* ptr);
    
    // Decrease reference count (automatic deallocation when reaches 0)
    void release(void* ptr);
    
    // Get current stats
    size_t get_allocation_count() const { return allocation_count_.load(); }
    size_t get_total_memory() const { return total_memory_.load(); }
};

// Global shared memory pool
extern SharedMemoryPool g_shared_memory_pool;

// Global work stealing scheduler
extern WorkStealingScheduler* g_work_stealing_scheduler;

// ============================================================================
// LOCK-FREE QUEUE - For inter-goroutine communication
// ============================================================================

template<typename T>
class LockFreeQueue {
private:
    struct Node {
        std::atomic<T*> data;
        std::atomic<Node*> next;
        
        Node() : data(nullptr), next(nullptr) {}
    };
    
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    std::atomic<size_t> size_;
    
public:
    LockFreeQueue() : size_(0) {
        Node* dummy = new Node;
        head_.store(dummy);
        tail_.store(dummy);
    }
    
    ~LockFreeQueue() {
        while (Node* old_head = head_.load()) {
            head_.store(old_head->next.load());
            delete old_head;
        }
    }
    
    void enqueue(T item) {
        Node* new_node = new Node;
        T* data = new T(std::move(item));
        new_node->data.store(data);
        
        while (true) {
            Node* last = tail_.load();
            Node* next = last->next.load();
            
            if (last == tail_.load()) {
                if (next == nullptr) {
                    if (last->next.compare_exchange_weak(next, new_node)) {
                        tail_.compare_exchange_weak(last, new_node);
                        size_.fetch_add(1);
                        break;
                    }
                } else {
                    tail_.compare_exchange_weak(last, next);
                }
            }
        }
    }
    
    bool dequeue(T& result) {
        while (true) {
            Node* first = head_.load();
            Node* last = tail_.load();
            Node* next = first->next.load();
            
            if (first == head_.load()) {
                if (first == last) {
                    if (next == nullptr) {
                        return false;
                    }
                    tail_.compare_exchange_weak(last, next);
                } else {
                    T* data = next->data.load();
                    if (data == nullptr) continue;
                    
                    if (head_.compare_exchange_weak(first, next)) {
                        result = std::move(*data);
                        delete data;
                        delete first;
                        size_.fetch_sub(1);
                        return true;
                    }
                }
            }
        }
    }
    
    size_t size() const { return size_.load(); }
    bool empty() const { return size_.load() == 0; }
};

// ============================================================================
// WORK STEALING SCHEDULER - Balance load across CPU cores
// ============================================================================

class WorkStealingScheduler {
private:
    struct WorkerThread {
        std::thread thread;
        std::deque<std::function<void()>> local_queue;
        mutable std::mutex queue_mutex;  // Make mutable for const access
        std::atomic<bool> active{true};
        std::atomic<size_t> tasks_executed{0};
        size_t id;
    };
    
    std::vector<std::unique_ptr<WorkerThread>> workers_;
    LockFreeQueue<std::function<void()>> global_queue_;
    std::atomic<bool> shutdown_{false};
    std::atomic<size_t> total_steals_{0};
    std::mt19937 rng_{std::random_device{}()};
    
    void worker_loop(WorkerThread* worker);
    bool try_steal(WorkerThread* thief);
    
public:
    WorkStealingScheduler(size_t num_threads = 0); // 0 = hardware concurrency
    ~WorkStealingScheduler();
    
    // Schedule a task (prefers local queue of current thread)
    void schedule(std::function<void()> task);
    
    // Schedule high priority task (goes to global queue)
    void schedule_priority(std::function<void()> task);
    
    // Get scheduler statistics
    size_t get_total_steals() const { return total_steals_.load(); }
    size_t get_pending_tasks() const;
    
    // Shutdown scheduler
    void shutdown();
};

// ============================================================================
// GOROUTINE POOL - Reuse goroutine contexts for efficiency
// ============================================================================

class GoroutinePool {
private:
    struct PooledGoroutine {
        std::shared_ptr<Goroutine> goroutine;
        std::atomic<bool> in_use{false};
        std::chrono::steady_clock::time_point last_used;
    };
    
    std::vector<std::unique_ptr<PooledGoroutine>> pool_;
    mutable std::mutex pool_mutex_;
    size_t max_pool_size_;
    size_t min_pool_size_;
    std::atomic<size_t> active_count_{0};
    std::atomic<size_t> reuse_count_{0};
    
    void cleanup_idle_goroutines();
    
public:
    GoroutinePool(size_t min_size = 4, size_t max_size = 100);
    ~GoroutinePool();
    
    // Get a goroutine from pool (creates new if none available)
    std::shared_ptr<Goroutine> acquire(std::function<void()> task);
    
    // Return goroutine to pool for reuse
    void release(std::shared_ptr<Goroutine> goroutine);
    
    // Get pool statistics
    size_t get_pool_size() const;
    size_t get_active_count() const { return active_count_.load(); }
    size_t get_reuse_count() const { return reuse_count_.load(); }
    
    // Manual pool management
    void shrink_pool();
    void grow_pool(size_t count);
};

// ============================================================================
// LOCK-FREE CHANNEL - Type-safe inter-goroutine communication
// ============================================================================

template<typename T>
class Channel {
private:
    LockFreeQueue<T> queue_;
    std::atomic<size_t> capacity_;
    std::atomic<bool> closed_{false};
    
public:
    explicit Channel(size_t capacity = 0) : capacity_(capacity) {}
    
    // Send value to channel (blocks if at capacity)
    bool send(T value) {
        if (closed_.load()) return false;
        
        if (capacity_.load() > 0) {
            while (queue_.size() >= capacity_.load()) {
                if (closed_.load()) return false;
                std::this_thread::yield();
            }
        }
        
        queue_.enqueue(std::move(value));
        return true;
    }
    
    // Receive value from channel
    bool receive(T& value) {
        while (!queue_.dequeue(value)) {
            if (closed_.load() && queue_.empty()) {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }
    
    // Try to receive without blocking
    bool try_receive(T& value) {
        return queue_.dequeue(value);
    }
    
    // Close channel
    void close() {
        closed_.store(true);
    }
    
    bool is_closed() const {
        return closed_.load();
    }
    
    size_t size() const {
        return queue_.size();
    }
};

// ============================================================================
// INTEGRATION WITH EXISTING GOROUTINE SYSTEM
// ============================================================================

// Initialize/shutdown advanced system
void initialize_advanced_goroutine_system();
void shutdown_advanced_goroutine_system();

// Enhanced spawn function using goroutine pool and work stealing
std::shared_ptr<Goroutine> spawn_optimized(std::function<void()> task);

// Create a channel for communication
template<typename T>
std::shared_ptr<Channel<T>> make_channel(size_t capacity = 0) {
    return std::make_shared<Channel<T>>(capacity);
}

} // namespace gots