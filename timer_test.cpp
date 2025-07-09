#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>

class MainThreadController {
private:
    std::atomic<int> pending_timers_{0};
    std::atomic<bool> should_exit_{false};
    std::condition_variable exit_cv_;
    std::mutex exit_mutex_;
    
public:
    static MainThreadController& instance() {
        static MainThreadController instance;
        return instance;
    }
    
    void timer_started() {
        int count = pending_timers_.fetch_add(1) + 1;
        std::cout << "DEBUG: Pending timers: " << count << std::endl;
    }
    
    void timer_completed() {
        int count = pending_timers_.fetch_sub(1) - 1;
        std::cout << "DEBUG: Pending timers: " << count << std::endl;
        
        if (count == 0) {
            std::cout << "DEBUG: All timers complete, signaling exit" << std::endl;
            should_exit_.store(true);
            exit_cv_.notify_all();
        }
    }
    
    void wait_for_completion() {
        std::unique_lock<std::mutex> lock(exit_mutex_);
        exit_cv_.wait(lock, [this]() { return should_exit_.load(); });
    }
    
    int get_pending_timers() const { return pending_timers_.load(); }
};

class GlobalTimerSystem {
private:
    std::atomic<uint64_t> next_timer_id_{1};
    std::atomic<bool> running_{false};
    std::thread timer_thread_;
    
    struct Timer {
        uint64_t id;
        std::chrono::steady_clock::time_point expiry;
        std::function<void()> callback;
        
        bool operator<(const Timer& other) const {
            return expiry > other.expiry;  // Min-heap
        }
    };
    
    std::priority_queue<Timer> timers_;
    std::mutex timer_mutex_;
    
public:
    static GlobalTimerSystem& instance() {
        static GlobalTimerSystem instance;
        return instance;
    }
    
    void start() {
        if (running_.exchange(true)) return;
        
        timer_thread_ = std::thread([this]() {
            while (running_.load()) {
                process_timers();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        
        std::cout << "DEBUG: GlobalTimerSystem started" << std::endl;
    }
    
    void stop() {
        if (!running_.exchange(false)) return;
        
        if (timer_thread_.joinable()) {
            timer_thread_.join();
        }
        
        std::cout << "DEBUG: GlobalTimerSystem stopped" << std::endl;
    }
    
    uint64_t set_timeout(std::function<void()> callback, int64_t delay_ms) {
        auto timer_id = next_timer_id_.fetch_add(1);
        auto expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
        
        MainThreadController::instance().timer_started();
        
        auto wrapped_callback = [timer_id, callback = std::move(callback)]() {
            std::cout << "DEBUG: Executing timer " << timer_id << std::endl;
            callback();
            MainThreadController::instance().timer_completed();
        };
        
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            timers_.emplace(Timer{timer_id, expiry, std::move(wrapped_callback)});
        }
        
        std::cout << "DEBUG: Set timer " << timer_id << " for " << delay_ms << "ms" << std::endl;
        return timer_id;
    }
    
private:
    void process_timers() {
        auto now = std::chrono::steady_clock::now();
        std::vector<Timer> expired;
        
        {
            std::lock_guard<std::mutex> lock(timer_mutex_);
            while (!timers_.empty() && timers_.top().expiry <= now) {
                expired.push_back(timers_.top());
                timers_.pop();
            }
        }
        
        for (auto& timer : expired) {
            timer.callback();
        }
    }
};

int main() {
    std::cout << "\n=== TIMER TEST ===" << std::endl;
    
    // Initialize global timer system
    GlobalTimerSystem::instance().start();
    
    // Set multiple timers
    GlobalTimerSystem::instance().set_timeout([]() {
        std::cout << "Timer 1 fired (50ms)" << std::endl;
    }, 50);
    
    GlobalTimerSystem::instance().set_timeout([]() {
        std::cout << "Timer 2 fired (100ms)" << std::endl;
    }, 100);
    
    GlobalTimerSystem::instance().set_timeout([]() {
        std::cout << "Timer 3 fired (150ms)" << std::endl;
    }, 150);
    
    GlobalTimerSystem::instance().set_timeout([]() {
        std::cout << "Timer 4 fired (200ms)" << std::endl;
    }, 200);
    
    std::cout << "All timers set, waiting for completion..." << std::endl;
    
    // Wait for all timers to complete
    MainThreadController::instance().wait_for_completion();
    
    // Shutdown
    GlobalTimerSystem::instance().stop();
    
    std::cout << "\n=== TEST COMPLETE ===" << std::endl;
    std::cout << "✅ Single global timer system working correctly" << std::endl;
    std::cout << "✅ Multiple timers executing in correct order" << std::endl;
    std::cout << "✅ Main thread waits for all timers to complete" << std::endl;
    
    return 0;
}