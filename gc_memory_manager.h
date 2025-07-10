#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <bit>
#include "gc_type_registry.h"
#include "gc_thread_cleanup.h"
#include "gc_optimized_barriers.h"

namespace gots {

// Forward declarations for concurrent marking
class ConcurrentMarkingCoordinator;

// ============================================================================
// GC CONFIGURATION
// ============================================================================

struct GCConfig {
    static constexpr size_t TLAB_SIZE = 256 * 1024;           // 256KB per thread
    static constexpr size_t YOUNG_GEN_SIZE = 32 * 1024 * 1024; // 32MB
    static constexpr size_t OLD_GEN_SIZE = 512 * 1024 * 1024;  // 512MB
    static constexpr size_t CARD_SIZE = 512;                   // bytes per card
    static constexpr size_t OBJECT_ALIGNMENT = 16;             // 16-byte aligned
    static constexpr size_t MIN_STACK_ALLOC_SIZE = 16;         // min for stack alloc
    static constexpr size_t MAX_STACK_ALLOC_SIZE = 1024;       // max for stack alloc
};

// ============================================================================
// OBJECT HEADER - Minimal overhead (8 bytes)
// ============================================================================

struct ObjectHeader {
    union {
        struct {
            uint32_t size : 24;        // Object size (16MB max per object)
            uint32_t flags : 8;        // GC flags
            uint32_t type_id : 16;     // Type identifier
            uint32_t forward_ptr : 16; // Forwarding pointer (during GC)
        };
        uint64_t raw;
    };
    
    enum Flags : uint8_t {
        MARKED = 0x01,
        PINNED = 0x02,
        HAS_FINALIZER = 0x04,
        IS_ARRAY = 0x08,
        IN_OLD_GEN = 0x10,
        HAS_WEAK_REFS = 0x20,
        STACK_ALLOCATED = 0x40,
        ESCAPE_ANALYZED = 0x80
    };
    
    bool is_marked() const { return flags & MARKED; }
    void set_marked(bool marked) { 
        if (marked) flags |= MARKED; 
        else flags &= ~MARKED; 
    }
    
    bool is_stack_allocated() const { return flags & STACK_ALLOCATED; }
    bool has_escaped() const { return !(flags & ESCAPE_ANALYZED) || !(flags & STACK_ALLOCATED); }
    
    void* get_object_start() { return reinterpret_cast<uint8_t*>(this) + sizeof(ObjectHeader); }
};

// ============================================================================
// ESCAPE ANALYSIS - Static analysis for stack allocation
// ============================================================================

class EscapeAnalyzer {
public:
    struct AnalysisResult {
        bool can_stack_allocate = false;
        size_t max_lifetime_scope = 0;
        std::vector<size_t> escape_points;
        
        // Reasons for escape
        bool escapes_to_heap = false;
        bool escapes_to_closure = false;
        bool escapes_to_return = false;
        bool escapes_to_global = false;
        bool size_too_large = false;
    };
    
    // Called during JIT compilation
    static AnalysisResult analyze_allocation(
        const void* jit_context,
        size_t allocation_site,
        size_t allocation_size,
        uint32_t type_id
    );
    
    // Register variable lifetime information
    static void register_scope_entry(size_t scope_id);
    static void register_scope_exit(size_t scope_id);
    static void register_variable_def(size_t var_id, size_t scope_id, size_t allocation_site);
    static void register_variable_use(size_t var_id, size_t use_site);
    static void register_assignment(size_t from_var, size_t to_var);
    static void register_return(size_t var_id);
    static void register_closure_capture(size_t var_id);
};

// ============================================================================
// THREAD LOCAL ALLOCATION BUFFER (TLAB)
// ============================================================================

class TLAB {
private:
    uint8_t* start_;
    uint8_t* current_;
    uint8_t* end_;
    size_t thread_id_;
    std::atomic<size_t> allocated_bytes_{0};
    
public:
    TLAB(uint8_t* start, size_t size, size_t thread_id) 
        : start_(start), current_(start), end_(start + size), thread_id_(thread_id) {}
    
    // Fast inline allocation - no function calls!
    inline void* allocate(size_t size) {
        size = align_size(size);
        uint8_t* result = current_;
        uint8_t* new_current = result + size;
        
        if (new_current > end_) {
            return nullptr; // Need slow path
        }
        
        current_ = new_current;
        allocated_bytes_.fetch_add(size, std::memory_order_relaxed);
        return result;
    }
    
    inline bool can_allocate(size_t size) const {
        return (current_ + align_size(size)) <= end_;
    }
    
    void reset(uint8_t* start, size_t size) {
        start_ = start;
        current_ = start;
        end_ = start + size;
        allocated_bytes_.store(0, std::memory_order_relaxed);
    }
    
    size_t used() const { return current_ - start_; }
    size_t remaining() const { return end_ - current_; }
    
private:
    static constexpr size_t align_size(size_t size) {
        return (size + GCConfig::OBJECT_ALIGNMENT - 1) & ~(GCConfig::OBJECT_ALIGNMENT - 1);
    }
};

// ============================================================================
// WRITE BARRIER - For generational GC
// ============================================================================

class WriteBarrier {
private:
    static uint8_t* card_table_;
    static size_t card_table_size_;
    
public:
    // Inline write barrier - emitted by JIT (delegates to optimized barriers)
    static inline void write_ref(void* obj, void* field, void* new_value) {
        AdaptiveWriteBarriers::adaptive_write_barrier(obj, reinterpret_cast<void**>(field), new_value);
    }
    
    // Batch card scanning
    static void scan_dirty_cards(std::function<void(void*, void*)> callback);
    static void clear_cards();
    
private:
    static inline ObjectHeader* get_header(void* obj) {
        if (!obj) return nullptr;
        return reinterpret_cast<ObjectHeader*>(
            reinterpret_cast<uint8_t*>(obj) - sizeof(ObjectHeader)
        );
    }
};

// ============================================================================
// GENERATIONAL HEAP
// ============================================================================

class GenerationalHeap {
    friend class GarbageCollector;
private:
    // Young generation (Eden + Survivor spaces)
    struct YoungGen {
        uint8_t* eden_start;
        uint8_t* eden_current;
        uint8_t* eden_end;
        
        uint8_t* survivor1_start;
        uint8_t* survivor1_end;
        
        uint8_t* survivor2_start;
        uint8_t* survivor2_end;
        
        uint8_t* active_survivor;
        
        std::atomic<size_t> collections{0};
    };
    
    // Old generation
    struct OldGen {
        uint8_t* start;
        uint8_t* current;
        uint8_t* end;
        
        std::atomic<size_t> collections{0};
    };
    
    YoungGen young_;
    OldGen old_;
    
    // Thread-local allocation buffers
    static thread_local TLAB* tlab_;
    std::mutex heap_mutex_;
    std::vector<std::unique_ptr<TLAB>> all_tlabs_;
    std::mutex tlabs_mutex_;
    
public:
    // Initialize heap
    void initialize();
    void shutdown();
    
    // Fast allocation path (inlined in JIT code)
    static inline void* allocate_fast(size_t size, uint32_t type_id, bool is_array = false) {
        // Try TLAB first
        if (tlab_ && tlab_->can_allocate(size + sizeof(ObjectHeader))) {
            void* mem = tlab_->allocate(size + sizeof(ObjectHeader));
            if (mem) {
                // Initialize header
                ObjectHeader* header = reinterpret_cast<ObjectHeader*>(mem);
                header->size = size;
                header->flags = 0;
                header->type_id = type_id;
                header->forward_ptr = 0;
                if (is_array) header->flags |= ObjectHeader::IS_ARRAY;
                
                return header->get_object_start();
            }
        }
        
        // Slow path
        return allocate_slow(size, type_id, is_array);
    }
    
    // Stack allocation helper (for escape analysis)
    static inline void* stack_allocate(size_t size, uint32_t type_id) {
        // This is a marker for JIT to emit stack allocation
        // Actual implementation is in JIT code generation
        return nullptr;
    }
    
    // Slow allocation path
    static void* allocate_slow(size_t size, uint32_t type_id, bool is_array);
    static void* allocate_large_slow(size_t size, uint32_t type_id, bool is_array);
    
    // GC triggers
    void collect_young();
    void collect_old();
    void collect_full();
    
    // Statistics
    size_t young_used() const;
    size_t old_used() const;
    size_t total_allocated() const;
};

// ============================================================================
// GARBAGE COLLECTOR
// ============================================================================

class GarbageCollector {
private:
    GenerationalHeap heap_;
    std::thread gc_thread_;
    std::atomic<bool> running_{true};
    std::atomic<bool> gc_requested_{false};
    std::condition_variable gc_cv_;
    std::mutex gc_mutex_;
    
    // Root set
    struct RootSet {
        std::vector<void**> stack_roots;
        std::vector<void**> global_roots;
        std::vector<void**> register_roots;
        std::mutex roots_mutex;
    };
    
    RootSet roots_;
    
    // GC state
    enum class Phase {
        IDLE,
        MARKING,
        RELOCATING,
        UPDATING_REFS
    };
    
    std::atomic<Phase> current_phase_{Phase::IDLE};
    
    // Safe points
    std::atomic<bool> safepoint_requested_{false};
    std::atomic<size_t> threads_at_safepoint_{0};
    size_t total_threads_{0};
    
    // Type registry
    TypeRegistry type_registry_;
    
    // Mark stack for DFS marking
    std::stack<void*> mark_stack_;
    std::mutex mark_stack_mutex_;
    
    // Statistics
    std::atomic<size_t> total_pause_time_ms_{0};
    std::atomic<size_t> max_pause_time_ms_{0};
    
    // Concurrent marking
    std::unique_ptr<ConcurrentMarkingCoordinator> concurrent_marker_;
    
public:
    GarbageCollector();
    ~GarbageCollector();
    
    // Initialize and shutdown
    void initialize();
    void shutdown();
    
    // Root registration
    void add_stack_root(void** root);
    void remove_stack_root(void** root);
    void add_global_root(void** root);
    void remove_global_root(void** root);
    
    // Safe points (called by JIT-generated code)
    static inline void safepoint_poll() {
        if (instance().safepoint_requested_.load(std::memory_order_acquire)) {
            safepoint_slow();
        }
    }
    
    // Manual GC trigger
    void request_gc(bool full = false);
    
    // Concurrent marking
    void start_concurrent_marking();
    void wait_for_concurrent_marking();
    
    // Statistics
    struct Stats {
        size_t young_collections;
        size_t old_collections;
        size_t total_pause_time_ms;
        size_t max_pause_time_ms;
        size_t total_allocated;
        size_t total_freed;
        size_t live_objects;
    };
    
    // Get type registry
    TypeRegistry& get_type_registry() { return type_registry_; }
    
    Stats get_stats() const;
    
    static GarbageCollector& instance();
    
private:
    void gc_thread_loop();
    void perform_young_gc();
    void perform_old_gc();
    void perform_full_gc();
    
    static void safepoint_slow();
    void wait_for_safepoint();
    void release_safepoint();
    
    // Marking
    void mark_roots();
    void mark_object(void* obj);
    void process_mark_stack();
    
    // Copying/Compacting
    void* copy_object(void* obj, bool to_old_gen);
    void update_references();
    void copy_young_survivors();
    
    // Card table scanning
    void scan_card_table();
};

// ============================================================================
// JIT INTEGRATION HELPERS
// ============================================================================

// These are the actual functions that JIT will inline or call
extern "C" {
    // Fast allocation (usually inlined)
    void* __gc_alloc_fast(size_t size, uint32_t type_id);
    void* __gc_alloc_array_fast(size_t element_size, size_t count, uint32_t type_id);
    
    // Stack allocation (always inlined by JIT)
    void* __gc_alloc_stack(size_t size, uint32_t type_id);
    
    // Write barrier (always inlined)
    void __gc_write_barrier(void* obj, void* field, void* new_value);
    
    // Safe point (partially inlined)
    void __gc_safepoint();
    
    // Root registration (called at function entry/exit)
    void __gc_register_roots(void** roots, size_t count);
    void __gc_unregister_roots(void** roots, size_t count);
    
    // Type information
    void __gc_register_type(uint32_t type_id, size_t size, void* vtable, uint32_t ref_offsets[], size_t ref_count);
}

// ============================================================================
// INLINE JIT CODE TEMPLATES
// ============================================================================

// These templates show what the JIT should emit for maximum performance

// X86-64 fast allocation sequence (TLAB):
// mov rax, [tlab_current]
// lea rdx, [rax + size]
// cmp rdx, [tlab_end]
// ja slow_path
// mov [tlab_current], rdx
// mov dword [rax], header_data
// lea rax, [rax + 8]  ; return object start
// ret

// X86-64 write barrier:
// mov [obj + offset], new_value  ; do the write
// test byte [obj - 8], 0x10      ; check IN_OLD_GEN flag
// jz no_barrier
// test byte [new_value - 8], 0x10 ; check if new_value is young
// jnz no_barrier
// ; Mark card
// mov rcx, obj
// shr rcx, 9                      ; divide by card size
// mov byte [card_table + rcx], 1
// no_barrier:

// WebAssembly equivalents use similar patterns with linear memory

} // namespace gots