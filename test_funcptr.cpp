#include "runtime.h"
#include <iostream>

// Simple test function
extern "C" int64_t test_func(int64_t n) {
    return n * 2;
}

int main() {
    using namespace gots;
    
    std::cout << "Testing function pointer call from thread..." << std::endl;
    
    try {
        auto& scheduler = GoroutineScheduler::instance();
        
        // Get function pointer
        typedef int64_t (*FuncType)(int64_t);
        void* func_ptr = reinterpret_cast<void*>(test_func);
        
        auto promise = scheduler.spawn([func_ptr]() -> int64_t {
            std::cout << "Thread executing function pointer..." << std::endl;
            
            typedef int64_t (*FuncType)(int64_t);
            auto func = reinterpret_cast<FuncType>(func_ptr);
            int64_t result = func(21);
            
            std::cout << "Function returned: " << result << std::endl;
            return result;
        });
        
        int64_t result = promise->await<int64_t>();
        std::cout << "Final result: " << result << std::endl;
        std::cout << "Function pointer test passed" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Function pointer test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
