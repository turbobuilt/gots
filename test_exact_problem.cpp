// Old goroutine system removed
#include <iostream>

using namespace gots;

// Callback function
extern "C" void timeout_callback() {
    std::cout << "Go timeout done" << std::endl;
}

// Goroutine function that sets timeout
extern "C" void goroutine_function() {
    std::cout << "DEBUG: Goroutine function starting" << std::endl;
    
    // Set timeout for 1 second
    __gots_set_timeout(reinterpret_cast<void*>(timeout_callback), 1000);
    
    std::cout << "DEBUG: Timeout set, goroutine function exiting" << std::endl;
}

int main() {
    std::cout << "=== Testing exact problem scenario ===" << std::endl;
    std::cout << "Simulating: go function() { setTimeout(function() { console.log(\"Go timeout done\") }, 1000) }" << std::endl;
    
    // Initialize system
    auto main_task = []() {};
    auto main_goroutine = std::make_shared<Goroutine>(0, main_task, nullptr);
    GoroutineScheduler::instance().set_main_goroutine(main_goroutine);
    current_goroutine = main_goroutine;
    
    // Spawn goroutine that sets timeout
    std::cout << "DEBUG: Spawning goroutine..." << std::endl;
    auto goroutine = GoroutineScheduler::instance().spawn([]() {
        goroutine_function();
    });
    
    // Wait for completion
    std::cout << "DEBUG: Waiting for goroutine and timer to complete..." << std::endl;
    GoroutineScheduler::instance().wait_all();
    
    std::cout << "=== Test completed successfully! ===" << std::endl;
    return 0;
}