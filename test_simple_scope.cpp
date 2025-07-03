#include "lexical_scope.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace gots;

// Simple test that doesn't depend on runtime
int main() {
    try {
        std::cout << "=== Testing Basic Lexical Scope ===" << std::endl;
        
        // Create a scope chain manually
        auto global_scope = std::make_shared<LexicalScope>();
        
        // Test variable declaration and access
        global_scope->declare_variable("test_var", DataType::INT64, true);
        global_scope->set_variable("test_var", 42L);
        
        std::cout << "Variable value: " << global_scope->get_variable<int64_t>("test_var") << std::endl;
        
        // Test nested scopes
        auto child_scope = global_scope->create_child_scope();
        child_scope->declare_variable("child_var", DataType::FLOAT64, true);
        child_scope->set_variable("child_var", 3.14);
        
        // Child can access parent variables
        std::cout << "Child accessing parent: " << child_scope->get_variable<int64_t>("test_var") << std::endl;
        std::cout << "Child variable: " << child_scope->get_variable<double>("child_var") << std::endl;
        
        // Test closure capture
        auto captured_scope = global_scope->capture_for_closure();
        std::cout << "Captured scope can access: " << captured_scope->get_variable<int64_t>("test_var") << std::endl;
        
        // Modify through captured scope
        captured_scope->set_variable("test_var", 84L);
        std::cout << "After modification through captured scope: " << global_scope->get_variable<int64_t>("test_var") << std::endl;
        
        // Test type casting
        global_scope->declare_variable("number", DataType::INT32, true);
        global_scope->set_variable("number", 123);
        
        std::cout << "As int32: " << global_scope->get_variable<int32_t>("number") << std::endl;
        std::cout << "As int64: " << global_scope->get_variable<int64_t>("number") << std::endl;
        std::cout << "As double: " << global_scope->get_variable<double>("number") << std::endl;
        
        std::cout << "\n=== Test completed successfully ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}