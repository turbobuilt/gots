#include "lexical_scope.h"
#include "runtime.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace gots;

// Test basic scope operations
void test_basic_scope() {
    std::cout << "=== Testing Basic Scope Operations ===" << std::endl;
    
    auto& chain = ScopeChain::get_thread_local_chain();
    
    // Test global scope
    chain.declare_variable("global_var", DataType::INT64, true);
    chain.set_variable("global_var", 42L);
    
    std::cout << "Global variable: " << chain.get_variable<int64_t>("global_var") << std::endl;
    
    // Test nested scope
    {
        ScopeGuard guard(&chain);
        
        chain.declare_variable("local_var", DataType::INT64, true);
        chain.set_variable("local_var", 100L);
        
        std::cout << "Local variable: " << chain.get_variable<int64_t>("local_var") << std::endl;
        std::cout << "Global from nested: " << chain.get_variable<int64_t>("global_var") << std::endl;
        
        // Modify global from nested scope
        chain.set_variable("global_var", 84L);
        std::cout << "Modified global: " << chain.get_variable<int64_t>("global_var") << std::endl;
    }
    
    // Back to global scope - local_var should not be accessible
    std::cout << "Global after nested: " << chain.get_variable<int64_t>("global_var") << std::endl;
    
    try {
        chain.get_variable<int64_t>("local_var");
        std::cout << "ERROR: local_var should not be accessible!" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Correctly caught: " << e.what() << std::endl;
    }
}

// Test type casting
void test_type_casting() {
    std::cout << "\n=== Testing Type Casting ===" << std::endl;
    
    auto& chain = ScopeChain::get_thread_local_chain();
    
    chain.declare_variable("number", DataType::INT32, true);
    chain.set_variable("number", 42);
    
    // Test casting to different types
    std::cout << "As int32: " << chain.get_variable<int32_t>("number") << std::endl;
    std::cout << "As int64: " << chain.get_variable<int64_t>("number") << std::endl;
    std::cout << "As double: " << chain.get_variable<double>("number") << std::endl;
    std::cout << "As float: " << chain.get_variable<float>("number") << std::endl;
}

// Test goroutine with captured scope
void test_goroutine_scope() {
    std::cout << "\n=== Testing Goroutine Scope Capture ===" << std::endl;
    
    auto& main_chain = ScopeChain::get_thread_local_chain();
    
    // Set up variables in main thread
    main_chain.declare_variable("shared_counter", DataType::INT64, true);
    main_chain.set_variable("shared_counter", 0L);
    
    main_chain.declare_variable("message", DataType::STRING, true);
    main_chain.set_variable("message", std::string("Hello from main thread"));
    
    std::cout << "Before goroutine - counter: " << main_chain.get_variable<int64_t>("shared_counter") << std::endl;
    std::cout << "Before goroutine - message: " << main_chain.get_variable<std::string>("message") << std::endl;
    
    // Capture current scope for goroutine
    auto captured_scope = main_chain.capture_current_scope();
    
    // Create a goroutine that modifies shared variables
    auto& scheduler = GoroutineScheduler::instance();
    auto promise = scheduler.spawn_with_scope([](){ 
        auto& goroutine_chain = ScopeChain::get_thread_local_chain();
        
        // Access and modify variables from parent scope
        int64_t current_counter = goroutine_chain.get_variable<int64_t>("shared_counter");
        std::cout << "Goroutine sees counter: " << current_counter << std::endl;
        
        goroutine_chain.set_variable("shared_counter", current_counter + 10);
        goroutine_chain.set_variable("message", std::string("Modified by goroutine"));
        
        std::cout << "Goroutine modified counter to: " << goroutine_chain.get_variable<int64_t>("shared_counter") << std::endl;
        std::cout << "Goroutine set message to: " << goroutine_chain.get_variable<std::string>("message") << std::endl;
        
        return 42;
    }, captured_scope);
    
    // Wait for goroutine to complete
    int result = promise->await<int>();
    
    // Check if main thread can see the modifications
    std::cout << "After goroutine - counter: " << main_chain.get_variable<int64_t>("shared_counter") << std::endl;
    std::cout << "After goroutine - message: " << main_chain.get_variable<std::string>("message") << std::endl;
    std::cout << "Goroutine returned: " << result << std::endl;
}

// Test concurrent access
void test_concurrent_access() {
    std::cout << "\n=== Testing Concurrent Access ===" << std::endl;
    
    auto global_scope = std::make_shared<LexicalScope>();
    global_scope->declare_variable("shared_data", DataType::INT64, true);
    global_scope->set_variable("shared_data", 0L);
    
    const int num_threads = 4;
    const int increments_per_thread = 1000;
    
    std::vector<std::thread> threads;
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([global_scope, increments_per_thread, i]() {
            ScopeChain::initialize_thread_local_chain(global_scope);
            auto& chain = ScopeChain::get_thread_local_chain();
            
            for (int j = 0; j < increments_per_thread; ++j) {
                int64_t current = chain.get_variable<int64_t>("shared_data");
                chain.set_variable("shared_data", current + 1);
            }
            
            ScopeChain::cleanup_thread_local_chain();
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "Expected: " << (num_threads * increments_per_thread) << std::endl;
    std::cout << "Actual: " << global_scope->get_variable<int64_t>("shared_data") << std::endl;
    std::cout << "Note: This may be less than expected due to race conditions - that's normal for this test" << std::endl;
}

int main() {
    try {
        test_basic_scope();
        test_type_casting();
        test_goroutine_scope();
        test_concurrent_access();
        
        std::cout << "\n=== All tests completed ===" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}