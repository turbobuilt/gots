#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <queue>

namespace gots {

// Forward declarations
class FunctionExpression;
class CodeGenerator;
class TypeInference;

// Compilation context for handling nested functions safely
class CompilationContext {
public:
    struct FunctionInfo {
        std::shared_ptr<FunctionExpression> function;
        std::string name;
        std::vector<std::string> dependencies;  // Functions this function depends on
        bool compiled = false;
        size_t nesting_level = 0;
        
        FunctionInfo(std::shared_ptr<FunctionExpression> func, const std::string& func_name)
            : function(func), name(func_name) {}
    };
    
    // Register a function for compilation
    void register_function(std::shared_ptr<FunctionExpression> func, const std::string& name);
    
    // Compile all registered functions in dependency order
    void compile_all_functions(CodeGenerator& gen, TypeInference& types);
    
    // Get function info by name
    FunctionInfo* get_function_info(const std::string& name);
    
    // Clear all registered functions
    void clear();
    
    // Get compilation statistics
    size_t get_total_functions() const { return functions.size(); }
    size_t get_compiled_functions() const;
    
private:
    std::unordered_map<std::string, std::unique_ptr<FunctionInfo>> functions;
    std::vector<std::string> compilation_order;
    
    // Build dependency graph and determine compilation order
    void build_compilation_order();
    
    // Analyze function dependencies
    void analyze_dependencies(FunctionInfo& func_info);
    
    // Compile a single function
    void compile_function(FunctionInfo& func_info, CodeGenerator& gen, TypeInference& types);
};

// Global compilation context
extern CompilationContext g_compilation_context;

} // namespace gots