#include "runtime.h"
#include <iostream>

int main() {
    using namespace gots;
    
    std::cout << "Testing GoroutineScheduler..." << std::endl;
    
    try {
        auto& scheduler = GoroutineScheduler::instance();
        
        auto promise = scheduler.spawn([]() -> int64_t {
            std::cout << "Goroutine executing..." << std::endl;
            return 42;
        });
        
        int64_t result = promise->await<int64_t>();
        std::cout << "Result: " << result << std::endl;
        std::cout << "GoroutineScheduler test passed" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "GoroutineScheduler test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
