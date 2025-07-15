#pragma once

#include <unordered_map>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <type_traits>
#include "compiler.h"

namespace gots {

// Forward declaration
class ScopeChain;

// Represents a single variable binding with thread-safe access
struct VariableBinding {
    std::string name;
    DataType type;
    std::atomic<void*> value_ptr{nullptr};
    std::atomic<bool> is_initialized{false};
    std::atomic<bool> is_mutable{true};
    std::atomic<bool> has_been_set{false};
    mutable std::shared_mutex access_mutex;
    
    // Type information for runtime casting
    std::atomic<DataType> runtime_type;
    
    VariableBinding(const std::string& var_name, DataType var_type, bool mutable_var = true) 
        : name(var_name), type(var_type), is_mutable(mutable_var), runtime_type(var_type) {}
    
    ~VariableBinding() {
        void* ptr = value_ptr.load();
        if (ptr) {
            // Safe cleanup based on type
            cleanup_value(ptr, runtime_type.load());
        }
    }
    
    // Thread-safe value operations
    template<typename T>
    void set_value(T&& value) {
        std::unique_lock<std::shared_mutex> lock(access_mutex);
        if (!is_mutable.load() && has_been_set.load()) {
            throw std::runtime_error("Cannot modify const variable: " + name);
        }
        
        // Cleanup old value
        void* old_ptr = value_ptr.load();
        if (old_ptr) {
            cleanup_value(old_ptr, runtime_type.load());
        }
        
        // Store new value with type information
        using ValueType = typename std::remove_reference<T>::type;
        ValueType* new_ptr = new ValueType(std::forward<T>(value));
        value_ptr.store(new_ptr);
        runtime_type.store(get_data_type<ValueType>());
        is_initialized.store(true);
        has_been_set.store(true);
    }
    
    template<typename T>
    T get_value() const {
        std::shared_lock<std::shared_mutex> lock(access_mutex);
        if (!is_initialized.load()) {
            throw std::runtime_error("Variable not initialized: " + name);
        }
        
        void* ptr = value_ptr.load();
        if (!ptr) {
            throw std::runtime_error("Null pointer for variable: " + name);
        }
        
        // Type-safe casting
        DataType current_type = runtime_type.load();
        if (current_type != get_data_type<T>()) {
            // Attempt automatic type conversion
            return perform_type_cast<T>(ptr, current_type);
        }
        
        return *static_cast<T*>(ptr);
    }
    
    // Note: Memory managed by GC, no manual reference counting needed

private:
    void cleanup_value(void* ptr, DataType type);
    
    template<typename T>
    constexpr DataType get_data_type() const {
        if constexpr (std::is_same_v<T, int8_t>) return DataType::INT8;
        else if constexpr (std::is_same_v<T, int16_t>) return DataType::INT16;
        else if constexpr (std::is_same_v<T, int32_t>) return DataType::INT32;
        else if constexpr (std::is_same_v<T, int64_t>) return DataType::INT64;
        else if constexpr (std::is_same_v<T, uint8_t>) return DataType::UINT8;
        else if constexpr (std::is_same_v<T, uint16_t>) return DataType::UINT16;
        else if constexpr (std::is_same_v<T, uint32_t>) return DataType::UINT32;
        else if constexpr (std::is_same_v<T, uint64_t>) return DataType::UINT64;
        else if constexpr (std::is_same_v<T, float>) return DataType::FLOAT32;
        else if constexpr (std::is_same_v<T, double>) return DataType::FLOAT64;
        else if constexpr (std::is_same_v<T, bool>) return DataType::BOOLEAN;
        else if constexpr (std::is_same_v<T, std::string>) return DataType::STRING;
        else return DataType::UNKNOWN;
    }
    
    template<typename T>
    T perform_type_cast(void* ptr, DataType from_type) const;
};

// Thread-safe scope for variable bindings
class LexicalScope : public std::enable_shared_from_this<LexicalScope> {
private:
    std::unordered_map<std::string, std::shared_ptr<VariableBinding>> variables;
    std::shared_ptr<LexicalScope> parent_scope;
    mutable std::shared_mutex scope_mutex;
    std::atomic<uint64_t> scope_id;
    static std::atomic<uint64_t> next_scope_id;

public:
    explicit LexicalScope(std::shared_ptr<LexicalScope> parent = nullptr) 
        : parent_scope(parent), scope_id(next_scope_id.fetch_add(1)) {}
    
    ~LexicalScope() = default;
    
    // Variable management
    void declare_variable(const std::string& name, DataType type, bool is_mutable = true);
    
    template<typename T>
    void set_variable(const std::string& name, T&& value) {
        auto binding = find_variable_in_chain(name);
        if (!binding) {
            throw std::runtime_error("Undefined variable: " + name);
        }
        binding->set_value(std::forward<T>(value));
    }
    
    template<typename T>
    T get_variable(const std::string& name) const {
        auto binding = find_variable_in_chain(name);
        if (!binding) {
            throw std::runtime_error("Undefined variable: " + name);
        }
        return binding->template get_value<T>();
    }
    
    bool has_variable(const std::string& name) const;
    bool has_local_variable(const std::string& name) const;
    
    // Scope chain management
    std::shared_ptr<LexicalScope> create_child_scope();
    std::shared_ptr<LexicalScope> get_parent() const { return parent_scope; }
    uint64_t get_id() const { return scope_id.load(); }
    
    // Note: Memory managed by GC, no manual reference counting needed
    
    // Closure capture - creates a reference to current scope for goroutines (NOT a snapshot)
    // This allows goroutines to access and modify variables from their lexical environment
    std::shared_ptr<LexicalScope> capture_for_closure(const std::vector<std::string>& captured_vars = {});
    
    // Debug information
    void dump_scope(int depth = 0) const;

private:
    std::shared_ptr<VariableBinding> find_variable_in_chain(const std::string& name) const;
    std::shared_ptr<VariableBinding> find_local_variable(const std::string& name) const;
    void* copy_value_by_type(void* value, DataType type) const;
};

// Thread-safe scope chain manager
class ScopeChain {
private:
    std::shared_ptr<LexicalScope> current_scope;
    std::shared_ptr<LexicalScope> global_scope;
    mutable std::mutex chain_mutex;
    
    // Thread-local storage for per-goroutine scope chains
    thread_local static std::unique_ptr<ScopeChain> thread_local_chain;

public:
    ScopeChain();
    ~ScopeChain() = default;
    
    // Scope management
    void push_scope(std::shared_ptr<LexicalScope> scope = nullptr);
    void pop_scope();
    std::shared_ptr<LexicalScope> get_current_scope() const;
    std::shared_ptr<LexicalScope> get_global_scope() const;
    
    // Variable operations
    void declare_variable(const std::string& name, DataType type, bool is_mutable = true);
    
    template<typename T>
    void set_variable(const std::string& name, T&& value) {
        std::lock_guard<std::mutex> lock(chain_mutex);
        if (!current_scope) {
            throw std::runtime_error("No active scope");
        }
        current_scope->set_variable(name, std::forward<T>(value));
    }
    
    template<typename T>
    T get_variable(const std::string& name) const {
        std::lock_guard<std::mutex> lock(chain_mutex);
        if (!current_scope) {
            throw std::runtime_error("No active scope");
        }
        return current_scope->template get_variable<T>(name);
    }
    
    bool has_variable(const std::string& name) const;
    
    // Thread-local scope chain management for goroutines
    static ScopeChain& get_thread_local_chain();
    static void initialize_thread_local_chain(std::shared_ptr<LexicalScope> captured_scope = nullptr);
    static void cleanup_thread_local_chain();
    
    // Closure support
    std::shared_ptr<LexicalScope> capture_current_scope(const std::vector<std::string>& captured_vars = {});
    
    // Debug
    void dump_chain() const;
};

// RAII scope guard for automatic scope management
class ScopeGuard {
private:
    ScopeChain* chain;
    bool should_pop;

public:
    explicit ScopeGuard(ScopeChain* scope_chain, std::shared_ptr<LexicalScope> scope = nullptr) 
        : chain(scope_chain), should_pop(true) {
        if (chain) {
            chain->push_scope(scope);
        }
    }
    
    ~ScopeGuard() {
        if (chain && should_pop) {
            chain->pop_scope();
        }
    }
    
    // Disable copy, enable move
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    
    ScopeGuard(ScopeGuard&& other) noexcept : chain(other.chain), should_pop(other.should_pop) {
        other.should_pop = false;
    }
    
    ScopeGuard& operator=(ScopeGuard&& other) noexcept {
        if (this != &other) {
            if (chain && should_pop) {
                chain->pop_scope();
            }
            chain = other.chain;
            should_pop = other.should_pop;
            other.should_pop = false;
        }
        return *this;
    }
    
    void release() { should_pop = false; }
};

// Runtime functions for scope management
extern "C" {
    void* __scope_create(void* parent_scope);
    void __scope_destroy(void* scope);
    void __scope_push(void* scope);
    void __scope_pop();
    
    void __scope_declare_var(const char* name, int type, int is_mutable);
    void __scope_set_var_int64(const char* name, int64_t value);
    void __scope_set_var_float64(const char* name, double value);
    void __scope_set_var_string(const char* name, const char* value);
    void __scope_set_var_bool(const char* name, int value);
    
    int64_t __scope_get_var_int64(const char* name);
    double __scope_get_var_float64(const char* name);
    const char* __scope_get_var_string(const char* name);
    int __scope_get_var_bool(const char* name);
    
    int __scope_has_var(const char* name);
    void* __scope_capture_for_closure(const char** var_names, int var_count);
    void __scope_init_thread_local(void* captured_scope);
    void __scope_cleanup_thread_local();
}

}