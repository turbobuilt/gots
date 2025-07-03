#include "lexical_scope.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>

namespace gots {

// Static member initialization
std::atomic<uint64_t> LexicalScope::next_scope_id{1};
thread_local std::unique_ptr<ScopeChain> ScopeChain::thread_local_chain;

// VariableBinding implementation
void VariableBinding::cleanup_value(void* ptr, DataType type) {
    if (!ptr) return;
    
    switch (type) {
        case DataType::INT8:
            delete static_cast<int8_t*>(ptr);
            break;
        case DataType::INT16:
            delete static_cast<int16_t*>(ptr);
            break;
        case DataType::INT32:
            delete static_cast<int32_t*>(ptr);
            break;
        case DataType::INT64:
            delete static_cast<int64_t*>(ptr);
            break;
        case DataType::UINT8:
            delete static_cast<uint8_t*>(ptr);
            break;
        case DataType::UINT16:
            delete static_cast<uint16_t*>(ptr);
            break;
        case DataType::UINT32:
            delete static_cast<uint32_t*>(ptr);
            break;
        case DataType::UINT64:
            delete static_cast<uint64_t*>(ptr);
            break;
        case DataType::FLOAT32:
            delete static_cast<float*>(ptr);
            break;
        case DataType::FLOAT64:
            delete static_cast<double*>(ptr);
            break;
        case DataType::BOOLEAN:
            delete static_cast<bool*>(ptr);
            break;
        case DataType::STRING:
            delete static_cast<std::string*>(ptr);
            break;
        default:
            // For unknown types, assume it's a generic pointer
            delete static_cast<char*>(ptr);
            break;
    }
}

template<typename T>
T VariableBinding::perform_type_cast(void* ptr, DataType from_type) const {
    // High-performance type casting with GoTS semantics
    // Types "cast up" - int32 + float32 = float64 (for precision)
    
    switch (from_type) {
        case DataType::INT8: {
            int8_t val = *static_cast<int8_t*>(ptr);
            if constexpr (std::is_same_v<T, double>) return static_cast<double>(val);
            if constexpr (std::is_same_v<T, float>) return static_cast<float>(val);
            if constexpr (std::is_same_v<T, int64_t>) return static_cast<int64_t>(val);
            if constexpr (std::is_same_v<T, int32_t>) return static_cast<int32_t>(val);
            break;
        }
        case DataType::INT16: {
            int16_t val = *static_cast<int16_t*>(ptr);
            if constexpr (std::is_same_v<T, double>) return static_cast<double>(val);
            if constexpr (std::is_same_v<T, float>) return static_cast<float>(val);
            if constexpr (std::is_same_v<T, int64_t>) return static_cast<int64_t>(val);
            if constexpr (std::is_same_v<T, int32_t>) return static_cast<int32_t>(val);
            break;
        }
        case DataType::INT32: {
            int32_t val = *static_cast<int32_t*>(ptr);
            if constexpr (std::is_same_v<T, double>) return static_cast<double>(val);
            if constexpr (std::is_same_v<T, float>) return static_cast<float>(val);
            if constexpr (std::is_same_v<T, int64_t>) return static_cast<int64_t>(val);
            break;
        }
        case DataType::INT64: {
            int64_t val = *static_cast<int64_t*>(ptr);
            if constexpr (std::is_same_v<T, double>) return static_cast<double>(val);
            if constexpr (std::is_same_v<T, float>) return static_cast<float>(val);
            break;
        }
        case DataType::FLOAT32: {
            float val = *static_cast<float*>(ptr);
            if constexpr (std::is_same_v<T, double>) return static_cast<double>(val);
            if constexpr (std::is_same_v<T, int64_t>) return static_cast<int64_t>(val);
            if constexpr (std::is_same_v<T, int32_t>) return static_cast<int32_t>(val);
            break;
        }
        case DataType::FLOAT64: {
            double val = *static_cast<double*>(ptr);
            if constexpr (std::is_same_v<T, float>) return static_cast<float>(val);
            if constexpr (std::is_same_v<T, int64_t>) return static_cast<int64_t>(val);
            if constexpr (std::is_same_v<T, int32_t>) return static_cast<int32_t>(val);
            break;
        }
        case DataType::BOOLEAN: {
            bool val = *static_cast<bool*>(ptr);
            if constexpr (std::is_same_v<T, int64_t>) return val ? 1 : 0;
            if constexpr (std::is_same_v<T, int32_t>) return val ? 1 : 0;
            if constexpr (std::is_same_v<T, double>) return val ? 1.0 : 0.0;
            if constexpr (std::is_same_v<T, float>) return val ? 1.0f : 0.0f;
            break;
        }
        default:
            break;
    }
    
    throw std::runtime_error("Invalid type cast for variable: " + name);
}

// Explicit template instantiations for common types
template int32_t VariableBinding::perform_type_cast<int32_t>(void* ptr, DataType from_type) const;
template int64_t VariableBinding::perform_type_cast<int64_t>(void* ptr, DataType from_type) const;
template float VariableBinding::perform_type_cast<float>(void* ptr, DataType from_type) const;
template double VariableBinding::perform_type_cast<double>(void* ptr, DataType from_type) const;
template bool VariableBinding::perform_type_cast<bool>(void* ptr, DataType from_type) const;
template std::string VariableBinding::perform_type_cast<std::string>(void* ptr, DataType from_type) const;

// LexicalScope implementation
void LexicalScope::declare_variable(const std::string& name, DataType type, bool is_mutable) {
    std::unique_lock<std::shared_mutex> lock(scope_mutex);
    
    // Check for redeclaration in current scope
    if (variables.find(name) != variables.end()) {
        throw std::runtime_error("Variable already declared in current scope: " + name);
    }
    
    variables[name] = std::make_shared<VariableBinding>(name, type, is_mutable);
}

bool LexicalScope::has_variable(const std::string& name) const {
    return find_variable_in_chain(name) != nullptr;
}

bool LexicalScope::has_local_variable(const std::string& name) const {
    return find_local_variable(name) != nullptr;
}

std::shared_ptr<LexicalScope> LexicalScope::create_child_scope() {
    return std::make_shared<LexicalScope>(shared_from_this());
}

std::shared_ptr<LexicalScope> LexicalScope::capture_for_closure(const std::vector<std::string>& captured_vars) {
    // For goroutines, we need to create a scope that REFERENCES the current scope chain
    // This allows goroutines to access and modify variables from their lexical environment
    // just like JavaScript functions, not taking snapshots but sharing the actual variables
    
    // Create a new scope that has the current scope as its parent
    // This maintains the lexical scope chain for the goroutine
    auto captured_scope = std::make_shared<LexicalScope>(shared_from_this());
    
    // The key insight: goroutines should access the SAME variable bindings as their parent scope
    // This means they can read and write to variables in their lexical environment
    // We don't copy values - we share references to the actual VariableBinding objects
    
    return captured_scope;
}

void LexicalScope::dump_scope(int depth) const {
    std::shared_lock<std::shared_mutex> lock(scope_mutex);
    
    std::string indent(depth * 2, ' ');
    std::cout << indent << "Scope " << scope_id.load() << " (variables: " << variables.size() << ")" << std::endl;
    
    for (const auto& [name, binding] : variables) {
        std::cout << indent << "  " << name << " (type: " << static_cast<int>(binding->type) 
                  << ", initialized: " << binding->is_initialized.load() << ")" << std::endl;
    }
    
    if (parent_scope) {
        std::cout << indent << "Parent:" << std::endl;
        parent_scope->dump_scope(depth + 1);
    }
}

std::shared_ptr<VariableBinding> LexicalScope::find_variable_in_chain(const std::string& name) const {
    auto current = shared_from_this();
    while (current) {
        auto binding = current->find_local_variable(name);
        if (binding) {
            return binding;
        }
        current = current->parent_scope;
    }
    return nullptr;
}

std::shared_ptr<VariableBinding> LexicalScope::find_local_variable(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(scope_mutex);
    auto it = variables.find(name);
    return (it != variables.end()) ? it->second : nullptr;
}

void* LexicalScope::copy_value_by_type(void* value, DataType type) const {
    switch (type) {
        case DataType::INT8:
            return new int8_t(*static_cast<int8_t*>(value));
        case DataType::INT16:
            return new int16_t(*static_cast<int16_t*>(value));
        case DataType::INT32:
            return new int32_t(*static_cast<int32_t*>(value));
        case DataType::INT64:
            return new int64_t(*static_cast<int64_t*>(value));
        case DataType::UINT8:
            return new uint8_t(*static_cast<uint8_t*>(value));
        case DataType::UINT16:
            return new uint16_t(*static_cast<uint16_t*>(value));
        case DataType::UINT32:
            return new uint32_t(*static_cast<uint32_t*>(value));
        case DataType::UINT64:
            return new uint64_t(*static_cast<uint64_t*>(value));
        case DataType::FLOAT32:
            return new float(*static_cast<float*>(value));
        case DataType::FLOAT64:
            return new double(*static_cast<double*>(value));
        case DataType::BOOLEAN:
            return new bool(*static_cast<bool*>(value));
        case DataType::STRING:
            return new std::string(*static_cast<std::string*>(value));
        default:
            return nullptr;
    }
}

// ScopeChain implementation
ScopeChain::ScopeChain() {
    global_scope = std::make_shared<LexicalScope>();
    current_scope = global_scope;
}

void ScopeChain::push_scope(std::shared_ptr<LexicalScope> scope) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    if (scope) {
        current_scope = scope;
    } else {
        current_scope = current_scope->create_child_scope();
    }
}

void ScopeChain::pop_scope() {
    std::lock_guard<std::mutex> lock(chain_mutex);
    
    if (current_scope && current_scope != global_scope) {
        auto parent = current_scope->get_parent();
        current_scope = parent ? parent : global_scope;
    }
}

std::shared_ptr<LexicalScope> ScopeChain::get_current_scope() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    return current_scope;
}

std::shared_ptr<LexicalScope> ScopeChain::get_global_scope() const {
    return global_scope;
}

void ScopeChain::declare_variable(const std::string& name, DataType type, bool is_mutable) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    if (!current_scope) {
        throw std::runtime_error("No active scope");
    }
    current_scope->declare_variable(name, type, is_mutable);
}

bool ScopeChain::has_variable(const std::string& name) const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    return current_scope && current_scope->has_variable(name);
}

ScopeChain& ScopeChain::get_thread_local_chain() {
    if (!thread_local_chain) {
        thread_local_chain = std::make_unique<ScopeChain>();
    }
    return *thread_local_chain;
}

void ScopeChain::initialize_thread_local_chain(std::shared_ptr<LexicalScope> captured_scope) {
    if (captured_scope) {
        thread_local_chain = std::make_unique<ScopeChain>();
        thread_local_chain->current_scope = captured_scope;
        thread_local_chain->global_scope = captured_scope;
    } else {
        thread_local_chain = std::make_unique<ScopeChain>();
    }
}

void ScopeChain::cleanup_thread_local_chain() {
    thread_local_chain.reset();
}

std::shared_ptr<LexicalScope> ScopeChain::capture_current_scope(const std::vector<std::string>& captured_vars) {
    std::lock_guard<std::mutex> lock(chain_mutex);
    if (!current_scope) {
        throw std::runtime_error("No active scope");
    }
    return current_scope->capture_for_closure(captured_vars);
}

void ScopeChain::dump_chain() const {
    std::lock_guard<std::mutex> lock(chain_mutex);
    std::cout << "Scope Chain:" << std::endl;
    if (current_scope) {
        current_scope->dump_scope(0);
    } else {
        std::cout << "  No active scope" << std::endl;
    }
}

// Runtime C interface
extern "C" {

void* __scope_create(void* parent_scope) {
    auto parent = parent_scope ? *static_cast<std::shared_ptr<LexicalScope>*>(parent_scope) : nullptr;
    auto scope = std::make_shared<LexicalScope>(parent);
    return new std::shared_ptr<LexicalScope>(scope);
}

void __scope_destroy(void* scope) {
    if (scope) {
        delete static_cast<std::shared_ptr<LexicalScope>*>(scope);
    }
}

void __scope_push(void* scope) {
    auto& chain = ScopeChain::get_thread_local_chain();
    if (scope) {
        auto scope_ptr = *static_cast<std::shared_ptr<LexicalScope>*>(scope);
        chain.push_scope(scope_ptr);
    } else {
        chain.push_scope();
    }
}

void __scope_pop() {
    auto& chain = ScopeChain::get_thread_local_chain();
    chain.pop_scope();
}

void __scope_declare_var(const char* name, int type, int is_mutable) {
    auto& chain = ScopeChain::get_thread_local_chain();
    chain.declare_variable(std::string(name), static_cast<DataType>(type), is_mutable != 0);
}

void __scope_set_var_int64(const char* name, int64_t value) {
    auto& chain = ScopeChain::get_thread_local_chain();
    chain.set_variable(std::string(name), value);
}

void __scope_set_var_float64(const char* name, double value) {
    auto& chain = ScopeChain::get_thread_local_chain();
    chain.set_variable(std::string(name), value);
}

void __scope_set_var_string(const char* name, const char* value) {
    auto& chain = ScopeChain::get_thread_local_chain();
    chain.set_variable(std::string(name), std::string(value));
}

void __scope_set_var_bool(const char* name, int value) {
    auto& chain = ScopeChain::get_thread_local_chain();
    chain.set_variable(std::string(name), value != 0);
}

int64_t __scope_get_var_int64(const char* name) {
    auto& chain = ScopeChain::get_thread_local_chain();
    return chain.get_variable<int64_t>(std::string(name));
}

double __scope_get_var_float64(const char* name) {
    auto& chain = ScopeChain::get_thread_local_chain();
    return chain.get_variable<double>(std::string(name));
}

const char* __scope_get_var_string(const char* name) {
    auto& chain = ScopeChain::get_thread_local_chain();
    static std::string result = chain.get_variable<std::string>(std::string(name));
    return result.c_str();
}

int __scope_get_var_bool(const char* name) {
    auto& chain = ScopeChain::get_thread_local_chain();
    return chain.get_variable<bool>(std::string(name)) ? 1 : 0;
}

int __scope_has_var(const char* name) {
    auto& chain = ScopeChain::get_thread_local_chain();
    return chain.has_variable(std::string(name)) ? 1 : 0;
}

void* __scope_capture_for_closure(const char** var_names, int var_count) {
    auto& chain = ScopeChain::get_thread_local_chain();
    
    std::vector<std::string> captured_vars;
    for (int i = 0; i < var_count; ++i) {
        captured_vars.emplace_back(var_names[i]);
    }
    
    auto captured_scope = chain.capture_current_scope(captured_vars);
    return new std::shared_ptr<LexicalScope>(captured_scope);
}

void __scope_init_thread_local(void* captured_scope) {
    if (captured_scope) {
        auto scope_ptr = *static_cast<std::shared_ptr<LexicalScope>*>(captured_scope);
        ScopeChain::initialize_thread_local_chain(scope_ptr);
    } else {
        ScopeChain::initialize_thread_local_chain();
    }
}

void __scope_cleanup_thread_local() {
    ScopeChain::cleanup_thread_local_chain();
}

}

}