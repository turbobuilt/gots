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
#include <unordered_map>
#include <unordered_set>

namespace gots {

// Forward declarations
class Goroutine;
class GoroutineScheduler;

// Timer structure using function ADDRESS as ID (not pointer)
struct Timer {
    int64_t id;
    std::chrono::steady_clock::time_point execute_time;
    void* function_address;  // Use address as stable ID, not function pointer
    bool is_interval;
    int64_t interval_ms;
    
    bool operator>(const Timer& other) const {
        return execute_time > other.execute_time;
    }
};

// Async operation types for event loop
enum class AsyncOpType {
    TIMER,
    CHILD_GOROUTINE,
    SERVER_HANDLE,
    NETWORK_SOCKET,
    FILE_HANDLE,
    CUSTOM_HANDLE
};

// Async operation tracking
struct AsyncOperation {
    int64_t id;
    AsyncOpType type;
    bool is_active;
    void* handle_data;  // Custom data for the operation
    
    AsyncOperation() : id(0), type(AsyncOpType::CUSTOM_HANDLE), is_active(false), handle_data(nullptr) {}
    
    AsyncOperation(int64_t id, AsyncOpType type, void* data = nullptr) 
        : id(id), type(type), is_active(true), handle_data(data) {}
};

// Goroutine state
enum class GoroutineState {
    CREATED,
    RUNNING,
    WAITING_FOR_CHILDREN,
    COMPLETED
};

// Goroutine with Node.js-style event loop
class Goroutine : public std::enable_shared_from_this<Goroutine> {
private:
    int64_t id_;
    GoroutineState state_;
    std::thread thread_;
    
    // Event loop components
    std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> timer_queue_;
    std::unordered_map<int64_t, AsyncOperation> async_operations_;
    mutable std::mutex event_loop_mutex_;
    std::condition_variable event_loop_cv_;
    std::atomic<bool> should_exit_{false};
    std::atomic<int64_t> next_async_id_{1};
    
    // Parent-child relationships - SIMPLE design
    std::weak_ptr<Goroutine> parent_;           // Weak reference to parent
    std::atomic<int> child_count_{0};           // Simple atomic counter, not vector
    int64_t child_async_op_id_{-1};             // Async operation ID for child tracking
    
    // The function to execute
    std::function<void()> task_;
    
    // Main thread completion signal (only for main goroutine)
    bool is_main_goroutine_;
    std::mutex* main_completion_mutex_;
    std::condition_variable* main_completion_cv_;
    
public:
    Goroutine(int64_t id, std::function<void()> task, std::shared_ptr<Goroutine> parent = nullptr);
    ~Goroutine();
    
    // Start execution
    void start();
    
    // Timer management
    int64_t add_timer(int64_t delay_ms, void* function_address, bool is_interval);
    bool cancel_timer(int64_t timer_id);
    
    // Async operation management - Node.js style
    int64_t add_async_operation(AsyncOpType type, void* handle_data = nullptr);
    void complete_async_operation(int64_t async_id);
    void cancel_async_operation(int64_t async_id);
    bool has_active_operations() const;
    
    // Child management - SIMPLE
    void increment_child_count();
    void decrement_child_count();
    
    // Event loop control
    void trigger_event_loop();  // Wake up event loop for any changes
    void run_event_loop();      // Main event loop like Node.js
    
    // Get state
    GoroutineState get_state() const { return state_; }
    int64_t get_id() const { return id_; }
    
    // Signal exit
    void signal_exit();
    
    // Set as main goroutine
    void set_as_main_goroutine();
    
    // Main execution function (runs in thread)
    void run();
    
    // Cleanup when child exits
    void on_child_completed();
};

// Simple scheduler - no complex thread pool
class GoroutineScheduler {
private:
    std::unordered_map<int64_t, std::shared_ptr<Goroutine>> goroutines_;
    mutable std::mutex goroutines_mutex_;
    std::atomic<int64_t> next_goroutine_id_{1};
    std::atomic<int64_t> next_timer_id_{1};
    std::shared_ptr<Goroutine> main_goroutine_;
    
    // Main thread completion synchronization
    std::mutex main_completion_mutex_;
    std::condition_variable main_completion_cv_;
    std::atomic<bool> main_goroutine_completed_{false};
    
    // Singleton
    GoroutineScheduler();
    
public:
    ~GoroutineScheduler();
    
    // Get singleton instance
    static GoroutineScheduler& instance();
    
    // Spawn a new goroutine
    std::shared_ptr<Goroutine> spawn(std::function<void()> task, std::shared_ptr<Goroutine> parent = nullptr);
    
    // Spawn the main goroutine
    void spawn_main_goroutine(std::function<void()> task);
    
    // Wait for main goroutine to complete
    void wait_for_main_goroutine();
    
    // Signal main goroutine completion
    void signal_main_goroutine_completion();
    
    // Get next timer ID
    int64_t get_next_timer_id() { return next_timer_id_.fetch_add(1); }
    
    // Register/unregister goroutines
    void register_goroutine(std::shared_ptr<Goroutine> g);
    void unregister_goroutine(int64_t id);
    
    // Get active goroutine count
    size_t get_active_count() const;
};

// Global cancelled timers set
extern std::unordered_set<int64_t> g_cancelled_timers;
extern std::mutex g_cancelled_timers_mutex;

// Timer and async operation functions
extern "C" {
    int64_t __gots_set_timeout(void* function_address, int64_t delay_ms);
    int64_t __gots_set_interval(void* function_address, int64_t delay_ms);
    bool __gots_clear_timeout(int64_t timer_id);
    bool __gots_clear_interval(int64_t timer_id);
    
    // Async operation functions
    int64_t __gots_add_async_handle(int64_t type, void* handle_data);
    void __gots_complete_async_handle(int64_t async_id);
    void __gots_cancel_async_handle(int64_t async_id);
    
    // Main goroutine functions
    void __runtime_spawn_main_goroutine(void* function_address);
    void __runtime_wait_for_main_goroutine();
}

// Current goroutine (thread-local)
extern thread_local std::shared_ptr<Goroutine> current_goroutine;

} // namespace gots