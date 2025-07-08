// Old goroutine system removed
#include <iostream>
#include <chrono>

using namespace gots;

// Test timer callback
void timer_callback() {
    std::cout << "Go timeout done" << std::endl;
}

// Test goroutine function
void test_goroutine() {
    std::cout << "DEBUG: Test goroutine started" << std::endl;
    
    // Set a timeout
    int64_t timer_id = __gots_set_timeout(reinterpret_cast<void*>(timer_callback), 1000);
    std::cout << "DEBUG: Timer scheduled with ID: " << timer_id << std::endl;
    
    std::cout << "DEBUG: Test goroutine main task done, will wait for timer" << std::endl;
}

int main() {
    std::cout << "=== Testing new goroutine system ===" << std::endl;
    
    // Initialize main goroutine context
    auto main_task = []() {
        std::cout << "DEBUG: Main goroutine running" << std::endl;
    };
    auto main_goroutine = std::make_shared<Goroutine>(0, main_task, nullptr);
    GoroutineScheduler::instance().set_main_goroutine(main_goroutine);
    current_goroutine = main_goroutine;
    
    // Spawn test goroutine
    std::cout << "DEBUG: Spawning test goroutine" << std::endl;
    auto goroutine = GoroutineScheduler::instance().spawn(test_goroutine);
    
    // Wait for all goroutines to complete
    std::cout << "DEBUG: Waiting for goroutines to complete..." << std::endl;
    GoroutineScheduler::instance().wait_all();
    
    std::cout << "=== Test completed ===" << std::endl;
    return 0;
}