#include "lexical_scope.h"
#include <iostream>
#include <cassert>

using namespace gots;

int main() {
    std::cout << "=== Testing C API for Lexical Scope ===" << std::endl;
    
    try {
        // Initialize thread-local scope chain
        __scope_init_thread_local(nullptr);
        
        // Test variable declaration and access
        __scope_declare_var("test_int", static_cast<int>(DataType::INT64), 1);
        __scope_set_var_int64("test_int", 42);
        
        int64_t value = __scope_get_var_int64("test_int");
        assert(value == 42);
        std::cout << "✓ C API int64 variable: " << value << std::endl;
        
        // Test string variables
        __scope_declare_var("test_string", static_cast<int>(DataType::STRING), 1);
        __scope_set_var_string("test_string", "Hello World");
        
        const char* str_value = __scope_get_var_string("test_string");
        std::cout << "✓ C API string variable: " << str_value << std::endl;
        
        // Test boolean variables
        __scope_declare_var("test_bool", static_cast<int>(DataType::BOOLEAN), 1);
        __scope_set_var_bool("test_bool", 1);
        
        int bool_value = __scope_get_var_bool("test_bool");
        assert(bool_value == 1);
        std::cout << "✓ C API boolean variable: " << bool_value << std::endl;
        
        // Test float variables
        __scope_declare_var("test_float", static_cast<int>(DataType::FLOAT64), 1);
        __scope_set_var_float64("test_float", 3.14159);
        
        double float_value = __scope_get_var_float64("test_float");
        assert(float_value == 3.14159);
        std::cout << "✓ C API float64 variable: " << float_value << std::endl;
        
        // Test variable existence check
        int exists = __scope_has_var("test_int");
        assert(exists == 1);
        std::cout << "✓ Variable existence check: " << exists << std::endl;
        
        int not_exists = __scope_has_var("nonexistent");
        assert(not_exists == 0);
        std::cout << "✓ Non-existent variable check: " << not_exists << std::endl;
        
        // Test nested scopes
        __scope_push(nullptr);  // Create new scope
        
        __scope_declare_var("nested_var", static_cast<int>(DataType::INT64), 1);
        __scope_set_var_int64("nested_var", 100);
        
        // Can access parent scope variables
        int64_t parent_value = __scope_get_var_int64("test_int");
        assert(parent_value == 42);
        std::cout << "✓ Access parent from nested scope: " << parent_value << std::endl;
        
        // Can access nested scope variables
        int64_t nested_value = __scope_get_var_int64("nested_var");
        assert(nested_value == 100);
        std::cout << "✓ Access nested scope variable: " << nested_value << std::endl;
        
        __scope_pop();  // Exit nested scope
        
        // Test scope capture for closure
        const char* var_names[] = {"test_int", "test_string"};
        void* captured_scope = __scope_capture_for_closure(var_names, 2);
        assert(captured_scope != nullptr);
        std::cout << "✓ Scope capture successful" << std::endl;
        
        // Test with captured scope in simulated goroutine
        __scope_init_thread_local(captured_scope);
        
        // Should be able to access captured variables
        int64_t captured_int = __scope_get_var_int64("test_int");
        assert(captured_int == 42);
        std::cout << "✓ Access captured variable: " << captured_int << std::endl;
        
        const char* captured_string = __scope_get_var_string("test_string");
        std::cout << "✓ Access captured string: " << captured_string << std::endl;
        
        // Modify captured variable
        __scope_set_var_int64("test_int", 84);
        int64_t modified_value = __scope_get_var_int64("test_int");
        assert(modified_value == 84);
        std::cout << "✓ Modified captured variable: " << modified_value << std::endl;
        
        __scope_cleanup_thread_local();
        
        // Cleanup
        delete static_cast<std::shared_ptr<LexicalScope>*>(captured_scope);
        
        std::cout << "\n✅ All C API tests passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "❌ C API test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}