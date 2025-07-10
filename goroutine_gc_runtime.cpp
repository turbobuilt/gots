#include "goroutine_aware_gc.h"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>

namespace gots {

// ============================================================================
// C API IMPLEMENTATION FOR GOROUTINE-AWARE GC
// ============================================================================

// Global system initialization state
static std::atomic<bool> g_gc_system_initialized{false};
static std::mutex g_gc_system_mutex;

// Runtime statistics
static std::atomic<size_t> g_total_allocations{0};
static std::atomic<size_t> g_total_deallocations{0};
static std::atomic<size_t> g_stack_allocations{0};
static std::atomic<size_t> g_private_allocations{0};
static std::atomic<size_t> g_shared_allocations{0};
static std::atomic<size_t> g_global_allocations{0};

// Performance counters
static std::atomic<size_t> g_fast_path_hits{0};
static std::atomic<size_t> g_slow_path_hits{0};
static std::atomic<size_t> g_gc_triggers{0};

} // namespace gots

// ============================================================================
// C API FUNCTIONS
// ============================================================================

extern "C" {

// ============================================================================
// SYSTEM INITIALIZATION
// ============================================================================

void __gc_initialize_system() {
    std::lock_guard<std::mutex> lock(gots::g_gc_system_mutex);
    
    if (gots::g_gc_system_initialized.load()) {
        return; // Already initialized
    }
    
    try {
        // Initialize heap manager
        gots::GoroutineAwareHeap::initialize();
        
        // Initialize write barriers
        gots::GoroutineWriteBarrier::initialize();
        
        // Initialize coordinated GC
        gots::GoroutineCoordinatedGC::instance(); // This initializes it
        
        gots::g_gc_system_initialized.store(true);
        
        std::cout << "[RUNTIME] Initialized goroutine-aware GC system\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Failed to initialize GC system: " << e.what() << "\n";
        throw;
    }
}

void __gc_shutdown_system() {
    std::lock_guard<std::mutex> lock(gots::g_gc_system_mutex);
    
    if (!gots::g_gc_system_initialized.load()) {
        return; // Not initialized
    }
    
    try {
        // Print final statistics
        __gc_print_statistics();
        
        // Shutdown components
        gots::GoroutineCoordinatedGC::instance().shutdown();
        gots::GoroutineWriteBarrier::cleanup();
        gots::GoroutineAwareHeap::shutdown();
        
        gots::g_gc_system_initialized.store(false);
        
        std::cout << "[RUNTIME] Shutdown goroutine-aware GC system\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error during GC system shutdown: " << e.what() << "\n";
    }
}

// ============================================================================
// ALLOCATION FUNCTIONS
// ============================================================================

void* __gc_alloc_by_ownership(size_t size, uint32_t type_id, uint32_t ownership, uint32_t goroutine_id) {
    if (!gots::g_gc_system_initialized.load()) {
        __gc_initialize_system();
    }
    
    gots::ObjectOwnership obj_ownership = static_cast<gots::ObjectOwnership>(ownership);
    
    try {
        void* result = gots::GoroutineAwareHeap::instance().allocate_by_ownership(
            size, type_id, obj_ownership, goroutine_id
        );
        
        if (result) {
            gots::g_total_allocations.fetch_add(1, std::memory_order_relaxed);
            
            switch (obj_ownership) {
                case gots::ObjectOwnership::STACK_LOCAL:
                    gots::g_stack_allocations.fetch_add(1, std::memory_order_relaxed);
                    break;
                case gots::ObjectOwnership::GOROUTINE_PRIVATE:
                    gots::g_private_allocations.fetch_add(1, std::memory_order_relaxed);
                    gots::g_fast_path_hits.fetch_add(1, std::memory_order_relaxed);
                    break;
                case gots::ObjectOwnership::GOROUTINE_SHARED:
                    gots::g_shared_allocations.fetch_add(1, std::memory_order_relaxed);
                    gots::g_slow_path_hits.fetch_add(1, std::memory_order_relaxed);
                    break;
                case gots::ObjectOwnership::GLOBAL_SHARED:
                    gots::g_global_allocations.fetch_add(1, std::memory_order_relaxed);
                    gots::g_slow_path_hits.fetch_add(1, std::memory_order_relaxed);
                    break;
            }
            
            GC_DEBUG_LOG("[RUNTIME] Allocated " << size << " bytes for goroutine " 
                      << goroutine_id << " with ownership " << ownership << " at " << result);
        } else {
            std::cout << "[RUNTIME] Allocation failed for " << size << " bytes, goroutine " 
                      << goroutine_id << ", ownership " << ownership << "\n";
        }
        
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Allocation error: " << e.what() << "\n";
        return nullptr;
    }
}

void* __gc_alloc_fast(size_t size, uint32_t type_id, uint32_t goroutine_id) {
    return __gc_alloc_by_ownership(size, type_id, 
                                  static_cast<uint32_t>(gots::ObjectOwnership::GOROUTINE_PRIVATE), 
                                  goroutine_id);
}

void* __gc_alloc_array_fast(size_t element_size, size_t count, uint32_t type_id, uint32_t goroutine_id) {
    size_t total_size = sizeof(size_t) + element_size * count; // length prefix
    void* result = __gc_alloc_fast(total_size, type_id, goroutine_id);
    
    if (result) {
        // Set array length
        *static_cast<size_t*>(result) = count;
        
        // Mark as array in header with proper validation
        gots::GoroutineObjectHeader* header = gots::get_goroutine_header(result);
        if (header) {
            header->flags |= gots::ObjectHeader::IS_ARRAY;
        } else {
            std::cerr << "[RUNTIME] CRITICAL ERROR: No header found for array allocation at " << result << "\n";
            // Return null to indicate allocation failure
            return nullptr;
        }
        
        std::cout << "[RUNTIME] Allocated array of " << count << " elements (" 
                  << total_size << " bytes) for goroutine " << goroutine_id << "\n";
    }
    
    return result;
}

void* __gc_alloc_stack(size_t size, uint32_t type_id, uint32_t goroutine_id) {
    return __gc_alloc_by_ownership(size, type_id, 
                                  static_cast<uint32_t>(gots::ObjectOwnership::STACK_LOCAL), 
                                  goroutine_id);
}

void* __gc_alloc_goroutine_shared(size_t size, uint32_t type_id, uint32_t ownership, uint32_t goroutine_id) {
    return gots::GoroutineAwareHeap::instance().allocate_goroutine_shared(size, type_id);
}

void* __gc_alloc_global_shared(size_t size, uint32_t type_id, uint32_t ownership, uint32_t goroutine_id) {
    return gots::GoroutineAwareHeap::instance().allocate_global_shared(size, type_id);
}

void* __gc_alloc_slow_path(size_t size, uint32_t type_id, uint32_t ownership, uint32_t goroutine_id) {
    std::cout << "[RUNTIME] Entering allocation slow path for " << size << " bytes\n";
    
    gots::g_slow_path_hits.fetch_add(1, std::memory_order_relaxed);
    
    // Try to trigger GC to free up space
    __gc_trigger_collection(0); // Private collection
    
    // Retry allocation
    return __gc_alloc_by_ownership(size, type_id, ownership, goroutine_id);
}

// ============================================================================
// WRITE BARRIER FUNCTIONS
// ============================================================================

void __gc_write_barrier_sync(void* obj, void* field, void* new_value, uint32_t goroutine_id) {
    if (!obj || !field) {
        std::cerr << "[RUNTIME] WARNING: Write barrier called with null obj=" << obj << " field=" << field << "\n";
        return;
    }
    
    try {
        gots::GoroutineWriteBarrier::write_ref_with_sync(obj, field, new_value, goroutine_id);
        
        GC_DEBUG_LOG("[RUNTIME] Write barrier: goroutine " << goroutine_id 
                  << " wrote to " << obj << " field " << field << " value " << new_value);
                  
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Write barrier error: " << e.what() << "\n";
    }
}

void* __gc_read_barrier_sync(void* obj, void* field, uint32_t goroutine_id) {
    if (!obj || !field) {
        if (!obj) std::cerr << "[RUNTIME] WARNING: Read barrier called with null obj\n";
        if (!field) std::cerr << "[RUNTIME] WARNING: Read barrier called with null field\n";
        return field ? *static_cast<void**>(field) : nullptr;
    }
    
    try {
        void* result = gots::GoroutineWriteBarrier::read_ref_with_sync(obj, field, goroutine_id);
        
        GC_DEBUG_LOG("[RUNTIME] Read barrier: goroutine " << goroutine_id 
                  << " read from " << obj << " field " << field << " value " << result);
        
        return result;
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Read barrier error: " << e.what() << "\n";
        return field ? *static_cast<void**>(field) : nullptr;
    }
}

void __gc_bulk_write_barrier(void* obj, void** fields, void** new_values, size_t count, uint32_t goroutine_id) {
    if (!obj || !fields || !new_values || count == 0) return;
    
    try {
        gots::GoroutineWriteBarrier::bulk_write_refs(obj, fields, new_values, count, goroutine_id);
        
        std::cout << "[RUNTIME] Bulk write barrier: goroutine " << goroutine_id 
                  << " wrote " << count << " fields to " << obj << "\n";
                  
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Bulk write barrier error: " << e.what() << "\n";
    }
}

void __gc_array_write_barrier(void* array, size_t index, void* new_value, uint32_t goroutine_id) {
    if (!array) return;
    
    try {
        gots::GoroutineWriteBarrier::array_write_ref(array, index, new_value, goroutine_id);
        
        std::cout << "[RUNTIME] Array write barrier: goroutine " << goroutine_id 
                  << " wrote to array " << array << " index " << index << " value " << new_value << "\n";
                  
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Array write barrier error: " << e.what() << "\n";
    }
}

// ============================================================================
// GOROUTINE LIFECYCLE
// ============================================================================

void __gc_register_goroutine(uint32_t goroutine_id) {
    if (!gots::g_gc_system_initialized.load()) {
        __gc_initialize_system();
    }
    
    try {
        gots::GoroutineAwareHeap::instance().register_goroutine(goroutine_id);
        gots::GoroutineCoordinatedGC::instance().register_goroutine(goroutine_id);
        gots::GoroutineAwareHeap::instance().set_current_goroutine(goroutine_id);
        
        std::cout << "[RUNTIME] Registered goroutine " << goroutine_id << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error registering goroutine " << goroutine_id << ": " << e.what() << "\n";
    }
}

void __gc_unregister_goroutine(uint32_t goroutine_id) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::GoroutineCoordinatedGC::instance().unregister_goroutine(goroutine_id);
        gots::GoroutineAwareHeap::instance().unregister_goroutine(goroutine_id);
        
        std::cout << "[RUNTIME] Unregistered goroutine " << goroutine_id << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error unregistering goroutine " << goroutine_id << ": " << e.what() << "\n";
    }
}

void __gc_set_current_goroutine(uint32_t goroutine_id) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::GoroutineAwareHeap::instance().set_current_goroutine(goroutine_id);
        gots::GoroutineEscapeAnalyzer::set_current_goroutine(goroutine_id);
        
        std::cout << "[RUNTIME] Set current goroutine to " << goroutine_id << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error setting current goroutine " << goroutine_id << ": " << e.what() << "\n";
    }
}

// ============================================================================
// ROOT MANAGEMENT
// ============================================================================

void __gc_register_goroutine_roots(size_t count, void** roots, uint32_t goroutine_id) {
    if (!gots::g_gc_system_initialized.load() || count == 0 || !roots) return;
    
    try {
        gots::GoroutineCoordinatedGC::instance().set_goroutine_stack_roots(goroutine_id, roots, count);
        
        std::cout << "[RUNTIME] Registered " << count << " stack roots for goroutine " << goroutine_id << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error registering roots for goroutine " << goroutine_id << ": " << e.what() << "\n";
    }
}

void __gc_unregister_goroutine_roots(size_t count, void** roots, uint32_t goroutine_id) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::GoroutineCoordinatedGC::instance().set_goroutine_stack_roots(goroutine_id, nullptr, 0);
        
        std::cout << "[RUNTIME] Unregistered " << count << " stack roots for goroutine " << goroutine_id << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error unregistering roots for goroutine " << goroutine_id << ": " << e.what() << "\n";
    }
}

void __gc_add_global_root(void** root) {
    if (!gots::g_gc_system_initialized.load() || !root) return;
    
    // For simplicity, global roots are handled by registering them with goroutine 0
    try {
        void* roots[1] = {*root};
        gots::GoroutineCoordinatedGC::instance().set_goroutine_stack_roots(0, roots, 1);
        
        std::cout << "[RUNTIME] Added global root " << root << " -> " << *root << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error adding global root: " << e.what() << "\n";
    }
}

void __gc_remove_global_root(void** root) {
    if (!gots::g_gc_system_initialized.load() || !root) return;
    
    try {
        // Remove from global roots (simplified implementation)
        std::cout << "[RUNTIME] Removed global root " << root << " -> " << *root << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error removing global root: " << e.what() << "\n";
    }
}

// ============================================================================
// SAFEPOINT FUNCTIONS
// ============================================================================

void __gc_safepoint_goroutine(uint32_t goroutine_id) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::GoroutineCoordinatedGC::safepoint_poll(goroutine_id);
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Safepoint error for goroutine " << goroutine_id << ": " << e.what() << "\n";
    }
}

void __gc_safepoint_handler(uint32_t goroutine_id) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::GoroutineCoordinatedGC::safepoint_slow(goroutine_id);
        
        std::cout << "[RUNTIME] Safepoint handler executed for goroutine " << goroutine_id << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Safepoint handler error for goroutine " << goroutine_id << ": " << e.what() << "\n";
    }
}

// ============================================================================
// GARBAGE COLLECTION TRIGGERS
// ============================================================================

void __gc_trigger_collection(uint32_t collection_type) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::g_gc_triggers.fetch_add(1, std::memory_order_relaxed);
        
        gots::GoroutineCoordinatedGC::GCType gc_type = 
            (collection_type == 0) ? gots::GoroutineCoordinatedGC::GCType::PRIVATE 
                                   : gots::GoroutineCoordinatedGC::GCType::SHARED;
        
        gots::GoroutineCoordinatedGC::instance().request_gc(gc_type);
        
        std::cout << "[RUNTIME] Triggered " << (collection_type == 0 ? "private" : "shared") 
                  << " garbage collection\n";
                  
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error triggering GC: " << e.what() << "\n";
    }
}

void __gc_collect_goroutine_private(uint32_t goroutine_id) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::GoroutineAwareHeap::instance().collect_goroutine_private(goroutine_id);
        
        std::cout << "[RUNTIME] Collected private heap for goroutine " << goroutine_id << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error collecting private heap for goroutine " << goroutine_id << ": " << e.what() << "\n";
    }
}

void __gc_collect_shared_heap() {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::GoroutineAwareHeap::instance().collect_shared_heap();
        
        std::cout << "[RUNTIME] Collected shared heap\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error collecting shared heap: " << e.what() << "\n";
    }
}

// ============================================================================
// ESCAPE ANALYSIS FUNCTIONS
// ============================================================================

void __gc_register_goroutine_spawn(uint32_t parent_id, uint32_t child_id, void** captured_vars, size_t var_count) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        std::vector<size_t> captured_var_ids;
        for (size_t i = 0; i < var_count; ++i) {
            captured_var_ids.push_back(reinterpret_cast<size_t>(captured_vars[i]));
        }
        
        gots::GoroutineEscapeAnalyzer::register_goroutine_spawn(parent_id, child_id, captured_var_ids);
        
        std::cout << "[RUNTIME] Registered goroutine spawn: " << parent_id << " -> " << child_id 
                  << " with " << var_count << " captured variables\n";
                  
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error registering goroutine spawn: " << e.what() << "\n";
    }
}

void __gc_register_cross_goroutine_access(uint32_t goroutine_id, size_t var_id, size_t allocation_site, uint32_t is_write) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::GoroutineEscapeAnalyzer::register_cross_goroutine_access(
            goroutine_id, var_id, allocation_site, is_write != 0
        );
        
        std::cout << "[RUNTIME] Registered cross-goroutine " << (is_write ? "write" : "read") 
                  << " by goroutine " << goroutine_id << " to var " << var_id << "\n";
                  
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error registering cross-goroutine access: " << e.what() << "\n";
    }
}

void __gc_register_variable_definition(size_t var_id, size_t allocation_site, size_t scope_id, uint32_t goroutine_id) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::GoroutineEscapeAnalyzer::register_variable_definition(var_id, allocation_site, scope_id, goroutine_id);
        
        std::cout << "[RUNTIME] Registered variable definition: var " << var_id 
                  << " at site " << allocation_site << " in scope " << scope_id << "\n";
                  
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error registering variable definition: " << e.what() << "\n";
    }
}

void __gc_register_variable_assignment(size_t from_var, size_t to_var, uint32_t goroutine_id) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::GoroutineEscapeAnalyzer::register_variable_assignment(from_var, to_var, goroutine_id);
        
        std::cout << "[RUNTIME] Registered variable assignment: " << from_var << " -> " << to_var 
                  << " by goroutine " << goroutine_id << "\n";
                  
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error registering variable assignment: " << e.what() << "\n";
    }
}

void __gc_scope_enter(size_t scope_id) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::GoroutineEscapeAnalyzer::register_scope_entry(scope_id);
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error entering scope " << scope_id << ": " << e.what() << "\n";
    }
}

void __gc_scope_exit(size_t scope_id) {
    if (!gots::g_gc_system_initialized.load()) return;
    
    try {
        gots::GoroutineEscapeAnalyzer::register_scope_exit(scope_id);
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error exiting scope " << scope_id << ": " << e.what() << "\n";
    }
}

// ============================================================================
// OBJECT INTROSPECTION
// ============================================================================

uint32_t __gc_get_object_ownership(void* obj) {
    if (!obj) return static_cast<uint32_t>(gots::ObjectOwnership::GOROUTINE_SHARED);
    
    try {
        gots::ObjectOwnership ownership = gots::get_object_ownership(obj);
        return static_cast<uint32_t>(ownership);
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error getting object ownership: " << e.what() << "\n";
        return static_cast<uint32_t>(gots::ObjectOwnership::GOROUTINE_SHARED);
    }
}

uint32_t __gc_get_object_owner_goroutine(void* obj) {
    if (!obj) return 0;
    
    try {
        return gots::get_object_owner_goroutine(obj);
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error getting object owner: " << e.what() << "\n";
        return 0;
    }
}

uint32_t __gc_is_object_shared(void* obj) {
    if (!obj) return 1;
    
    try {
        return gots::is_object_shared(obj) ? 1 : 0;
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error checking if object is shared: " << e.what() << "\n";
        return 1;
    }
}

void __gc_mark_object_accessed(void* obj, uint32_t goroutine_id) {
    if (!obj) return;
    
    try {
        gots::mark_object_accessed_by_goroutine(obj, goroutine_id);
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error marking object access: " << e.what() << "\n";
    }
}

// ============================================================================
// STATISTICS AND DEBUGGING
// ============================================================================

void __gc_get_statistics(uint64_t* total_allocations, uint64_t* total_deallocations, 
                        uint64_t* stack_allocations, uint64_t* private_allocations,
                        uint64_t* shared_allocations, uint64_t* global_allocations) {
    if (total_allocations) *total_allocations = gots::g_total_allocations.load();
    if (total_deallocations) *total_deallocations = gots::g_total_deallocations.load();
    if (stack_allocations) *stack_allocations = gots::g_stack_allocations.load();
    if (private_allocations) *private_allocations = gots::g_private_allocations.load();
    if (shared_allocations) *shared_allocations = gots::g_shared_allocations.load();
    if (global_allocations) *global_allocations = gots::g_global_allocations.load();
}

void __gc_print_statistics() {
    if (!gots::g_gc_system_initialized.load()) {
        std::cout << "[RUNTIME] GC system not initialized\n";
        return;
    }
    
    try {
        std::cout << "\n=== GOROUTINE-AWARE GC RUNTIME STATISTICS ===\n";
        
        // Allocation statistics
        std::cout << "Allocation statistics:\n";
        std::cout << "  Total allocations: " << gots::g_total_allocations.load() << "\n";
        std::cout << "  Total deallocations: " << gots::g_total_deallocations.load() << "\n";
        std::cout << "  Stack allocations: " << gots::g_stack_allocations.load() << "\n";
        std::cout << "  Private allocations: " << gots::g_private_allocations.load() << "\n";
        std::cout << "  Shared allocations: " << gots::g_shared_allocations.load() << "\n";
        std::cout << "  Global allocations: " << gots::g_global_allocations.load() << "\n";
        
        // Performance statistics
        std::cout << "\nPerformance statistics:\n";
        std::cout << "  Fast path hits: " << gots::g_fast_path_hits.load() << "\n";
        std::cout << "  Slow path hits: " << gots::g_slow_path_hits.load() << "\n";
        std::cout << "  GC triggers: " << gots::g_gc_triggers.load() << "\n";
        
        size_t total_paths = gots::g_fast_path_hits.load() + gots::g_slow_path_hits.load();
        if (total_paths > 0) {
            double fast_percentage = (double)gots::g_fast_path_hits.load() / total_paths * 100.0;
            std::cout << "  Fast path percentage: " << fast_percentage << "%\n";
        }
        
        // Component statistics
        gots::GoroutineAwareHeap::instance().print_all_statistics();
        gots::GoroutineWriteBarrier::print_statistics();
        gots::GoroutineCoordinatedGC::instance().print_all_statistics();
        gots::GoroutineEscapeAnalyzer::print_analysis_statistics();
        
        std::cout << "=============================================\n\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error printing statistics: " << e.what() << "\n";
    }
}

void __gc_reset_statistics() {
    gots::g_total_allocations.store(0);
    gots::g_total_deallocations.store(0);
    gots::g_stack_allocations.store(0);
    gots::g_private_allocations.store(0);
    gots::g_shared_allocations.store(0);
    gots::g_global_allocations.store(0);
    gots::g_fast_path_hits.store(0);
    gots::g_slow_path_hits.store(0);
    gots::g_gc_triggers.store(0);
    
    try {
        gots::GoroutineWriteBarrier::reset_statistics();
        gots::GoroutineEscapeAnalyzer::reset_analysis();
        
        std::cout << "[RUNTIME] Reset all statistics\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error resetting statistics: " << e.what() << "\n";
    }
}

size_t __gc_get_total_allocated_bytes() {
    if (!gots::g_gc_system_initialized.load()) return 0;
    
    try {
        return gots::GoroutineAwareHeap::instance().get_total_allocated_bytes();
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] Error getting total allocated bytes: " << e.what() << "\n";
        return 0;
    }
}

uint32_t __gc_is_system_initialized() {
    return gots::g_gc_system_initialized.load() ? 1 : 0;
}

// ============================================================================
// TESTING AND DEBUGGING
// ============================================================================

void __gc_test_system() {
    std::cout << "[RUNTIME] Testing goroutine-aware GC system...\n";
    
    try {
        // Test system initialization
        __gc_initialize_system();
        
        // Test goroutine registration
        __gc_register_goroutine(1);
        __gc_register_goroutine(2);
        
        // Test allocations
        void* obj1 = __gc_alloc_fast(64, 42, 1);
        void* obj2 = __gc_alloc_by_ownership(128, 43, 
                                           static_cast<uint32_t>(gots::ObjectOwnership::GOROUTINE_SHARED), 2);
        
        // Test write barriers
        if (obj1 && obj2) {
            __gc_write_barrier_sync(obj1, &obj1, obj2, 1);
            void* read_result = __gc_read_barrier_sync(obj1, &obj1, 2);
            std::cout << "[RUNTIME] Read result: " << read_result << "\n";
        }
        
        // Test garbage collection
        __gc_trigger_collection(0); // Private collection
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        __gc_trigger_collection(1); // Shared collection
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Test statistics
        __gc_print_statistics();
        
        // Test unregistration
        __gc_unregister_goroutine(1);
        __gc_unregister_goroutine(2);
        
        std::cout << "[RUNTIME] GC system test completed successfully\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] GC system test failed: " << e.what() << "\n";
    }
}

void __gc_stress_test() {
    std::cout << "[RUNTIME] Starting GC stress test...\n";
    
    const size_t NUM_GOROUTINES = 4;
    const size_t ALLOCATIONS_PER_GOROUTINE = 1000;
    
    try {
        // Initialize system
        __gc_initialize_system();
        
        // Create stress test threads
        std::vector<std::thread> stress_threads;
        
        for (size_t i = 1; i <= NUM_GOROUTINES; ++i) {
            stress_threads.emplace_back([i, ALLOCATIONS_PER_GOROUTINE]() {
                __gc_register_goroutine(i);
                
                std::vector<void*> allocated_objects;
                
                // Allocate many objects
                for (size_t j = 0; j < ALLOCATIONS_PER_GOROUTINE; ++j) {
                    gots::ObjectOwnership ownership;
                    
                    if (j % 4 == 0) ownership = gots::ObjectOwnership::STACK_LOCAL;
                    else if (j % 4 == 1) ownership = gots::ObjectOwnership::GOROUTINE_PRIVATE;
                    else if (j % 4 == 2) ownership = gots::ObjectOwnership::GOROUTINE_SHARED;
                    else ownership = gots::ObjectOwnership::GLOBAL_SHARED;
                    
                    void* obj = __gc_alloc_by_ownership(64 + (j % 256), 42 + (j % 10), 
                                                       static_cast<uint32_t>(ownership), i);
                    if (obj) {
                        allocated_objects.push_back(obj);
                    }
                    
                    // Occasionally trigger GC
                    if (j % 100 == 0) {
                        __gc_trigger_collection(j % 2);
                    }
                    
                    // Add some cross-goroutine writes
                    if (!allocated_objects.empty() && j % 10 == 0) {
                        size_t idx = j % allocated_objects.size();
                        __gc_write_barrier_sync(allocated_objects[idx], &allocated_objects[idx], obj, i);
                    }
                }
                
                __gc_unregister_goroutine(i);
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : stress_threads) {
            thread.join();
        }
        
        // Final GC
        __gc_trigger_collection(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Print final statistics
        __gc_print_statistics();
        
        std::cout << "[RUNTIME] GC stress test completed\n";
        
    } catch (const std::exception& e) {
        std::cerr << "[RUNTIME] GC stress test failed: " << e.what() << "\n";
    }
}

} // extern "C"