#include "goroutine_event_system.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <cstring>

namespace gots {

// Thread-local current goroutine
thread_local std::shared_ptr<Goroutine> current_goroutine = nullptr;

// ============================================================================
// GOROUTINE IMPLEMENTATION
// ============================================================================

Goroutine::Goroutine(uint64_t id, std::function<void()> task, std::shared_ptr<Goroutine> parent)
    : id_(id), main_task_(std::move(task)), parent_(parent) {
    
    // Increment parent's child count if we have a parent
    if (parent) {
        parent->child_count_.fetch_add(1);
    }
    
}

Goroutine::~Goroutine() {
    // Signal exit and ensure clean shutdown
    should_exit_loop_.store(true);
    
    // Wake up event loop without holding locks
    event_cv_.notify_all();
    
    // Wait for thread to complete if we're not on the same thread
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) {
        thread_.join();
    } else if (thread_.joinable()) {
        // Detach if we're trying to join ourselves
        thread_.detach();
    }
    
}

void Goroutine::start() {
    state_.store(GoroutineState::RUNNING);
    
    // Need to ensure shared_ptr is properly set up before using shared_from_this()
    auto self = shared_from_this();
    thread_ = std::thread([self]() {
        self->run();
    });
    
}

void Goroutine::run() {
    // Set thread-local context
    set_current_goroutine(shared_from_this());
    
    
    try {
        // Execute main task
        if (main_task_) {
            main_task_();
        }
        
        // Main task finished, but we may need to wait for children/async
        state_.store(GoroutineState::FINISHED_WAITING_FOR_CHILDREN);
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Goroutine " << id_ << " main task failed: " << e.what() << std::endl;
        state_.store(GoroutineState::FINISHED_WAITING_FOR_CHILDREN);
    }
    
    // Run event loop regardless of main task success/failure
    run_event_loop();
    
    // Notify parent we're completely done
    notify_parent_completion();
    
    state_.store(GoroutineState::COMPLETED);
}

void Goroutine::run_event_loop() {
    
    std::unique_lock<std::mutex> lock(event_mutex_);
    
    while (should_continue_event_loop()) {
        std::cout << "  - Async events: " << async_events_.size() << std::endl;
        std::cout << "  - Timers: " << timers_.size() << std::endl;
        std::cout << "  - Async handles: " << pending_async_handles_.size() << std::endl;
        std::cout << "  - Children: " << child_count_.load() << std::endl;
        
        // PRIORITY 1: Process async events immediately (no waiting)
        if (!async_events_.empty()) {
            auto event = async_events_.front();
            async_events_.pop();
            
            
            // Unlock while executing callback to prevent deadlock
            lock.unlock();
            try {
                event.callback();
            } catch (const std::exception& e) {
                std::cerr << "ERROR: Async event callback failed: " << e.what() << std::endl;
            }
            lock.lock();
            
            // After each async event, check for expired timers
            process_expired_timers_locked();
            continue;  // Don't wait, check for more async events
        }
        
        // PRIORITY 2: Process expired timers
        process_expired_timers_locked();
        
        // PRIORITY 3: Decide what to wait for
        auto next_timer_time = get_next_timer_time_locked();
        
        if (next_timer_time.has_value()) {
            // Wait until next timer OR early wake-up
            auto sleep_duration = *next_timer_time - std::chrono::steady_clock::now();
            auto sleep_ms = std::chrono::duration_cast<std::chrono::milliseconds>(sleep_duration).count();
            
            
            event_cv_.wait_until(lock, *next_timer_time, [this] {
                return should_exit_loop_.load() || 
                       !async_events_.empty() || 
                       has_timer_changes_.load();
            });
            
            has_timer_changes_.store(false);  // Reset flag
            
        } else if (has_pending_async_handles()) {
            // No timers, but we have servers/etc. waiting for events
            event_cv_.wait(lock, [this] {
                return should_exit_loop_.load() || 
                       !async_events_.empty() ||
                       has_timer_changes_.load();
            });
            
        } else {
            // No timers, no async handles - can we exit?
            if (can_exit_event_loop()) {
                break;
            } else {
                // Waiting for children to complete
                event_cv_.wait(lock, [this] {
                    return should_exit_loop_.load() || 
                           can_exit_event_loop();
                });
            }
        }
    }
    
}

bool Goroutine::should_continue_event_loop() const {
    if (should_exit_loop_.load()) {
        return false;
    }
    
    bool has_async_events = !async_events_.empty();
    bool has_timers = !timers_.empty();
    bool has_handles = !pending_async_handles_.empty();
    bool has_children = child_count_.load() > 0;
    
    bool should_continue = has_async_events || has_timers || has_handles || has_children;
    
              << " (events:" << has_async_events 
              << " timers:" << has_timers 
              << " handles:" << has_handles 
              << " children:" << has_children << ")" << std::endl;
    
    return should_continue;
}

bool Goroutine::can_exit_event_loop() const {
    bool can_exit = async_events_.empty() &&           // No pending events
                   timers_.empty() &&                  // No pending timers
                   pending_async_handles_.empty() &&   // No active handles
                   child_count_.load() == 0;           // No children
                   
    return can_exit;
}

bool Goroutine::has_pending_async_handles() const {
    return !pending_async_handles_.empty();
}

// ============================================================================
// TIMER MANAGEMENT
// ============================================================================

uint64_t Goroutine::add_timer(uint64_t delay_ms, std::function<void()> callback, bool is_interval) {
    auto timer_id = next_timer_id_.fetch_add(1);
    auto expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
    
    
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        
        // Check if this timer is earlier than current next timer
        bool is_earlier = timers_.empty() || expiry < timers_.top().expiry;
        
        timers_.emplace(Timer{
            timer_id, 
            expiry, 
            callback, 
            is_interval,
            std::chrono::milliseconds(delay_ms)
        });
        
        if (is_earlier) {
            has_timer_changes_.store(true);  // Signal early wake-up needed
        }
    }
    
    // Wake up event loop if timer is earlier or if loop is waiting
    trigger_event_loop();
    return timer_id;
}

bool Goroutine::cancel_timer(uint64_t timer_id) {
    
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        cancelled_timers_.insert(timer_id);
        has_timer_changes_.store(true);  // Signal to recheck timers
    }
    
    trigger_event_loop();
    return true;
}

void Goroutine::process_expired_timers_locked() {
    auto now = std::chrono::steady_clock::now();
    std::vector<Timer> expired_timers;
    
    // Clean up cancelled timers first
    clean_cancelled_timers_locked();
    
    // Collect expired timers
    while (!timers_.empty() && timers_.top().expiry <= now) {
        auto timer = timers_.top();
        timers_.pop();
        
        // Check if this timer was cancelled
        if (cancelled_timers_.find(timer.id) == cancelled_timers_.end()) {
            expired_timers.push_back(timer);
        } else {
            // Remove from cancelled set
            cancelled_timers_.erase(timer.id);
        }
    }
    
    // Execute expired timers (unlock during execution to prevent deadlock)
    for (auto& timer : expired_timers) {
        
        event_mutex_.unlock();
        try {
            timer.callback();
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Timer callback failed: " << e.what() << std::endl;
        }
        event_mutex_.lock();
        
        // Reschedule if interval timer (but check if cancelled first)
        if (timer.is_interval) {
            // Check if this interval was cancelled during callback execution
            if (cancelled_timers_.find(timer.id) == cancelled_timers_.end()) {
                auto next_expiry = now + timer.interval_duration;
                timers_.emplace(Timer{
                    timer.id,
                    next_expiry,
                    timer.callback,
                    true,
                    timer.interval_duration
                });
            } else {
                // Timer was cancelled during execution, remove from cancelled set
                cancelled_timers_.erase(timer.id);
            }
        }
    }
}

std::optional<std::chrono::steady_clock::time_point> Goroutine::get_next_timer_time_locked() const {
    if (timers_.empty()) {
        return std::nullopt;
    }
    return timers_.top().expiry;
}

void Goroutine::clean_cancelled_timers_locked() {
    if (cancelled_timers_.empty()) return;
    
    std::vector<Timer> remaining_timers;
    
    // Extract all timers and filter out cancelled ones
    while (!timers_.empty()) {
        auto timer = timers_.top();
        timers_.pop();
        
        if (cancelled_timers_.find(timer.id) == cancelled_timers_.end()) {
            remaining_timers.push_back(timer);
        } else {
        }
    }
    
    // Re-add non-cancelled timers
    for (const auto& timer : remaining_timers) {
        timers_.push(timer);
    }
    
    // Clear cancelled timers set
    cancelled_timers_.clear();
}

// ============================================================================
// ASYNC EVENT MANAGEMENT
// ============================================================================

void Goroutine::queue_async_event(AsyncEvent event) {
    
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        async_events_.push(event);
    }
    
    trigger_event_loop();  // Wake up immediately
}

uint64_t Goroutine::add_async_handle(AsyncHandleType type, void* handle_data) {
    auto handle_id = next_handle_id_.fetch_add(1);
    
    
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        pending_async_handles_.emplace(handle_id, type, true, handle_data);
    }
    
    trigger_event_loop();
    return handle_id;
}

void Goroutine::remove_async_handle(uint64_t handle_id) {
    
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        pending_async_handles_.erase(AsyncHandle{handle_id, AsyncHandleType::CUSTOM_HANDLE});
    }
    
    trigger_event_loop();
}

bool Goroutine::has_async_handle(uint64_t handle_id) const {
    std::lock_guard<std::mutex> lock(event_mutex_);
    return pending_async_handles_.find(AsyncHandle{handle_id, AsyncHandleType::CUSTOM_HANDLE}) 
           != pending_async_handles_.end();
}

// ============================================================================
// SERVER/NETWORK FUNCTIONALITY
// ============================================================================

uint64_t Goroutine::start_server(int port, std::function<void(int)> handler) {
    auto handle_id = add_async_handle(AsyncHandleType::SERVER_LISTENING);
    
    
    // Start server in background thread
    std::thread([this, handle_id, port, handler]() {
        run_server_thread(handle_id, port, handler);
    }).detach();
    
    return handle_id;
}

void Goroutine::stop_server(uint64_t server_id) {
    remove_async_handle(server_id);
}

void Goroutine::run_server_thread(uint64_t handle_id, int port, std::function<void(int)> handler) {
    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "ERROR: Failed to create server socket" << std::endl;
        return;
    }
    
    // Set socket options
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind and listen
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "ERROR: Failed to bind server socket to port " << port << std::endl;
        close(server_fd);
        return;
    }
    
    if (listen(server_fd, 10) < 0) {
        std::cerr << "ERROR: Failed to listen on server socket" << std::endl;
        close(server_fd);
        return;
    }
    
    
    // Accept connections while server is active
    while (server_running(handle_id)) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd >= 0) {
            
            // Queue async event for main goroutine
            auto event_id = next_event_id_.fetch_add(1);
            queue_async_event(AsyncEvent{
                event_id,
                AsyncEventType::SERVER_REQUEST,
                [handler, client_fd]() { 
                    handler(client_fd); 
                },
                nullptr
            });
        } else {
            // Accept failed, probably because we're shutting down
            if (server_running(handle_id)) {
                std::cerr << "ERROR: Accept failed on server socket" << std::endl;
            }
            break;
        }
    }
    
    close(server_fd);
}

bool Goroutine::server_running(uint64_t handle_id) {
    return has_async_handle(handle_id);
}

// ============================================================================
// PARENT-CHILD COORDINATION
// ============================================================================

std::shared_ptr<Goroutine> Goroutine::spawn_child(std::function<void()> task) {
    auto child_id = MainProgramController::instance().get_next_goroutine_id();
    auto child = std::make_shared<Goroutine>(child_id, task, shared_from_this());
    
    
    // Start child immediately
    child->start();
    
    return child;
}

void Goroutine::child_completed() {
    int remaining = child_count_.fetch_sub(1) - 1;
    
    if (remaining == 0) {
        // All children done, wake up event loop
        trigger_event_loop();
    }
}

void Goroutine::notify_parent_completion() {
    if (auto parent = parent_.lock()) {
        parent->child_completed();
    } else {
        // This is the main goroutine
        MainProgramController::instance().signal_program_completion();
    }
}

void Goroutine::signal_exit() {
    should_exit_loop_.store(true);
    trigger_event_loop();
}

void Goroutine::trigger_event_loop() {
    event_cv_.notify_one();
}

// ============================================================================
// MAIN PROGRAM CONTROLLER IMPLEMENTATION
// ============================================================================

void MainProgramController::run_main_goroutine(std::function<void()> main_task) {
    
    // Reset state for new test
    reset_for_new_test();
    
    main_goroutine_ = std::make_shared<Goroutine>(0, main_task, nullptr);
    
    // Keep reference alive during execution
    auto main_ref = main_goroutine_;
    
    // Start the main goroutine (it manages its own thread)
    main_ref->start();
    
}

void MainProgramController::wait_for_completion() {
    std::unique_lock<std::mutex> lock(completion_mutex_);
    completion_cv_.wait(lock, [this] { return program_completed_.load(); });
    
}

void MainProgramController::signal_program_completion() {
    {
        std::lock_guard<std::mutex> lock(completion_mutex_);
        program_completed_.store(true);
    }
    completion_cv_.notify_one();
}

void MainProgramController::reset_for_new_test() {
    std::lock_guard<std::mutex> lock(completion_mutex_);
    program_completed_.store(false);
    main_goroutine_.reset();  // Release previous main goroutine
}

// ============================================================================
// GLOBAL FUNCTIONS
// ============================================================================

std::shared_ptr<Goroutine> get_current_goroutine() {
    return current_goroutine;
}

void set_current_goroutine(std::shared_ptr<Goroutine> goroutine) {
    current_goroutine = goroutine;
}

std::shared_ptr<Goroutine> spawn_goroutine(std::function<void()> task) {
    if (current_goroutine) {
        return current_goroutine->spawn_child(task);
    } else {
        // Create a new root goroutine
        auto id = MainProgramController::instance().get_next_goroutine_id();
        auto goroutine = std::make_shared<Goroutine>(id, task, nullptr);
        goroutine->start();
        return goroutine;
    }
}

uint64_t setTimeout(std::function<void()> callback, uint64_t delay_ms) {
    if (current_goroutine) {
        return current_goroutine->add_timer(delay_ms, callback, false);
    }
    return 0;
}

uint64_t setInterval(std::function<void()> callback, uint64_t interval_ms) {
    if (current_goroutine) {
        return current_goroutine->add_timer(interval_ms, callback, true);
    }
    return 0;
}

bool clearTimeout(uint64_t timer_id) {
    if (current_goroutine) {
        return current_goroutine->cancel_timer(timer_id);
    }
    return false;
}

bool clearInterval(uint64_t timer_id) {
    if (current_goroutine) {
        return current_goroutine->cancel_timer(timer_id);
    }
    return false;
}

uint64_t createServer(int port, std::function<void(int)> handler) {
    if (current_goroutine) {
        return current_goroutine->start_server(port, handler);
    }
    return 0;
}

void closeServer(uint64_t server_id) {
    if (current_goroutine) {
        current_goroutine->stop_server(server_id);
    }
}

void initialize_goroutine_system() {
}

void shutdown_goroutine_system() {
}

} // namespace gots