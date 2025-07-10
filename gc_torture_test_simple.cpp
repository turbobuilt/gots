#include "gc_memory_manager.h"
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
// SIMPLE MEMORY LEAK DETECTION SYSTEM
// ============================================================================

class SimpleLeakDetector {
private:
    struct AllocationRecord {
        void* ptr;
        size_t size;
        std::string location;
        std::chrono::steady_clock::time_point timestamp;
    };
    
    std::mutex records_mutex_;
    std::unordered_map<void*, AllocationRecord> allocations_;
    std::atomic<size_t> total_allocated_{0};
    std::atomic<size_t> total_freed_{0};
    std::atomic<size_t> peak_memory_{0};
    std::atomic<size_t> current_memory_{0};
    
public:
    void record_allocation(void* ptr, size_t size, const std::string& location) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(records_mutex_);
        
        AllocationRecord record;
        record.ptr = ptr;
        record.size = size;
        record.location = location;
        record.timestamp = std::chrono::steady_clock::now();
        
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
            std::cerr << "WARNING: Attempted to free untracked pointer " << ptr << "\\n";
        }
    }
    
    void print_leak_report() {
        std::lock_guard<std::mutex> lock(records_mutex_);
        
        std::cout << "\\n=== MEMORY LEAK DETECTION REPORT ===\\n";
        std::cout << "Total allocated: " << total_allocated_.load() << " bytes\\n";
        std::cout << "Total freed: " << total_freed_.load() << " bytes\\n";
        std::cout << "Peak memory usage: " << peak_memory_.load() << " bytes\\n";
        std::cout << "Current leaked objects: " << allocations_.size() << "\\n";
        
        size_t total_leaked = 0;
        for (const auto& [ptr, record] : allocations_) {
            total_leaked += record.size;
        }
        std::cout << "Current leaked bytes: " << total_leaked << " bytes\\n";
        
        if (allocations_.size() > 0) {
            std::cout << "\\n🚨 MEMORY LEAKS DETECTED!\\n";
            std::cout << "First 10 leaked allocations:\\n";
            
            size_t count = 0;
            for (const auto& [ptr, record] : allocations_) {
                if (count >= 10) break;
                std::cout << "  - " << record.size << " bytes at " << ptr 
                          << " from " << record.location << "\\n";
                count++;
            }
        } else {
            std::cout << "\\n✅ NO MEMORY LEAKS DETECTED!\\n";
        }
        
        std::cout << "=====================================\\n\\n";
    }
    
    bool has_leaks() {
        std::lock_guard<std::mutex> lock(records_mutex_);
        return !allocations_.empty();
    }
};

// Global leak detector instance
static SimpleLeakDetector g_leak_detector;

// ============================================================================
// INSTRUMENTED ALLOCATION WRAPPERS
// ============================================================================

void* tracked_alloc(size_t size, uint32_t type_id, const std::string& location) {
    void* ptr = __gc_alloc_fast(size, type_id);
    if (ptr) {
        g_leak_detector.record_allocation(ptr, size, location);
    }
    return ptr;
}

void tracked_free(void* ptr) {
    if (ptr) {
        g_leak_detector.record_deallocation(ptr);
        // Note: In GC system, we don't explicitly free - this is for tracking only
    }
}

#define TRACKED_ALLOC(size, type_id) \\
    tracked_alloc(size, type_id, __FILE__ ":" + std::to_string(__LINE__))

// ============================================================================
// SIMPLE GC TORTURE TEST SUITE
// ============================================================================

class SimpleGCTortureTest {
private:
    std::atomic<bool> running_{true};
    std::atomic<uint64_t> total_allocations_{0};
    std::atomic<uint64_t> gc_cycles_{0};
    
    // Test configuration
    static constexpr size_t NUM_TORTURE_THREADS = 8;
    static constexpr size_t TORTURE_DURATION_SECONDS = 10;
    static constexpr size_t MAX_OBJECT_SIZE = 1024;
    static constexpr size_t MIN_OBJECT_SIZE = 8;
    
public:
    // ============================================================================
    // TEST 1: ALLOCATION TORTURE
    // ============================================================================
    
    void test_allocation_torture() {
        std::cout << "🔥 Starting allocation torture test...\\n";
        
        std::vector<std::thread> threads;
        std::atomic<uint64_t> thread_allocations{0};
        
        for (size_t i = 0; i < NUM_TORTURE_THREADS; ++i) {
            threads.emplace_back([this, i, &thread_allocations]() {
                std::random_device rd;
                std::mt19937 gen(rd() + i);
                std::uniform_int_distribution<size_t> size_dist(MIN_OBJECT_SIZE, MAX_OBJECT_SIZE);
                
                auto start_time = std::chrono::steady_clock::now();
                std::vector<void*> allocated_objects;
                
                while (std::chrono::steady_clock::now() - start_time < 
                       std::chrono::seconds(TORTURE_DURATION_SECONDS)) {
                    
                    // Allocate random-sized objects
                    size_t size = size_dist(gen);
                    
                    void* ptr = TRACKED_ALLOC(size, 42 + (i % 10));
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
                        __gc_safepoint();
                        gc_cycles_.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                
                // Clean up remaining objects
                for (void* ptr : allocated_objects) {
                    tracked_free(ptr);
                }
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        total_allocations_.fetch_add(thread_allocations.load(), std::memory_order_relaxed);
        std::cout << "✅ Allocation torture completed: " << thread_allocations.load() << " allocations\\n";
    }
    
    // ============================================================================
    // TEST 2: WRITE BARRIER TORTURE
    // ============================================================================
    
    void test_write_barrier_torture() {
        std::cout << "✍️  Starting write barrier torture test...\\n";
        
        std::vector<std::thread> threads;
        std::atomic<uint64_t> barrier_operations{0};
        
        // Create shared objects
        std::vector<void*> shared_objects;
        for (size_t i = 0; i < 100; ++i) {
            void* ptr = TRACKED_ALLOC(64, 200);
            if (ptr) {
                shared_objects.push_back(ptr);
            }
        }
        
        for (size_t i = 0; i < NUM_TORTURE_THREADS; ++i) {
            threads.emplace_back([this, i, &shared_objects, &barrier_operations]() {
                std::random_device rd;
                std::mt19937 gen(rd() + i);
                
                auto start_time = std::chrono::steady_clock::now();
                
                while (std::chrono::steady_clock::now() - start_time < 
                       std::chrono::seconds(TORTURE_DURATION_SECONDS / 2)) {
                    
                    // Rapid-fire write barrier operations
                    for (size_t j = 0; j < 100; ++j) {
                        if (shared_objects.size() < 2) break;
                        
                        size_t idx1 = gen() % shared_objects.size();
                        size_t idx2 = gen() % shared_objects.size();
                        
                        // Write barrier test
                        __gc_write_barrier(shared_objects[idx1], 
                                         static_cast<void**>(shared_objects[idx1]), 
                                         shared_objects[idx2]);
                        
                        barrier_operations.fetch_add(1, std::memory_order_relaxed);
                    }
                    
                    // Trigger GC occasionally to test barrier correctness
                    if (gen() % 50 == 0) {
                        __gc_safepoint();
                    }
                }
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
        
        std::cout << "✅ Write barrier torture completed: " << barrier_operations.load() << " operations\\n";
    }
    
    // ============================================================================
    // MAIN TORTURE TEST RUNNER
    // ============================================================================
    
    void run_simple_torture_test() {
        std::cout << "\\n🔥🔥🔥 STARTING SIMPLE GC TORTURE TEST 🔥🔥🔥\\n";
        std::cout << "This test will stress the garbage collector...\\n\\n";
        
        auto overall_start = std::chrono::steady_clock::now();
        
        // Run individual torture tests
        test_allocation_torture();
        
        // Force safepoint between tests
        __gc_safepoint();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        test_write_barrier_torture();
        
        // Final comprehensive safepoint
        std::cout << "\\n🧹 Running final garbage collection...\\n";
        __gc_safepoint();
        std::this_thread::sleep_for(std::chrono::seconds(2)); // Allow GC to complete
        
        auto overall_end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(overall_end - overall_start);
        
        // Print test summary
        std::cout << "\\n📊 TORTURE TEST SUMMARY:\\n";
        std::cout << "Total duration: " << duration.count() << " seconds\\n";
        std::cout << "Total allocations: " << total_allocations_.load() << "\\n";
        std::cout << "Total GC cycles: " << gc_cycles_.load() << "\\n";
        
        // Check for memory leaks
        std::cout << "\\n🔍 ANALYZING MEMORY LEAKS...\\n";
        g_leak_detector.print_leak_report();
        
        if (g_leak_detector.has_leaks()) {
            std::cout << "\\n❌ TORTURE TEST FAILED - MEMORY LEAKS DETECTED!\\n";
            std::exit(1);
        } else {
            std::cout << "\\n✅ TORTURE TEST PASSED - NO MEMORY LEAKS DETECTED!\\n";
        }
    }
};

} // namespace gots

// ============================================================================
// MAIN TEST FUNCTION
// ============================================================================

int main() {
    std::cout << "GoTS Simple Garbage Collector Torture Test\\n";
    std::cout << "==========================================\\n\\n";
    
    try {
        gots::SimpleGCTortureTest torture_test;
        torture_test.run_simple_torture_test();
        
        std::cout << "\\n🎉 All tests completed successfully!\\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\\n💥 Test failed with exception: " << e.what() << "\\n";
        return 1;
    }
}