#include "goroutine_system.h"
#include <iostream>
#include <algorithm>

namespace gots {

// Thread-local current goroutine
thread_local std::shared_ptr<Goroutine> current_goroutine = nullptr;

// Goroutine implementation
Goroutine::Goroutine(int64_t id, std::function<void()> task, std::shared_ptr<Goroutine> parent)
    : id_(id), state_(GoroutineState::CREATED), task_(std::move(task)), parent_(parent), result_promise_(nullptr) {
    // Parent-child relationship will be established after shared_ptr is created
}

Goroutine::~Goroutine() {
    signal_exit();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Goroutine::start() {
    state_ = GoroutineState::RUNNING;
    thread_ = std::thread(&Goroutine::run, this);
}

void Goroutine::run() {
    // Set thread-local current goroutine - will be set by scheduler
    
    std::cout << "DEBUG: Goroutine " << id_ << " starting execution" << std::endl;
    
    try {
        // Execute the main task
        task_();
        std::cout << "DEBUG: Goroutine " << id_ << " main task completed" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Goroutine " << id_ << " exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "ERROR: Goroutine " << id_ << " unknown exception" << std::endl;
    }
    
    // Process timers if any
    if (!timer_queue_.empty()) {
        state_ = GoroutineState::WAITING_FOR_TIMERS;
        std::cout << "DEBUG: Goroutine " << id_ << " has timers, processing..." << std::endl;
        process_timers();
    }
    
    state_ = GoroutineState::COMPLETED;
    
    // Wait for children to complete
    {
        std::lock_guard<std::mutex> lock(children_mutex_);
        while (!children_.empty()) {
            // Remove completed children
            children_.erase(
                std::remove_if(children_.begin(), children_.end(),
                    [](const std::weak_ptr<Goroutine>& child) {
                        auto c = child.lock();
                        return !c || c->get_state() == GoroutineState::COMPLETED;
                    }),
                children_.end()
            );
            
            if (!children_.empty()) {
                std::cout << "DEBUG: Goroutine " << id_ << " waiting for " << children_.size() << " children" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    
    std::cout << "DEBUG: Goroutine " << id_ << " all children completed" << std::endl;
    
    // Cleanup chain
    state_ = GoroutineState::CLEANING_UP;
    cleanup_chain();
    
    // Unregister from scheduler
    GoroutineScheduler::instance().unregister_goroutine(id_);
    
    std::cout << "DEBUG: Goroutine " << id_ << " fully completed" << std::endl;
}

void Goroutine::process_timers() {
    while (!should_exit_.load()) {
        std::unique_lock<std::mutex> lock(timer_mutex_);
        
        // Remove cancelled timers
        while (!timer_queue_.empty()) {
            const Timer& timer = timer_queue_.top();
            if (cancelled_timers_.find(timer.id) != cancelled_timers_.end()) {
                timer_queue_.pop();
                cancelled_timers_.erase(timer.id);
                continue;
            }
            break;
        }
        
        if (timer_queue_.empty()) {
            std::cout << "DEBUG: Goroutine " << id_ << " no more timers, exiting timer loop" << std::endl;
            break;
        }
        
        const Timer& next_timer = timer_queue_.top();
        auto now = std::chrono::steady_clock::now();
        
        if (next_timer.execute_time <= now) {
            // Execute timer
            Timer timer = next_timer;
            timer_queue_.pop();
            
            lock.unlock();
            
            std::cout << "DEBUG: Goroutine " << id_ << " executing timer " << timer.id << std::endl;
            
            try {
                typedef void (*TimerCallback)();
                TimerCallback callback = reinterpret_cast<TimerCallback>(timer.callback_ptr);
                callback();
                std::cout << "DEBUG: Timer " << timer.id << " callback completed" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "ERROR: Timer " << timer.id << " exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "ERROR: Timer " << timer.id << " unknown exception" << std::endl;
            }
            
            lock.lock();
            
            // Reschedule if interval timer
            if (timer.is_interval) {
                timer.execute_time = now + std::chrono::milliseconds(timer.interval_ms);
                timer_queue_.push(timer);
                std::cout << "DEBUG: Rescheduled interval timer " << timer.id << std::endl;
            }
        } else {
            // Wait until timer is ready
            timer_cv_.wait_until(lock, next_timer.execute_time, [this] {
                return should_exit_.load() || !timer_queue_.empty();
            });
        }
    }
}

int64_t Goroutine::add_timer(int64_t delay_ms, void* callback, bool is_interval) {
    Timer timer;
    timer.id = GoroutineScheduler::instance().get_next_timer_id();
    timer.execute_time = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
    timer.callback_ptr = callback;
    timer.is_interval = is_interval;
    timer.interval_ms = delay_ms;
    
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timer_queue_.push(timer);
        std::cout << "DEBUG: Goroutine " << id_ << " added timer " << timer.id << " with " << delay_ms << "ms delay" << std::endl;
    }
    
    timer_cv_.notify_one();
    return timer.id;
}

bool Goroutine::cancel_timer(int64_t timer_id) {
    std::lock_guard<std::mutex> lock(timer_mutex_);
    cancelled_timers_.insert(timer_id);
    timer_cv_.notify_one();
    std::cout << "DEBUG: Goroutine " << id_ << " cancelled timer " << timer_id << std::endl;
    return true;
}

void Goroutine::add_child(std::shared_ptr<Goroutine> child) {
    std::lock_guard<std::mutex> lock(children_mutex_);
    children_.push_back(child);
    std::cout << "DEBUG: Goroutine " << id_ << " added child " << child->id_ << std::endl;
}

void Goroutine::remove_child(std::shared_ptr<Goroutine> child) {
    std::lock_guard<std::mutex> lock(children_mutex_);
    children_.erase(
        std::remove_if(children_.begin(), children_.end(),
            [&child](const std::weak_ptr<Goroutine>& c) {
                auto locked = c.lock();
                return !locked || locked->get_id() == child->get_id();
            }),
        children_.end()
    );
}

bool Goroutine::can_exit() const {
    std::lock_guard<std::mutex> lock1(timer_mutex_);
    std::lock_guard<std::mutex> lock2(children_mutex_);
    
    // Can exit if no timers and no active children
    if (!timer_queue_.empty()) return false;
    
    for (const auto& weak_child : children_) {
        if (auto child = weak_child.lock()) {
            if (child->get_state() != GoroutineState::COMPLETED) {
                return false;
            }
        }
    }
    
    return true;
}

void Goroutine::signal_exit() {
    should_exit_.store(true);
    timer_cv_.notify_all();
}

void Goroutine::cleanup_chain() {
    // Try to cleanup parent if it's completed
    if (auto parent = parent_.lock()) {
        if (parent->can_exit() && parent->get_state() == GoroutineState::COMPLETED) {
            parent->cleanup_chain();
        }
    }
}

// Thread pool implementation
GoroutineThreadPool::GoroutineThreadPool(size_t num_threads) {
    for (size_t i = 0; i < num_threads; ++i) {
        threads_.emplace_back(&GoroutineThreadPool::worker, this);
    }
}

GoroutineThreadPool::~GoroutineThreadPool() {
    shutdown();
}

void GoroutineThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!shutdown_) {
            task_queue_.push(std::move(task));
        }
    }
    queue_cv_.notify_one();
}

void GoroutineThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        shutdown_ = true;
    }
    queue_cv_.notify_all();
    
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void GoroutineThreadPool::worker() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { return shutdown_ || !task_queue_.empty(); });
            
            if (shutdown_ && task_queue_.empty()) {
                return;
            }
            
            if (!task_queue_.empty()) {
                task = std::move(task_queue_.front());
                task_queue_.pop();
            }
        }
        
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "Thread pool task exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Thread pool task unknown exception" << std::endl;
            }
        }
    }
}

// Scheduler implementation
GoroutineScheduler::GoroutineScheduler() 
    : thread_pool_(std::make_unique<GoroutineThreadPool>(std::thread::hardware_concurrency())) {
}

GoroutineScheduler::~GoroutineScheduler() {
    wait_all();
}

GoroutineScheduler& GoroutineScheduler::instance() {
    static GoroutineScheduler instance;
    return instance;
}

std::shared_ptr<Goroutine> GoroutineScheduler::spawn(std::function<void()> task, std::shared_ptr<Goroutine> parent) {
    int64_t id = next_goroutine_id_.fetch_add(1);
    
    // If no parent specified, use current goroutine as parent
    if (!parent && current_goroutine) {
        parent = current_goroutine;
    }
    
    auto goroutine = std::make_shared<Goroutine>(id, std::move(task), parent);
    
    // Establish parent-child relationship after shared_ptr is created
    if (parent) {
        parent->add_child(goroutine);
    }
    
    register_goroutine(goroutine);
    
    // Submit to thread pool
    thread_pool_->submit([goroutine] {
        // Set thread-local current goroutine
        current_goroutine = goroutine;
        goroutine->run();
    });
    
    return goroutine;
}

void GoroutineScheduler::register_goroutine(std::shared_ptr<Goroutine> g) {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    goroutines_[g->get_id()] = g;
    std::cout << "DEBUG: Registered goroutine " << g->get_id() << ", total: " << goroutines_.size() << std::endl;
}

void GoroutineScheduler::unregister_goroutine(int64_t id) {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    goroutines_.erase(id);
    std::cout << "DEBUG: Unregistered goroutine " << id << ", remaining: " << goroutines_.size() << std::endl;
}

size_t GoroutineScheduler::get_active_count() const {
    std::lock_guard<std::mutex> lock(goroutines_mutex_);
    return goroutines_.size();
}

void GoroutineScheduler::wait_all() {
    std::cout << "DEBUG: Waiting for all goroutines to complete..." << std::endl;
    
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Check if any goroutines are still active
        bool any_active = false;
        {
            std::lock_guard<std::mutex> lock(goroutines_mutex_);
            for (const auto& pair : goroutines_) {
                auto& goroutine = pair.second;
                if (goroutine && goroutine != main_goroutine_) {
                    auto state = goroutine->get_state();
                    if (state != GoroutineState::COMPLETED && state != GoroutineState::CLEANING_UP) {
                        any_active = true;
                        break;
                    }
                }
            }
        }
        
        if (!any_active) {
            break;
        }
        
        size_t count = get_active_count();
        std::cout << "DEBUG: Still " << count << " goroutines registered" << std::endl;
    }
    
    std::cout << "DEBUG: All goroutines completed" << std::endl;
}

// C interface implementation
extern "C" {

int64_t __gots_set_timeout(void* callback, int64_t delay_ms) {
    if (!current_goroutine) {
        std::cerr << "ERROR: setTimeout called outside goroutine context" << std::endl;
        return -1;
    }
    
    return current_goroutine->add_timer(delay_ms, callback, false);
}

int64_t __gots_set_interval(void* callback, int64_t delay_ms) {
    if (!current_goroutine) {
        std::cerr << "ERROR: setInterval called outside goroutine context" << std::endl;
        return -1;
    }
    
    return current_goroutine->add_timer(delay_ms, callback, true);
}

bool __gots_clear_timeout(int64_t timer_id) {
    if (!current_goroutine) {
        std::cerr << "ERROR: clearTimeout called outside goroutine context" << std::endl;
        return false;
    }
    
    return current_goroutine->cancel_timer(timer_id);
}

bool __gots_clear_interval(int64_t timer_id) {
    return __gots_clear_timeout(timer_id);
}

} // extern "C"

} // namespace gots