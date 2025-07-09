#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include "compiler.h"

namespace gots {

// Forward declarations
class FunctionExpression;
class CodeGenerator;
class TypeInference;

struct FunctionInfo {
    std::string name;
    uint16_t function_id;  // Fast function ID for O(1) lookup
    std::shared_ptr<FunctionExpression> function_expr;
    void* address;
    size_t code_offset;
    size_t code_size;
    bool is_compiled;
    
    FunctionInfo(const std::string& n, std::shared_ptr<FunctionExpression> expr) 
        : name(n), function_id(0), function_expr(expr), address(nullptr), code_offset(0), code_size(0), is_compiled(false) {}
};

class FunctionCompilationManager {
public:
    static FunctionCompilationManager& instance();
    
    // Phase 1: Function Discovery
    void discover_functions(const std::vector<std::unique_ptr<ASTNode>>& ast);
    std::string register_function(std::shared_ptr<FunctionExpression> func_expr, const std::string& preferred_name = "");
    
    // Phase 2: Function Compilation
    void compile_all_functions(CodeGenerator& gen, TypeInference& types);
    void assign_function_addresses(void* executable_memory, size_t memory_size);
    
    // Phase 3: Execution Code Generation
    void* get_function_address(const std::string& function_name);
    size_t get_function_offset(const std::string& function_name); // NEW: Get relative offset
    uint16_t get_function_id(const std::string& function_name);
    bool is_function_compiled(const std::string& function_name);
    
    // Utility methods
    void clear();
    size_t get_total_function_code_size() const;
    void register_function_in_runtime();
    
    // Debug methods
    void print_function_registry() const;
    
private:
    FunctionCompilationManager() = default;
    
    std::unordered_map<std::string, std::unique_ptr<FunctionInfo>> functions_;
    std::vector<std::string> compilation_order_;
    size_t next_function_id_;
    size_t total_function_code_size_;
    
    void discover_functions_recursive(ASTNode* node);
    std::string generate_unique_function_name(const std::string& base_name);
    void compile_function_body(CodeGenerator& gen, TypeInference& types, FunctionInfo* func_info);
};

} // namespace gots