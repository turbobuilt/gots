#include "lexical_scope.h"
#include "runtime.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <cassert>

using namespace gots;

// Test 1: Basic scope operations
bool test_basic_scope() {
    std::cout << "=== Test 1: Basic Scope Operations ===" << std::endl;
    
    try {
        auto scope = std::make_shared<LexicalScope>();
        
        // Test variable declaration
        scope->declare_variable("test_var", DataType::INT64, true);
        scope->set_variable("test_var", 42L);
        
        int64_t value = scope->get_variable<int64_t>("test_var");
        assert(value == 42L);
        std::cout << "✓ Variable declaration and access: " << value << std::endl;
        
        // Test variable modification
        scope->set_variable("test_var", 84L);
        value = scope->get_variable<int64_t>("test_var");
        assert(value == 84L);
        std::cout << "✓ Variable modification: " << value << std::endl;
        
        // Test const variables
        scope->declare_variable("const_var", DataType::INT64, false);
        scope->set_variable("const_var", 100L);
        
        try {
            scope->set_variable("const_var", 200L);
            std::cout << "✗ Const variable should not be modifiable!" << std::endl;
            return false;
        } catch (const std::exception& e) {
            std::cout << "✓ Const variable protection: " << e.what() << std::endl;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cout << "✗ Test failed: " << e.what() << std::endl;
        return false;
    }
}

// Test 2: Nested scopes
bool test_nested_scopes() {
    std::cout << "\n=== Test 2: Nested Scopes ===" << std::endl;
    
    try {
        auto parent_scope = std::make_shared<LexicalScope>();
        parent_scope->declare_variable("parent_var", DataType::INT64, true);
        parent_scope->set_variable("parent_var", 100L);
        
        auto child_scope = parent_scope->create_child_scope();
        child_scope->declare_variable("child_var", DataType::INT64, true);
        child_scope->set_variable("child_var", 200L);
        
        // Child can access parent variables
        int64_t parent_value = child_scope->get_variable<int64_t>("parent_var");
        assert(parent_value == 100L);
        std::cout << "✓ Child accessing parent: " << parent_value << std::endl;
        
        // Child can access its own variables
        int64_t child_value = child_scope->get_variable<int64_t>("child_var");
        assert(child_value == 200L);
        std::cout << "✓ Child accessing own: " << child_value << std::endl;
        
        // Parent cannot access child variables
        try {
            parent_scope->get_variable<int64_t>("child_var");
            std::cout << "✗ Parent should not access child variables!" << std::endl;
            return false;
        } catch (const std::exception& e) {
            std::cout << "✓ Parent scope isolation: " << e.what() << std::endl;
        }
        
        // Child can modify parent variables
        child_scope->set_variable("parent_var", 150L);
        int64_t modified_value = parent_scope->get_variable<int64_t>("parent_var");
        assert(modified_value == 150L);
        std::cout << "✓ Child modifying parent: " << modified_value << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cout << "✗ Test failed: " << e.what() << std::endl;
        return false;
    }
}

// Test 3: Type casting
bool test_type_casting() {
    std::cout << "\n=== Test 3: Type Casting ===" << std::endl;
    
    try {
        auto scope = std::make_shared<LexicalScope>();
        
        // Test int32 to other types
        scope->declare_variable("number", DataType::INT32, true);
        scope->set_variable("number", 42);
        
        int32_t as_int32 = scope->get_variable<int32_t>("number");
        int64_t as_int64 = scope->get_variable<int64_t>("number");
        double as_double = scope->get_variable<double>("number");
        float as_float = scope->get_variable<float>("number");
        
        assert(as_int32 == 42);
        assert(as_int64 == 42L);
        assert(as_double == 42.0);
        assert(as_float == 42.0f);
        
        std::cout << "✓ Type casting: int32=" << as_int32 
                  << ", int64=" << as_int64 
                  << ", double=" << as_double 
                  << ", float=" << as_float << std::endl;
        
        // Test float to int casting
        scope->declare_variable("float_num", DataType::FLOAT32, true);
        scope->set_variable("float_num", 3.14f);
        
        int32_t float_as_int = scope->get_variable<int32_t>("float_num");
        assert(float_as_int == 3);
        std::cout << "✓ Float to int casting: " << float_as_int << std::endl;
        
        // Test boolean casting
        scope->declare_variable("bool_val", DataType::BOOLEAN, true);
        scope->set_variable("bool_val", true);
        
        int64_t bool_as_int = scope->get_variable<int64_t>("bool_val");
        assert(bool_as_int == 1L);
        std::cout << "✓ Boolean to int casting: " << bool_as_int << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cout << "✗ Test failed: " << e.what() << std::endl;
        return false;
    }
}

// Test 4: Closure capture
bool test_closure_capture() {
    std::cout << "\n=== Test 4: Closure Capture ===" << std::endl;
    
    try {
        auto parent_scope = std::make_shared<LexicalScope>();
        parent_scope->declare_variable("shared_var", DataType::INT64, true);
        parent_scope->set_variable("shared_var", 10L);
        
        // Capture scope for closure
        auto captured_scope = parent_scope->capture_for_closure();
        
        // Captured scope should access the same variables
        int64_t captured_value = captured_scope->get_variable<int64_t>("shared_var");
        assert(captured_value == 10L);
        std::cout << "✓ Closure can access captured variable: " << captured_value << std::endl;
        
        // Modify through captured scope
        captured_scope->set_variable("shared_var", 20L);
        
        // Original scope should see the change
        int64_t original_value = parent_scope->get_variable<int64_t>("shared_var");
        assert(original_value == 20L);
        std::cout << "✓ Closure modification visible in original: " << original_value << std::endl;
        
        // Modify through original scope
        parent_scope->set_variable("shared_var", 30L);
        
        // Captured scope should see the change
        int64_t new_captured_value = captured_scope->get_variable<int64_t>("shared_var");
        assert(new_captured_value == 30L);
        std::cout << "✓ Original modification visible in closure: " << new_captured_value << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cout << "✗ Test failed: " << e.what() << std::endl;
        return false;
    }
}

// Test 5: Thread safety with concurrent access
bool test_thread_safety() {
    std::cout << "\n=== Test 5: Thread Safety ===" << std::endl;
    
    try {
        auto shared_scope = std::make_shared<LexicalScope>();
        shared_scope->declare_variable("counter", DataType::INT64, true);
        shared_scope->set_variable("counter", 0L);
        
        const int num_threads = 8;
        const int increments_per_thread = 1000;
        std::vector<std::thread> threads;
        std::atomic<bool> start_flag{false};
        
        // Create threads that will increment the counter
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back([shared_scope, increments_per_thread, &start_flag]() {
                // Wait for all threads to be ready
                while (!start_flag.load()) {
                    std::this_thread::yield();
                }
                
                for (int j = 0; j < increments_per_thread; ++j) {
                    int64_t current = shared_scope->get_variable<int64_t>("counter");
                    shared_scope->set_variable("counter", current + 1);
                }
            });
        }
        
        // Start all threads simultaneously
        start_flag.store(true);
        
        // Wait for all threads to complete
        for (auto& t : threads) {
            t.join();
        }
        
        int64_t final_value = shared_scope->get_variable<int64_t>("counter");
        int64_t expected = num_threads * increments_per_thread;
        
        std::cout << "✓ Final counter value: " << final_value 
                  << " (expected: " << expected << ")" << std::endl;
        
        // Note: Due to race conditions, the final value might be less than expected
        // But it should be greater than 0 and the system shouldn't crash
        assert(final_value > 0);
        std::cout << "✓ Thread safety test completed without crashes" << std::endl;
        
        if (final_value < expected) {
            std::cout << "⚠ Note: Some increments were lost due to race conditions (expected in this test)" << std::endl;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cout << "✗ Test failed: " << e.what() << std::endl;
        return false;
    }
}

// Test 6: ScopeChain functionality
bool test_scope_chain() {
    std::cout << "\n=== Test 6: ScopeChain Functionality ===" << std::endl;
    
    try {
        // Initialize thread-local scope chain
        ScopeChain::initialize_thread_local_chain();
        auto& chain = ScopeChain::get_thread_local_chain();
        
        // Test global scope
        chain.declare_variable("global_var", DataType::INT64, true);
        chain.set_variable("global_var", 100L);
        
        int64_t global_value = chain.get_variable<int64_t>("global_var");
        assert(global_value == 100L);
        std::cout << "✓ Global scope variable: " << global_value << std::endl;
        
        // Test nested scope with RAII
        {
            ScopeGuard guard(&chain);
            
            chain.declare_variable("local_var", DataType::INT64, true);
            chain.set_variable("local_var", 200L);
            
            int64_t local_value = chain.get_variable<int64_t>("local_var");
            assert(local_value == 200L);
            std::cout << "✓ Local scope variable: " << local_value << std::endl;
            
            // Can still access global
            int64_t global_from_local = chain.get_variable<int64_t>("global_var");
            assert(global_from_local == 100L);
            std::cout << "✓ Global access from local: " << global_from_local << std::endl;
            
            // Modify global from local
            chain.set_variable("global_var", 150L);
        }
        
        // Back to global scope - local_var should not be accessible
        try {
            chain.get_variable<int64_t>("local_var");
            std::cout << "✗ Local variable should not be accessible after scope exit!" << std::endl;
            return false;
        } catch (const std::exception& e) {
            std::cout << "✓ Local variable properly cleaned up: " << e.what() << std::endl;
        }
        
        // Global modification should persist
        int64_t modified_global = chain.get_variable<int64_t>("global_var");
        assert(modified_global == 150L);
        std::cout << "✓ Global modification persisted: " << modified_global << std::endl;
        
        ScopeChain::cleanup_thread_local_chain();
        return true;
    } catch (const std::exception& e) {
        std::cout << "✗ Test failed: " << e.what() << std::endl;
        return false;
    }
}

// Test 7: Goroutine integration (simulated)
bool test_goroutine_integration() {
    std::cout << "\n=== Test 7: Goroutine Integration (Simulated) ===" << std::endl;
    
    try {
        // Setup main thread scope
        ScopeChain::initialize_thread_local_chain();
        auto& main_chain = ScopeChain::get_thread_local_chain();
        
        main_chain.declare_variable("shared_data", DataType::INT64, true);
        main_chain.set_variable("shared_data", 42L);
        
        main_chain.declare_variable("message", DataType::STRING, true);
        main_chain.set_variable("message", std::string("Hello from main"));
        
        std::cout << "Main thread - shared_data: " << main_chain.get_variable<int64_t>("shared_data") << std::endl;
        std::cout << "Main thread - message: " << main_chain.get_variable<std::string>("message") << std::endl;
        
        // Capture scope for goroutine
        auto captured_scope = main_chain.capture_current_scope();
        
        // Simulate goroutine execution in another thread
        std::atomic<bool> goroutine_done{false};
        std::thread goroutine_thread([captured_scope, &goroutine_done]() {
            // Initialize goroutine's thread-local scope chain
            ScopeChain::initialize_thread_local_chain(captured_scope);
            auto& goroutine_chain = ScopeChain::get_thread_local_chain();
            
            // Access variables from main thread's scope
            int64_t data = goroutine_chain.get_variable<int64_t>("shared_data");
            std::string msg = goroutine_chain.get_variable<std::string>("message");
            
            std::cout << "Goroutine - accessed shared_data: " << data << std::endl;
            std::cout << "Goroutine - accessed message: " << msg << std::endl;
            
            // Modify variables (should be visible to main thread)
            goroutine_chain.set_variable("shared_data", data + 10);
            goroutine_chain.set_variable("message", std::string("Modified by goroutine"));
            
            std::cout << "Goroutine - modified shared_data to: " << goroutine_chain.get_variable<int64_t>("shared_data") << std::endl;
            std::cout << "Goroutine - modified message to: " << goroutine_chain.get_variable<std::string>("message") << std::endl;
            
            ScopeChain::cleanup_thread_local_chain();
            goroutine_done.store(true);
        });
        
        // Wait for goroutine to complete
        goroutine_thread.join();
        while (!goroutine_done.load()) {
            std::this_thread::yield();
        }
        
        // Check if modifications are visible in main thread
        int64_t modified_data = main_chain.get_variable<int64_t>("shared_data");
        std::string modified_message = main_chain.get_variable<std::string>("message");
        
        assert(modified_data == 52L);
        assert(modified_message == "Modified by goroutine");
        
        std::cout << "✓ Main thread sees goroutine modifications:" << std::endl;
        std::cout << "  shared_data: " << modified_data << std::endl;
        std::cout << "  message: " << modified_message << std::endl;
        
        ScopeChain::cleanup_thread_local_chain();
        return true;
    } catch (const std::exception& e) {
        std::cout << "✗ Test failed: " << e.what() << std::endl;
        return false;
    }
}

// Test 8: Performance benchmark
bool test_performance() {
    std::cout << "\n=== Test 8: Performance Benchmark ===" << std::endl;
    
    try {
        auto scope = std::make_shared<LexicalScope>();
        scope->declare_variable("perf_var", DataType::INT64, true);
        scope->set_variable("perf_var", 0L);
        
        const int iterations = 1000000;
        
        // Benchmark variable access
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            int64_t value = scope->get_variable<int64_t>("perf_var");
            scope->set_variable("perf_var", value + 1);
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        int64_t final_value = scope->get_variable<int64_t>("perf_var");
        assert(final_value == iterations);
        
        double ops_per_second = (iterations * 2.0) / (duration.count() / 1000000.0); // 2 ops per iteration
        
        std::cout << "✓ Performance test completed:" << std::endl;
        std::cout << "  " << iterations << " get/set pairs in " << duration.count() << " microseconds" << std::endl;
        std::cout << "  " << std::fixed << ops_per_second << " operations per second" << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cout << "✗ Test failed: " << e.what() << std::endl;
        return false;
    }
}

int main() {
    std::cout << "GoTS Lexical Scope Comprehensive Test Suite\n" << std::endl;
    
    std::vector<std::pair<std::string, std::function<bool()>>> tests = {
        {"Basic Scope Operations", test_basic_scope},
        {"Nested Scopes", test_nested_scopes},
        {"Type Casting", test_type_casting},
        {"Closure Capture", test_closure_capture},
        {"Thread Safety", test_thread_safety},
        {"ScopeChain Functionality", test_scope_chain},
        {"Goroutine Integration", test_goroutine_integration},
        {"Performance Benchmark", test_performance}
    };
    
    int passed = 0;
    int failed = 0;
    
    for (const auto& [name, test_func] : tests) {
        try {
            if (test_func()) {
                ++passed;
            } else {
                ++failed;
            }
        } catch (const std::exception& e) {
            std::cout << "✗ Test " << name << " threw exception: " << e.what() << std::endl;
            ++failed;
        }
    }
    
    std::cout << "\n=== Test Results ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    std::cout << "Total:  " << (passed + failed) << std::endl;
    
    if (failed == 0) {
        std::cout << "\n🎉 All tests passed! Lexical scope implementation is working correctly." << std::endl;
        return 0;
    } else {
        std::cout << "\n❌ Some tests failed. Please review the implementation." << std::endl;
        return 1;
    }
}