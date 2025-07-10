#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <memory>
#include <algorithm>
#include <unordered_map>
#include <fstream>
#include <cassert>
#include <mutex>

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
// INSTRUMENTED ALLOCATION WRAPPERS (using regular malloc/free for testing)
// ============================================================================

void* tracked_alloc(size_t size, const std::string& location) {
    void* ptr = malloc(size);
    if (ptr) {
        g_leak_detector.record_allocation(ptr, size, location);
    }
    return ptr;
}

void tracked_free(void* ptr) {
    if (ptr) {
        g_leak_detector.record_deallocation(ptr);
        free(ptr);
    }
}

#define TRACKED_ALLOC(size) \
    tracked_alloc(size, __FILE__ ":" + std::to_string(__LINE__))

// ============================================================================
// SIMPLE ALLOCATION TORTURE TEST
// ============================================================================

class SimpleAllocationTest {
private:
    std::atomic<uint64_t> total_allocations_{0};
    
    // Test configuration
    static constexpr size_t NUM_THREADS = 8;
    static constexpr size_t DURATION_SECONDS = 5;
    static constexpr size_t MAX_OBJECT_SIZE = 1024;
    static constexpr size_t MIN_OBJECT_SIZE = 8;
    
public:
    void run_allocation_test() {
        std::cout << "🔥 Starting allocation test...\\n";
        
        std::vector<std::thread> threads;
        std::atomic<uint64_t> thread_allocations{0};
        
        for (size_t i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back([this, i, &thread_allocations]() {
                std::random_device rd;
                std::mt19937 gen(rd() + i);
                std::uniform_int_distribution<size_t> size_dist(MIN_OBJECT_SIZE, MAX_OBJECT_SIZE);
                
                auto start_time = std::chrono::steady_clock::now();
                std::vector<void*> allocated_objects;
                
                while (std::chrono::steady_clock::now() - start_time < 
                       std::chrono::seconds(DURATION_SECONDS)) {
                    
                    // Allocate random-sized objects
                    size_t size = size_dist(gen);
                    
                    void* ptr = TRACKED_ALLOC(size);
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
        
        total_allocations_.store(thread_allocations.load());
        std::cout << "✅ Allocation test completed: " << thread_allocations.load() << " allocations\\n";
    }
    
    void run_simple_test() {
        std::cout << "\\n🔥🔥 STARTING SIMPLE ALLOCATION TEST 🔥🔥\\n";
        std::cout << "This test will verify basic allocation tracking...\\n\\n";
        
        auto overall_start = std::chrono::steady_clock::now();
        
        // Run allocation test
        run_allocation_test();
        
        auto overall_end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(overall_end - overall_start);
        
        // Print test summary
        std::cout << "\\n📊 TEST SUMMARY:\\n";
        std::cout << "Total duration: " << duration.count() << " seconds\\n";
        std::cout << "Total allocations: " << total_allocations_.load() << "\\n";
        
        // Check for memory leaks
        std::cout << "\\n🔍 ANALYZING MEMORY LEAKS...\\n";
        g_leak_detector.print_leak_report();
        
        if (g_leak_detector.has_leaks()) {
            std::cout << "\\n❌ TEST FAILED - MEMORY LEAKS DETECTED!\\n";
            return;
        } else {
            std::cout << "\\n✅ TEST PASSED - NO MEMORY LEAKS DETECTED!\\n";
        }
    }
};

// ============================================================================
// MAIN TEST FUNCTION
// ============================================================================

int main() {
    std::cout << "GoTS Simple Allocation Test (Memory Leak Detection)\\n";
    std::cout << "===================================================\\n\\n";
    
    try {
        SimpleAllocationTest test;
        test.run_simple_test();
        
        std::cout << "\\n🎉 Test completed successfully!\\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\\n💥 Test failed with exception: " << e.what() << "\\n";
        return 1;
    }
}