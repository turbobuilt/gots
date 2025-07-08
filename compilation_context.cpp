#include "compilation_context.h"
#include "compiler.h"
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include <functional>

namespace gots {

// Global compilation context instance
CompilationContext g_compilation_context;

void CompilationContext::register_function(std::shared_ptr<FunctionExpression> func, const std::string& name) {
    if (!func) {
        std::cerr << "ERROR: Attempting to register null function: " << name << std::endl;
        return;
    }
    
    std::cout << "DEBUG: Registering function for compilation: " << name << std::endl;
    
    auto func_info = std::make_unique<FunctionInfo>(func, name);
    functions[name] = std::move(func_info);
}

void CompilationContext::compile_all_functions(CodeGenerator& gen, TypeInference& types) {
    if (functions.empty()) {
        std::cout << "DEBUG: No functions to compile" << std::endl;
        return;
    }
    
    std::cout << "DEBUG: Starting new compilation framework with " << functions.size() << " functions" << std::endl;
    
    // Build dependency graph and compilation order
    build_compilation_order();
    
    // Compile functions in dependency order
    size_t compiled_count = 0;
    for (const std::string& func_name : compilation_order) {
        auto it = functions.find(func_name);
        if (it != functions.end() && !it->second->compiled) {
            std::cout << "DEBUG: Compiling function: " << func_name << std::endl;
            compile_function(*it->second, gen, types);
            compiled_count++;
        }
    }
    
    std::cout << "DEBUG: New compilation framework completed. Compiled " << compiled_count << " functions" << std::endl;
}

void CompilationContext::build_compilation_order() {
    compilation_order.clear();
    
    // First pass: analyze dependencies for all functions
    for (auto& pair : functions) {
        analyze_dependencies(*pair.second);
    }
    
    // Second pass: topological sort to determine compilation order
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> in_stack;
    
    std::function<void(const std::string&)> dfs = [&](const std::string& func_name) {
        if (in_stack.count(func_name)) {
            std::cout << "DEBUG: Circular dependency detected involving: " << func_name << std::endl;
            return;
        }
        
        if (visited.count(func_name)) {
            return;
        }
        
        visited.insert(func_name);
        in_stack.insert(func_name);
        
        auto it = functions.find(func_name);
        if (it != functions.end()) {
            for (const std::string& dep : it->second->dependencies) {
                dfs(dep);
            }
        }
        
        in_stack.erase(func_name);
        compilation_order.push_back(func_name);
    };
    
    // Start DFS from all functions
    for (const auto& pair : functions) {
        if (!visited.count(pair.first)) {
            dfs(pair.first);
        }
    }
    
    std::cout << "DEBUG: Compilation order determined: ";
    for (const std::string& name : compilation_order) {
        std::cout << name << " ";
    }
    std::cout << std::endl;
}

void CompilationContext::analyze_dependencies(FunctionInfo& func_info) {
    func_info.dependencies.clear();
    
    // For now, assume no dependencies (leaf functions first)
    // In a full implementation, we would analyze the function body
    // to find calls to other user-defined functions
    
    std::cout << "DEBUG: Function " << func_info.name << " has " << func_info.dependencies.size() << " dependencies" << std::endl;
}

void CompilationContext::compile_function(FunctionInfo& func_info, CodeGenerator& gen, TypeInference& types) {
    if (func_info.compiled) {
        return;
    }
    
    try {
        // Compile the function body
        func_info.function->compile_function_body(gen, types, func_info.name);
        func_info.compiled = true;
        std::cout << "DEBUG: Successfully compiled function: " << func_info.name << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Failed to compile function " << func_info.name << ": " << e.what() << std::endl;
        throw;
    }
}

CompilationContext::FunctionInfo* CompilationContext::get_function_info(const std::string& name) {
    auto it = functions.find(name);
    return (it != functions.end()) ? it->second.get() : nullptr;
}

void CompilationContext::clear() {
    functions.clear();
    compilation_order.clear();
    std::cout << "DEBUG: Compilation context cleared" << std::endl;
}

size_t CompilationContext::get_compiled_functions() const {
    size_t count = 0;
    for (const auto& pair : functions) {
        if (pair.second->compiled) {
            count++;
        }
    }
    return count;
}

} // namespace gots