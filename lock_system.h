#pragma once

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <unordered_set>
#include <chrono>

namespace gots {

// Minimal Goroutine interface for Lock system integration
class Goroutine {
public:
    virtual ~Goroutine() = default;
    virtual int64_t get_id() const = 0;
    virtual void yield() {}
    virtual void notify_scheduler() {}
};

// Simple stub for getting current goroutine (will be properly implemented later)
std::shared_ptr<Goroutine> get_current_goroutine();

class Lock {
public:
    Lock();
    ~Lock();
    
    // Primary interface - matches GoTS syntax: lock.lock(), lock.unlock()
    void lock();
    void unlock();
    
    // Try to acquire lock without blocking
    bool try_lock();
    
    // Try to acquire lock with timeout
    bool try_lock_for(const std::chrono::milliseconds& timeout);
    
    // Check if current goroutine owns this lock
    bool is_locked_by_current() const;
    
    // Get lock ID for debugging
    uint64_t get_id() const { return lock_id_; }

private:
    mutable std::mutex internal_mutex_;
    std::condition_variable cv_;
    
    // Track which goroutine owns the lock
    std::atomic<int64_t> owner_goroutine_id_;
    std::atomic<uint32_t> lock_count_; // For recursive locking
    
    // Unique lock identifier
    uint64_t lock_id_;
    static std::atomic<uint64_t> next_lock_id_;
    
    // Performance optimization: fast path for uncontended locks
    std::atomic<bool> is_locked_;
    
    // Integration with goroutine event loop
    void yield_to_scheduler_if_needed();
    void notify_goroutine_scheduler();
    
    // Deadlock detection (optional, for debug builds)
    #ifdef GOTS_DEBUG
    static thread_local std::unordered_set<uint64_t> held_locks_;
    void check_for_deadlock();
    #endif
};

// Lock factory for integration with runtime system
class LockFactory {
public:
    static std::shared_ptr<Lock> create_lock();
    static void register_with_runtime();
};

// Helper for RAII-style locking
class LockGuard {
public:
    explicit LockGuard(Lock& lock) : lock_(lock) {
        lock_.lock();
    }
    
    ~LockGuard() {
        lock_.unlock();
    }
    
    // Non-copyable, non-movable
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    LockGuard(LockGuard&&) = delete;
    LockGuard& operator=(LockGuard&&) = delete;

private:
    Lock& lock_;
};

} // namespace gots