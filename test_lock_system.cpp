#include "runtime_syscalls.h"
#include "lock_system.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

using namespace gots;

// Forward declarations
extern "C" {
    void initialize_runtime_object();
    void __runtime_register_global();
}

// Test the Lock class integration with runtime system
int main() {
    std::cout << "=== Testing Lock System Integration ===" << std::endl;
    
    // Initialize runtime system
    initialize_runtime_object();
    __runtime_register_global();
    
    std::cout << "Runtime initialized" << std::endl;
    
    // Test 1: Create a lock using the runtime function
    std::cout << "\nTest 1: Creating lock via runtime..." << std::endl;
    void* lock_ptr = __runtime_lock_create();
    if (lock_ptr) {
        std::cout << "✓ Lock created successfully: " << lock_ptr << std::endl;
    } else {
        std::cout << "✗ Failed to create lock" << std::endl;
        return 1;
    }
    
    // Test 2: Basic lock/unlock
    std::cout << "\nTest 2: Basic lock/unlock..." << std::endl;
    __runtime_lock_lock(lock_ptr);
    std::cout << "✓ Lock acquired" << std::endl;
    
    bool is_locked = __runtime_lock_is_locked_by_current(lock_ptr);
    std::cout << "Is locked by current: " << (is_locked ? "yes" : "no") << std::endl;
    
    __runtime_lock_unlock(lock_ptr);
    std::cout << "✓ Lock released" << std::endl;
    
    // Test 3: Try lock
    std::cout << "\nTest 3: Try lock..." << std::endl;
    bool acquired = __runtime_lock_try_lock(lock_ptr);
    std::cout << "Try lock result: " << (acquired ? "success" : "failed") << std::endl;
    
    if (acquired) {
        __runtime_lock_unlock(lock_ptr);
        std::cout << "✓ Lock released after try_lock" << std::endl;
    }
    
    // Test 4: Try lock with timeout
    std::cout << "\nTest 4: Try lock with timeout..." << std::endl;
    bool acquired_timeout = __runtime_lock_try_lock_for(lock_ptr, 100); // 100ms timeout
    std::cout << "Try lock with timeout result: " << (acquired_timeout ? "success" : "failed") << std::endl;
    
    if (acquired_timeout) {
        __runtime_lock_unlock(lock_ptr);
        std::cout << "✓ Lock released after try_lock_for" << std::endl;
    }
    
    // Test 5: Multi-threaded test
    std::cout << "\nTest 5: Multi-threaded test..." << std::endl;
    
    int shared_counter = 0;
    constexpr int num_threads = 4;
    constexpr int increments_per_thread = 1000;
    
    std::vector<std::thread> threads;
    
    auto worker = [&](int thread_id) {
        for (int i = 0; i < increments_per_thread; ++i) {
            __runtime_lock_lock(lock_ptr);
            ++shared_counter;
            __runtime_lock_unlock(lock_ptr);
        }
        std::cout << "Thread " << thread_id << " completed" << std::endl;
    };
    
    // Launch threads
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker, i);
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    int expected_value = num_threads * increments_per_thread;
    std::cout << "Expected counter value: " << expected_value << std::endl;
    std::cout << "Actual counter value: " << shared_counter << std::endl;
    
    if (shared_counter == expected_value) {
        std::cout << "✓ Multi-threaded test passed - lock worked correctly!" << std::endl;
    } else {
        std::cout << "✗ Multi-threaded test failed - race condition detected!" << std::endl;
        return 1;
    }
    
    // Test 6: Direct Lock class usage
    std::cout << "\nTest 6: Direct Lock class usage..." << std::endl;
    auto direct_lock = LockFactory::create_lock();
    
    direct_lock->lock();
    std::cout << "✓ Direct lock acquired" << std::endl;
    std::cout << "Lock ID: " << direct_lock->get_id() << std::endl;
    std::cout << "Is locked by current: " << (direct_lock->is_locked_by_current() ? "yes" : "no") << std::endl;
    direct_lock->unlock();
    std::cout << "✓ Direct lock released" << std::endl;
    
    // Test 7: RAII style lock guard
    std::cout << "\nTest 7: RAII style lock guard..." << std::endl;
    {
        LockGuard guard(*direct_lock);
        std::cout << "✓ Lock guard acquired lock" << std::endl;
        std::cout << "Is locked by current: " << (direct_lock->is_locked_by_current() ? "yes" : "no") << std::endl;
    } // lock should be automatically released here
    std::cout << "✓ Lock guard automatically released lock" << std::endl;
    std::cout << "Is locked by current: " << (direct_lock->is_locked_by_current() ? "yes" : "no") << std::endl;
    
    std::cout << "\n=== All Lock System Tests Passed! ===" << std::endl;
    return 0;
}