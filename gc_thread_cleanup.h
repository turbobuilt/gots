#pragma once

#include <thread>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace gots {

// Forward declarations
class TLAB;
class GarbageCollector;


// ============================================================================
// THREAD LOCAL CLEANUP - Manages thread-local GC resources
// ============================================================================

class ThreadLocalCleanup {
private:
    struct ThreadData {
        TLAB* tlab = nullptr;
        bool escape_data_initialized = false;
        std::thread::id thread_id;
        void* root_cleanup = nullptr; // Will be cast to GarbageCollector::ThreadRootCleanup*
    };
    
    static std::unordered_map<std::thread::id, ThreadData> thread_data_;
    static std::mutex thread_data_mutex_;
    
public:
    // Register thread for cleanup
    static void register_thread();
    
    // Cleanup thread resources
    static void cleanup_thread();
    
    // Get thread data
    static ThreadData* get_thread_data();
    
    // Cleanup all threads (for shutdown)
    static void cleanup_all_threads();
    
    // Thread exit handler
    static void thread_exit_handler();
};

// ============================================================================
// TLAB CLEANUP - Proper cleanup of Thread Local Allocation Buffers
// ============================================================================

class TLABCleanup {
public:
    // Cleanup TLAB for current thread
    static void cleanup_current_tlab();
    
    // Process remaining allocations in TLAB
    static void process_tlab_allocations(TLAB* tlab);
    
    // Return unused TLAB space to heap
    static void return_tlab_space(TLAB* tlab);
};

// ============================================================================
// ESCAPE ANALYSIS CLEANUP - Clean up thread-local escape data
// ============================================================================

class EscapeDataCleanup {
public:
    // Clear escape analysis data for current thread
    static void clear_escape_data();
    
    // Save escape analysis results (for debugging/profiling)
    static void save_escape_results();
};

// ============================================================================
// THREAD REGISTRATION - Automatic registration/cleanup
// ============================================================================

class ThreadRegistration {
private:
    static thread_local bool is_registered_;
    static thread_local std::unique_ptr<ThreadRegistration> instance_;
    
public:
    ThreadRegistration() {
        if (!is_registered_) {
            ThreadLocalCleanup::register_thread();
            is_registered_ = true;
        }
    }
    
    ~ThreadRegistration() {
        if (is_registered_) {
            ThreadLocalCleanup::cleanup_thread();
            is_registered_ = false;
        }
    }
    
    static void ensure_registered() {
        if (!is_registered_) {
            instance_ = std::make_unique<ThreadRegistration>();
        }
    }
};

// ============================================================================
// THREAD EXIT HOOKS - Platform-specific thread exit detection
// ============================================================================

#ifdef __linux__
#include <pthread.h>

class LinuxThreadExitHook {
private:
    static pthread_key_t cleanup_key_;
    static bool key_created_;
    
    static void cleanup_destructor(void* arg);
    
public:
    static void initialize();
    static void register_thread();
    static void shutdown();
};
#endif

#ifdef _WIN32
#include <windows.h>

class WindowsThreadExitHook {
private:
    static DWORD tls_index_;
    
public:
    static void initialize();
    static void register_thread();
    static void shutdown();
    static void thread_detach_callback();
};
#endif

// ============================================================================
// AUTOMATIC THREAD MANAGEMENT - RAII for thread GC resources
// ============================================================================

class ScopedThreadRegistration {
public:
    ScopedThreadRegistration() {
        ThreadRegistration::ensure_registered();
    }
    
    ~ScopedThreadRegistration() {
        // Cleanup happens in ThreadRegistration destructor
    }
    
    // Disable copy/move
    ScopedThreadRegistration(const ScopedThreadRegistration&) = delete;
    ScopedThreadRegistration& operator=(const ScopedThreadRegistration&) = delete;
    ScopedThreadRegistration(ScopedThreadRegistration&&) = delete;
    ScopedThreadRegistration& operator=(ScopedThreadRegistration&&) = delete;
};

// ============================================================================
// GLOBAL INITIALIZATION - Initialize thread cleanup system
// ============================================================================

void initialize_thread_cleanup_system();
void shutdown_thread_cleanup_system();

// Macro for automatic thread registration
#define GOTS_REGISTER_THREAD() gots::ScopedThreadRegistration __thread_reg

} // namespace gots