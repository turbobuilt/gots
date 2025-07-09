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
        std::cout << "DEBUG: Executing timer " << timer_id << " for goroutine " << goroutine_id << std::endl;
        
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
    
    std::cout << "DEBUG: Set timeout " << timer_id << " for " << delay_ms << "ms on goroutine " << goroutine_id << std::endl;
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
        std::cout << "DEBUG: Executing interval " << timer_id << " for goroutine " << goroutine_id << std::endl;
        
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
    
    std::cout << "DEBUG: Set interval " << timer_id << " for " << interval_ms << "ms on goroutine " << goroutine_id << std::endl;
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
        
        std::cout << "DEBUG: Cleared timer " << timer_id << std::endl;
        
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
    std::cout << "DEBUG: Goroutine " << goroutine_id << " started. Active: " << count << std::endl;
}

void MainThreadController::goroutine_completed(uint64_t goroutine_id) {
    cleanup_goroutine_references(goroutine_id);
    
    int count = active_goroutines_.fetch_sub(1) - 1;
    std::cout << "DEBUG: Goroutine " << goroutine_id << " completed. Active: " << count << std::endl;
    
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
    std::cout << "DEBUG: Timer " << timer_id << " started for goroutine " << goroutine_id << ". Pending: " << count << std::endl;
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
    std::cout << "DEBUG: Timer " << timer_id << " completed for goroutine " << goroutine_id << ". Pending: " << count << std::endl;
    
    if (count == 0) {
        check_exit_condition();
    }
}

void MainThreadController::io_operation_started() {
    int count = active_io_operations_.fetch_add(1) + 1;
    std::cout << "DEBUG: I/O operation started. Active: " << count << std::endl;
}

void MainThreadController::io_operation_completed() {
    int count = active_io_operations_.fetch_sub(1) - 1;
    std::cout << "DEBUG: I/O operation completed. Active: " << count << std::endl;
    
    if (count == 0) {
        check_exit_condition();
    }
}

void MainThreadController::wait_for_completion() {
    std::cout << "DEBUG: Main thread waiting for completion..." << std::endl;
    std::cout << "DEBUG: Active goroutines: " << active_goroutines_.load() << std::endl;
    std::cout << "DEBUG: Pending timers: " << pending_timers_.load() << std::endl;
    std::cout << "DEBUG: Active I/O: " << active_io_operations_.load() << std::endl;
    
    std::unique_lock<std::mutex> lock(exit_mutex_);
    exit_cv_.wait(lock, [this]() { return should_exit_.load(); });
    
    std::cout << "DEBUG: Main thread exit condition met" << std::endl;
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
        std::cout << "DEBUG: All work complete, signaling main thread exit" << std::endl;
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
            std::cout << "DEBUG: Cleaned up timer " << timer_id << " for completed goroutine " << goroutine_id << std::endl;
        }
        pending_timers_per_goroutine_.erase(it);
    }
}

// ============================================================================
// GLOBAL EVENT LOOP IMPLEMENTATION
// ============================================================================

void GlobalEventLoop::start(WorkStealingScheduler* scheduler) {
    if (running_.exchange(true)) {
        std::cout << "DEBUG: GlobalEventLoop already running" << std::endl;
        return;
    }
    
    scheduler_ = scheduler;
    event_thread_ = std::thread(&GlobalEventLoop::event_loop, this);
    std::cout << "DEBUG: GlobalEventLoop started" << std::endl;
}

void GlobalEventLoop::stop() {
    if (!running_.exchange(false)) {
        std::cout << "DEBUG: GlobalEventLoop not running" << std::endl;
        return;
    }
    
    if (event_thread_.joinable()) {
        event_thread_.join();
    }
    std::cout << "DEBUG: GlobalEventLoop stopped" << std::endl;
}

void GlobalEventLoop::event_loop() {
    std::cout << "DEBUG: GlobalEventLoop thread started - efficient mode" << std::endl;
    
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
    
    std::cout << "DEBUG: GlobalEventLoop thread exiting" << std::endl;
}

// ============================================================================
// GOROUTINE MANAGER IMPLEMENTATION
// ============================================================================

void GoroutineManager::register_goroutine(uint64_t goroutine_id, std::shared_ptr<Goroutine> goroutine) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_goroutines_[goroutine_id] = goroutine;
    std::cout << "DEBUG: GoroutineManager registered goroutine " << goroutine_id << std::endl;
}

void GoroutineManager::unregister_goroutine(uint64_t goroutine_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    active_goroutines_.erase(goroutine_id);
    pending_timers_per_goroutine_.erase(goroutine_id);
    std::cout << "DEBUG: GoroutineManager unregistered goroutine " << goroutine_id << std::endl;
}

void GoroutineManager::add_timer_reference(uint64_t goroutine_id, uint64_t timer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_timers_per_goroutine_[goroutine_id].insert(timer_id);
    std::cout << "DEBUG: GoroutineManager added timer reference " << timer_id << " for goroutine " << goroutine_id << std::endl;
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
    std::cout << "DEBUG: GoroutineManager removed timer reference " << timer_id << " for goroutine " << goroutine_id << std::endl;
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
            std::cout << "DEBUG: Cleaning up completed goroutine " << it->first << std::endl;
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
    if (lexical_env_) {
        lexical_env_->add_ref();
    }
    std::cout << "DEBUG: Created goroutine " << id_ << std::endl;
}

Goroutine::~Goroutine() {
    if (lexical_env_) {
        lexical_env_->release();
    }
    std::cout << "DEBUG: Destroyed goroutine " << id_ << std::endl;
}

void Goroutine::run() {
    std::cout << "DEBUG: Goroutine " << id_ << " starting execution" << std::endl;
    
    // Set thread-local context
    set_current_goroutine(shared_from_this());
    set_current_lexical_env(lexical_env_);
    
    state_.store(GoroutineState::RUNNING);
    
    try {
        if (main_task_) {
            main_task_();
        }
        
        state_.store(GoroutineState::COMPLETED);
        std::cout << "DEBUG: Goroutine " << id_ << " completed successfully" << std::endl;
        
    } catch (const std::exception& e) {
        state_.store(GoroutineState::FAILED);
        std::cerr << "ERROR: Goroutine " << id_ << " failed: " << e.what() << std::endl;
    }
    
    // Clean up any remaining timers
    GlobalTimerSystem::instance().clear_all_timers_for_goroutine(id_);
    
    // Notify main controller
    MainThreadController::instance().goroutine_completed(id_);
    
    std::cout << "DEBUG: Goroutine " << id_ << " execution finished" << std::endl;
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
    
    std::cout << "DEBUG: Goroutine " << id_ << " spawned child goroutine " << child_id << std::endl;
    
    // Register with systems
    GoroutineManager::instance().register_goroutine(child_id, child);
    MainThreadController::instance().goroutine_started(child_id, child);
    
    return child;
}

void Goroutine::child_completed() {
    int remaining = child_count_.fetch_sub(1) - 1;
    std::cout << "DEBUG: Goroutine " << id_ << " child completed. Remaining children: " << remaining << std::endl;
}

void Goroutine::suspend() {
    state_.store(GoroutineState::SUSPENDED);
    std::cout << "DEBUG: Goroutine " << id_ << " suspended" << std::endl;
}

void Goroutine::resume() {
    state_.store(GoroutineState::RUNNING);
    std::cout << "DEBUG: Goroutine " << id_ << " resumed" << std::endl;
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
    std::cout << "DEBUG: Initializing unified event system" << std::endl;
    
    // Initialize global event loop
    GlobalEventLoop::instance().start(nullptr);
    
    std::cout << "DEBUG: Unified event system initialized" << std::endl;
}

void shutdown_unified_event_system() {
    std::cout << "DEBUG: Shutting down unified event system" << std::endl;
    
    // Stop global event loop
    GlobalEventLoop::instance().stop();
    
    // Force exit main thread if needed
    MainThreadController::instance().force_exit();
    
    std::cout << "DEBUG: Unified event system shut down" << std::endl;
}

} // namespace gots