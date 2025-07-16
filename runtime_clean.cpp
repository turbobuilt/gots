#include "runtime.h"
#include "compiler.h"
#include "lexical_scope.h"
#include "regex.h"
#include "goroutine_system.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <pthread.h>
#include <signal.h>
#include <setjmp.h>
#include <cmath>
#include <regex>

// Forward declarations for new goroutine system
extern "C" {
    int64_t __gots_set_timeout(void* callback, int64_t delay_ms);
    int64_t __gots_set_interval(void* callback, int64_t delay_ms);  
    bool __gots_clear_timeout(int64_t timer_id);
    bool __gots_clear_interval(int64_t timer_id);
    void __new_goroutine_system_init();
    void __new_goroutine_system_cleanup();
    void __new_goroutine_spawn(void* func_ptr);
}

// External function declarations
extern "C" void* __lookup_function_by_id(int64_t function_id);

// Global function ID to pointer map
static std::unordered_map<int64_t, void*> g_function_id_map;
static std::mutex g_function_id_mutex;
static std::atomic<int64_t> g_next_function_id{1};

// Global console output mutex for thread safety
std::mutex g_console_mutex;

namespace gots {

// Global instances
// Using pointers to control initialization/destruction order
static GoroutineScheduler* global_scheduler = nullptr;
static std::mutex scheduler_mutex;

// Simple timer system using goroutines
std::atomic<int64_t> g_timer_id_counter{1};
std::atomic<int64_t> g_active_timer_count{0};
std::atomic<int64_t> g_active_goroutine_count{0};
std::unordered_set<int64_t> g_cancelled_timers;
std::mutex g_cancelled_timers_mutex;

ThreadPool::ThreadPool(size_t num_threads) {
    // Use the full number of available hardware threads for maximum performance
    // This is essential for proper goroutine parallelism
    size_t optimal_thread_count = (num_threads > 0) ? num_threads : std::thread::hardware_concurrency();
    
    for (size_t i = 0; i < optimal_thread_count; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    condition.wait(lock, [this] { return stop.load() || !tasks.empty(); });
                    
                    if (stop.load() && tasks.empty()) {
                        return;
                    }
                    
                    if (!tasks.empty()) {
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                }
                
                if (task) {
                    try {
                        task();
                    } catch (const std::exception& e) {
                        std::cerr << "Worker task failed: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "Worker task failed with unknown exception" << std::endl;
                    }
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::shutdown() {
    stop.store(true);
    condition.notify_all();
    
    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::enqueue_simple(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        
        if (stop.load()) {
            return; // Don't enqueue if stopped
        }
        
        tasks.push(task);
    }
    
    condition.notify_one();
}

// Old GoroutineScheduler implementations removed - using new system

// Global object registry
std::unordered_map<int64_t, std::unique_ptr<ObjectInstance>> object_registry;
std::atomic<int64_t> next_object_id{1};

// Global function registry for goroutines
std::unordered_map<std::string, void*> gots_function_registry;

// Global promise registry for cleanup
static std::unordered_set<void*> g_allocated_promises;
static std::mutex g_promise_registry_mutex;

// Helper function to create and track a promise
static void* create_tracked_promise(std::shared_ptr<Promise> promise) {
    auto* promise_ptr = new std::shared_ptr<Promise>(promise);
    
    // Track allocated promise for cleanup
    {
        std::lock_guard<std::mutex> lock(g_promise_registry_mutex);
        g_allocated_promises.insert(promise_ptr);
    }
    
    return promise_ptr;
}

// Global executable memory info for thread-safe access
ExecutableMemoryInfo g_executable_memory = {nullptr, 0, {}};

extern "C" {

// Function ID registration and lookup
void __register_function_id(int64_t function_id, void* function_ptr) {
    std::lock_guard<std::mutex> lock(g_function_id_mutex);
    g_function_id_map[function_id] = function_ptr;
}

// __lookup_function_by_id is now defined in ast_codegen.cpp to avoid duplicate symbol

int64_t __allocate_function_id() {
    return g_next_function_id.fetch_add(1);
}

extern "C" void* __goroutine_spawn(const char* function_name) {
    
    // Look up function in registry
    auto it = gots_function_registry.find(std::string(function_name));
    if (it == gots_function_registry.end()) {
        std::cerr << "ERROR: Function " << function_name << " not found in registry" << std::endl;
        return nullptr;
    }
    
    void* func_ptr = it->second;
    
    // Use NEW goroutine system
    auto task = [func_ptr, function_name]() {
        
        typedef int64_t (*FuncType)();
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        return func();
    };
    
    // Spawn using NEW goroutine system
    gots::GoroutineScheduler::instance().spawn(task);
    
    // Return dummy for compatibility
    return reinterpret_cast<void*>(1);
}

void __register_function(const char* name, void* func_ptr) {
    gots_function_registry[std::string(name)] = func_ptr;
}

// Initialize the new goroutine system
void __runtime_init() {
    // New goroutine system initializes automatically via singleton
}

// Main cleanup - wait for all goroutines
void __runtime_cleanup() {
    // New goroutine system cleanup happens automatically in destructor
}

extern "C" void* __lookup_function(const char* name) {
    std::string func_name(name);
    auto it = gots_function_registry.find(func_name);
    if (it != gots_function_registry.end()) {
        return it->second;
    }
    std::cerr << "ERROR: Function " << name << " not found in registry!" << std::endl;
    return nullptr;
}

// Thread-local storage for goroutine context
thread_local bool g_is_goroutine_context = false;

extern "C" void __set_goroutine_context(int64_t is_goroutine) {
    bool was_goroutine = g_is_goroutine_context;
    g_is_goroutine_context = (is_goroutine != 0);
    
    if (g_is_goroutine_context && !was_goroutine) {
        // Setting up goroutine context
        // Initialize thread-local timer manager for this goroutine
        if (!g_thread_timer_manager) {
            g_thread_timer_manager = std::make_unique<GoroutineTimerManager>();
        }
        
        // Increment active goroutine count
        g_active_goroutine_count.fetch_add(1);
    } else if (!g_is_goroutine_context && was_goroutine) {
        // Cleaning up goroutine context
        // Process any pending timers before cleanup
        if (g_thread_timer_manager) {
            g_thread_timer_manager->process_timers();
        }
        
        // Cleanup thread-local timer manager
        g_thread_timer_manager.reset();
        
        // Decrement active goroutine count
        g_active_goroutine_count.fetch_sub(1);
    }
}


// Simple non-lambda worker function for testing
void simple_worker_function(std::shared_ptr<Promise>* promise_ptr, void* func_ptr, int64_t arg1) {
    auto promise = *promise_ptr;
    
    // Initialize scope chain for this thread
    ScopeChain::initialize_thread_local_chain();
    
    try {
        typedef int64_t (*FuncType1)(int64_t);
        FuncType1 func = reinterpret_cast<FuncType1>(func_ptr);
        
        // Ensure proper floating point state and CPU features
std::atomic<int64_t> g_simple_timer_id_counter{1};
std::unordered_map<int64_t, std::thread> g_active_timers;
std::mutex g_timers_mutex;

extern "C" {

// Simple timer functions
int64_t __gots_set_timeout(void* callback, int64_t delay_ms) {
    
    int64_t timer_id = g_simple_timer_id_counter.fetch_add(1);
    
    std::lock_guard<std::mutex> lock(g_timers_mutex);
    g_active_timers[timer_id] = std::thread([callback, delay_ms, timer_id]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        
        // Check if timer was cancelled
        {
            std::lock_guard<std::mutex> lock(g_timers_mutex);
            if (g_active_timers.find(timer_id) == g_active_timers.end()) {
                return; // Timer was cancelled
            }
        }
        
        typedef void (*TimerCallback)();
        TimerCallback cb = reinterpret_cast<TimerCallback>(callback);
        cb();
        
        // Remove timer from active list
        {
            std::lock_guard<std::mutex> lock(g_timers_mutex);
            auto it = g_active_timers.find(timer_id);
            if (it != g_active_timers.end()) {
                it->second.detach(); // Detach the thread so it can cleanup
                g_active_timers.erase(it);
            }
        }
    });
    
    return timer_id;
}

int64_t __gots_set_interval(void* callback, int64_t delay_ms) {
    // For now, just implement as a single timeout
    return __gots_set_timeout(callback, delay_ms);
}

bool __gots_clear_timeout(int64_t timer_id) {
    std::lock_guard<std::mutex> lock(g_timers_mutex);
    auto it = g_active_timers.find(timer_id);
    if (it != g_active_timers.end()) {
        // Just remove from map; the thread will check and exit
        g_active_timers.erase(it);
        return true;
    }
    return false;
}

bool __gots_clear_interval(int64_t timer_id) {
    return __gots_clear_timeout(timer_id);
}

// Simple goroutine spawn using thread pool
void __new_goroutine_spawn(void* func_ptr) {
    
    std::thread([func_ptr]() {
        typedef void (*FuncType)();
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        func();
    }).detach();
}

void __new_goroutine_system_init() {
}

void __new_goroutine_system_cleanup() {
    
    // Wait for all active timers to complete
    while (true) {
        {
            std::lock_guard<std::mutex> lock(g_timers_mutex);
            if (g_active_timers.empty()) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
}

} // extern "C"

} // namespace gots
