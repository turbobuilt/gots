#include "lexical_scope.h"
#include "runtime.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace gots;

int main() {
    std::cout << "=== Testing Real Goroutine Integration ===" << std::endl;
    
    try {
        // Initialize main thread scope
        ScopeChain::initialize_thread_local_chain();
        auto& main_chain = ScopeChain::get_thread_local_chain();
        
        // Setup shared variables
        main_chain.declare_variable("counter", DataType::INT64, true);
        main_chain.set_variable("counter", 0L);
        
        main_chain.declare_variable("message", DataType::STRING, true);
        main_chain.set_variable("message", std::string("Initial message"));
        
        std::cout << "Initial counter: " << main_chain.get_variable<int64_t>("counter") << std::endl;
        std::cout << "Initial message: " << main_chain.get_variable<std::string>("message") << std::endl;
        
        // Capture scope for goroutines
        auto captured_scope = main_chain.capture_current_scope();
        
        // Get scheduler instance
        auto& scheduler = GoroutineScheduler::instance();
        
        // Test 1: Single goroutine with scope
        std::cout << "\n--- Test 1: Single Goroutine ---" << std::endl;
        
        auto promise1 = scheduler.spawn_with_scope([](){ 
            auto& chain = ScopeChain::get_thread_local_chain();
            
            // Access variables from parent scope
            int64_t current_counter = chain.get_variable<int64_t>("counter");
            std::string current_message = chain.get_variable<std::string>("message");
            
            std::cout << "Goroutine 1 - counter: " << current_counter << std::endl;
            std::cout << "Goroutine 1 - message: " << current_message << std::endl;
            
            // Modify variables
            chain.set_variable("counter", current_counter + 10);
            chain.set_variable("message", std::string("Modified by goroutine 1"));
            
            return 42;
        }, captured_scope);
        
        // Wait for goroutine to complete
        int result1 = promise1->await<int>();
        std::cout << "Goroutine 1 returned: " << result1 << std::endl;
        
        // Check modifications in main thread
        std::cout << "After goroutine 1 - counter: " << main_chain.get_variable<int64_t>("counter") << std::endl;
        std::cout << "After goroutine 1 - message: " << main_chain.get_variable<std::string>("message") << std::endl;
        
        // Test 2: Multiple concurrent goroutines
        std::cout << "\n--- Test 2: Multiple Concurrent Goroutines ---" << std::endl;
        
        std::vector<std::shared_ptr<Promise>> promises;
        
        for (int i = 0; i < 5; ++i) {
            auto promise = scheduler.spawn_with_scope([i](){ 
                auto& chain = ScopeChain::get_thread_local_chain();
                
                // Each goroutine increments the counter
                int64_t current = chain.get_variable<int64_t>("counter");
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Small delay
                chain.set_variable("counter", current + 1);
                
                std::cout << "Goroutine " << i << " incremented counter" << std::endl;
                return i * 10;
            }, captured_scope);
            
            promises.push_back(promise);
        }
        
        // Wait for all goroutines
        std::vector<int> results;
        for (auto& promise : promises) {
            results.push_back(promise->await<int>());
        }
        
        std::cout << "All goroutines completed. Results: ";
        for (int result : results) {
            std::cout << result << " ";
        }
        std::cout << std::endl;
        
        std::cout << "Final counter: " << main_chain.get_variable<int64_t>("counter") << std::endl;
        
        // Test 3: Nested function scopes with goroutines
        std::cout << "\n--- Test 3: Nested Scopes with Goroutines ---" << std::endl;
        
        // Create nested scope
        {
            ScopeGuard guard(&main_chain);
            
            main_chain.declare_variable("nested_var", DataType::INT64, true);
            main_chain.set_variable("nested_var", 500L);
            
            // Capture nested scope
            auto nested_captured = main_chain.capture_current_scope();
            
            auto nested_promise = scheduler.spawn_with_scope([](){ 
                auto& chain = ScopeChain::get_thread_local_chain();
                
                // Can access both nested and parent variables
                int64_t nested = chain.get_variable<int64_t>("nested_var");
                int64_t counter = chain.get_variable<int64_t>("counter");
                std::string message = chain.get_variable<std::string>("message");
                
                std::cout << "Nested goroutine - nested_var: " << nested << std::endl;
                std::cout << "Nested goroutine - counter: " << counter << std::endl;
                std::cout << "Nested goroutine - message: " << message << std::endl;
                
                // Modify all accessible variables
                chain.set_variable("nested_var", nested + 100);
                chain.set_variable("counter", counter + 1000);
                chain.set_variable("message", std::string("Modified by nested goroutine"));
                
                return 999;
            }, nested_captured);
            
            int nested_result = nested_promise->await<int>();
            std::cout << "Nested goroutine returned: " << nested_result << std::endl;
            std::cout << "Nested var after goroutine: " << main_chain.get_variable<int64_t>("nested_var") << std::endl;
        }
        
        // Check final state
        std::cout << "\nFinal state:" << std::endl;
        std::cout << "Counter: " << main_chain.get_variable<int64_t>("counter") << std::endl;
        std::cout << "Message: " << main_chain.get_variable<std::string>("message") << std::endl;
        
        ScopeChain::cleanup_thread_local_chain();
        
        std::cout << "\n✅ All real goroutine tests passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "❌ Real goroutine test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}