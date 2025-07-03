#include "runtime.h"
#include <iostream>

int main() {
    using namespace gots;
    
    std::cout << "Testing ThreadPool..." << std::endl;
    
    try {
        ThreadPool pool(2);
        
        auto future = pool.enqueue([]() -> int {
            std::cout << "Task executing..." << std::endl;
            return 42;
        });
        
        int result = future.get();
        std::cout << "Result: " << result << std::endl;
        
        pool.shutdown();
        std::cout << "ThreadPool test passed" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "ThreadPool test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
