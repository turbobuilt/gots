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
#include <cstring>

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

// Timer globals moved to goroutine_system.cpp to avoid duplicates

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
    std::cout << "DEBUG: Registered function ID " << function_id << " -> " << function_ptr << std::endl;
}

// __lookup_function_by_id is now defined in ast_codegen.cpp to avoid duplicate symbol

int64_t __allocate_function_id() {
    return g_next_function_id.fetch_add(1);
}

extern "C" void* __goroutine_spawn(const char* function_name) {
    std::cout << "DEBUG: __goroutine_spawn redirecting to new system: " << function_name << std::endl;
    
    // Look up function in registry
    auto it = gots_function_registry.find(std::string(function_name));
    if (it == gots_function_registry.end()) {
        std::cerr << "ERROR: Function " << function_name << " not found in registry" << std::endl;
        return nullptr;
    }
    
    void* func_ptr = it->second;
    std::cout << "DEBUG: Found function " << function_name << " at " << func_ptr << std::endl;
    
    // Use NEW goroutine system
    auto task = [func_ptr, function_name]() {
        std::cout << "DEBUG: New goroutine executing function: " << function_name << std::endl;
        
        typedef int64_t (*FuncType)();
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        return func();
    };
    
    // Spawn using NEW goroutine system  
    GoroutineScheduler::instance().spawn(task);
    
    // Return dummy for compatibility
    return reinterpret_cast<void*>(1);
}

void __register_function(const char* name, void* func_ptr) {
    std::cerr << "DEBUG: Registering function: " << name << " at address: " << func_ptr << std::endl;
    gots_function_registry[std::string(name)] = func_ptr;
}

// Initialize the new goroutine system
void __runtime_init() {
    std::cout << "DEBUG: Initializing new goroutine system" << std::endl;
    // New goroutine system initializes automatically via singleton
}

// Main cleanup - wait for all goroutines
void __runtime_cleanup() {
    std::cout << "DEBUG: Cleaning up goroutine system" << std::endl;
    // New goroutine system cleanup happens automatically in destructor
}

// Main goroutine functions moved to goroutine_system.cpp

// Proper implementations of goroutine functions for the new system

void* __goroutine_spawn_with_scope(const char* function_name, void* captured_scope) {
    std::cout << "DEBUG: __goroutine_spawn_with_scope called with: " << function_name << std::endl;
    
    // Look up function in registry
    auto it = gots_function_registry.find(std::string(function_name));
    if (it == gots_function_registry.end()) {
        std::cerr << "ERROR: Function " << function_name << " not found in registry" << std::endl;
        return nullptr;
    }
    
    void* func_ptr = it->second;
    
    // Create a task that calls the function with captured scope
    auto task = [func_ptr, captured_scope, function_name]() {
        std::cout << "DEBUG: Goroutine executing function: " << function_name << " with scope" << std::endl;
        
        // Set up lexical scope context for this goroutine if needed
        // For now, just call the function normally
        typedef int64_t (*FuncType)();
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        return func();
    };
    
    GoroutineScheduler::instance().spawn(task);
    return reinterpret_cast<void*>(1);
}

void* __goroutine_spawn_func_ptr(void* func_ptr, void* arg) {
    std::cout << "DEBUG: __goroutine_spawn_func_ptr called" << std::endl;
    
    // Create a task that calls the function pointer with argument
    auto task = [func_ptr, arg]() {
        std::cout << "DEBUG: Goroutine executing function pointer with arg" << std::endl;
        
        typedef int64_t (*FuncType)(void*);
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        return func(arg);
    };
    
    GoroutineScheduler::instance().spawn(task);
    return reinterpret_cast<void*>(1);
}

void* __goroutine_spawn_func_id(int64_t func_id, void* arg) {
    std::cout << "DEBUG: __goroutine_spawn_func_id called with ID: " << func_id << std::endl;
    
    // Look up function by ID and spawn it
    void* func_ptr = __lookup_function_by_id(func_id);
    if (func_ptr) {
        return __goroutine_spawn_func_ptr(func_ptr, arg);
    }
    std::cerr << "ERROR: Function ID " << func_id << " not found" << std::endl;
    return nullptr;
}

// Handle both int64_t and void* versions of argument functions
void* __goroutine_spawn_with_arg1(const char* function_name, int64_t arg1) {
    std::cout << "DEBUG: __goroutine_spawn_with_arg1 (int64_t) called with: " << function_name << std::endl;
    
    // Look up function in registry
    auto it = gots_function_registry.find(std::string(function_name));
    if (it == gots_function_registry.end()) {
        std::cerr << "ERROR: Function " << function_name << " not found in registry" << std::endl;
        return nullptr;
    }
    
    void* func_ptr = it->second;
    
    // Create a task that calls the function with one argument
    auto task = [func_ptr, arg1, function_name]() {
        std::cout << "DEBUG: Goroutine executing function: " << function_name << " with arg1=" << arg1 << std::endl;
        
        typedef int64_t (*FuncType)(int64_t);
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        return func(arg1);
    };
    
    GoroutineScheduler::instance().spawn(task);
    return reinterpret_cast<void*>(1);
}

void* __goroutine_spawn_with_arg1_ptr(const char* function_name, void* arg1) {
    std::cout << "DEBUG: __goroutine_spawn_with_arg1_ptr (void*) called with: " << function_name << std::endl;
    
    // Look up function in registry
    auto it = gots_function_registry.find(std::string(function_name));
    if (it == gots_function_registry.end()) {
        std::cerr << "ERROR: Function " << function_name << " not found in registry" << std::endl;
        return nullptr;
    }
    
    void* func_ptr = it->second;
    
    // Create a task that calls the function with one argument
    auto task = [func_ptr, arg1, function_name]() {
        std::cout << "DEBUG: Goroutine executing function: " << function_name << " with arg1" << std::endl;
        
        typedef int64_t (*FuncType)(void*);
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        return func(arg1);
    };
    
    GoroutineScheduler::instance().spawn(task);
    return reinterpret_cast<void*>(1);
}

void* __goroutine_spawn_with_arg2(const char* function_name, int64_t arg1, int64_t arg2) {
    std::cout << "DEBUG: __goroutine_spawn_with_arg2 (int64_t) called with: " << function_name << std::endl;
    
    // Look up function in registry
    auto it = gots_function_registry.find(std::string(function_name));
    if (it == gots_function_registry.end()) {
        std::cerr << "ERROR: Function " << function_name << " not found in registry" << std::endl;
        return nullptr;
    }
    
    void* func_ptr = it->second;
    
    // Create a task that calls the function with two arguments
    auto task = [func_ptr, arg1, arg2, function_name]() {
        std::cout << "DEBUG: Goroutine executing function: " << function_name << " with arg1=" << arg1 << " arg2=" << arg2 << std::endl;
        
        typedef int64_t (*FuncType)(int64_t, int64_t);
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        return func(arg1, arg2);
    };
    
    GoroutineScheduler::instance().spawn(task);
    return reinterpret_cast<void*>(1);
}

void* __goroutine_spawn_with_arg2_ptr(const char* function_name, void* arg1, void* arg2) {
    std::cout << "DEBUG: __goroutine_spawn_with_arg2_ptr (void*) called with: " << function_name << std::endl;
    
    // Look up function in registry
    auto it = gots_function_registry.find(std::string(function_name));
    if (it == gots_function_registry.end()) {
        std::cerr << "ERROR: Function " << function_name << " not found in registry" << std::endl;
        return nullptr;
    }
    
    void* func_ptr = it->second;
    
    // Create a task that calls the function with two arguments
    auto task = [func_ptr, arg1, arg2, function_name]() {
        std::cout << "DEBUG: Goroutine executing function: " << function_name << " with args" << std::endl;
        
        typedef int64_t (*FuncType)(void*, void*);
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        return func(arg1, arg2);
    };
    
    GoroutineScheduler::instance().spawn(task);
    return reinterpret_cast<void*>(1);
}

// Proper array creation that creates a GoTS TypedArray
void* __array_create(int64_t size) {
    std::cout << "DEBUG: __array_create called with size: " << size << std::endl;
    
    // Create a simple TypedArray-like structure
    // For now, use a basic implementation that can be extended
    struct SimpleArray {
        int64_t size;
        void** data;
    };
    
    SimpleArray* array = new SimpleArray;
    array->size = size;
    array->data = size > 0 ? new void*[size] : nullptr;
    
    // Initialize to nullptr
    for (int64_t i = 0; i < size; i++) {
        array->data[i] = nullptr;
    }
    
    return array;
}

// Missing utility functions
void __set_executable_memory(void* memory, size_t size) {
    std::cout << "DEBUG: __set_executable_memory called with " << memory << " size=" << size << std::endl;
    // Set the global executable memory pointer
    std::lock_guard<std::mutex> lock(g_executable_memory.mutex);
    g_executable_memory.ptr = memory;
    g_executable_memory.size = size;
}

void __console_log(const char* message) {
    std::cout << message;
}

void __console_log_newline() {
    std::cout << std::endl;
}

void __console_log_space() {
    std::cout << " ";
}

// Stub function for unimplemented runtime functions
void __runtime_stub_function() {
    // Do nothing - just return
}

// Forward declarations for timer functions
extern "C" int64_t __gots_set_timeout(void* callback, int64_t delay_ms);
extern "C" int64_t __gots_set_interval(void* callback, int64_t delay_ms);  
extern "C" bool __gots_clear_timeout(int64_t timer_id);
extern "C" bool __gots_clear_interval(int64_t timer_id);

// Stub implementations for functions used by runtime_syscalls.cpp
void* __string_create(const char* str) {
    return (void*)strdup(str);
}

void __array_push(void* array, int64_t value) {
    // Basic stub - do nothing for now
    (void)array;
    (void)value;
}

// Timer management functions moved to goroutine_system.cpp

} // extern "C"

extern "C" void* __lookup_function(const char* name) {
    std::string func_name(name);
    auto it = gots_function_registry.find(func_name);
    if (it != gots_function_registry.end()) {
        std::cerr << "DEBUG: Found function " << name << " at address: " << it->second << std::endl;
        return it->second;
    }
    std::cerr << "ERROR: Function " << name << " not found in registry!" << std::endl;
    return nullptr;
}

// Thread-local storage for goroutine context
thread_local bool g_is_goroutine_context = false;

// Extern reference to global in goroutine_system.cpp
extern std::atomic<int64_t> g_active_goroutine_count;

extern "C" void __set_goroutine_context(int64_t is_goroutine) {
    bool was_goroutine = g_is_goroutine_context;
    g_is_goroutine_context = (is_goroutine != 0);
    std::cout << "DEBUG: Set goroutine context to " << g_is_goroutine_context << std::endl;
    
    if (g_is_goroutine_context && !was_goroutine) {
        // Setting up goroutine context
        // Increment active goroutine count
        g_active_goroutine_count.fetch_add(1);
        std::cout << "DEBUG: Incremented active goroutine count to " << g_active_goroutine_count.load() << std::endl;
    } else if (!g_is_goroutine_context && was_goroutine) {
        // Cleaning up goroutine context
        // Decrement active goroutine count
        g_active_goroutine_count.fetch_sub(1);
        std::cout << "DEBUG: Decremented active goroutine count to " << g_active_goroutine_count.load() << std::endl;
    }
}


// Timer system functions moved to goroutine_system.cpp

} // namespace gots
