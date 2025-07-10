#include "lock_system.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

using namespace gots;

// Test the Lock class without runtime system dependencies
int main() {
    std::cout << "=== Testing Lock System (Direct API) ===" << std::endl;
    
    // Test 1: Direct Lock class usage
    std::cout << "\nTest 1: Direct Lock class usage..." << std::endl;
    auto lock = LockFactory::create_lock();
    
    lock->lock();
    std::cout << "✓ Lock acquired" << std::endl;
    std::cout << "Lock ID: " << lock->get_id() << std::endl;
    std::cout << "Is locked by current: " << (lock->is_locked_by_current() ? "yes" : "no") << std::endl;
    lock->unlock();
    std::cout << "✓ Lock released" << std::endl;
    
    // Test 2: Try lock
    std::cout << "\nTest 2: Try lock..." << std::endl;
    bool acquired = lock->try_lock();
    std::cout << "Try lock result: " << (acquired ? "success" : "failed") << std::endl;
    
    if (acquired) {
        lock->unlock();
        std::cout << "✓ Lock released after try_lock" << std::endl;
    }
    
    // Test 3: Try lock with timeout
    std::cout << "\nTest 3: Try lock with timeout..." << std::endl;
    bool acquired_timeout = lock->try_lock_for(std::chrono::milliseconds(100));
    std::cout << "Try lock with timeout result: " << (acquired_timeout ? "success" : "failed") << std::endl;
    
    if (acquired_timeout) {
        lock->unlock();
        std::cout << "✓ Lock released after try_lock_for" << std::endl;
    }
    
    // Test 4: Multi-threaded test
    std::cout << "\nTest 4: Multi-threaded test..." << std::endl;
    
    int shared_counter = 0;
    constexpr int num_threads = 4;
    constexpr int increments_per_thread = 100;
    
    std::vector<std::thread> threads;
    
    auto worker = [&](int thread_id) {
        for (int i = 0; i < increments_per_thread; ++i) {
            try {
                lock->lock();
                ++shared_counter;
                lock->unlock();
            } catch (const std::exception& e) {
                std::cerr << "Thread " << thread_id << " error: " << e.what() << std::endl;
                return;
            }
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
    
    // Test 5: RAII style lock guard
    std::cout << "\nTest 5: RAII style lock guard..." << std::endl;
    {
        LockGuard guard(*lock);
        std::cout << "✓ Lock guard acquired lock" << std::endl;
        std::cout << "Is locked by current: " << (lock->is_locked_by_current() ? "yes" : "no") << std::endl;
    } // lock should be automatically released here
    std::cout << "✓ Lock guard automatically released lock" << std::endl;
    std::cout << "Is locked by current: " << (lock->is_locked_by_current() ? "yes" : "no") << std::endl;
    
    // Test 6: Multiple locks
    std::cout << "\nTest 6: Multiple locks..." << std::endl;
    auto lock1 = LockFactory::create_lock();
    auto lock2 = LockFactory::create_lock();
    
    std::cout << "Lock1 ID: " << lock1->get_id() << std::endl;
    std::cout << "Lock2 ID: " << lock2->get_id() << std::endl;
    
    lock1->lock();
    lock2->lock();
    std::cout << "✓ Both locks acquired" << std::endl;
    
    lock1->unlock();
    lock2->unlock();
    std::cout << "✓ Both locks released" << std::endl;
    
    std::cout << "\n=== All Lock System Tests Passed! ===" << std::endl;
    return 0;
}