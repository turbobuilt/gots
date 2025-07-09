#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>

// ============================================================================
// APPROACH 1: SINGLE EVENT LOOP (Current approach)
// ============================================================================

class SingleEventLoop {
private:
    std::atomic<bool> running_{false};
    std::thread event_thread_;
    
    // Timer management
    struct Timer {
        std::chrono::steady_clock::time_point expiry;
        std::function<void()> callback;
        
        bool operator<(const Timer& other) const {
            return expiry > other.expiry;
        }
    };
    
    std::priority_queue<Timer> timers_;
    std::mutex timer_mutex_;
    std::condition_variable timer_cv_;
    
    // Goroutine management
    std::queue<std::function<void()>> goroutine_queue_;
    std::mutex goroutine_mutex_;
    std::condition_variable goroutine_cv_;
    
    // I/O simulation
    std::queue<std::function<void()>> io_queue_;
    std::mutex io_mutex_;
    
public:
    void start() {
        running_ = true;
        event_thread_ = std::thread([this]() {
            single_event_loop();
        });
    }
    
    void stop() {
        running_ = false;
        timer_cv_.notify_all();
        goroutine_cv_.notify_all();
        if (event_thread_.joinable()) {
            event_thread_.join();
        }
    }
    
    void add_timer(std::function<void()> callback, int delay_ms) {
        auto expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
        
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            timers_.emplace(Timer{expiry, callback});
        }
        
        timer_cv_.notify_one();  // Wake up event loop
    }
    
    void spawn_goroutine(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(goroutine_mutex_);
            goroutine_queue_.push(task);
        }
        
        goroutine_cv_.notify_one();  // Wake up event loop
    }
    
    void add_io_event(std::function<void()> callback) {
        {
            std::lock_guard<std::mutex> lock(io_mutex_);
            io_queue_.push(callback);
        }
        
        // PROBLEM: No way to wake up event loop if it's sleeping for timers!
        // We'd need another condition variable, making it complex
    }
    
private:
    void single_event_loop() {
        std::cout << "Single event loop started" << std::endl;
        
        while (running_) {
            // 1. Process expired timers
            process_timers();
            
            // 2. Process goroutine spawning
            process_goroutines();
            
            // 3. Process I/O events
            process_io_events();
            
            // 4. Calculate sleep time
            auto sleep_duration = calculate_sleep_time();
            
            // 5. Sleep until next event
            if (sleep_duration.count() > 0) {
                std::unique_lock<std::mutex> lock(timer_mutex_);
                
                // PROBLEM: We can only wait on ONE condition variable!
                // What if goroutine or I/O event comes in while sleeping?
                timer_cv_.wait_for(lock, sleep_duration, [this]() {
                    return !running_;
                });
            }
        }
        
        std::cout << "Single event loop stopped" << std::endl;
    }
    
    void process_timers() {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        auto now = std::chrono::steady_clock::now();
        
        while (!timers_.empty() && timers_.top().expiry <= now) {
            auto timer = timers_.top();
            timers_.pop();
            
            // Execute timer callback
            timer.callback();
        }
    }
    
    void process_goroutines() {
        std::lock_guard<std::mutex> lock(goroutine_mutex_);
        
        while (!goroutine_queue_.empty()) {
            auto task = goroutine_queue_.front();
            goroutine_queue_.pop();
            
            // Spawn goroutine on separate thread
            std::thread([task]() {
                task();
            }).detach();
        }
    }
    
    void process_io_events() {
        std::lock_guard<std::mutex> lock(io_mutex_);
        
        while (!io_queue_.empty()) {
            auto callback = io_queue_.front();
            io_queue_.pop();
            
            callback();
        }
    }
    
    std::chrono::milliseconds calculate_sleep_time() {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        
        if (timers_.empty()) {
            return std::chrono::milliseconds(100);  // Default sleep
        }
        
        auto now = std::chrono::steady_clock::now();
        auto next_expiry = timers_.top().expiry;
        
        if (next_expiry <= now) {
            return std::chrono::milliseconds(0);  // Don't sleep
        }
        
        return std::chrono::duration_cast<std::chrono::milliseconds>(next_expiry - now);
    }
};

// ============================================================================
// APPROACH 2: DUAL EVENT LOOPS (Your suggested approach)
// ============================================================================

class DualEventLoops {
private:
    std::atomic<bool> running_{false};
    std::thread timer_thread_;
    std::thread goroutine_thread_;
    
    // Timer loop
    struct Timer {
        std::chrono::steady_clock::time_point expiry;
        std::function<void()> callback;
        
        bool operator<(const Timer& other) const {
            return expiry > other.expiry;
        }
    };
    
    std::priority_queue<Timer> timers_;
    std::mutex timer_mutex_;
    std::condition_variable timer_cv_;
    
    // Goroutine loop
    std::queue<std::function<void()>> goroutine_queue_;
    std::mutex goroutine_mutex_;
    std::condition_variable goroutine_cv_;
    
    // I/O events (can be handled by either loop)
    std::queue<std::function<void()>> io_queue_;
    std::mutex io_mutex_;
    std::condition_variable io_cv_;
    
public:
    void start() {
        running_ = true;
        
        // Start timer event loop
        timer_thread_ = std::thread([this]() {
            timer_event_loop();
        });
        
        // Start goroutine event loop
        goroutine_thread_ = std::thread([this]() {
            goroutine_event_loop();
        });
    }
    
    void stop() {
        running_ = false;
        timer_cv_.notify_all();
        goroutine_cv_.notify_all();
        io_cv_.notify_all();
        
        if (timer_thread_.joinable()) {
            timer_thread_.join();
        }
        if (goroutine_thread_.joinable()) {
            goroutine_thread_.join();
        }
    }
    
    void add_timer(std::function<void()> callback, int delay_ms) {
        auto expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
        
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            timers_.emplace(Timer{expiry, callback});
        }
        
        timer_cv_.notify_one();  // Wake up timer loop
    }
    
    void spawn_goroutine(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(goroutine_mutex_);
            goroutine_queue_.push(task);
        }
        
        goroutine_cv_.notify_one();  // Wake up goroutine loop
    }
    
    void add_io_event(std::function<void()> callback) {
        {
            std::lock_guard<std::mutex> lock(io_mutex_);
            io_queue_.push(callback);
        }
        
        io_cv_.notify_one();  // Wake up I/O processing
    }
    
private:
    // TIMER EVENT LOOP - Dedicated to timers only
    void timer_event_loop() {
        std::cout << "Timer event loop started" << std::endl;
        
        std::unique_lock<std::mutex> lock(timer_mutex_);
        
        while (running_) {
            // Process expired timers
            process_timers();
            
            if (timers_.empty()) {
                // No timers, sleep indefinitely
                std::cout << "Timer loop: No timers, sleeping indefinitely" << std::endl;
                timer_cv_.wait(lock, [this]() {
                    return !running_ || !timers_.empty();
                });
            } else {
                // Sleep until next timer
                auto next_expiry = timers_.top().expiry;
                auto now = std::chrono::steady_clock::now();
                
                if (next_expiry > now) {
                    auto sleep_duration = next_expiry - now;
                    auto sleep_ms = std::chrono::duration_cast<std::chrono::milliseconds>(sleep_duration).count();
                    
                    std::cout << "Timer loop: Sleeping for " << sleep_ms << "ms" << std::endl;
                    
                    timer_cv_.wait_until(lock, next_expiry, [this]() {
                        return !running_;
                    });
                }
            }
        }
        
        std::cout << "Timer event loop stopped" << std::endl;
    }
    
    // GOROUTINE EVENT LOOP - Dedicated to goroutines and I/O
    void goroutine_event_loop() {
        std::cout << "Goroutine event loop started" << std::endl;
        
        while (running_) {
            // Process goroutine spawning
            process_goroutines();
            
            // Process I/O events
            process_io_events();
            
            // Sleep briefly or wait for events
            std::unique_lock<std::mutex> lock(goroutine_mutex_);
            if (goroutine_queue_.empty()) {
                std::cout << "Goroutine loop: Waiting for goroutines or I/O" << std::endl;
                goroutine_cv_.wait_for(lock, std::chrono::milliseconds(10), [this]() {
                    return !running_ || !goroutine_queue_.empty();
                });
            }
        }
        
        std::cout << "Goroutine event loop stopped" << std::endl;
    }
    
    void process_timers() {
        auto now = std::chrono::steady_clock::now();
        
        while (!timers_.empty() && timers_.top().expiry <= now) {
            auto timer = timers_.top();
            timers_.pop();
            
            std::cout << "Timer fired!" << std::endl;
            timer.callback();
        }
    }
    
    void process_goroutines() {
        std::lock_guard<std::mutex> lock(goroutine_mutex_);
        
        while (!goroutine_queue_.empty()) {
            auto task = goroutine_queue_.front();
            goroutine_queue_.pop();
            
            std::cout << "Spawning goroutine on separate thread" << std::endl;
            
            // Spawn goroutine on separate thread
            std::thread([task]() {
                task();
            }).detach();
        }
    }
    
    void process_io_events() {
        std::lock_guard<std::mutex> lock(io_mutex_);
        
        while (!io_queue_.empty()) {
            auto callback = io_queue_.front();
            io_queue_.pop();
            
            std::cout << "Processing I/O event" << std::endl;
            callback();
        }
    }
};

// ============================================================================
// DEMO: Compare both approaches
// ============================================================================

void test_single_loop() {
    std::cout << "\n=== TESTING SINGLE EVENT LOOP ===" << std::endl;
    
    SingleEventLoop loop;
    loop.start();
    
    // Add timer
    loop.add_timer([]() {
        std::cout << "Single loop: Timer 1 fired" << std::endl;
    }, 500);
    
    // Spawn goroutine
    loop.spawn_goroutine([]() {
        std::cout << "Single loop: Goroutine executing" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Single loop: Goroutine completed" << std::endl;
    });
    
    // Add I/O event
    loop.add_io_event([]() {
        std::cout << "Single loop: I/O event processed" << std::endl;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    loop.stop();
}

void test_dual_loops() {
    std::cout << "\n=== TESTING DUAL EVENT LOOPS ===" << std::endl;
    
    DualEventLoops loops;
    loops.start();
    
    // Add timer
    loops.add_timer([]() {
        std::cout << "Dual loops: Timer 1 fired" << std::endl;
    }, 500);
    
    // Spawn goroutine
    loops.spawn_goroutine([]() {
        std::cout << "Dual loops: Goroutine executing" << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "Dual loops: Goroutine completed" << std::endl;
    });
    
    // Add I/O event
    loops.add_io_event([]() {
        std::cout << "Dual loops: I/O event processed" << std::endl;
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    loops.stop();
}

int main() {
    test_single_loop();
    test_dual_loops();
    
    std::cout << "\n=== COMPARISON ===" << std::endl;
    std::cout << "Single Loop:" << std::endl;
    std::cout << "  ✅ Simpler architecture" << std::endl;
    std::cout << "  ✅ Node.js compatible" << std::endl;
    std::cout << "  ❌ Complex condition variable coordination" << std::endl;
    std::cout << "  ❌ Potential blocking issues" << std::endl;
    
    std::cout << "\nDual Loops:" << std::endl;
    std::cout << "  ✅ Clean separation of concerns" << std::endl;
    std::cout << "  ✅ No blocking between timers and goroutines" << std::endl;
    std::cout << "  ✅ Each loop can optimize for its specific task" << std::endl;
    std::cout << "  ❌ More complex architecture" << std::endl;
    std::cout << "  ❌ Different from Node.js model" << std::endl;
    
    return 0;
}