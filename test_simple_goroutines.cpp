#include "lexical_scope.h"
#include "runtime.h"
#include <iostream>
#include <functional>

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
    std::cout << "=== Testing Simplified Goroutine Integration ===" << std::endl;
    
    try {
        // Initialize main thread scope
        ScopeChain::initialize_thread_local_chain();
        auto& main_chain = ScopeChain::get_thread_local_chain();
        
        // Setup shared variables
        main_chain.declare_variable("shared_counter", DataType::INT64, true);
        main_chain.set_variable("shared_counter", 10L);
        
        std::cout << "Initial counter: " << main_chain.get_variable<int64_t>("shared_counter") << std::endl;
        
        // Capture scope for goroutines
        auto captured_scope = main_chain.capture_current_scope();
        
        // Test 1: Simple goroutine with scope access
        std::cout << "\n--- Test: Goroutine with Scope Access ---" << std::endl;
        
        auto promise = spawn_scoped_goroutine([](){ 
            auto& chain = ScopeChain::get_thread_local_chain();
            
            std::cout << "Goroutine starting..." << std::endl;
            
            // Access and modify the shared counter
            int64_t current = chain.get_variable<int64_t>("shared_counter");
            std::cout << "Goroutine read counter: " << current << std::endl;
            
            chain.set_variable("shared_counter", current + 100);
            std::cout << "Goroutine incremented counter to: " << chain.get_variable<int64_t>("shared_counter") << std::endl;
            
            std::cout << "Goroutine completed." << std::endl;
        }, captured_scope);
        
        // Wait for goroutine to complete
        bool result = promise->await<bool>();
        std::cout << "Goroutine returned: " << (result ? "success" : "failed") << std::endl;
        
        // Check if modification is visible
        std::cout << "Main thread sees counter: " << main_chain.get_variable<int64_t>("shared_counter") << std::endl;
        
        // Test 2: Multiple sequential goroutines
        std::cout << "\n--- Test: Multiple Sequential Goroutines ---" << std::endl;
        
        for (int i = 0; i < 3; ++i) {
            auto promise = spawn_scoped_goroutine([i](){ 
                auto& chain = ScopeChain::get_thread_local_chain();
                
                int64_t current = chain.get_variable<int64_t>("shared_counter");
                chain.set_variable("shared_counter", current + (i + 1));
                
                std::cout << "Goroutine " << i << " incremented counter by " << (i + 1) << std::endl;
            }, captured_scope);
            
            promise->await<bool>();
        }
        
        std::cout << "Final counter after sequential goroutines: " << main_chain.get_variable<int64_t>("shared_counter") << std::endl;
        
        ScopeChain::cleanup_thread_local_chain();
        
        std::cout << "\n✅ Simplified goroutine integration test passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "❌ Test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}