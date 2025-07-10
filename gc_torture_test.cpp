#include "goroutine_aware_gc.h"
#include "performance_integration.h"
#include "unified_event_system.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <memory>
#include <algorithm>
#include <unordered_set>
#include <fstream>
#include <cassert>

namespace gots {

// ============================================================================
// MEMORY LEAK DETECTION SYSTEM
// ============================================================================

class MemoryLeakDetector {
private:
    struct AllocationRecord {
        void* ptr;
        size_t size;
        std::string location;
        std::chrono::steady_clock::time_point timestamp;
        uint32_t goroutine_id;
        ObjectOwnership ownership;
    };
    
    std::mutex records_mutex_;
    std::unordered_map<void*, AllocationRecord> allocations_;
    std::atomic<size_t> total_allocated_{0};
    std::atomic<size_t> total_freed_{0};
    std::atomic<size_t> peak_memory_{0};
    std::atomic<size_t> current_memory_{0};
    
public:
    void record_allocation(void* ptr, size_t size, const std::string& location, 
                          uint32_t goroutine_id, ObjectOwnership ownership) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(records_mutex_);
        
        AllocationRecord record;
        record.ptr = ptr;
        record.size = size;
        record.location = location;
        record.timestamp = std::chrono::steady_clock::now();
        record.goroutine_id = goroutine_id;
        record.ownership = ownership;
        
        allocations_[ptr] = record;
        
        total_allocated_.fetch_add(size, std::memory_order_relaxed);
        size_t current = current_memory_.fetch_add(size, std::memory_order_relaxed) + size;
        
        // Update peak memory
        size_t peak = peak_memory_.load();
        while (current > peak && !peak_memory_.compare_exchange_weak(peak, current)) {
            // Retry if peak was updated by another thread
        }
    }
    
    void record_deallocation(void* ptr) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(records_mutex_);
        
        auto it = allocations_.find(ptr);
        if (it != allocations_.end()) {
            size_t size = it->second.size;
            allocations_.erase(it);
            
            total_freed_.fetch_add(size, std::memory_order_relaxed);
            current_memory_.fetch_sub(size, std::memory_order_relaxed);
        } else {
            std::cerr << "WARNING: Attempted to free untracked pointer " << ptr << "\n";
        }
    }
    
    struct LeakReport {
        size_t total_leaked_bytes;
        size_t total_leaked_objects;
        std::vector<AllocationRecord> leaked_allocations;
        size_t peak_memory_usage;
        size_t total_allocated;
        size_t total_freed;
    };
    
    LeakReport generate_leak_report() {
        std::lock_guard<std::mutex> lock(records_mutex_);
        
        LeakReport report;
        report.total_leaked_bytes = 0;
        report.total_leaked_objects = allocations_.size();
        report.peak_memory_usage = peak_memory_.load();
        report.total_allocated = total_allocated_.load();
        report.total_freed = total_freed_.load();
        
        for (const auto& [ptr, record] : allocations_) {
            report.total_leaked_bytes += record.size;
            report.leaked_allocations.push_back(record);
        }
        
        // Sort by timestamp to find oldest leaks
        std::sort(report.leaked_allocations.begin(), report.leaked_allocations.end(),
                 [](const AllocationRecord& a, const AllocationRecord& b) {
                     return a.timestamp < b.timestamp;
                 });
        
        return report;
    }
    
    void print_leak_report() {
        auto report = generate_leak_report();
        
        std::cout << "\n=== MEMORY LEAK DETECTION REPORT ===\n";
        std::cout << "Total allocated: " << report.total_allocated << " bytes\n";
        std::cout << "Total freed: " << report.total_freed << " bytes\n";
        std::cout << "Peak memory usage: " << report.peak_memory_usage << " bytes\n";
        std::cout << "Current leaked objects: " << report.total_leaked_objects << "\n";
        std::cout << "Current leaked bytes: " << report.total_leaked_bytes << " bytes\n";
        
        if (report.total_leaked_objects > 0) {
            std::cout << "\n🚨 MEMORY LEAKS DETECTED!\n";
            std::cout << "Oldest 10 leaked allocations:\n";
            
            size_t count = std::min(report.leaked_allocations.size(), size_t(10));
            for (size_t i = 0; i < count; ++i) {
                const auto& record = report.leaked_allocations[i];
                std::cout << "  - " << record.size << " bytes at " << record.ptr 
                          << " (goroutine " << record.goroutine_id << ") from " 
                          << record.location << "\n";
            }
        } else {
            std::cout << "\n✅ NO MEMORY LEAKS DETECTED!\n";
        }
        
        std::cout << "=====================================\n\n";
    }
    
    void save_report_to_file(const std::string& filename) {
        auto report = generate_leak_report();
        
        std::ofstream file(filename);
        file << "Memory Leak Detection Report\n";
        file << "===========================\n\n";
        file << "Total allocated: " << report.total_allocated << " bytes\n";
        file << "Total freed: " << report.total_freed << " bytes\n";
        file << "Peak memory usage: " << report.peak_memory_usage << " bytes\n";
        file << "Leaked objects: " << report.total_leaked_objects << "\n";
        file << "Leaked bytes: " << report.total_leaked_bytes << " bytes\n\n";
        
        if (!report.leaked_allocations.empty()) {
            file << "All leaked allocations:\n";
            for (const auto& record : report.leaked_allocations) {
                file << record.ptr << "," << record.size << "," << record.goroutine_id 
                     << "," << record.location << "\n";
            }
        }
        
        file.close();
    }
    
    bool has_leaks() {
        std::lock_guard<std::mutex> lock(records_mutex_);
        return !allocations_.empty();
    }
};

// Global leak detector instance
static MemoryLeakDetector g_leak_detector;

// ============================================================================
// INSTRUMENTED ALLOCATION WRAPPERS
// ============================================================================

void* tracked_alloc(size_t size, uint32_t type_id, ObjectOwnership ownership, 
                   uint32_t goroutine_id, const std::string& location) {
    void* ptr = __gc_alloc_by_ownership(size, type_id, static_cast<uint32_t>(ownership), goroutine_id);
    if (ptr) {
        g_leak_detector.record_allocation(ptr, size, location, goroutine_id, ownership);
    }
    return ptr;
}

void tracked_free(void* ptr) {
    if (ptr) {
        g_leak_detector.record_deallocation(ptr);
        // Note: In GC system, we don't explicitly free - this is for tracking only
    }
}

#define TRACKED_ALLOC(size, type_id, ownership, goroutine_id) \
    tracked_alloc(size, type_id, ownership, goroutine_id, __FILE__ ":" + std::to_string(__LINE__))

// ============================================================================
// GC TORTURE TEST SUITE
// ============================================================================

class GCTortureTest {
private:
    std::atomic<bool> running_{true};
    std::atomic<uint64_t> total_allocations_{0};
    std::atomic<uint64_t> total_goroutines_{0};
    std::atomic<uint64_t> gc_cycles_{0};
    
    // Test configuration
    static constexpr size_t NUM_TORTURE_THREADS = 16;
    static constexpr size_t NUM_GOROUTINES_PER_THREAD = 100;
    static constexpr size_t TORTURE_DURATION_SECONDS = 30;
    static constexpr size_t MAX_OBJECT_SIZE = 64 * 1024; // 64KB
    static constexpr size_t MIN_OBJECT_SIZE = 8;
    
    // Reference graph for cycle testing
    struct TestObject {
        uint32_t id;
        uint32_t goroutine_id;
        std::vector<TestObject*> references;
        uint8_t* data;
        size_t data_size;
        std::atomic<int> ref_count{1};
        
        TestObject(uint32_t obj_id, uint32_t gor_id, size_t size) 
            : id(obj_id), goroutine_id(gor_id), data_size(size) {
            data = static_cast<uint8_t*>(TRACKED_ALLOC(size, 42, ObjectOwnership::GOROUTINE_PRIVATE, gor_id));
            if (data) {
                // Fill with recognizable pattern
                for (size_t i = 0; i < size; ++i) {
                    data[i] = static_cast<uint8_t>((obj_id + i) % 256);
                }
            }
        }
        
        ~TestObject() {
            if (data) {
                tracked_free(data);
            }
        }
        
        void add_reference(TestObject* other) {
            if (other) {
                references.push_back(other);
                other->ref_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
        
        void remove_reference(TestObject* other) {
            auto it = std::find(references.begin(), references.end(), other);
            if (it != references.end()) {
                references.erase(it);
                if (other->ref_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
                    delete other; // Last reference
                }
            }
        }
    };
    
public:
    // ============================================================================
    // TEST 1: ALLOCATION TORTURE
    // ============================================================================
    
    void test_allocation_torture() {
        std::cout << "🔥 Starting allocation torture test...\n";
        
        std::vector<std::thread> threads;
        std::atomic<uint64_t> thread_allocations{0};
        
        for (size_t i = 0; i < NUM_TORTURE_THREADS; ++i) {
            threads.emplace_back([this, i, &thread_allocations]() {
                std::random_device rd;
                std::mt19937 gen(rd() + i);
                std::uniform_int_distribution<size_t> size_dist(MIN_OBJECT_SIZE, MAX_OBJECT_SIZE);
                std::uniform_int_distribution<int> ownership_dist(0, 3);
                
                uint32_t goroutine_id = i + 1;
                __gc_register_goroutine(goroutine_id);
                
                auto start_time = std::chrono::steady_clock::now();
                std::vector<void*> allocated_objects;
                
                while (std::chrono::steady_clock::now() - start_time < 
                       std::chrono::seconds(TORTURE_DURATION_SECONDS)) {
                    
                    // Allocate random-sized objects
                    size_t size = size_dist(gen);
                    ObjectOwnership ownership = static_cast<ObjectOwnership>(ownership_dist(gen));
                    
                    void* ptr = TRACKED_ALLOC(size, 42 + (i % 10), ownership, goroutine_id);
                    if (ptr) {
                        allocated_objects.push_back(ptr);
                        thread_allocations.fetch_add(1, std::memory_order_relaxed);
                        
                        // Fill with test pattern
                        uint8_t* data = static_cast<uint8_t*>(ptr);
                        for (size_t j = 0; j < size; ++j) {
                            data[j] = static_cast<uint8_t>((i + j) % 256);
                        }
                    }
                    
                    // Randomly free some objects
                    if (!allocated_objects.empty() && gen() % 3 == 0) {
                        size_t idx = gen() % allocated_objects.size();
                        tracked_free(allocated_objects[idx]);
                        allocated_objects.erase(allocated_objects.begin() + idx);
                    }
                    
                    // Trigger GC occasionally
                    if (gen() % 100 == 0) {
                        __gc_trigger_collection(gen() % 2);
                        gc_cycles_.fetch_add(1, std::memory_order_relaxed);
                    }
                    
                    // Cross-goroutine writes (stress write barriers)
                    if (!allocated_objects.empty() && gen() % 5 == 0) {
                        size_t idx1 = gen() % allocated_objects.size();
                        size_t idx2 = gen() % allocated_objects.size();
                        
                        __gc_write_barrier_sync(allocated_objects[idx1], 
                                              static_cast<void**>(allocated_objects[idx1]), 
                                              allocated_objects[idx2], goroutine_id);
                    }
                }
                
                // Clean up remaining objects
                for (void* ptr : allocated_objects) {
                    tracked_free(ptr);
                }
                
                __gc_unregister_goroutine(goroutine_id);
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        total_allocations_.fetch_add(thread_allocations.load(), std::memory_order_relaxed);
        std::cout << "✅ Allocation torture completed: " << thread_allocations.load() << " allocations\n";
    }
    
    // ============================================================================
    // TEST 2: REFERENCE CYCLE TORTURE
    // ============================================================================
    
    void test_reference_cycles() {
        std::cout << "🔄 Starting reference cycle torture test...\n";
        
        std::vector<std::thread> threads;
        std::atomic<uint64_t> cycles_created{0};
        
        for (size_t i = 0; i < NUM_TORTURE_THREADS; ++i) {
            threads.emplace_back([this, i, &cycles_created]() {
                std::random_device rd;
                std::mt19937 gen(rd() + i);
                std::uniform_int_distribution<size_t> cycle_size_dist(3, 20);
                std::uniform_int_distribution<size_t> obj_size_dist(MIN_OBJECT_SIZE, MAX_OBJECT_SIZE);
                
                uint32_t goroutine_id = i + 100;
                __gc_register_goroutine(goroutine_id);
                
                auto start_time = std::chrono::steady_clock::now();
                
                while (std::chrono::steady_clock::now() - start_time < 
                       std::chrono::seconds(TORTURE_DURATION_SECONDS / 2)) {
                    
                    // Create a random-sized reference cycle
                    size_t cycle_size = cycle_size_dist(gen);
                    std::vector<TestObject*> cycle_objects;
                    
                    // Create objects
                    for (size_t j = 0; j < cycle_size; ++j) {
                        size_t obj_size = obj_size_dist(gen);
                        TestObject* obj = new TestObject(i * 1000 + j, goroutine_id, obj_size);
                        cycle_objects.push_back(obj);
                    }
                    
                    // Create circular references
                    for (size_t j = 0; j < cycle_size; ++j) {
                        size_t next_idx = (j + 1) % cycle_size;
                        cycle_objects[j]->add_reference(cycle_objects[next_idx]);
                        
                        // Add some random cross-references
                        if (gen() % 3 == 0 && cycle_size > 3) {
                            size_t random_idx = gen() % cycle_size;
                            if (random_idx != j) {
                                cycle_objects[j]->add_reference(cycle_objects[random_idx]);
                            }
                        }
                    }
                    
                    cycles_created.fetch_add(1, std::memory_order_relaxed);
                    
                    // Let the cycle exist for a while
                    std::this_thread::sleep_for(std::chrono::milliseconds(gen() % 100));
                    
                    // Break the cycle randomly
                    if (gen() % 2 == 0) {
                        size_t break_idx = gen() % cycle_size;
                        cycle_objects[break_idx]->references.clear();
                    }
                    
                    // Clean up (this should trigger cycle collection)
                    for (TestObject* obj : cycle_objects) {
                        if (obj->ref_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
                            delete obj;
                        }
                    }
                    
                    // Trigger GC to collect cycles
                    if (gen() % 10 == 0) {
                        __gc_trigger_collection(1); // Full collection
                        gc_cycles_.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                
                __gc_unregister_goroutine(goroutine_id);
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        std::cout << "✅ Reference cycle torture completed: " << cycles_created.load() << " cycles created\n";
    }
    
    // ============================================================================
    // TEST 3: GOROUTINE LIFECYCLE TORTURE
    // ============================================================================
    
    void test_goroutine_lifecycle() {
        std::cout << "👥 Starting goroutine lifecycle torture test...\n";
        
        std::atomic<uint64_t> goroutines_created{0};
        
        auto start_time = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - start_time < 
               std::chrono::seconds(TORTURE_DURATION_SECONDS / 3)) {
            
            std::vector<std::thread> goroutine_threads;
            
            // Create a burst of goroutines
            for (size_t i = 0; i < NUM_GOROUTINES_PER_THREAD; ++i) {
                goroutine_threads.emplace_back([this, i, &goroutines_created]() {
                    uint32_t goroutine_id = 1000 + i + goroutines_created.load();
                    __gc_register_goroutine(goroutine_id);
                    
                    std::random_device rd;
                    std::mt19937 gen(rd() + goroutine_id);
                    std::uniform_int_distribution<size_t> size_dist(MIN_OBJECT_SIZE, MAX_OBJECT_SIZE);
                    
                    // Allocate objects with different ownership patterns
                    std::vector<void*> objects;
                    
                    for (size_t j = 0; j < 50; ++j) {
                        size_t size = size_dist(gen);
                        ObjectOwnership ownership;
                        
                        switch (j % 4) {
                            case 0: ownership = ObjectOwnership::STACK_LOCAL; break;
                            case 1: ownership = ObjectOwnership::GOROUTINE_PRIVATE; break;
                            case 2: ownership = ObjectOwnership::GOROUTINE_SHARED; break;
                            case 3: ownership = ObjectOwnership::GLOBAL_SHARED; break;
                        }
                        
                        void* ptr = TRACKED_ALLOC(size, 100 + (j % 20), ownership, goroutine_id);
                        if (ptr) {
                            objects.push_back(ptr);
                        }
                    }
                    
                    // Simulate some work
                    std::this_thread::sleep_for(std::chrono::milliseconds(gen() % 100));
                    
                    // Cross-goroutine sharing
                    if (!objects.empty()) {
                        for (size_t k = 0; k < 10; ++k) {
                            size_t idx1 = gen() % objects.size();
                            size_t idx2 = gen() % objects.size();
                            
                            __gc_write_barrier_sync(objects[idx1], 
                                                  static_cast<void**>(objects[idx1]), 
                                                  objects[idx2], goroutine_id);
                        }
                    }
                    
                    // Clean up
                    for (void* ptr : objects) {
                        tracked_free(ptr);
                    }
                    
                    __gc_unregister_goroutine(goroutine_id);
                    goroutines_created.fetch_add(1, std::memory_order_relaxed);
                });
            }
            
            // Wait for this burst to complete
            for (auto& thread : goroutine_threads) {
                thread.join();
            }
            
            // Trigger collection after each burst
            __gc_trigger_collection(1);
            gc_cycles_.fetch_add(1, std::memory_order_relaxed);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        total_goroutines_.fetch_add(goroutines_created.load(), std::memory_order_relaxed);
        std::cout << "✅ Goroutine lifecycle torture completed: " << goroutines_created.load() << " goroutines\n";
    }
    
    // ============================================================================
    // TEST 4: WRITE BARRIER TORTURE
    // ============================================================================
    
    void test_write_barrier_torture() {
        std::cout << "✍️  Starting write barrier torture test...\n";
        
        std::vector<std::thread> threads;
        std::atomic<uint64_t> barrier_operations{0};
        
        // Create shared objects
        std::vector<void*> shared_objects;
        for (size_t i = 0; i < 1000; ++i) {
            void* ptr = TRACKED_ALLOC(64, 200, ObjectOwnership::GLOBAL_SHARED, 0);
            if (ptr) {
                shared_objects.push_back(ptr);
            }
        }
        
        for (size_t i = 0; i < NUM_TORTURE_THREADS; ++i) {
            threads.emplace_back([this, i, &shared_objects, &barrier_operations]() {
                std::random_device rd;
                std::mt19937 gen(rd() + i);
                
                uint32_t goroutine_id = 2000 + i;
                __gc_register_goroutine(goroutine_id);
                
                auto start_time = std::chrono::steady_clock::now();
                
                while (std::chrono::steady_clock::now() - start_time < 
                       std::chrono::seconds(TORTURE_DURATION_SECONDS / 4)) {
                    
                    // Rapid-fire write barrier operations
                    for (size_t j = 0; j < 1000; ++j) {
                        if (shared_objects.size() < 2) break;
                        
                        size_t idx1 = gen() % shared_objects.size();
                        size_t idx2 = gen() % shared_objects.size();
                        
                        // Cross-goroutine write
                        __gc_write_barrier_sync(shared_objects[idx1], 
                                              static_cast<void**>(shared_objects[idx1]), 
                                              shared_objects[idx2], goroutine_id);
                        
                        // Bulk write barrier test
                        if (shared_objects.size() >= 10) {
                            std::vector<void**> fields;
                            std::vector<void*> values;
                            
                            for (size_t k = 0; k < 5; ++k) {
                                size_t idx = gen() % shared_objects.size();
                                fields.push_back(static_cast<void**>(shared_objects[idx]));
                                values.push_back(shared_objects[gen() % shared_objects.size()]);
                            }
                            
                            __gc_bulk_write_barrier(shared_objects[idx1], 
                                                   fields.data(), values.data(), 
                                                   fields.size(), goroutine_id);
                        }
                        
                        barrier_operations.fetch_add(2, std::memory_order_relaxed);
                    }
                    
                    // Trigger GC occasionally to test barrier correctness
                    if (gen() % 50 == 0) {
                        __gc_trigger_collection(gen() % 2);
                    }
                }
                
                __gc_unregister_goroutine(goroutine_id);
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Clean up shared objects
        for (void* ptr : shared_objects) {
            tracked_free(ptr);
        }
        
        std::cout << "✅ Write barrier torture completed: " << barrier_operations.load() << " operations\n";
    }
    
    // ============================================================================
    // MAIN TORTURE TEST RUNNER
    // ============================================================================
    
    void run_full_torture_test() {
        std::cout << "\n🔥🔥🔥 STARTING COMPREHENSIVE GC TORTURE TEST 🔥🔥🔥\n";
        std::cout << "This test will stress every aspect of the garbage collector...\n\n";
        
        // Initialize GC system
        __gc_initialize_system();
        
        auto overall_start = std::chrono::steady_clock::now();
        
        // Run individual torture tests
        test_allocation_torture();
        
        // Force collection between tests
        __gc_trigger_collection(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        test_reference_cycles();
        
        // Force collection between tests
        __gc_trigger_collection(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        test_goroutine_lifecycle();
        
        // Force collection between tests
        __gc_trigger_collection(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        test_write_barrier_torture();
        
        // Final comprehensive collection
        std::cout << "\n🧹 Running final garbage collection...\n";
        __gc_trigger_collection(1); // Full collection
        std::this_thread::sleep_for(std::chrono::seconds(3)); // Allow GC to complete
        
        auto overall_end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(overall_end - overall_start);
        
        // Print test summary
        std::cout << "\n📊 TORTURE TEST SUMMARY:\n";
        std::cout << "Total duration: " << duration.count() << " seconds\n";
        std::cout << "Total allocations: " << total_allocations_.load() << "\n";
        std::cout << "Total goroutines: " << total_goroutines_.load() << "\n";
        std::cout << "Total GC cycles: " << gc_cycles_.load() << "\n";
        
        // Print GC statistics
        __gc_print_statistics();
        
        // Check for memory leaks
        std::cout << "\n🔍 ANALYZING MEMORY LEAKS...\n";
        g_leak_detector.print_leak_report();
        g_leak_detector.save_report_to_file("gc_torture_leak_report.txt");
        
        // Shutdown GC system
        __gc_shutdown_system();
        
        if (g_leak_detector.has_leaks()) {
            std::cout << "\n❌ TORTURE TEST FAILED - MEMORY LEAKS DETECTED!\n";
            std::exit(1);
        } else {
            std::cout << "\n✅ TORTURE TEST PASSED - NO MEMORY LEAKS DETECTED!\n";
        }
    }
};

} // namespace gots

// ============================================================================
// MAIN TEST FUNCTION
// ============================================================================

int main() {
    std::cout << "GoTS Garbage Collector Torture Test\n";
    std::cout << "===================================\n\n";
    
    try {
        gots::GCTortureTest torture_test;
        torture_test.run_full_torture_test();
        
        std::cout << "\n🎉 All tests completed successfully!\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n💥 Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}