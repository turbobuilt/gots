#include "gc_thread_cleanup.h"
#include "gc_memory_manager.h"
#include <iostream>
#include <algorithm>
#include <cstring>


namespace gots {

// ============================================================================
// STATIC MEMBER DEFINITIONS
// ============================================================================

std::unordered_map<std::thread::id, ThreadLocalCleanup::ThreadData> ThreadLocalCleanup::thread_data_;
std::mutex ThreadLocalCleanup::thread_data_mutex_;

thread_local bool ThreadRegistration::is_registered_ = false;
thread_local std::unique_ptr<ThreadRegistration> ThreadRegistration::instance_;

#ifdef __linux__
pthread_key_t LinuxThreadExitHook::cleanup_key_;
bool LinuxThreadExitHook::key_created_ = false;
#endif

#ifdef _WIN32
DWORD WindowsThreadExitHook::tls_index_ = TLS_OUT_OF_INDEXES;
#endif

// Thread-local escape analysis data (declared in gc_memory_manager.cpp)
extern thread_local struct {
    std::vector<std::pair<size_t, size_t>> scope_stack;
    std::unordered_map<size_t, EscapeAnalyzer::AnalysisResult> allocation_sites;
    std::unordered_map<size_t, std::vector<size_t>> var_to_sites;
    std::unordered_map<size_t, size_t> var_scope;
    size_t current_scope;
} g_escape_data;

// ============================================================================
// THREAD LOCAL CLEANUP IMPLEMENTATION
// ============================================================================

void ThreadLocalCleanup::register_thread() {
    std::lock_guard<std::mutex> lock(thread_data_mutex_);
    
    auto thread_id = std::this_thread::get_id();
    ThreadData& data = thread_data_[thread_id];
    data.thread_id = thread_id;
    data.escape_data_initialized = true;
    
    
#ifdef __linux__
    LinuxThreadExitHook::register_thread();
#endif
#ifdef _WIN32
    WindowsThreadExitHook::register_thread();
#endif
}

void ThreadLocalCleanup::cleanup_thread() {
    auto thread_id = std::this_thread::get_id();
    
    
    // Clean up thread root cleanup first
    void* root_cleanup_ptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(thread_data_mutex_);
        auto it = thread_data_.find(thread_id);
        if (it != thread_data_.end()) {
            root_cleanup_ptr = it->second.root_cleanup;
        }
    }
    
    if (root_cleanup_ptr) {
        // Note: The ThreadRootCleanup destructor will handle cleanup automatically
        // We don't delete it here as it's managed by thread_local storage
    }
    
    // Clean up TLAB
    TLABCleanup::cleanup_current_tlab();
    
    // Clean up escape analysis data
    EscapeDataCleanup::clear_escape_data();
    
    // Remove from registry
    std::lock_guard<std::mutex> lock(thread_data_mutex_);
    thread_data_.erase(thread_id);
}

ThreadLocalCleanup::ThreadData* ThreadLocalCleanup::get_thread_data() {
    std::lock_guard<std::mutex> lock(thread_data_mutex_);
    auto thread_id = std::this_thread::get_id();
    auto it = thread_data_.find(thread_id);
    return it != thread_data_.end() ? &it->second : nullptr;
}

void ThreadLocalCleanup::cleanup_all_threads() {
    std::lock_guard<std::mutex> lock(thread_data_mutex_);
    
    
    for (auto& [thread_id, data] : thread_data_) {
        if (data.tlab) {
            TLABCleanup::process_tlab_allocations(data.tlab);
        }
    }
    
    thread_data_.clear();
}

void ThreadLocalCleanup::thread_exit_handler() {
    cleanup_thread();
}

// ============================================================================
// TLAB CLEANUP IMPLEMENTATION
// ============================================================================

void TLABCleanup::cleanup_current_tlab() {
    // Access thread-local TLAB
    extern thread_local TLAB* GenerationalHeap::tlab_;
    
    if (GenerationalHeap::tlab_) {
                  << GenerationalHeap::tlab_->used() << " bytes used\n";
        
        // Store TLAB pointer for cleanup
        TLAB* tlab_to_cleanup = GenerationalHeap::tlab_;
        
        process_tlab_allocations(tlab_to_cleanup);
        return_tlab_space(tlab_to_cleanup);
        
        // Remove TLAB from the global list in GarbageCollector
        auto& gc = GarbageCollector::instance();
        {
            std::lock_guard<std::mutex> lock(gc.tlabs_mutex_);
            
            // Find and remove this TLAB from all_tlabs_
            auto& all_tlabs = gc.all_tlabs_;
            auto it = std::find_if(all_tlabs.begin(), all_tlabs.end(),
                [tlab_to_cleanup](const std::unique_ptr<TLAB>& tlab_ptr) {
                    return tlab_ptr.get() == tlab_to_cleanup;
                });
            
            if (it != all_tlabs.end()) {
                all_tlabs.erase(it);
            } else {
                std::cout << "WARNING: TLAB not found in global list during cleanup\n";
            }
        }
        
        // Update thread data
        auto* thread_data = ThreadLocalCleanup::get_thread_data();
        if (thread_data) {
            thread_data->tlab = nullptr;
        }
        
        // Clear thread-local pointer
        GenerationalHeap::tlab_ = nullptr;
        
    }
}

void TLABCleanup::process_tlab_allocations(TLAB* tlab) {
    if (!tlab) return;
    
    // Mark any objects in TLAB as potentially live
    // In a real implementation, we'd scan for live objects
    size_t used_bytes = tlab->used();
    if (used_bytes > 0) {
        
        // Trigger a minor GC to handle any live objects
        GarbageCollector::instance().request_gc(false);
    }
}

void TLABCleanup::return_tlab_space(TLAB* tlab) {
    if (!tlab) return;
    
    size_t remaining = tlab->remaining();
    if (remaining > 0) {
        
        // Return unused TLAB space to the heap's free list
        auto& gc = GarbageCollector::instance();
        std::lock_guard<std::mutex> lock(gc.heap_mutex_);
        
        // Calculate the unused region using public methods
        size_t used_bytes = tlab->used();
        size_t unused_size = remaining;
        
        // Only return significant chunks to avoid fragmentation
        if (unused_size >= GCConfig::OBJECT_ALIGNMENT * 4) {
            // For now, just log the space recovery
            // A full implementation would need a proper free list
        }
    }
    
    // Important: Clear TLAB contents to prevent dangling pointers
    // Note: We can't access private members directly, so we rely on reset()
    
    // Reset TLAB state
    tlab->reset(nullptr, 0);
}

// ============================================================================
// ESCAPE ANALYSIS CLEANUP IMPLEMENTATION
// ============================================================================

void EscapeDataCleanup::clear_escape_data() {
    
    // Clear thread-local escape data
    g_escape_data.scope_stack.clear();
    g_escape_data.allocation_sites.clear();
    g_escape_data.var_to_sites.clear();
    g_escape_data.var_scope.clear();
    g_escape_data.current_scope = 0;
}

void EscapeDataCleanup::save_escape_results() {
    // In debug builds, we might want to save escape analysis results
    #ifdef DEBUG_ESCAPE_ANALYSIS
    std::cout << "  Allocation sites: " << g_escape_data.allocation_sites.size() << "\n";
    std::cout << "  Variables tracked: " << g_escape_data.var_to_sites.size() << "\n";
    #endif
}

// ============================================================================
// PLATFORM-SPECIFIC THREAD EXIT HOOKS
// ============================================================================

#ifdef __linux__
void LinuxThreadExitHook::initialize() {
    if (!key_created_) {
        int result = pthread_key_create(&cleanup_key_, cleanup_destructor);
        if (result == 0) {
            key_created_ = true;
        } else {
            std::cerr << "ERROR: Failed to create pthread cleanup key\n";
        }
    }
}

void LinuxThreadExitHook::register_thread() {
    if (key_created_) {
        // Set a non-null value to ensure destructor is called
        pthread_setspecific(cleanup_key_, reinterpret_cast<void*>(1));
    }
}

void LinuxThreadExitHook::cleanup_destructor(void* arg) {
    ThreadLocalCleanup::thread_exit_handler();
}

void LinuxThreadExitHook::shutdown() {
    if (key_created_) {
        pthread_key_delete(cleanup_key_);
        key_created_ = false;
    }
}
#endif

#ifdef _WIN32
void WindowsThreadExitHook::initialize() {
    tls_index_ = TlsAlloc();
    if (tls_index_ == TLS_OUT_OF_INDEXES) {
        std::cerr << "ERROR: Failed to allocate TLS index\n";
    } else {
    }
}

void WindowsThreadExitHook::register_thread() {
    if (tls_index_ != TLS_OUT_OF_INDEXES) {
        TlsSetValue(tls_index_, reinterpret_cast<void*>(1));
    }
}

void WindowsThreadExitHook::thread_detach_callback() {
    ThreadLocalCleanup::thread_exit_handler();
}

void WindowsThreadExitHook::shutdown() {
    if (tls_index_ != TLS_OUT_OF_INDEXES) {
        TlsFree(tls_index_);
        tls_index_ = TLS_OUT_OF_INDEXES;
    }
}
#endif

// ============================================================================
// GLOBAL INITIALIZATION
// ============================================================================

void initialize_thread_cleanup_system() {
    
#ifdef __linux__
    LinuxThreadExitHook::initialize();
#endif
#ifdef _WIN32
    WindowsThreadExitHook::initialize();
#endif
    
    // Register the main thread
    ThreadLocalCleanup::register_thread();
}

void shutdown_thread_cleanup_system() {
    
    // Clean up all registered threads
    ThreadLocalCleanup::cleanup_all_threads();
    
#ifdef __linux__
    LinuxThreadExitHook::shutdown();
#endif
#ifdef _WIN32
    WindowsThreadExitHook::shutdown();
#endif
}

} // namespace gots