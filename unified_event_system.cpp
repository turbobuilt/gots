#include "unified_event_system.h"
#include "goroutine_advanced.h"
#include <iostream>
#include <algorithm>
#include <any>

// Forward declaration for global scheduler
// extern gots::WorkStealingScheduler* g_work_stealing_scheduler;

namespace gots {

// Thread-local storage
thread_local std::shared_ptr<Goroutine> current_goroutine;
thread_local std::shared_ptr<LexicalEnvironment> current_lexical_env;

// ============================================================================
// GLOBAL TIMER SYSTEM IMPLEMENTATION
// ============================================================================

uint64_t GlobalTimerSystem::set_timeout(uint64_t goroutine_id, 
                                        std::function<void()> callback, 
                                        int64_t delay_ms) {
    auto timer_id = next_timer_id_.fetch_add(1);
    auto expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
    
    // Wrap callback to handle lifecycle
    auto wrapped_callback = [goroutine_id, timer_id, callback = std::move(callback)]() {
        
        try {
            callback();
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Timer callback exception: " << e.what() << std::endl;
        }
        
        // Notify main controller that timer completed
        MainThreadController::instance().timer_completed(goroutine_id, timer_id);
    };
    
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timers_.emplace(Timer{timer_id, expiry, goroutine_id, std::move(wrapped_callback), false, {}});
        timer_to_goroutine_[timer_id] = goroutine_id;
    }
    
    // KEY: Wake up event loop for early scheduling
    timer_cv_.notify_one();
    
    // Register timer with main controller
    MainThreadController::instance().timer_started(goroutine_id, timer_id);
    
    return timer_id;
}

uint64_t GlobalTimerSystem::set_interval(uint64_t goroutine_id, 
                                         std::function<void()> callback, 
                                         int64_t interval_ms) {
    auto timer_id = next_timer_id_.fetch_add(1);
    auto expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms);
    auto interval_duration = std::chrono::milliseconds(interval_ms);
    
    // Wrap callback to handle lifecycle and rescheduling
    std::function<void()> wrapped_callback = [this, goroutine_id, timer_id, callback, interval_duration]() {
        
        try {
            callback();
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Interval callback exception: " << e.what() << std::endl;
        }
        
        // Reschedule the interval
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            auto next_expiry = std::chrono::steady_clock::now() + interval_duration;
            timers_.emplace(Timer{timer_id, next_expiry, goroutine_id, 
                                [this, goroutine_id, timer_id, callback, interval_duration]() {
                                    // Create a new wrapped callback for the next execution
                                    std::function<void()> next_callback = [this, goroutine_id, timer_id, callback, interval_duration]() {
                                        try {
                                            callback();
                                        } catch (const std::exception& e) {
                                            std::cerr << "ERROR: Interval callback exception: " << e.what() << std::endl;
                                        }
                                    };
                                    next_callback();
                                }, 
                                true, interval_duration});
        }
    };
    
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timers_.emplace(Timer{timer_id, expiry, goroutine_id, wrapped_callback, true, interval_duration});
        timer_to_goroutine_[timer_id] = goroutine_id;
    }
    
    // Register timer with main controller
    MainThreadController::instance().timer_started(goroutine_id, timer_id);
    
    return timer_id;
}

bool GlobalTimerSystem::clear_timer(uint64_t timer_id) {
    std::lock_guard<std::mutex> lock(timer_mutex_);
    
    // Add to cancelled timers set
    cancelled_timers_.insert(timer_id);
    intervals_.erase(timer_id);  // Remove interval info
    
    auto it = timer_to_goroutine_.find(timer_id);
    if (it != timer_to_goroutine_.end()) {
        uint64_t goroutine_id = it->second;
        timer_to_goroutine_.erase(it);
        
        // Notify main controller
        MainThreadController::instance().timer_completed(goroutine_id, timer_id);
        
        
        // Wake up event loop to process cancellation
        timer_cv_.notify_one();
        
        return true;
    }
    return false;
}

void GlobalTimerSystem::process_expired_timers() {
    auto now = std::chrono::steady_clock::now();
    std::vector<Timer> expired_timers;
    
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        while (!timers_.empty() && timers_.top().expiry <= now) {
            expired_timers.push_back(timers_.top());
            timers_.pop();
        }
    }
    
    // Execute expired timers outside of lock
    for (auto& timer : expired_timers) {
        if (timer.callback) {
            // Execute directly for now
            timer.callback();
        }
    }
}

std::chrono::milliseconds GlobalTimerSystem::process_expired_timers_and_get_sleep_duration() {
    auto now = std::chrono::steady_clock::now();
    std::vector<Timer> expired_timers;
    std::chrono::milliseconds sleep_duration{0};
    
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        
        // Process expired timers
        while (!timers_.empty() && timers_.top().expiry <= now) {
            expired_timers.push_back(timers_.top());
            timers_.pop();
        }
        
        // Calculate sleep duration until next timer
        if (!timers_.empty()) {
            auto next_expiry = timers_.top().expiry;
            auto duration = next_expiry - now;
            sleep_duration = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
            
            // Ensure minimum sleep time and maximum to prevent overflow
            if (sleep_duration.count() < 1) {
                sleep_duration = std::chrono::milliseconds(1);
            } else if (sleep_duration.count() > 60000) {  // Max 1 minute sleep
                sleep_duration = std::chrono::milliseconds(60000);
            }
        } else {
            // No timers, sleep for a reasonable time
            sleep_duration = std::chrono::milliseconds(1000);
        }
    }
    
    // Execute expired timers outside of lock
    for (auto& timer : expired_timers) {
        if (timer.callback) {
            timer.callback();
        }
    }
    
    return sleep_duration;
}

void GlobalTimerSystem::clear_all_timers_for_goroutine(uint64_t goroutine_id) {
    std::lock_guard<std::mutex> lock(timer_mutex_);
    
    // Remove all timers for this goroutine
    auto it = timer_to_goroutine_.begin();
    while (it != timer_to_goroutine_.end()) {
        if (it->second == goroutine_id) {
            MainThreadController::instance().timer_completed(goroutine_id, it->first);
            it = timer_to_goroutine_.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// MAIN THREAD CONTROLLER IMPLEMENTATION
// ============================================================================

void MainThreadController::goroutine_started(uint64_t goroutine_id, std::shared_ptr<Goroutine> goroutine) {
    {
        std::lock_guard<std::mutex> lock(refs_mutex_);
        goroutine_refs_[goroutine_id] = goroutine;
    }
    
    int count = active_goroutines_.fetch_add(1) + 1;
}

void MainThreadController::goroutine_completed(uint64_t goroutine_id) {
    cleanup_goroutine_references(goroutine_id);
    
    int count = active_goroutines_.fetch_sub(1) - 1;
    
    if (count == 0) {
        check_exit_condition();
    }
}

void MainThreadController::timer_started(uint64_t goroutine_id, uint64_t timer_id) {
    {
        std::lock_guard<std::mutex> lock(refs_mutex_);
        pending_timers_per_goroutine_[goroutine_id].insert(timer_id);
    }
    
    int count = pending_timers_.fetch_add(1) + 1;
}

void MainThreadController::timer_completed(uint64_t goroutine_id, uint64_t timer_id) {
    {
        std::lock_guard<std::mutex> lock(refs_mutex_);
        auto it = pending_timers_per_goroutine_.find(goroutine_id);
        if (it != pending_timers_per_goroutine_.end()) {
            it->second.erase(timer_id);
            if (it->second.empty()) {
                pending_timers_per_goroutine_.erase(it);
            }
        }
    }
    
    int count = pending_timers_.fetch_sub(1) - 1;
    
    if (count == 0) {
        check_exit_condition();
    }
}

void MainThreadController::io_operation_started() {
    int count = active_io_operations_.fetch_add(1) + 1;
}

void MainThreadController::io_operation_completed() {
    int count = active_io_operations_.fetch_sub(1) - 1;
    
    if (count == 0) {
        check_exit_condition();
    }
}

void MainThreadController::wait_for_completion() {
    
    std::unique_lock<std::mutex> lock(exit_mutex_);
    exit_cv_.wait(lock, [this]() { return should_exit_.load(); });
    
}

void MainThreadController::force_exit() {
    should_exit_.store(true);
    exit_cv_.notify_all();
}

void MainThreadController::check_exit_condition() {
    bool should_exit = (active_goroutines_.load() == 0 && 
                       pending_timers_.load() == 0 && 
                       active_io_operations_.load() == 0);
    
    if (should_exit) {
        should_exit_.store(true);
        exit_cv_.notify_all();
    }
}

void MainThreadController::cleanup_goroutine_references(uint64_t goroutine_id) {
    std::lock_guard<std::mutex> lock(refs_mutex_);
    
    // Remove goroutine reference
    goroutine_refs_.erase(goroutine_id);
    
    // Clean up any remaining timers
    auto it = pending_timers_per_goroutine_.find(goroutine_id);
    if (it != pending_timers_per_goroutine_.end()) {
        for (uint64_t timer_id : it->second) {
            pending_timers_.fetch_sub(1);
        }
        pending_timers_per_goroutine_.erase(it);
    }
}

// ============================================================================
// GLOBAL EVENT LOOP IMPLEMENTATION
// ============================================================================

void GlobalEventLoop::start(WorkStealingScheduler* scheduler) {
    if (running_.exchange(true)) {
        return;
    }
    
    scheduler_ = scheduler;
    event_thread_ = std::thread(&GlobalEventLoop::event_loop, this);
}

void GlobalEventLoop::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    
    if (event_thread_.joinable()) {
        event_thread_.join();
    }
}

void GlobalEventLoop::event_loop() {
    
    while (running_.load()) {
        try {
            // Process expired timers and get sleep duration
            auto sleep_duration = timer_system_.process_expired_timers_and_get_sleep_duration();
            
            // TODO: Add I/O event processing here
            // io_multiplexer_.process_events();
            
            if (sleep_duration.count() > 0) {
                // Sleep efficiently until next timer or I/O event
                std::this_thread::sleep_for(sleep_duration);
            }
            
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Exception in event loop: " << e.what() << std::endl;
        }
    }
    
}

// ============================================================================
// GOROUTINE MANAGER IMPLEMENTATION
// ============================================================================

void GoroutineManager::register_goroutine(uint64_t goroutine_id, std::shared_ptr<Goroutine> goroutine) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_goroutines_[goroutine_id] = goroutine;
}

void GoroutineManager::unregister_goroutine(uint64_t goroutine_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_goroutines_.erase(goroutine_id);
    pending_timers_per_goroutine_.erase(goroutine_id);
}

void GoroutineManager::add_timer_reference(uint64_t goroutine_id, uint64_t timer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_timers_per_goroutine_[goroutine_id].insert(timer_id);
}

void GoroutineManager::remove_timer_reference(uint64_t goroutine_id, uint64_t timer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pending_timers_per_goroutine_.find(goroutine_id);
    if (it != pending_timers_per_goroutine_.end()) {
        it->second.erase(timer_id);
        if (it->second.empty()) {
            pending_timers_per_goroutine_.erase(it);
        }
    }
}

std::shared_ptr<Goroutine> GoroutineManager::get_goroutine(uint64_t goroutine_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = active_goroutines_.find(goroutine_id);
    return (it != active_goroutines_.end()) ? it->second : nullptr;
}

bool GoroutineManager::is_goroutine_active(uint64_t goroutine_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_goroutines_.find(goroutine_id) != active_goroutines_.end();
}

size_t GoroutineManager::get_active_count() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    return active_goroutines_.size();
}

void GoroutineManager::cleanup_completed_goroutines() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_goroutines_.begin();
    while (it != active_goroutines_.end()) {
        if (it->second->is_completed()) {
            it = active_goroutines_.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// ENHANCED GOROUTINE IMPLEMENTATION
// ============================================================================

Goroutine::Goroutine(uint64_t id, std::shared_ptr<LexicalEnvironment> env) 
    : id_(id), lexical_env_(env) {
    // shared_ptr handles reference counting automatically
}

Goroutine::~Goroutine() {
    // shared_ptr handles reference counting automatically
}

void Goroutine::run() {
    
    // Set thread-local context
    set_current_goroutine(shared_from_this());
    set_current_lexical_env(lexical_env_);
    
    state_.store(GoroutineState::RUNNING);
    
    try {
        if (main_task_) {
            main_task_();
        }
        
        state_.store(GoroutineState::COMPLETED);
        
    } catch (const std::exception& e) {
        state_.store(GoroutineState::FAILED);
        std::cerr << "ERROR: Goroutine " << id_ << " failed: " << e.what() << std::endl;
    }
    
    // Clean up any remaining timers
    GlobalTimerSystem::instance().clear_all_timers_for_goroutine(id_);
    
    // Notify main controller
    MainThreadController::instance().goroutine_completed(id_);
    
}

std::shared_ptr<Goroutine> Goroutine::spawn_child(std::function<void()> task) {
    // Create child goroutine with inherited lexical environment
    static std::atomic<uint64_t> next_id{1};
    uint64_t child_id = next_id.fetch_add(1);
    
    auto child = std::make_shared<Goroutine>(child_id, lexical_env_);
    child->parent_ = weak_from_this();
    child->set_main_task(task);
    
    {
        std::lock_guard<std::mutex> lock(children_mutex_);
        child_goroutines_.push_back(child);
    }
    
    child_count_.fetch_add(1);
    
    
    // Register with systems
    GoroutineManager::instance().register_goroutine(child_id, child);
    MainThreadController::instance().goroutine_started(child_id, child);
    
    return child;
}

void Goroutine::child_completed() {
    int remaining = child_count_.fetch_sub(1) - 1;
}

void Goroutine::suspend() {
    state_.store(GoroutineState::SUSPENDED);
}

void Goroutine::resume() {
    state_.store(GoroutineState::RUNNING);
}

// ============================================================================
// GLOBAL FUNCTIONS IMPLEMENTATION
// ============================================================================

void set_current_goroutine(std::shared_ptr<Goroutine> goroutine) {
    current_goroutine = goroutine;
}

std::shared_ptr<Goroutine> get_current_goroutine() {
    return current_goroutine;
}

void set_current_lexical_env(std::shared_ptr<LexicalEnvironment> env) {
    current_lexical_env = env;
}

std::shared_ptr<LexicalEnvironment> get_current_lexical_env() {
    return current_lexical_env;
}

void initialize_unified_event_system() {
    
    // Initialize global event loop
    GlobalEventLoop::instance().start(nullptr);
    
}

void shutdown_unified_event_system() {
    
    // Stop global event loop
    GlobalEventLoop::instance().stop();
    
    // Force exit main thread if needed
    MainThreadController::instance().force_exit();
    
}

} // namespace gots