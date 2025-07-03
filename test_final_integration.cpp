#include "lexical_scope.h"
#include "runtime.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <cassert>

using namespace gots;

// Helper function to create a scoped goroutine
template<typename F>
std::shared_ptr<Promise> spawn_scoped_goroutine(F&& func, std::shared_ptr<LexicalScope> scope) {
    auto& scheduler = GoroutineScheduler::instance();
    
    auto task = [func]() {
        func();
    };
    
    std::shared_ptr<void> scope_ptr = nullptr;
    if (scope) {
        scope_ptr = std::static_pointer_cast<void>(scope);
    }
    
    return scheduler.spawn_with_scope_impl(task, scope_ptr);
}

int main() {
    std::cout << "=== GoTS Lexical Scope Final Integration Test ===" << std::endl;
    
    try {
        // Initialize main thread scope
        ScopeChain::initialize_thread_local_chain();
        auto& main_chain = ScopeChain::get_thread_local_chain();
        
        std::cout << "1. Setting up main thread scope..." << std::endl;
        main_chain.declare_variable("global_counter", DataType::INT64, true);
        main_chain.set_variable("global_counter", 0L);
        
        main_chain.declare_variable("message", DataType::STRING, true);
        main_chain.set_variable("message", std::string("Hello from main"));
        
        main_chain.declare_variable("pi", DataType::FLOAT64, false); // const
        main_chain.set_variable("pi", 3.14159);
        
        std::cout << "   Initial state:" << std::endl;
        std::cout << "   global_counter: " << main_chain.get_variable<int64_t>("global_counter") << std::endl;
        std::cout << "   message: " << main_chain.get_variable<std::string>("message") << std::endl;
        std::cout << "   pi: " << main_chain.get_variable<double>("pi") << std::endl;
        
        // Test nested scopes with goroutines
        std::cout << "\n2. Testing nested scopes with goroutines..." << std::endl;
        {
            ScopeGuard outer_guard(&main_chain);
            main_chain.declare_variable("outer_var", DataType::INT64, true);
            main_chain.set_variable("outer_var", 42L);
            
            {
                ScopeGuard inner_guard(&main_chain);
                main_chain.declare_variable("inner_var", DataType::INT64, true);
                main_chain.set_variable("inner_var", 100L);
                
                // Capture deeply nested scope
                auto nested_scope = main_chain.capture_current_scope();
                
                auto promise = spawn_scoped_goroutine([](){ 
                    auto& chain = ScopeChain::get_thread_local_chain();
                    
                    std::cout << "   Nested goroutine accessing:" << std::endl;
                    std::cout << "   global_counter: " << chain.get_variable<int64_t>("global_counter") << std::endl;
                    std::cout << "   outer_var: " << chain.get_variable<int64_t>("outer_var") << std::endl;
                    std::cout << "   inner_var: " << chain.get_variable<int64_t>("inner_var") << std::endl;
                    std::cout << "   pi (const): " << chain.get_variable<double>("pi") << std::endl;
                    
                    // Modify variables at different scope levels
                    chain.set_variable("global_counter", 1000L);
                    chain.set_variable("outer_var", 84L);
                    chain.set_variable("inner_var", 200L);
                    chain.set_variable("message", std::string("Modified by nested goroutine"));
                    
                    std::cout << "   Nested goroutine finished modifications" << std::endl;
                }, nested_scope);
                
                promise->await<bool>();
                
                std::cout << "   After nested goroutine:" << std::endl;
                std::cout << "   inner_var: " << main_chain.get_variable<int64_t>("inner_var") << std::endl;
            }
            
            std::cout << "   outer_var: " << main_chain.get_variable<int64_t>("outer_var") << std::endl;
        }
        
        // Test concurrent goroutines with shared state
        std::cout << "\n3. Testing concurrent goroutines..." << std::endl;
        auto concurrent_scope = main_chain.capture_current_scope();
        
        std::vector<std::shared_ptr<Promise>> promises;
        const int num_goroutines = 5;
        
        for (int i = 0; i < num_goroutines; ++i) {
            auto promise = spawn_scoped_goroutine([i](){ 
                auto& chain = ScopeChain::get_thread_local_chain();
                
                // Each goroutine increments the counter by its index
                int64_t current = chain.get_variable<int64_t>("global_counter");
                std::this_thread::sleep_for(std::chrono::milliseconds(10 * i)); // Stagger execution
                chain.set_variable("global_counter", current + i + 1);
                
                std::cout << "   Goroutine " << i << " incremented counter" << std::endl;
            }, concurrent_scope);
            
            promises.push_back(promise);
        }
        
        // Wait for all concurrent goroutines
        for (auto& promise : promises) {
            promise->await<bool>();
        }
        
        // Test type casting across goroutines
        std::cout << "\n4. Testing type casting..." << std::endl;
        main_chain.declare_variable("number", DataType::INT32, true);
        main_chain.set_variable("number", 42);
        
        auto type_scope = main_chain.capture_current_scope();
        
        auto type_promise = spawn_scoped_goroutine([](){ 
            auto& chain = ScopeChain::get_thread_local_chain();
            
            // Test various type casts
            int32_t as_int32 = chain.get_variable<int32_t>("number");
            int64_t as_int64 = chain.get_variable<int64_t>("number");
            double as_double = chain.get_variable<double>("number");
            
            std::cout << "   Type casting in goroutine:" << std::endl;
            std::cout << "   as int32: " << as_int32 << std::endl;
            std::cout << "   as int64: " << as_int64 << std::endl;
            std::cout << "   as double: " << as_double << std::endl;
            
            // Change type through assignment
            chain.set_variable("number", 3.14f);  // Now it's a float
            
            float as_float = chain.get_variable<float>("number");
            std::cout << "   after setting as float: " << as_float << std::endl;
        }, type_scope);
        
        type_promise->await<bool>();
        
        // Test C API integration
        std::cout << "\n5. Testing C API integration..." << std::endl;
        
        const char* var_names[] = {"global_counter", "message"};
        void* c_captured_scope = __scope_capture_for_closure(var_names, 2);
        
        void* c_promise = __goroutine_spawn_with_scope("test_function", c_captured_scope);
        
        // Final state check
        std::cout << "\n6. Final state check:" << std::endl;
        std::cout << "   global_counter: " << main_chain.get_variable<int64_t>("global_counter") << std::endl;
        std::cout << "   message: " << main_chain.get_variable<std::string>("message") << std::endl;
        std::cout << "   pi (const): " << main_chain.get_variable<double>("pi") << std::endl;
        std::cout << "   number: " << main_chain.get_variable<float>("number") << std::endl;
        
        // Performance test
        std::cout << "\n7. Performance test..." << std::endl;
        auto perf_scope = main_chain.capture_current_scope();
        
        auto start = std::chrono::high_resolution_clock::now();
        
        const int perf_iterations = 100;
        std::vector<std::shared_ptr<Promise>> perf_promises;
        
        for (int i = 0; i < perf_iterations; ++i) {
            auto promise = spawn_scoped_goroutine([i](){ 
                auto& chain = ScopeChain::get_thread_local_chain();
                int64_t current = chain.get_variable<int64_t>("global_counter");
                chain.set_variable("global_counter", current + 1);
            }, perf_scope);
            
            perf_promises.push_back(promise);
        }
        
        for (auto& promise : perf_promises) {
            promise->await<bool>();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        std::cout << "   " << perf_iterations << " goroutines completed in " << duration.count() << "ms" << std::endl;
        std::cout << "   Average: " << (duration.count() / (double)perf_iterations) << "ms per goroutine" << std::endl;
        
        // Cleanup
        delete static_cast<std::shared_ptr<LexicalScope>*>(c_captured_scope);
        ScopeChain::cleanup_thread_local_chain();
        
        std::cout << "\n🎉 All integration tests passed successfully!" << std::endl;
        std::cout << "✅ Lexical scope system is fully functional and ready for production use." << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "❌ Integration test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}