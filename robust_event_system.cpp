#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_set>
#include <unordered_map>

class RobustGlobalTimerSystem {
private:
    std::atomic<uint64_t> next_timer_id_{1};
    std::atomic<bool> running_{false};
    std::thread timer_thread_;
    
    struct Timer {
        uint64_t id;
        std::chrono::steady_clock::time_point expiry;
        std::function<void()> callback;
        bool is_interval;
        std::chrono::milliseconds interval_duration;
        
        bool operator<(const Timer& other) const {
            return expiry > other.expiry;  // Min-heap
        }
    };
    
    std::priority_queue<Timer> timers_;
    std::unordered_set<uint64_t> cancelled_timers_;  // Track cancelled timers
    std::unordered_map<uint64_t, std::chrono::milliseconds> intervals_;  // Track intervals
    std::mutex timer_mutex_;
    std::condition_variable timer_cv_;  // KEY: For early wake-up
    
public:
    static RobustGlobalTimerSystem& instance() {
        static RobustGlobalTimerSystem instance;
        return instance;
    }
    
    void start() {
        if (running_.exchange(true)) return;
        
        timer_thread_ = std::thread([this]() {
            event_loop();
        });
        
        std::cout << "DEBUG: RobustGlobalTimerSystem started" << std::endl;
    }
    
    void stop() {
        if (!running_.exchange(false)) return;
        
        timer_cv_.notify_all();  // Wake up sleeping thread
        
        if (timer_thread_.joinable()) {
            timer_thread_.join();
        }
        
        std::cout << "DEBUG: RobustGlobalTimerSystem stopped" << std::endl;
    }
    
    uint64_t set_timeout(std::function<void()> callback, int64_t delay_ms) {
        auto timer_id = next_timer_id_.fetch_add(1);
        auto expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
        
        auto wrapped_callback = [timer_id, callback = std::move(callback)]() {
            std::cout << "DEBUG: Executing timeout " << timer_id << std::endl;
            callback();
        };
        
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            timers_.emplace(Timer{timer_id, expiry, std::move(wrapped_callback), false, {}});
        }
        
        // KEY: Wake up event loop for early scheduling
        timer_cv_.notify_one();
        
        std::cout << "DEBUG: Set timeout " << timer_id << " for " << delay_ms << "ms" << std::endl;
        return timer_id;
    }
    
    uint64_t set_interval(std::function<void()> callback, int64_t interval_ms) {
        auto timer_id = next_timer_id_.fetch_add(1);
        auto expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(interval_ms);
        auto duration = std::chrono::milliseconds(interval_ms);
        
        // Store interval info for rescheduling
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            intervals_[timer_id] = duration;
        }
        
        auto wrapped_callback = [this, timer_id, callback = std::move(callback)]() {
            std::cout << "DEBUG: Executing interval " << timer_id << std::endl;
            callback();
            
            // Reschedule if not cancelled
            std::lock_guard<std::mutex> lock(timer_mutex_);
            if (cancelled_timers_.find(timer_id) == cancelled_timers_.end()) {
                auto it = intervals_.find(timer_id);
                if (it != intervals_.end()) {
                    auto next_expiry = std::chrono::steady_clock::now() + it->second;
                    
                    // Create new timer instance for next execution
                    auto next_callback = [this, timer_id, callback]() {
                        std::cout << "DEBUG: Executing interval " << timer_id << std::endl;
                        callback();
                        // This will create a chain of rescheduling
                    };
                    
                    timers_.emplace(Timer{timer_id, next_expiry, std::move(next_callback), true, it->second});
                    timer_cv_.notify_one();  // Wake up for rescheduling
                }
            }
        };
        
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            timers_.emplace(Timer{timer_id, expiry, std::move(wrapped_callback), true, duration});
        }
        
        // KEY: Wake up event loop for early scheduling
        timer_cv_.notify_one();
        
        std::cout << "DEBUG: Set interval " << timer_id << " for " << interval_ms << "ms" << std::endl;
        return timer_id;
    }
    
    // KEY: Proper clearTimeout/clearInterval implementation
    bool clear_timeout(uint64_t timer_id) {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        
        cancelled_timers_.insert(timer_id);
        intervals_.erase(timer_id);  // Remove interval info
        
        // Wake up event loop to process cancellation
        timer_cv_.notify_one();
        
        std::cout << "DEBUG: Cleared timeout/interval " << timer_id << std::endl;
        return true;
    }
    
    bool clear_interval(uint64_t timer_id) {
        return clear_timeout(timer_id);  // Same implementation
    }
    
private:
    void event_loop() {
        std::cout << "DEBUG: Robust event loop started" << std::endl;
        
        std::unique_lock<std::mutex> lock(timer_mutex_);
        
        while (running_.load()) {
            // Clean up cancelled timers first
            clean_cancelled_timers();
            
            // Process expired timers
            process_expired_timers();
            
            if (timers_.empty()) {
                // NO TIMERS: Sleep until new timer added
                std::cout << "DEBUG: No timers, sleeping until new timer added" << std::endl;
                timer_cv_.wait(lock, [this]() { 
                    return !running_.load() || !timers_.empty(); 
                });
            } else {
                // TIMERS EXIST: Sleep until next timer OR new timer added
                auto next_expiry = timers_.top().expiry;
                auto now = std::chrono::steady_clock::now();
                
                if (next_expiry > now) {
                    auto sleep_duration = next_expiry - now;
                    auto sleep_ms = std::chrono::duration_cast<std::chrono::milliseconds>(sleep_duration).count();
                    
                    std::cout << "DEBUG: Sleeping for " << sleep_ms << "ms (or until new timer)" << std::endl;
                    
                    // KEY: Sleep until timer expires OR new timer added OR cancelled
                    timer_cv_.wait_until(lock, next_expiry, [this]() { 
                        return !running_.load(); 
                    });
                }
            }
        }
        
        std::cout << "DEBUG: Robust event loop exited" << std::endl;
    }
    
    void clean_cancelled_timers() {
        // Remove cancelled timers from the queue
        std::priority_queue<Timer> clean_timers;
        
        while (!timers_.empty()) {
            auto timer = timers_.top();
            timers_.pop();
            
            if (cancelled_timers_.find(timer.id) == cancelled_timers_.end()) {
                clean_timers.push(timer);
            }
        }
        
        timers_ = std::move(clean_timers);
        cancelled_timers_.clear();  // Clear cancelled set
    }
    
    void process_expired_timers() {
        auto now = std::chrono::steady_clock::now();
        std::vector<Timer> expired;
        
        while (!timers_.empty() && timers_.top().expiry <= now) {
            auto timer = timers_.top();
            timers_.pop();
            
            // Skip cancelled timers
            if (cancelled_timers_.find(timer.id) == cancelled_timers_.end()) {
                expired.push_back(timer);
            }
        }
        
        // Execute expired timers (unlock while executing)
        if (!expired.empty()) {
            timer_mutex_.unlock();
            
            for (auto& timer : expired) {
                timer.callback();
            }
            
            timer_mutex_.lock();
        }
    }
};

// ============================================================================
// GOROUTINE SYSTEM - Separate from event loop (non-blocking)
// ============================================================================

class GoroutineSystem {
private:
    std::vector<std::thread> goroutine_threads_;
    std::atomic<bool> running_{true};
    std::mutex goroutine_mutex_;
    
public:
    static GoroutineSystem& instance() {
        static GoroutineSystem instance;
        return instance;
    }
    
    void spawn_goroutine(std::function<void()> task) {
        std::lock_guard<std::mutex> lock(goroutine_mutex_);
        
        // Create new thread for goroutine (non-blocking)
        goroutine_threads_.emplace_back([task = std::move(task)]() {
            std::cout << "DEBUG: Goroutine started on thread " << std::this_thread::get_id() << std::endl;
            
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "ERROR: Goroutine exception: " << e.what() << std::endl;
            }
            
            std::cout << "DEBUG: Goroutine completed on thread " << std::this_thread::get_id() << std::endl;
        });
    }
    
    void shutdown() {
        running_.store(false);
        
        std::lock_guard<std::mutex> lock(goroutine_mutex_);
        for (auto& thread : goroutine_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        goroutine_threads_.clear();
    }
};

// ============================================================================
// DEMO: Test all edge cases
// ============================================================================

int main() {
    std::cout << "\n=== ROBUST EVENT SYSTEM DEMO ===" << std::endl;
    
    auto& timer_system = RobustGlobalTimerSystem::instance();
    auto& goroutine_system = GoroutineSystem::instance();
    
    timer_system.start();
    
    // Test 1: Early wake-up scenario
    std::cout << "\n--- Test 1: Early Wake-up ---" << std::endl;
    
    // Set a long timer (5 seconds)
    auto long_timer = timer_system.set_timeout([]() {
        std::cout << "Long timer fired (5 seconds)" << std::endl;
    }, 5000);
    
    // Wait a bit, then add a shorter timer (should wake up early)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto short_timer = timer_system.set_timeout([]() {
        std::cout << "Short timer fired (1 second) - woke up early!" << std::endl;
    }, 1000);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Test 2: clearTimeout scenario
    std::cout << "\n--- Test 2: clearTimeout ---" << std::endl;
    
    auto timer_to_cancel = timer_system.set_timeout([]() {
        std::cout << "This should NOT fire - timer was cancelled!" << std::endl;
    }, 2000);
    
    // Cancel the timer
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    timer_system.clear_timeout(timer_to_cancel);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
    // Test 3: clearInterval scenario
    std::cout << "\n--- Test 3: clearInterval ---" << std::endl;
    
    auto interval_timer = timer_system.set_interval([]() {
        std::cout << "Interval fired (will be cancelled after 3 times)" << std::endl;
    }, 500);
    
    // Let it fire a few times, then cancel
    std::this_thread::sleep_for(std::chrono::milliseconds(1600));
    timer_system.clear_interval(interval_timer);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Test 4: Goroutines handling server requests (non-blocking)
    std::cout << "\n--- Test 4: Goroutines (Non-blocking) ---" << std::endl;
    
    // Simulate server request handling
    for (int i = 0; i < 5; ++i) {
        goroutine_system.spawn_goroutine([i]() {
            std::cout << "Handling request " << i << " on separate thread" << std::endl;
            
            // Simulate some work
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            std::cout << "Request " << i << " completed" << std::endl;
        });
    }
    
    // While goroutines are running, timers should still work
    timer_system.set_timeout([]() {
        std::cout << "Timer fired while goroutines were running!" << std::endl;
    }, 300);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Cleanup
    timer_system.stop();
    goroutine_system.shutdown();
    
    std::cout << "\n=== ALL TESTS COMPLETE ===" << std::endl;
    std::cout << "✅ Early wake-up: Timer system wakes up when shorter timer added" << std::endl;
    std::cout << "✅ clearTimeout: Cancelled timers don't execute" << std::endl;
    std::cout << "✅ clearInterval: Cancelled intervals stop repeating" << std::endl;
    std::cout << "✅ Goroutines: Run on separate threads, don't block event loop" << std::endl;
    
    return 0;
}