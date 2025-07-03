#include "runtime.h"
#include <iostream>
#include <thread>

using namespace gots;

void worker_with_promise(std::shared_ptr<Promise>* promise_ptr) {
    std::cout << "Worker: Starting..." << std::endl;
    auto promise = *promise_ptr;
    std::cout << "Worker: Got promise" << std::endl;
    
    promise->resolve(42);
    std::cout << "Worker: Resolved promise" << std::endl;
    
    delete promise_ptr;
}

int main() {
    std::cout << "=== Testing Promise with threads ===" << std::endl;
    
    try {
        auto promise = std::make_shared<Promise>();
        std::cout << "Main: Created promise" << std::endl;
        
        auto promise_ptr = new std::shared_ptr<Promise>(promise);
        std::cout << "Main: Created promise_ptr" << std::endl;
        
        std::cout << "Main: Creating thread..." << std::endl;
        std::thread worker(worker_with_promise, promise_ptr);
        std::cout << "Main: Thread created" << std::endl;
        
        worker.join();
        std::cout << "Main: Thread joined" << std::endl;
        
        auto result = promise->await<int64_t>();
        std::cout << "Main: Promise result: " << result << std::endl;
        
        std::cout << "\n✅ Test passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "❌ Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "❌ Unknown exception" << std::endl;
        return 1;
    }
    
    return 0;
}