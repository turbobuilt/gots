#pragma once

#include "ultra_fast_jit.h"
#include "high_performance_scheduler.h"
#include "simd_optimizations.h"
#include "gc_optimized_barriers.h"

namespace gots {

// ============================================================================
// PERFORMANCE INTEGRATION LAYER
// ============================================================================

class PerformanceEngine {
private:
    std::unique_ptr<LockFreeWorkStealingScheduler> scheduler_;
    std::unique_ptr<AllocationProfiler> profiler_;
    std::unique_ptr<AdaptiveLoadBalancer> load_balancer_;
    
    // Performance statistics
    std::atomic<uint64_t> fast_allocations_{0};
    std::atomic<uint64_t> jit_compilations_{0};
    std::atomic<uint64_t> simd_operations_{0};
    std::atomic<uint64_t> lock_free_operations_{0};
    
public:
    PerformanceEngine() {
        // Initialize all performance subsystems
        scheduler_ = std::make_unique<LockFreeWorkStealingScheduler>();
        profiler_ = std::make_unique<AllocationProfiler>();
        load_balancer_ = std::make_unique<AdaptiveLoadBalancer>();
        
        // Check hardware capabilities
        if (SIMDOptimizations::is_avx2_supported()) {
            std::cout << "[PERF] AVX2 SIMD optimizations enabled\n";
        }
        
        // Initialize optimized write barriers
        OptimizedWriteBarrier::initialize(nullptr, 0, 512);
    }
    
    // ============================================================================
    // ULTRA-FAST ALLOCATION WITH JIT INTEGRATION
    // ============================================================================
    
    template<typename T>
    T* allocate_optimized(size_t count = 1) {
        size_t size = sizeof(T) * count;
        uint32_t type_id = get_type_id<T>();
        
        // Profile allocation pattern
        profiler_->record_allocation(size, type_id, count > 1, needs_heap_allocation<T>());
        
        // Use JIT-optimized allocation if pattern is hot
        auto hot_patterns = profiler_->get_hot_patterns(10);
        for (const auto& pattern : hot_patterns) {
            if (pattern.size == size && pattern.type_id == type_id) {
                // Emit JIT-optimized allocation code
                return allocate_with_jit_pattern<T>(pattern, count);
            }
        }
        
        // Fall back to optimized standard allocation
        void* ptr = allocate_fast_path(size, type_id, count > 1);
        fast_allocations_.fetch_add(1, std::memory_order_relaxed);
        
        return static_cast<T*>(ptr);
    }
    
    // ============================================================================
    // JIT-COMPILED HOT PATHS
    // ============================================================================
    
    void compile_hot_functions() {
        auto hot_patterns = profiler_->get_hot_patterns(20);
        
        for (const auto& pattern : hot_patterns) {
            if (pattern.frequency > 1000) { // Compile if used frequently
                compile_allocation_sequence(pattern);
                jit_compilations_.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    
    // ============================================================================
    // SIMD-ACCELERATED OPERATIONS
    // ============================================================================
    
    void process_gc_operations_simd() {
        if (!SIMDOptimizations::is_avx2_supported()) {
            return;
        }
        
        // SIMD-optimized card table scanning
        uint8_t* card_table = OptimizedWriteBarrier::get_card_table();
        size_t card_count = OptimizedWriteBarrier::get_card_table_size();
        
        uint32_t dirty_cards[1024];
        size_t found = SIMDOptimizations::scan_dirty_cards_avx2(
            card_table, card_count, dirty_cards, 1024
        );
        
        // Process dirty cards
        for (size_t i = 0; i < found; ++i) {
            process_dirty_card(dirty_cards[i]);
        }
        
        // Clear processed cards using SIMD
        SIMDOptimizations::clear_cards_avx2(card_table, card_count);
        
        simd_operations_.fetch_add(1, std::memory_order_relaxed);
    }
    
    // ============================================================================
    // LOCK-FREE GOROUTINE SCHEDULING
    // ============================================================================
    
    void schedule_goroutine_optimized(std::shared_ptr<Goroutine> goroutine) {
        scheduler_->schedule(goroutine);
        lock_free_operations_.fetch_add(1, std::memory_order_relaxed);
        
        // Adaptive load balancing
        load_balancer_->balance_load(scheduler_.get());
    }
    
    // ============================================================================
    // VARIABLE ACCESS OPTIMIZATION
    // ============================================================================
    
    template<typename T>
    T get_variable_optimized(const std::string& name, LexicalScope* scope) {
        // Check if variable access can be JIT-optimized
        static std::unordered_map<std::string, uint32_t> variable_offsets;
        
        auto it = variable_offsets.find(name);
        if (it != variable_offsets.end()) {
            // Direct memory access - no hash lookup needed
            return get_variable_direct<T>(it->second, scope);
        }
        
        // Fall back to standard lookup and cache the offset
        T result = scope->get_variable<T>(name);
        
        // Cache variable offset for future JIT compilation
        uint32_t offset = calculate_variable_offset(name, scope);
        variable_offsets[name] = offset;
        
        return result;
    }
    
    // ============================================================================
    // STRING OPERATIONS WITH SIMD
    // ============================================================================
    
    bool compare_strings_optimized(const std::string& a, const std::string& b) {
        if (a.length() != b.length()) {
            return false;
        }
        
        if (SIMDOptimizations::is_avx2_supported() && a.length() >= 32) {
            return SIMDOptimizations::strings_equal_avx2(
                a.c_str(), b.c_str(), a.length()
            );
        }
        
        return a == b;
    }
    
    uint64_t hash_string_optimized(const std::string& str) {
        if (SIMDOptimizations::is_avx2_supported() && str.length() >= 32) {
            return SIMDOptimizations::hash_string_avx2(str.c_str(), str.length());
        }
        
        // Fall back to standard hash
        return std::hash<std::string>{}(str);
    }
    
    // ============================================================================
    // PERFORMANCE MONITORING
    // ============================================================================
    
    struct PerformanceMetrics {
        uint64_t fast_allocations;
        uint64_t jit_compilations;
        uint64_t simd_operations;
        uint64_t lock_free_operations;
        double allocation_hit_rate;
        double jit_compilation_rate;
        double simd_utilization;
    };
    
    PerformanceMetrics get_metrics() const {
        PerformanceMetrics metrics;
        metrics.fast_allocations = fast_allocations_.load();
        metrics.jit_compilations = jit_compilations_.load();
        metrics.simd_operations = simd_operations_.load();
        metrics.lock_free_operations = lock_free_operations_.load();
        
        // Calculate derived metrics
        uint64_t total_operations = metrics.fast_allocations + metrics.simd_operations + 
                                   metrics.lock_free_operations;
        
        if (total_operations > 0) {
            metrics.allocation_hit_rate = static_cast<double>(metrics.fast_allocations) / total_operations;
            metrics.jit_compilation_rate = static_cast<double>(metrics.jit_compilations) / total_operations;
            metrics.simd_utilization = static_cast<double>(metrics.simd_operations) / total_operations;
        } else {
            metrics.allocation_hit_rate = 0.0;
            metrics.jit_compilation_rate = 0.0;
            metrics.simd_utilization = 0.0;
        }
        
        return metrics;
    }
    
    void print_performance_report() const {
        auto metrics = get_metrics();
        
        std::cout << "\n=== GOTS PERFORMANCE REPORT ===\n";
        std::cout << "Fast allocations: " << metrics.fast_allocations << "\n";
        std::cout << "JIT compilations: " << metrics.jit_compilations << "\n";
        std::cout << "SIMD operations: " << metrics.simd_operations << "\n";
        std::cout << "Lock-free operations: " << metrics.lock_free_operations << "\n";
        std::cout << "Allocation hit rate: " << (metrics.allocation_hit_rate * 100) << "%\n";
        std::cout << "JIT compilation rate: " << (metrics.jit_compilation_rate * 100) << "%\n";
        std::cout << "SIMD utilization: " << (metrics.simd_utilization * 100) << "%\n";
        
        // Hardware-specific optimizations
        std::cout << "\nHardware optimizations:\n";
        std::cout << "AVX2 support: " << (SIMDOptimizations::is_avx2_supported() ? "YES" : "NO") << "\n";
        std::cout << "CPU cores: " << std::thread::hardware_concurrency() << "\n";
        
        std::cout << "==============================\n\n";
    }

private:
    template<typename T>
    uint32_t get_type_id() {
        // Type ID generation - could be done at compile time
        static uint32_t id = std::hash<std::string>{}(typeid(T).name()) & 0xFFFFFF;
        return id;
    }
    
    template<typename T>
    constexpr bool needs_heap_allocation() {
        // Simple heuristic: large objects or objects with pointers need heap
        return sizeof(T) > 64 || std::is_pointer_v<T>;
    }
    
    template<typename T>
    T* allocate_with_jit_pattern(const UltraFastJIT::AllocationPattern& pattern, size_t count) {
        // This would emit JIT code for the specific allocation pattern
        // For now, fall back to optimized allocation
        return static_cast<T*>(allocate_fast_path(pattern.size * count, pattern.type_id, count > 1));
    }
    
    void* allocate_fast_path(size_t size, uint32_t type_id, bool is_array) {
        // Ultra-optimized allocation using all available techniques
        
        // Try stack allocation for small objects (determined by escape analysis)
        if (size <= 64 && !is_array) {
            return allocate_stack_optimized(size, type_id);
        }
        
        // Use TLAB allocation for larger objects
        return allocate_tlab_optimized(size, type_id, is_array);
    }
    
    void* allocate_stack_optimized(size_t size, uint32_t type_id) {
        // This would be JIT-compiled to inline stack allocation
        return alloca(size + 8); // + header size
    }
    
    void* allocate_tlab_optimized(size_t size, uint32_t type_id, bool is_array) {
        // Use the optimized TLAB allocation from goroutine_heap_manager.cpp
        // This would integrate with the existing optimized allocate_fast() method
        return nullptr; // Placeholder
    }
    
    void compile_allocation_sequence(const UltraFastJIT::AllocationPattern& pattern) {
        // Emit JIT code for this specific allocation pattern
        // This is where the UltraFastJIT class would be used
    }
    
    void process_dirty_card(uint32_t card_index) {
        // Process objects in dirty card for GC
        // This would integrate with the existing GC system
    }
    
    template<typename T>
    T get_variable_direct(uint32_t offset, LexicalScope* scope) {
        // Direct memory access using pre-computed offset
        // This would be JIT-compiled to a simple memory load
        uint8_t* scope_memory = reinterpret_cast<uint8_t*>(scope);
        return *reinterpret_cast<T*>(scope_memory + offset);
    }
    
    uint32_t calculate_variable_offset(const std::string& name, LexicalScope* scope) {
        // Calculate memory offset for variable in scope
        // This would be done during JIT compilation
        return 0; // Placeholder
    }
};

// ============================================================================
// GLOBAL PERFORMANCE INSTANCE
// ============================================================================

// Singleton performance engine
PerformanceEngine& get_performance_engine() {
    static PerformanceEngine engine;
    return engine;
}

// Convenience macros for high-performance operations
#define GOTS_ALLOC_FAST(type, count) \
    get_performance_engine().allocate_optimized<type>(count)

#define GOTS_SCHEDULE_GOROUTINE(goroutine) \
    get_performance_engine().schedule_goroutine_optimized(goroutine)

#define GOTS_GET_VARIABLE(name, scope, type) \
    get_performance_engine().get_variable_optimized<type>(name, scope)

#define GOTS_COMPARE_STRINGS(a, b) \
    get_performance_engine().compare_strings_optimized(a, b)

#define GOTS_HASH_STRING(str) \
    get_performance_engine().hash_string_optimized(str)

} // namespace gots