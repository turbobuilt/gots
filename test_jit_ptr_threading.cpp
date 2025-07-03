#include "runtime.h"
#include <iostream>

using namespace gots;

// Simple test function that mimics a JIT function
extern "C" int64_t test_func(int64_t x) {
    return x * 2;
}

int main() {
    std::cout << "=== Testing function pointer capture in threads ===\n";
    
    try {
        auto& scheduler = GoroutineScheduler::instance();
        std::cout << "Got scheduler instance\n";
        
        // Get the function pointer
        void* func_ptr = reinterpret_cast<void*>(test_func);
        int64_t arg1 = 10;
        
        std::cout << "Function pointer: " << func_ptr << ", arg: " << arg1 << std::endl;
        
        // Test the exact same lambda pattern as in __goroutine_spawn_with_arg1
        auto lambda_with_ptr = [func_ptr, arg1]() -> int64_t {
            std::cout << "Lambda executing with func_ptr: " << func_ptr << ", arg: " << arg1 << std::endl;
            
            typedef int64_t (*FuncType1)(int64_t);
            FuncType1 func = reinterpret_cast<FuncType1>(func_ptr);
            
            auto result = func(arg1);
            std::cout << "Function returned: " << result << std::endl;
            return result;
        };
        
        std::cout << "About to spawn lambda with function pointer\n";
        auto promise = scheduler.spawn(lambda_with_ptr);
        std::cout << "Lambda spawned\n";
        
        auto result = promise->await<int64_t>();
        std::cout << "Lambda result: " << result << std::endl;
        
        std::cout << "✅ Function pointer threading test passed!\n";
        
    } catch (const std::exception& e) {
        std::cout << "❌ Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "❌ Unknown exception\n";
        return 1;
    }
    
    return 0;
}