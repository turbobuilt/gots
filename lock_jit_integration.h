#pragma once

#include "compiler.h"
#include "lock_system.h"
#include <string>
#include <unordered_map>
#include <functional>

namespace gots {

// Lock operation types for JIT compilation
enum class LockOperation {
    CREATE,
    ACQUIRE,
    RELEASE,
    TRY_ACQUIRE,
    TRY_ACQUIRE_TIMEOUT,
    IS_LOCKED_BY_CURRENT
};

// Lock JIT compiler integration
class LockJITCompiler {
public:
    // Check if an identifier or expression is a Lock operation
    static bool is_lock_operation(const std::string& object_name, const std::string& method_name);
    static bool is_lock_constructor(const std::string& expression);
    
    // Get lock operation type from method name
    static LockOperation get_lock_operation(const std::string& method_name);
    
    // Generate optimized assembly for lock operations
    static void emit_lock_operation(CodeGenerator& gen, LockOperation op, 
                                  int lock_reg, int arg_reg = -1, int result_reg = -1);
    
    // Register management for lock objects
    static int allocate_lock_register(CodeGenerator& gen);
    static void deallocate_lock_register(CodeGenerator& gen, int reg);
    
    // Type checking for lock operations
    static bool validate_lock_operation(LockOperation op, const std::vector<DataType>& arg_types);
    
    // Optimization: inline common lock patterns
    static bool try_emit_lock_pattern(CodeGenerator& gen, const std::string& pattern);
    
private:
    static std::unordered_map<std::string, LockOperation> method_map_;
    static void initialize_method_map();
};

// Lock-aware AST nodes for high-performance compilation
struct LockCreation : ExpressionNode {
    LockCreation() {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct LockMethodCall : ExpressionNode {
    std::string lock_variable;
    LockOperation operation;
    std::vector<std::unique_ptr<ExpressionNode>> arguments;
    
    LockMethodCall(const std::string& var, LockOperation op) 
        : lock_variable(var), operation(op) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

// Lock pattern recognition for common usage patterns
class LockPatternOptimizer {
public:
    // Detect and optimize common lock patterns
    static bool optimize_lock_guard_pattern(CodeGenerator& gen, TypeInference& types);
    static bool optimize_critical_section_pattern(CodeGenerator& gen, TypeInference& types);
    static bool optimize_producer_consumer_pattern(CodeGenerator& gen, TypeInference& types);
    
    // Pattern matching
    struct LockPattern {
        std::string name;
        std::function<bool(CodeGenerator&, TypeInference&)> optimizer;
    };
    
    static std::vector<LockPattern> patterns_;
    static void register_pattern(const std::string& name, 
                               std::function<bool(CodeGenerator&, TypeInference&)> optimizer);
};

// High-performance lock allocation pool
class LockAllocationPool {
public:
    // Allocate locks from object pool for zero-overhead allocation
    static void* allocate_lock();
    static void deallocate_lock(void* lock_ptr);
    
    // Pre-allocate locks for JIT-compiled functions
    static void preallocate_locks(size_t count);
    
    // Integration with garbage collector
    static void register_with_gc();
    
    // Thread cleanup to prevent memory leaks
    static void cleanup_thread_local_pools();
    static void register_thread_cleanup_handler();
    
private:
    static constexpr size_t POOL_SIZE = 1024;
    static constexpr size_t LOCK_SIZE = sizeof(Lock);
    
    static thread_local char lock_pool_[POOL_SIZE * LOCK_SIZE];
    static thread_local bool lock_used_[POOL_SIZE];
    static thread_local size_t next_free_index_;
};

// Integration with the runtime type system
void register_lock_type_with_jit();

// Lock-specific optimizations
namespace LockOptimizations {
    // Eliminate unnecessary lock operations
    bool eliminate_redundant_locks(CodeGenerator& gen);
    
    // Combine adjacent lock operations
    bool combine_lock_operations(CodeGenerator& gen);
    
    // Convert locks to lock-free algorithms where possible
    bool convert_to_lockfree(CodeGenerator& gen);
    
    // Optimize lock ordering to prevent deadlocks
    bool optimize_lock_ordering(CodeGenerator& gen);
}

} // namespace gots