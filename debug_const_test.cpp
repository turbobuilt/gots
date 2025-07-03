#include "lexical_scope.h"
#include <iostream>

using namespace gots;

int main() {
    try {
        auto scope = std::make_shared<LexicalScope>();
        
        std::cout << "Testing const variable behavior..." << std::endl;
        
        // Declare const variable
        scope->declare_variable("const_var", DataType::INT64, false);  // false = not mutable
        std::cout << "✓ Declared const variable" << std::endl;
        
        // Set initial value
        scope->set_variable("const_var", 100L);
        std::cout << "✓ Set initial value: " << scope->get_variable<int64_t>("const_var") << std::endl;
        
        // Try to modify (should throw exception)
        try {
            scope->set_variable("const_var", 200L);
            std::cout << "✗ ERROR: Const variable was modified!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "✓ Correctly prevented const modification: " << e.what() << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "Unexpected error: " << e.what() << std::endl;
    }
    
    return 0;
}