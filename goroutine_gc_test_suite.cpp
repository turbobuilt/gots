#include "goroutine_aware_gc.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <memory>

namespace gots {

// ============================================================================
// TEST FRAMEWORK
// ============================================================================

class GCTestFramework {
private:
    size_t tests_run_ = 0;
    size_t tests_passed_ = 0;
    size_t tests_failed_ = 0;
    std::vector<std::string> failed_tests_;
    
public:
    void run_test(const std::string& test_name, std::function<void()> test_func) {
        tests_run_++;
        std::cout << "[TEST] Running: " << test_name << "... ";
        
        try {
            test_func();
            tests_passed_++;
            std::cout << "PASSED\n";
        } catch (const std::exception& e) {
            tests_failed_++;
            failed_tests_.push_back(test_name + ": " + e.what());
            std::cout << "FAILED: " << e.what() << "\n";
        } catch (...) {
            tests_failed_++;
            failed_tests_.push_back(test_name + ": Unknown exception");
            std::cout << "FAILED: Unknown exception\n";
        }
    }
    
    void print_summary() {
        std::cout << "\n=== TEST SUMMARY ===\n";
        std::cout << "Tests run: " << tests_run_ << "\n";
        std::cout << "Tests passed: " << tests_passed_ << "\n";
        std::cout << "Tests failed: " << tests_failed_ << "\n";
        
        if (!failed_tests_.empty()) {
            std::cout << "\nFailed tests:\n";
            for (const auto& failure : failed_tests_) {
                std::cout << "  - " << failure << "\n";
            }
        }
        
        std::cout << "===================\n\n";
    }
    
    bool all_tests_passed() const {
        return tests_failed_ == 0;
    }
};

// ============================================================================
// TEST UTILITIES
// ============================================================================

#define ASSERT_TRUE(condition) \
    do { \
        if (!(condition)) { \
            throw std::runtime_error("Assertion failed: " #condition); \
        } \
    } while (0)

#define ASSERT_FALSE(condition) \
    do { \
        if (condition) { \
            throw std::runtime_error("Assertion failed: !(" #condition ")"); \
        } \
    } while (0)

#define ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            throw std::runtime_error("Assertion failed: " #expected " == " #actual); \
        } \
    } while (0)

#define ASSERT_NE(expected, actual) \
    do { \
        if ((expected) == (actual)) { \
            throw std::runtime_error("Assertion failed: " #expected " != " #actual); \
        } \
    } while (0)

#define ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == nullptr) { \
            throw std::runtime_error("Assertion failed: " #ptr " != nullptr"); \
        } \
    } while (0)

// ============================================================================
// ESCAPE ANALYSIS TESTS
// ============================================================================

void test_escape_analysis_basic() {
    GoroutineEscapeAnalyzer::reset_analysis();
    
    // Test basic variable definition
    GoroutineEscapeAnalyzer::register_variable_definition(100, 1000, 1, 1);
    
    // Test goroutine spawn with capture
    std::vector<size_t> captured_vars = {100};
    GoroutineEscapeAnalyzer::register_goroutine_spawn(1, 2, captured_vars);
    
    // Analyze allocation
    auto result = GoroutineEscapeAnalyzer::analyze_goroutine_allocation(
        nullptr, 1000, 64, 42, 1
    );
    
    ASSERT_TRUE(result.captured_by_goroutine);
    ASSERT_EQ(static_cast<int>(ObjectOwnership::GOROUTINE_SHARED), static_cast<int>(result.ownership));
    ASSERT_TRUE(result.needs_synchronization);
}

void test_escape_analysis_cross_goroutine_access() {
    GoroutineEscapeAnalyzer::reset_analysis();
    
    // Register variable and allocation
    GoroutineEscapeAnalyzer::register_variable_definition(200, 2000, 2, 1);
    
    // Register cross-goroutine access
    GoroutineEscapeAnalyzer::register_cross_goroutine_access(2, 200, 2000, true);
    GoroutineEscapeAnalyzer::register_cross_goroutine_access(3, 200, 2000, false);
    
    // Analyze allocation
    auto result = GoroutineEscapeAnalyzer::analyze_goroutine_allocation(
        nullptr, 2000, 128, 43, 1
    );
    
    ASSERT_TRUE(result.accessed_across_goroutines);
    ASSERT_EQ(static_cast<int>(ObjectOwnership::GOROUTINE_SHARED), static_cast<int>(result.ownership));
    ASSERT_TRUE(result.needs_synchronization);
    ASSERT_TRUE(result.accessing_goroutines.size() >= 2);
}

void test_escape_analysis_stack_allocation() {
    GoroutineEscapeAnalyzer::reset_analysis();
    
    // Test small allocation with no escapes
    auto result = GoroutineEscapeAnalyzer::analyze_goroutine_allocation(
        nullptr, 3000, 32, 44, 1
    );
    
    ASSERT_FALSE(result.captured_by_goroutine);
    ASSERT_FALSE(result.accessed_across_goroutines);
    ASSERT_EQ(static_cast<int>(ObjectOwnership::STACK_LOCAL), static_cast<int>(result.ownership));
    ASSERT_FALSE(result.needs_synchronization);
}

void test_escape_analysis_size_limits() {
    GoroutineEscapeAnalyzer::reset_analysis();
    
    // Test allocation that's too large for stack
    auto result = GoroutineEscapeAnalyzer::analyze_goroutine_allocation(
        nullptr, 4000, GCConfig::MAX_STACK_ALLOC_SIZE + 1, 45, 1
    );
    
    ASSERT_TRUE(result.size_too_large);
    ASSERT_EQ(static_cast<int>(ObjectOwnership::GOROUTINE_PRIVATE), static_cast<int>(result.ownership));
}

// ============================================================================
// HEAP ALLOCATION TESTS
// ============================================================================

void test_heap_initialization() {
    GoroutineAwareHeap::initialize();
    
    auto& heap = GoroutineAwareHeap::instance();
    ASSERT_EQ(0, heap.get_total_allocated_bytes());
    
    GoroutineAwareHeap::shutdown();
}

void test_goroutine_registration() {
    GoroutineAwareHeap::initialize();
    auto& heap = GoroutineAwareHeap::instance();
    
    // Register goroutines
    heap.register_goroutine(1);
    heap.register_goroutine(2);
    
    auto goroutines = heap.get_registered_goroutines();
    ASSERT_EQ(2, goroutines.size());
    ASSERT_TRUE(std::find(goroutines.begin(), goroutines.end(), 1) != goroutines.end());
    ASSERT_TRUE(std::find(goroutines.begin(), goroutines.end(), 2) != goroutines.end());
    
    // Unregister goroutines
    heap.unregister_goroutine(1);
    heap.unregister_goroutine(2);
    
    goroutines = heap.get_registered_goroutines();
    ASSERT_EQ(0, goroutines.size());
    
    GoroutineAwareHeap::shutdown();
}

void test_stack_allocation() {
    GoroutineAwareHeap::initialize();
    auto& heap = GoroutineAwareHeap::instance();
    
    // Stack allocation should return marker
    void* obj = heap.allocate_by_ownership(64, 42, ObjectOwnership::STACK_LOCAL, 1);
    ASSERT_EQ(reinterpret_cast<void*>(0xDEADBEEF), obj);
    
    GoroutineAwareHeap::shutdown();
}

void test_goroutine_private_allocation() {
    GoroutineAwareHeap::initialize();
    auto& heap = GoroutineAwareHeap::instance();
    
    heap.register_goroutine(1);
    
    // Allocate private object
    void* obj = heap.allocate_by_ownership(64, 42, ObjectOwnership::GOROUTINE_PRIVATE, 1);
    ASSERT_NOT_NULL(obj);
    
    // Check object header
    GoroutineObjectHeader* header = get_goroutine_header(obj);
    ASSERT_NOT_NULL(header);
    ASSERT_EQ(64, header->size);
    ASSERT_EQ(42, header->type_id);
    ASSERT_EQ(static_cast<uint32_t>(ObjectOwnership::GOROUTINE_PRIVATE), header->ownership_type);
    ASSERT_EQ(1, header->owner_goroutine_id);
    
    heap.unregister_goroutine(1);
    GoroutineAwareHeap::shutdown();
}

void test_shared_allocation() {
    GoroutineAwareHeap::initialize();
    auto& heap = GoroutineAwareHeap::instance();
    
    // Allocate shared object
    void* obj = heap.allocate_by_ownership(128, 43, ObjectOwnership::GOROUTINE_SHARED, 0);
    ASSERT_NOT_NULL(obj);
    
    // Check object header
    GoroutineObjectHeader* header = get_goroutine_header(obj);
    ASSERT_NOT_NULL(header);
    ASSERT_EQ(128, header->size);
    ASSERT_EQ(43, header->type_id);
    ASSERT_EQ(static_cast<uint32_t>(ObjectOwnership::GOROUTINE_SHARED), header->ownership_type);
    ASSERT_TRUE(header->needs_sync);
    
    GoroutineAwareHeap::shutdown();
}

void test_global_allocation() {
    GoroutineAwareHeap::initialize();
    auto& heap = GoroutineAwareHeap::instance();
    
    // Allocate global object
    void* obj = heap.allocate_by_ownership(256, 44, ObjectOwnership::GLOBAL_SHARED, 0);
    ASSERT_NOT_NULL(obj);
    
    // Check object header
    GoroutineObjectHeader* header = get_goroutine_header(obj);
    ASSERT_NOT_NULL(header);
    ASSERT_EQ(256, header->size);
    ASSERT_EQ(44, header->type_id);
    ASSERT_EQ(static_cast<uint32_t>(ObjectOwnership::GLOBAL_SHARED), header->ownership_type);
    ASSERT_TRUE(header->needs_sync);
    ASSERT_EQ(0xFFFFFFFF, header->accessing_goroutines.load());
    
    GoroutineAwareHeap::shutdown();
}

// ============================================================================
// WRITE BARRIER TESTS
// ============================================================================

void test_write_barrier_initialization() {
    GoroutineWriteBarrier::initialize();
    
    auto stats = GoroutineWriteBarrier::get_statistics();
    ASSERT_EQ(0, stats.total_writes);
    ASSERT_EQ(0, stats.fast_writes);
    ASSERT_EQ(0, stats.sync_writes);
    
    GoroutineWriteBarrier::cleanup();
}

void test_fast_write_barrier() {
    GoroutineAwareHeap::initialize();
    GoroutineWriteBarrier::initialize();
    
    auto& heap = GoroutineAwareHeap::instance();
    heap.register_goroutine(1);
    
    // Allocate objects
    void* obj1 = heap.allocate_by_ownership(64, 42, ObjectOwnership::GOROUTINE_PRIVATE, 1);
    void* obj2 = heap.allocate_by_ownership(64, 43, ObjectOwnership::GOROUTINE_PRIVATE, 1);
    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);
    
    // Perform same-goroutine write
    void* field = obj1;
    GoroutineWriteBarrier::write_ref_with_sync(obj1, &field, obj2, 1);
    
    auto stats = GoroutineWriteBarrier::get_statistics();
    ASSERT_TRUE(stats.fast_writes > 0);
    
    heap.unregister_goroutine(1);
    GoroutineWriteBarrier::cleanup();
    GoroutineAwareHeap::shutdown();
}

void test_sync_write_barrier() {
    GoroutineAwareHeap::initialize();
    GoroutineWriteBarrier::initialize();
    
    auto& heap = GoroutineAwareHeap::instance();
    
    // Allocate shared object
    void* obj1 = heap.allocate_by_ownership(64, 42, ObjectOwnership::GOROUTINE_SHARED, 0);
    void* obj2 = heap.allocate_by_ownership(64, 43, ObjectOwnership::GOROUTINE_SHARED, 0);
    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);
    
    // Perform cross-goroutine write
    void* field = obj1;
    GoroutineWriteBarrier::write_ref_with_sync(obj1, &field, obj2, 1);
    
    auto stats = GoroutineWriteBarrier::get_statistics();
    ASSERT_TRUE(stats.sync_writes > 0);
    
    GoroutineWriteBarrier::cleanup();
    GoroutineAwareHeap::shutdown();
}

void test_bulk_write_barrier() {
    GoroutineAwareHeap::initialize();
    GoroutineWriteBarrier::initialize();
    
    auto& heap = GoroutineAwareHeap::instance();
    heap.register_goroutine(1);
    
    // Allocate objects
    void* obj = heap.allocate_by_ownership(256, 42, ObjectOwnership::GOROUTINE_PRIVATE, 1);
    void* val1 = heap.allocate_by_ownership(64, 43, ObjectOwnership::GOROUTINE_PRIVATE, 1);
    void* val2 = heap.allocate_by_ownership(64, 44, ObjectOwnership::GOROUTINE_PRIVATE, 1);
    void* val3 = heap.allocate_by_ownership(64, 45, ObjectOwnership::GOROUTINE_PRIVATE, 1);
    
    ASSERT_NOT_NULL(obj);
    ASSERT_NOT_NULL(val1);
    ASSERT_NOT_NULL(val2);
    ASSERT_NOT_NULL(val3);
    
    // Perform bulk write
    void* fields[3] = {
        static_cast<uint8_t*>(obj) + 0,
        static_cast<uint8_t*>(obj) + 8,
        static_cast<uint8_t*>(obj) + 16
    };
    void* values[3] = {val1, val2, val3};
    
    GoroutineWriteBarrier::bulk_write_refs(obj, fields, values, 3, 1);
    
    auto stats = GoroutineWriteBarrier::get_statistics();
    ASSERT_TRUE(stats.fast_writes >= 3);
    
    heap.unregister_goroutine(1);
    GoroutineWriteBarrier::cleanup();
    GoroutineAwareHeap::shutdown();
}

// ============================================================================
// COORDINATED GC TESTS
// ============================================================================

void test_gc_initialization() {
    auto& gc = GoroutineCoordinatedGC::instance();
    
    auto stats = gc.get_stats();
    ASSERT_EQ(0, stats.total_goroutines);
    ASSERT_EQ(0, stats.private_collections);
    ASSERT_EQ(0, stats.shared_collections);
}

void test_gc_goroutine_registration() {
    auto& gc = GoroutineCoordinatedGC::instance();
    
    gc.register_goroutine(1);
    gc.register_goroutine(2);
    
    auto stats = gc.get_stats();
    ASSERT_EQ(2, stats.total_goroutines);
    
    gc.unregister_goroutine(1);
    gc.unregister_goroutine(2);
    
    stats = gc.get_stats();
    ASSERT_EQ(0, stats.total_goroutines);
}

void test_gc_root_registration() {
    auto& gc = GoroutineCoordinatedGC::instance();
    
    gc.register_goroutine(1);
    
    // Create some dummy roots
    void* root1 = reinterpret_cast<void*>(0x1000);
    void* root2 = reinterpret_cast<void*>(0x2000);
    void* roots[2] = {root1, root2};
    
    gc.set_goroutine_stack_roots(1, roots, 2);
    
    // No direct way to verify, but should not crash
    
    gc.unregister_goroutine(1);
}

void test_gc_collection_request() {
    auto& gc = GoroutineCoordinatedGC::instance();
    
    gc.register_goroutine(1);
    
    // Request private collection
    gc.request_gc(GoroutineCoordinatedGC::GCType::PRIVATE);
    
    // Give it time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto stats = gc.get_stats();
    ASSERT_TRUE(stats.private_collections > 0);
    
    gc.unregister_goroutine(1);
}

// ============================================================================
// RUNTIME API TESTS
// ============================================================================

void test_runtime_initialization() {
    __gc_initialize_system();
    ASSERT_TRUE(__gc_is_system_initialized());
    __gc_shutdown_system();
}

void test_runtime_allocation() {
    __gc_initialize_system();
    
    __gc_register_goroutine(1);
    
    // Test different allocation types
    void* obj1 = __gc_alloc_fast(64, 42, 1);
    ASSERT_NOT_NULL(obj1);
    
    void* obj2 = __gc_alloc_by_ownership(128, 43, 
                                        static_cast<uint32_t>(ObjectOwnership::GOROUTINE_SHARED), 1);
    ASSERT_NOT_NULL(obj2);
    
    void* obj3 = __gc_alloc_array_fast(sizeof(int), 10, 44, 1);
    ASSERT_NOT_NULL(obj3);
    
    __gc_unregister_goroutine(1);
    __gc_shutdown_system();
}

void test_runtime_write_barriers() {
    __gc_initialize_system();
    
    __gc_register_goroutine(1);
    
    void* obj1 = __gc_alloc_fast(64, 42, 1);
    void* obj2 = __gc_alloc_fast(64, 43, 1);
    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);
    
    // Test write barrier
    void* field = obj1;
    __gc_write_barrier_sync(obj1, &field, obj2, 1);
    
    // Test read barrier
    void* read_result = __gc_read_barrier_sync(obj1, &field, 1);
    ASSERT_EQ(obj2, read_result);
    
    __gc_unregister_goroutine(1);
    __gc_shutdown_system();
}

void test_runtime_object_introspection() {
    __gc_initialize_system();
    
    __gc_register_goroutine(1);
    
    void* obj = __gc_alloc_by_ownership(64, 42, 
                                       static_cast<uint32_t>(ObjectOwnership::GOROUTINE_PRIVATE), 1);
    ASSERT_NOT_NULL(obj);
    
    // Test object introspection
    uint32_t ownership = __gc_get_object_ownership(obj);
    ASSERT_EQ(static_cast<uint32_t>(ObjectOwnership::GOROUTINE_PRIVATE), ownership);
    
    uint32_t owner = __gc_get_object_owner_goroutine(obj);
    ASSERT_EQ(1, owner);
    
    uint32_t is_shared = __gc_is_object_shared(obj);
    ASSERT_FALSE(is_shared);
    
    __gc_unregister_goroutine(1);
    __gc_shutdown_system();
}

// ============================================================================
// STRESS TESTS
// ============================================================================

void test_concurrent_allocation() {
    __gc_initialize_system();
    
    const size_t NUM_THREADS = 4;
    const size_t ALLOCATIONS_PER_THREAD = 100;
    
    std::vector<std::thread> threads;
    std::vector<std::vector<void*>> thread_objects(NUM_THREADS);
    
    // Create worker threads
    for (size_t i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back([i, ALLOCATIONS_PER_THREAD, &thread_objects]() {
            uint32_t goroutine_id = i + 1;
            __gc_register_goroutine(goroutine_id);
            
            for (size_t j = 0; j < ALLOCATIONS_PER_THREAD; ++j) {
                void* obj = __gc_alloc_fast(64 + (j % 128), 42 + (j % 10), goroutine_id);
                if (obj) {
                    thread_objects[i].push_back(obj);
                }
            }
            
            __gc_unregister_goroutine(goroutine_id);
        });
    }
    
    // Wait for completion
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify allocations
    size_t total_allocated = 0;
    for (const auto& objects : thread_objects) {
        total_allocated += objects.size();
        for (void* obj : objects) {
            ASSERT_NOT_NULL(obj);
        }
    }
    
    ASSERT_TRUE(total_allocated > 0);
    std::cout << "[TEST] Concurrent allocation: " << total_allocated << " objects allocated\n";
    
    __gc_shutdown_system();
}

void test_cross_goroutine_references() {
    __gc_initialize_system();
    
    __gc_register_goroutine(1);
    __gc_register_goroutine(2);
    
    // Allocate objects in different goroutines
    void* obj1 = __gc_alloc_by_ownership(64, 42, 
                                        static_cast<uint32_t>(ObjectOwnership::GOROUTINE_SHARED), 1);
    void* obj2 = __gc_alloc_by_ownership(64, 43, 
                                        static_cast<uint32_t>(ObjectOwnership::GOROUTINE_SHARED), 2);
    
    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);
    
    // Create cross-goroutine references
    void* field1 = obj1;
    void* field2 = obj2;
    
    __gc_write_barrier_sync(obj1, &field1, obj2, 1); // G1 -> G2 reference
    __gc_write_barrier_sync(obj2, &field2, obj1, 2); // G2 -> G1 reference
    
    // Verify references
    void* read1 = __gc_read_barrier_sync(obj1, &field1, 2);
    void* read2 = __gc_read_barrier_sync(obj2, &field2, 1);
    
    ASSERT_EQ(obj2, read1);
    ASSERT_EQ(obj1, read2);
    
    __gc_unregister_goroutine(1);
    __gc_unregister_goroutine(2);
    __gc_shutdown_system();
}

void test_gc_under_pressure() {
    __gc_initialize_system();
    
    const size_t NUM_GOROUTINES = 2;
    const size_t ALLOCATIONS = 500;
    
    std::vector<std::thread> threads;
    
    for (size_t i = 0; i < NUM_GOROUTINES; ++i) {
        threads.emplace_back([i, ALLOCATIONS]() {
            uint32_t goroutine_id = i + 1;
            __gc_register_goroutine(goroutine_id);
            
            std::vector<void*> objects;
            
            for (size_t j = 0; j < ALLOCATIONS; ++j) {
                void* obj = __gc_alloc_fast(1024, 42, goroutine_id); // Large objects
                if (obj) {
                    objects.push_back(obj);
                }
                
                // Trigger GC occasionally
                if (j % 50 == 0) {
                    __gc_trigger_collection(0); // Private GC
                }
                
                // Clear some objects to create pressure
                if (j % 100 == 99 && objects.size() > 50) {
                    objects.erase(objects.begin(), objects.begin() + 25);
                }
            }
            
            __gc_unregister_goroutine(goroutine_id);
        });
    }
    
    // Wait for completion
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Trigger final GC
    __gc_trigger_collection(1); // Shared GC
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    __gc_shutdown_system();
}

// ============================================================================
// PERFORMANCE TESTS
// ============================================================================

void test_allocation_performance() {
    __gc_initialize_system();
    __gc_register_goroutine(1);
    
    const size_t NUM_ALLOCATIONS = 10000;
    
    // Measure stack allocation performance
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        __gc_alloc_stack(64, 42, 1);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto stack_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Measure private allocation performance
    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        __gc_alloc_fast(64, 42, 1);
    }
    end = std::chrono::high_resolution_clock::now();
    auto private_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Measure shared allocation performance
    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_ALLOCATIONS; ++i) {
        __gc_alloc_by_ownership(64, 42, static_cast<uint32_t>(ObjectOwnership::GOROUTINE_SHARED), 1);
    }
    end = std::chrono::high_resolution_clock::now();
    auto shared_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "[PERF] " << NUM_ALLOCATIONS << " allocations:\n";
    std::cout << "  Stack: " << stack_time.count() << " μs\n";
    std::cout << "  Private: " << private_time.count() << " μs\n";
    std::cout << "  Shared: " << shared_time.count() << " μs\n";
    
    __gc_unregister_goroutine(1);
    __gc_shutdown_system();
}

void test_write_barrier_performance() {
    __gc_initialize_system();
    __gc_register_goroutine(1);
    
    const size_t NUM_WRITES = 10000;
    
    // Allocate objects
    void* obj1 = __gc_alloc_fast(64, 42, 1);
    void* obj2 = __gc_alloc_fast(64, 43, 1);
    void* shared_obj = __gc_alloc_by_ownership(64, 44, 
                                              static_cast<uint32_t>(ObjectOwnership::GOROUTINE_SHARED), 1);
    
    ASSERT_NOT_NULL(obj1);
    ASSERT_NOT_NULL(obj2);
    ASSERT_NOT_NULL(shared_obj);
    
    // Measure fast write barrier performance
    void* field1 = obj1;
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_WRITES; ++i) {
        __gc_write_barrier_sync(obj1, &field1, obj2, 1);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto fast_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    // Measure sync write barrier performance
    void* field2 = shared_obj;
    start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NUM_WRITES; ++i) {
        __gc_write_barrier_sync(shared_obj, &field2, obj2, 1);
    }
    end = std::chrono::high_resolution_clock::now();
    auto sync_time = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "[PERF] " << NUM_WRITES << " write barriers:\n";
    std::cout << "  Fast: " << fast_time.count() << " μs\n";
    std::cout << "  Sync: " << sync_time.count() << " μs\n";
    std::cout << "  Overhead: " << (double)sync_time.count() / fast_time.count() << "x\n";
    
    __gc_unregister_goroutine(1);
    __gc_shutdown_system();
}

} // namespace gots

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main() {
    std::cout << "GoTS Goroutine-Aware GC Test Suite\n";
    std::cout << "==================================\n\n";
    
    gots::GCTestFramework framework;
    
    // Escape Analysis Tests
    std::cout << "=== ESCAPE ANALYSIS TESTS ===\n";
    framework.run_test("Escape Analysis Basic", gots::test_escape_analysis_basic);
    framework.run_test("Cross-Goroutine Access", gots::test_escape_analysis_cross_goroutine_access);
    framework.run_test("Stack Allocation Analysis", gots::test_escape_analysis_stack_allocation);
    framework.run_test("Size Limits", gots::test_escape_analysis_size_limits);
    
    // Heap Allocation Tests
    std::cout << "\n=== HEAP ALLOCATION TESTS ===\n";
    framework.run_test("Heap Initialization", gots::test_heap_initialization);
    framework.run_test("Goroutine Registration", gots::test_goroutine_registration);
    framework.run_test("Stack Allocation", gots::test_stack_allocation);
    framework.run_test("Goroutine Private Allocation", gots::test_goroutine_private_allocation);
    framework.run_test("Shared Allocation", gots::test_shared_allocation);
    framework.run_test("Global Allocation", gots::test_global_allocation);
    
    // Write Barrier Tests
    std::cout << "\n=== WRITE BARRIER TESTS ===\n";
    framework.run_test("Write Barrier Initialization", gots::test_write_barrier_initialization);
    framework.run_test("Fast Write Barrier", gots::test_fast_write_barrier);
    framework.run_test("Sync Write Barrier", gots::test_sync_write_barrier);
    framework.run_test("Bulk Write Barrier", gots::test_bulk_write_barrier);
    
    // Coordinated GC Tests
    std::cout << "\n=== COORDINATED GC TESTS ===\n";
    framework.run_test("GC Initialization", gots::test_gc_initialization);
    framework.run_test("GC Goroutine Registration", gots::test_gc_goroutine_registration);
    framework.run_test("GC Root Registration", gots::test_gc_root_registration);
    framework.run_test("GC Collection Request", gots::test_gc_collection_request);
    
    // Runtime API Tests
    std::cout << "\n=== RUNTIME API TESTS ===\n";
    framework.run_test("Runtime Initialization", gots::test_runtime_initialization);
    framework.run_test("Runtime Allocation", gots::test_runtime_allocation);
    framework.run_test("Runtime Write Barriers", gots::test_runtime_write_barriers);
    framework.run_test("Runtime Object Introspection", gots::test_runtime_object_introspection);
    
    // Stress Tests
    std::cout << "\n=== STRESS TESTS ===\n";
    framework.run_test("Concurrent Allocation", gots::test_concurrent_allocation);
    framework.run_test("Cross-Goroutine References", gots::test_cross_goroutine_references);
    framework.run_test("GC Under Pressure", gots::test_gc_under_pressure);
    
    // Performance Tests
    std::cout << "\n=== PERFORMANCE TESTS ===\n";
    framework.run_test("Allocation Performance", gots::test_allocation_performance);
    framework.run_test("Write Barrier Performance", gots::test_write_barrier_performance);
    
    // Print final summary
    framework.print_summary();
    
    // Run system tests
    std::cout << "=== SYSTEM TESTS ===\n";
    __gc_test_system();
    
    std::cout << "\n=== STRESS TEST ===\n";
    __gc_stress_test();
    
    return framework.all_tests_passed() ? 0 : 1;
}