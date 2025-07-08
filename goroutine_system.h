#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <functional>
#include <chrono>
#include <unordered_set>

namespace gots {

// Forward declarations
class Goroutine;
class GoroutineScheduler;
struct Timer;

// Timer structure
struct Timer {
    int64_t id;
    std::chrono::steady_clock::time_point execute_time;
    void* callback_ptr;  // Direct function pointer
    bool is_interval;
    int64_t interval_ms;
    
    bool operator>(const Timer& other) const {
        return execute_time > other.execute_time;
    }
};

// Goroutine state
enum class GoroutineState {
    CREATED,
    RUNNING,
    WAITING_FOR_TIMERS,
    COMPLETED,
    CLEANING_UP
};

// Goroutine class - represents a single goroutine
class Goroutine : public std::enable_shared_from_this<Goroutine> {
private:
    int64_t id_;
    GoroutineState state_;
    std::thread thread_;
    
    // Timer management
    std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> timer_queue_;
    mutable std::mutex timer_mutex_;
    std::condition_variable timer_cv_;
    std::atomic<bool> should_exit_{false};
    std::unordered_set<int64_t> cancelled_timers_;
    
    // Lexical scope management
    std::weak_ptr<Goroutine> parent_;
    std::vector<std::weak_ptr<Goroutine>> children_;
    mutable std::mutex children_mutex_;
    
    // The actual function to execute
    std::function<void()> task_;
    
    // Result promise (if needed)
    void* result_promise_;
    
public:
    Goroutine(int64_t id, std::function<void()> task, std::shared_ptr<Goroutine> parent = nullptr);
    ~Goroutine();
    
    // Start execution
    void start();
    
    // Add a timer to this goroutine
    int64_t add_timer(int64_t delay_ms, void* callback, bool is_interval);
    
    // Cancel a timer
    bool cancel_timer(int64_t timer_id);
    
    // Add a child goroutine
    void add_child(std::shared_ptr<Goroutine> child);
    
    // Remove a child goroutine
    void remove_child(std::shared_ptr<Goroutine> child);
    
    // Get state
    GoroutineState get_state() const { return state_; }
    
    // Get ID
    int64_t get_id() const { return id_; }
    
    // Check if goroutine can exit (no timers, no children)
    bool can_exit() const;
    
    // Signal exit
    void signal_exit();
    
    // Main execution function (runs in thread)
    void run();
    
    // Process timers after main execution
    void process_timers();
    
    // Cleanup self and parents if possible
    void cleanup_chain();
};

// Thread pool for goroutine execution
class GoroutineThreadPool {
private:
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> shutdown_{false};
    
public:
    explicit GoroutineThreadPool(size_t num_threads);
    ~GoroutineThreadPool();
    
    // Submit a task to the pool
    void submit(std::function<void()> task);
    
    // Shutdown the pool
    void shutdown();
    
private:
    // Worker thread function
    void worker();
};

// Global goroutine scheduler
class GoroutineScheduler {
private:
    std::unordered_map<int64_t, std::shared_ptr<Goroutine>> goroutines_;
    mutable std::mutex goroutines_mutex_;
    std::atomic<int64_t> next_goroutine_id_{1};
    std::atomic<int64_t> next_timer_id_{1};
    std::unique_ptr<GoroutineThreadPool> thread_pool_;
    std::shared_ptr<Goroutine> main_goroutine_;
    
    // Singleton
    GoroutineScheduler();
    
public:
    ~GoroutineScheduler();
    
    // Get singleton instance
    static GoroutineScheduler& instance();
    
    // Spawn a new goroutine
    std::shared_ptr<Goroutine> spawn(std::function<void()> task, std::shared_ptr<Goroutine> parent = nullptr);
    
    // Wait for all goroutines to complete
    void wait_all();
    
    // Get next timer ID
    int64_t get_next_timer_id() { return next_timer_id_.fetch_add(1); }
    
    // Register a goroutine
    void register_goroutine(std::shared_ptr<Goroutine> g);
    
    // Unregister a goroutine
    void unregister_goroutine(int64_t id);
    
    // Get active goroutine count
    size_t get_active_count() const;
    
    // Set main goroutine
    void set_main_goroutine(std::shared_ptr<Goroutine> g) { main_goroutine_ = g; }
};

// Global timer functions
extern "C" {
    int64_t __gots_set_timeout(void* callback, int64_t delay_ms);
    int64_t __gots_set_interval(void* callback, int64_t delay_ms);
    bool __gots_clear_timeout(int64_t timer_id);
    bool __gots_clear_interval(int64_t timer_id);
}

// Thread-local current goroutine
extern thread_local std::shared_ptr<Goroutine> current_goroutine;

} // namespace gots