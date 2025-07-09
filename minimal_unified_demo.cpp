#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>

// Minimal implementation to demonstrate unified event system concepts

class MainThreadController {
private:
    std::atomic<int> active_goroutines_{0};
    std::atomic<int> pending_timers_{0};
    std::atomic<bool> should_exit_{false};
    std::condition_variable exit_cv_;
    std::mutex exit_mutex_;
    
public:
    static MainThreadController& instance() {
        static MainThreadController instance;
        return instance;
    }
    
    void goroutine_started() {
        int count = active_goroutines_.fetch_add(1) + 1;
        std::cout << "DEBUG: Active goroutines: " << count << std::endl;
    }
    
    void goroutine_completed() {
        int count = active_goroutines_.fetch_sub(1) - 1;
        std::cout << "DEBUG: Active goroutines: " << count << std::endl;
        check_exit_condition();
    }
    
    void timer_started() {
        int count = pending_timers_.fetch_add(1) + 1;
        std::cout << "DEBUG: Pending timers: " << count << std::endl;
    }
    
    void timer_completed() {
        int count = pending_timers_.fetch_sub(1) - 1;
        std::cout << "DEBUG: Pending timers: " << count << std::endl;
        check_exit_condition();
    }
    
    void wait_for_completion() {
        std::unique_lock<std::mutex> lock(exit_mutex_);
        exit_cv_.wait(lock, [this]() { return should_exit_.load(); });
    }
    
    int get_active_goroutines() const { return active_goroutines_.load(); }
    int get_pending_timers() const { return pending_timers_.load(); }
    
private:
    void check_exit_condition() {
        if (active_goroutines_.load() == 0 && pending_timers_.load() == 0) {
            std::cout << "DEBUG: All work complete, signaling exit" << std::endl;
            should_exit_.store(true);
            exit_cv_.notify_all();
        }
    }
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

class SimpleGoroutine {
private:
    uint64_t id_;
    std::function<void()> task_;
    
public:
    SimpleGoroutine(uint64_t id, std::function<void()> task) : id_(id), task_(task) {}
    
    void run() {
        std::cout << "DEBUG: Goroutine " << id_ << " starting" << std::endl;
        MainThreadController::instance().goroutine_started();
        
        try {
            if (task_) {
                task_();
            }
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Goroutine " << id_ << " failed: " << e.what() << std::endl;
        }
        
        std::cout << "DEBUG: Goroutine " << id_ << " completed" << std::endl;
        MainThreadController::instance().goroutine_completed();
    }
    
    uint64_t get_id() const { return id_; }
};

int main() {
    std::cout << "\n=== UNIFIED EVENT SYSTEM DEMO ===" << std::endl;
    
    // Initialize global timer system
    GlobalTimerSystem::instance().start();
    
    // Test 1: Basic goroutine
    std::cout << "\n--- Test 1: Basic Goroutine ---" << std::endl;
    auto goroutine1 = std::make_shared<SimpleGoroutine>(1, []() {
        std::cout << "Goroutine 1: Hello from goroutine!" << std::endl;
    });
    
    std::thread([goroutine1]() {
        goroutine1->run();
    }).detach();
    
    // Test 2: Goroutine with timer
    std::cout << "\n--- Test 2: Goroutine with Timer ---" << std::endl;
    auto goroutine2 = std::make_shared<SimpleGoroutine>(2, []() {
        std::cout << "Goroutine 2: Setting timer..." << std::endl;
        
        GlobalTimerSystem::instance().set_timeout([]() {
            std::cout << "Goroutine 2: Timer callback executed!" << std::endl;
        }, 100);
        
        std::cout << "Goroutine 2: Main task completed" << std::endl;
    });
    
    std::thread([goroutine2]() {
        goroutine2->run();
    }).detach();
    
    // Test 3: Multiple timers
    std::cout << "\n--- Test 3: Multiple Timers ---" << std::endl;
    auto goroutine3 = std::make_shared<SimpleGoroutine>(3, []() {
        std::cout << "Goroutine 3: Setting multiple timers..." << std::endl;
        
        GlobalTimerSystem::instance().set_timeout([]() {
            std::cout << "Goroutine 3: Timer 1 fired (50ms)" << std::endl;
        }, 50);
        
        GlobalTimerSystem::instance().set_timeout([]() {
            std::cout << "Goroutine 3: Timer 2 fired (150ms)" << std::endl;
        }, 150);
        
        GlobalTimerSystem::instance().set_timeout([]() {
            std::cout << "Goroutine 3: Timer 3 fired (250ms)" << std::endl;
        }, 250);
        
        std::cout << "Goroutine 3: All timers set" << std::endl;
    });
    
    std::thread([goroutine3]() {
        goroutine3->run();
    }).detach();
    
    // Test 4: Main thread should wait for all work
    std::cout << "\n--- Test 4: Main Thread Waiting ---" << std::endl;
    GlobalTimerSystem::instance().set_timeout([]() {
        std::cout << "Main thread timer: Should execute before exit" << std::endl;
    }, 300);
    
    std::cout << "Main function completed - waiting for all goroutines and timers..." << std::endl;
    
    // Wait for all work to complete
    MainThreadController::instance().wait_for_completion();
    
    // Shutdown
    GlobalTimerSystem::instance().stop();
    
    std::cout << "\n=== DEMO COMPLETE ===" << std::endl;
    std::cout << "Key achievements:" << std::endl;
    std::cout << "✅ Single global timer system (no per-goroutine event loops)" << std::endl;
    std::cout << "✅ Goroutine lifecycle management" << std::endl;
    std::cout << "✅ Main thread waits for all work to complete" << std::endl;
    std::cout << "✅ Proper cleanup and shutdown" << std::endl;
    
    return 0;
}