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
#include <optional>
#include <iostream>

namespace gots {

// Forward declarations
class Goroutine;
class MainProgramController;

// ============================================================================
// CORE TYPES AND ENUMS
// ============================================================================

enum class GoroutineState {
    CREATED,                           // Just created, not started
    RUNNING,                          // Executing main task + event loop
    FINISHED_WAITING_FOR_CHILDREN,   // Main task done, waiting for children
    COMPLETED                         // All children done, ready for cleanup
};

enum class AsyncEventType {
    TIMER_FIRED,
    SERVER_REQUEST,
    IO_READY,
    NETWORK_DATA,
    FILE_OPERATION,
    CUSTOM_EVENT
};

enum class AsyncHandleType {
    SERVER_LISTENING,
    FILE_WATCHING,
    NETWORK_SOCKET,
    TIMER_HANDLE,
    CUSTOM_HANDLE
};

// ============================================================================
// EVENT STRUCTURES
// ============================================================================

struct Timer {
    uint64_t id;
    std::chrono::steady_clock::time_point expiry;
    std::function<void()> callback;
    bool is_interval;
    std::chrono::milliseconds interval_duration;
    
    bool operator<(const Timer& other) const {
        return expiry > other.expiry;  // Min-heap (earliest first)
    }
    
    bool operator>(const Timer& other) const {
        return expiry > other.expiry;
    }
};

struct AsyncEvent {
    uint64_t id;
    AsyncEventType type;
    std::function<void()> callback;
    void* event_data;
    
    AsyncEvent(uint64_t id, AsyncEventType type, std::function<void()> callback, void* data = nullptr)
        : id(id), type(type), callback(std::move(callback)), event_data(data) {}
};

struct AsyncHandle {
    uint64_t id;
    AsyncHandleType type;
    bool is_active;
    void* handle_data;
    
    AsyncHandle(uint64_t id, AsyncHandleType type, bool active = true, void* data = nullptr)
        : id(id), type(type), is_active(active), handle_data(data) {}
        
    bool operator==(const AsyncHandle& other) const {
        return id == other.id;
    }
};

// Hash function for AsyncHandle in unordered_set
struct AsyncHandleHash {
    std::size_t operator()(const AsyncHandle& handle) const {
        return std::hash<uint64_t>{}(handle.id);
    }
};

// ============================================================================
// MAIN GOROUTINE CLASS
// ============================================================================

class Goroutine : public std::enable_shared_from_this<Goroutine> {
private:
    // Identity and state
    uint64_t id_;
    std::atomic<GoroutineState> state_{GoroutineState::CREATED};
    std::function<void()> main_task_;
    
    // Parent-child relationships
    std::weak_ptr<Goroutine> parent_;
    std::atomic<int> child_count_{0};
    
    // Event loop components
    std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> timers_;
    std::queue<AsyncEvent> async_events_;
    std::unordered_set<AsyncHandle, AsyncHandleHash> pending_async_handles_;
    std::unordered_set<uint64_t> cancelled_timers_;
    
    // Synchronization
    mutable std::mutex event_mutex_;
    std::condition_variable event_cv_;
    std::atomic<bool> should_exit_loop_{false};
    std::atomic<bool> has_timer_changes_{false};
    
    // ID generators
    std::atomic<uint64_t> next_timer_id_{1};
    std::atomic<uint64_t> next_event_id_{1};
    std::atomic<uint64_t> next_handle_id_{1};
    
    // Thread handle
    std::thread thread_;
    
public:
    Goroutine(uint64_t id, std::function<void()> task, std::shared_ptr<Goroutine> parent = nullptr);
    ~Goroutine();
    
    // Basic properties
    uint64_t get_id() const { return id_; }
    GoroutineState get_state() const { return state_.load(); }
    int get_child_count() const { return child_count_.load(); }
    
    // Lifecycle management
    void start();
    void run();
    void signal_exit();
    
    // Child management
    std::shared_ptr<Goroutine> spawn_child(std::function<void()> task);
    void child_completed();
    void notify_parent_completion();
    
    // Timer management
    uint64_t add_timer(uint64_t delay_ms, std::function<void()> callback, bool is_interval = false);
    bool cancel_timer(uint64_t timer_id);
    
    // Async event management
    void queue_async_event(AsyncEvent event);
    uint64_t add_async_handle(AsyncHandleType type, void* handle_data = nullptr);
    void remove_async_handle(uint64_t handle_id);
    bool has_async_handle(uint64_t handle_id) const;
    
    // Server/network helpers
    uint64_t start_server(int port, std::function<void(int)> handler);
    void stop_server(uint64_t server_id);
    
    // Event loop control
    void trigger_event_loop();
    
private:
    // Event loop implementation
    void run_event_loop();
    bool should_continue_event_loop() const;
    bool can_exit_event_loop() const;
    bool has_pending_async_handles() const;
    
    // Timer processing
    void process_expired_timers_locked();
    std::optional<std::chrono::steady_clock::time_point> get_next_timer_time_locked() const;
    void clean_cancelled_timers_locked();
    
    // Async event processing
    void process_async_events_locked();
    
    // Background server thread helper
    void run_server_thread(uint64_t handle_id, int port, std::function<void(int)> handler);
    bool server_running(uint64_t handle_id);
};

// ============================================================================
// MAIN PROGRAM CONTROLLER
// ============================================================================

class MainProgramController {
private:
    std::shared_ptr<Goroutine> main_goroutine_;
    std::mutex completion_mutex_;
    std::condition_variable completion_cv_;
    std::atomic<bool> program_completed_{false};
    std::atomic<uint64_t> next_goroutine_id_{1};
    
public:
    static MainProgramController& instance() {
        static MainProgramController instance;
        return instance;
    }
    
    void run_main_goroutine(std::function<void()> main_task);
    void wait_for_completion();
    void signal_program_completion();
    void reset_for_new_test();
    uint64_t get_next_goroutine_id() { return next_goroutine_id_.fetch_add(1); }
    
private:
    MainProgramController() = default;
};

// ============================================================================
// GLOBAL FUNCTIONS AND UTILITIES
// ============================================================================

// Thread-local current goroutine
extern thread_local std::shared_ptr<Goroutine> current_goroutine;

// Helper functions for current goroutine context
std::shared_ptr<Goroutine> get_current_goroutine();
void set_current_goroutine(std::shared_ptr<Goroutine> goroutine);

// Goroutine spawning
std::shared_ptr<Goroutine> spawn_goroutine(std::function<void()> task);

// Timer functions (work on current goroutine)
uint64_t setTimeout(std::function<void()> callback, uint64_t delay_ms);
uint64_t setInterval(std::function<void()> callback, uint64_t interval_ms);
bool clearTimeout(uint64_t timer_id);
bool clearInterval(uint64_t timer_id);

// Server functions
uint64_t createServer(int port, std::function<void(int)> handler);
void closeServer(uint64_t server_id);

// System initialization and cleanup
void initialize_goroutine_system();
void shutdown_goroutine_system();

} // namespace gots