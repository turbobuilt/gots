#include "runtime.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace gots;

// Test function to be run in goroutines
int64_t test_function(int64_t value) {
    std::cout << "Goroutine " << value << " running on thread " 
              << std::this_thread::get_id() << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return value * 2;
}

int main() {
    std::cout << "=== Testing Runtime Init/Cleanup ===" << std::endl;
    
    // Initialize runtime
    __runtime_init();
    
    // Register test function
    __register_function("test_function", (void*)test_function);
    
    // Spawn some goroutines
    std::cout << "Spawning goroutines..." << std::endl;
    auto promise1 = __goroutine_spawn_with_arg1("test_function", 10);
    auto promise2 = __goroutine_spawn_with_arg1("test_function", 20);
    auto promise3 = __goroutine_spawn_with_arg1("test_function", 30);
    
    // Wait for results
    if (promise1) {
        auto* p1 = static_cast<std::shared_ptr<Promise>*>(promise1);
        int64_t result1 = (*p1)->await<int64_t>();
        std::cout << "Result 1: " << result1 << std::endl;
        delete p1;
    }
    
    if (promise2) {
        auto* p2 = static_cast<std::shared_ptr<Promise>*>(promise2);
        int64_t result2 = (*p2)->await<int64_t>();
        std::cout << "Result 2: " << result2 << std::endl;
        delete p2;
    }
    
    if (promise3) {
        auto* p3 = static_cast<std::shared_ptr<Promise>*>(promise3);
        int64_t result3 = (*p3)->await<int64_t>();
        std::cout << "Result 3: " << result3 << std::endl;
        delete p3;
    }
    
    std::cout << "All goroutines completed." << std::endl;
    
    // Cleanup runtime
    __runtime_cleanup();
    
    std::cout << "Runtime cleanup completed." << std::endl;
    
    // Try to spawn another goroutine after cleanup (should create new scheduler)
    std::cout << "\nTesting re-initialization..." << std::endl;
    __runtime_init();
    
    auto promise4 = __goroutine_spawn_with_arg1("test_function", 40);
    if (promise4) {
        auto* p4 = static_cast<std::shared_ptr<Promise>*>(promise4);
        int64_t result4 = (*p4)->await<int64_t>();
        std::cout << "Result 4 (after re-init): " << result4 << std::endl;
        delete p4;
    }
    
    __runtime_cleanup();
    
    std::cout << "Test completed successfully!" << std::endl;
    return 0;
}