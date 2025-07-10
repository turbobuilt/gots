#include "lock_system.h"
#include <cassert>
#include <thread>
#include <stdexcept>

namespace gots {

// Simple stub implementation for goroutine integration
// In a full implementation, this would integrate with the goroutine scheduler
std::shared_ptr<Goroutine> get_current_goroutine() {
    // For now, return nullptr to indicate main thread or fallback behavior
    return nullptr;
}

std::atomic<uint64_t> Lock::next_lock_id_{1};

#ifdef GOTS_DEBUG
thread_local std::unordered_set<uint64_t> Lock::held_locks_;
#endif

Lock::Lock() 
    : owner_goroutine_id_(-1)
    , lock_count_(0)
    , lock_id_(next_lock_id_.fetch_add(1))
    , is_locked_(false) {
}

Lock::~Lock() {
    // Ensure lock is not held during destruction
    assert(!is_locked_.load());
}

void Lock::lock() {
    auto current_goroutine = get_current_goroutine();
    int64_t current_id;
    if (current_goroutine) {
        current_id = current_goroutine->get_id();
    } else {
        // Fallback to thread ID when no goroutine context
        current_id = static_cast<int64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    }
    
    #ifdef GOTS_DEBUG
    check_for_deadlock();
    #endif
    
    // Fast path: try atomic compare-exchange for uncontended case
    bool expected = false;
    if (is_locked_.compare_exchange_weak(expected, true, std::memory_order_acquire)) {
        // Got the lock immediately
        owner_goroutine_id_.store(current_id, std::memory_order_relaxed);
        lock_count_.store(1, std::memory_order_relaxed);
        
        #ifdef GOTS_DEBUG
        held_locks_.insert(lock_id_);
        #endif
        return;
    }
    
    // Check for recursive locking by same goroutine
    if (owner_goroutine_id_.load(std::memory_order_relaxed) == current_id) {
        // Recursive lock - just increment count
        lock_count_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    
    // Slow path: need to wait
    std::unique_lock<std::mutex> guard(internal_mutex_);
    
    while (is_locked_.load(std::memory_order_acquire)) {
        // Yield to goroutine scheduler while waiting
        yield_to_scheduler_if_needed();
        
        // Wait for lock to become available
        cv_.wait(guard, [this] { 
            return !is_locked_.load(std::memory_order_acquire); 
        });
    }
    
    // Acquire the lock
    is_locked_.store(true, std::memory_order_relaxed);
    owner_goroutine_id_.store(current_id, std::memory_order_relaxed);
    lock_count_.store(1, std::memory_order_relaxed);
    
    #ifdef GOTS_DEBUG
    held_locks_.insert(lock_id_);
    #endif
}

void Lock::unlock() {
    auto current_goroutine = get_current_goroutine();
    int64_t current_id;
    if (current_goroutine) {
        current_id = current_goroutine->get_id();
    } else {
        // Fallback to thread ID when no goroutine context
        current_id = static_cast<int64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    }
    
    // Verify current goroutine owns the lock
    if (owner_goroutine_id_.load(std::memory_order_relaxed) != current_id) {
        throw std::runtime_error("Lock::unlock() called by non-owner goroutine");
    }
    
    uint32_t count = lock_count_.load(std::memory_order_relaxed);
    if (count == 0) {
        throw std::runtime_error("Lock::unlock() called on unlocked lock");
    }
    
    if (count > 1) {
        // Recursive unlock - just decrement count
        lock_count_.fetch_sub(1, std::memory_order_relaxed);
        return;
    }
    
    // Final unlock
    #ifdef GOTS_DEBUG
    held_locks_.erase(lock_id_);
    #endif
    
    {
        std::lock_guard<std::mutex> guard(internal_mutex_);
        owner_goroutine_id_.store(-1, std::memory_order_relaxed);
        lock_count_.store(0, std::memory_order_relaxed);
        is_locked_.store(false, std::memory_order_release);
    }
    
    // Notify waiting goroutines
    cv_.notify_one();
    notify_goroutine_scheduler();
}

bool Lock::try_lock() {
    auto current_goroutine = get_current_goroutine();
    int64_t current_id;
    if (current_goroutine) {
        current_id = current_goroutine->get_id();
    } else {
        // Fallback to thread ID when no goroutine context
        current_id = static_cast<int64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    }
    
    // Check for recursive locking
    if (owner_goroutine_id_.load(std::memory_order_relaxed) == current_id) {
        lock_count_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    
    // Try to acquire lock atomically
    bool expected = false;
    if (is_locked_.compare_exchange_weak(expected, true, std::memory_order_acquire)) {
        owner_goroutine_id_.store(current_id, std::memory_order_relaxed);
        lock_count_.store(1, std::memory_order_relaxed);
        
        #ifdef GOTS_DEBUG
        held_locks_.insert(lock_id_);
        #endif
        return true;
    }
    
    return false;
}

bool Lock::try_lock_for(const std::chrono::milliseconds& timeout) {
    auto current_goroutine = get_current_goroutine();
    int64_t current_id;
    if (current_goroutine) {
        current_id = current_goroutine->get_id();
    } else {
        // Fallback to thread ID when no goroutine context
        current_id = static_cast<int64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    }
    
    // Check for recursive locking
    if (owner_goroutine_id_.load(std::memory_order_relaxed) == current_id) {
        lock_count_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    
    // Try fast path first
    bool expected = false;
    if (is_locked_.compare_exchange_weak(expected, true, std::memory_order_acquire)) {
        owner_goroutine_id_.store(current_id, std::memory_order_relaxed);
        lock_count_.store(1, std::memory_order_relaxed);
        
        #ifdef GOTS_DEBUG
        held_locks_.insert(lock_id_);
        #endif
        return true;
    }
    
    // Wait with timeout
    std::unique_lock<std::mutex> guard(internal_mutex_);
    
    if (cv_.wait_for(guard, timeout, [this] { 
        return !is_locked_.load(std::memory_order_acquire); 
    })) {
        // Got the lock within timeout
        is_locked_.store(true, std::memory_order_relaxed);
        owner_goroutine_id_.store(current_id, std::memory_order_relaxed);
        lock_count_.store(1, std::memory_order_relaxed);
        
        #ifdef GOTS_DEBUG
        held_locks_.insert(lock_id_);
        #endif
        return true;
    }
    
    return false; // Timeout
}

bool Lock::is_locked_by_current() const {
    auto current_goroutine = get_current_goroutine();
    int64_t current_id;
    if (current_goroutine) {
        current_id = current_goroutine->get_id();
    } else {
        // Fallback to thread ID when no goroutine context
        current_id = static_cast<int64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
    }
    
    return owner_goroutine_id_.load(std::memory_order_relaxed) == current_id &&
           lock_count_.load(std::memory_order_relaxed) > 0;
}

void Lock::yield_to_scheduler_if_needed() {
    // Integrate with goroutine scheduler - yield current goroutine
    // if we're blocking and allow other goroutines to run
    auto current_goroutine = get_current_goroutine();
    if (current_goroutine) {
        // Signal that this goroutine is waiting
        current_goroutine->yield();
    } else {
        // Fallback for non-goroutine threads
        std::this_thread::yield();
    }
}

void Lock::notify_goroutine_scheduler() {
    // Notify scheduler that a lock has been released
    // This can help the scheduler make better decisions about
    // which goroutines to run next
    auto current_goroutine = get_current_goroutine();
    if (current_goroutine) {
        current_goroutine->notify_scheduler();
    }
}

#ifdef GOTS_DEBUG
void Lock::check_for_deadlock() {
    // Simple deadlock detection: check if we already hold this lock
    if (held_locks_.find(lock_id_) != held_locks_.end()) {
        throw std::runtime_error("Potential deadlock detected: recursive lock acquisition");
    }
    
    // More sophisticated deadlock detection could be added here
    // using lock ordering, wait-for graphs, etc.
}
#endif

// LockFactory implementation
std::shared_ptr<Lock> LockFactory::create_lock() {
    return std::make_shared<Lock>();
}

void LockFactory::register_with_runtime() {
    // This will be called during runtime initialization
    // to register the Lock constructor with the global runtime object
    // Implementation depends on the runtime system structure
}

} // namespace gots