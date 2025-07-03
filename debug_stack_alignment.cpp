#include "runtime.h"
#include <iostream>

// Test function with specific alignment requirements
extern "C" int64_t test_alignment() {
    // Check if stack is 16-byte aligned
    void* stack_ptr;
    __asm__ volatile("movq %%rsp, %0" : "=r"(stack_ptr));
    
    uintptr_t stack_addr = reinterpret_cast<uintptr_t>(stack_ptr);
    if (stack_addr % 16 != 0) {
        std::cout << "ERROR: Stack not 16-byte aligned! Address: 0x" << std::hex << stack_addr << std::endl;
        return -1;
    }
    
    std::cout << "Stack is properly aligned at 0x" << std::hex << stack_addr << std::endl;
    return 42;
}

int main() {
    using namespace gots;
    
    std::cout << "Testing stack alignment..." << std::endl;
    
    // Test direct call
    std::cout << "Direct call:" << std::endl;
    int64_t result = test_alignment();
    std::cout << "Result: " << result << std::endl;
    
    // Test from thread
    std::cout << "Thread call:" << std::endl;
    try {
        auto& scheduler = GoroutineScheduler::instance();
        
        auto promise = scheduler.spawn([]() -> int64_t {
            return test_alignment();
        });
        
        result = promise->await<int64_t>();
        std::cout << "Thread result: " << result << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "Thread test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}