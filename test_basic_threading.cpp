#include "runtime.h"
#include <iostream>

using namespace gots;

int main() {
    std::cout << "=== Testing basic threading without JIT ===\n";
    
    try {
        auto& scheduler = GoroutineScheduler::instance();
        std::cout << "Got scheduler instance\n";
        
        // Test 1: Simple function that returns a constant
        auto simple_lambda = []() -> int64_t {
            std::cout << "Simple lambda executing in thread\n";
            return 42;
        };
        
        std::cout << "About to spawn simple lambda\n";
        auto promise = scheduler.spawn(simple_lambda);
        std::cout << "Simple lambda spawned\n";
        
        auto result = promise->await<int64_t>();
        std::cout << "Simple lambda result: " << result << std::endl;
        
        std::cout << "✅ Basic threading test passed!\n";
        
    } catch (const std::exception& e) {
        std::cout << "❌ Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "❌ Unknown exception\n";
        return 1;
    }
    
    return 0;
}