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

// High-Performance Function Registry Implementation
FunctionEntry g_function_table[MAX_FUNCTIONS];
std::atomic<uint16_t> g_next_function_id{1};  // Start at 1, 0 is reserved for "invalid"

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


// High-Performance Function Registration - O(1) access
uint16_t __register_function_fast(void* func_ptr, uint16_t arg_count, uint8_t calling_convention) {
    uint16_t func_id = g_next_function_id.fetch_add(1);
    
    if (func_id >= MAX_FUNCTIONS) {
        std::cerr << "ERROR: Function table overflow! Maximum " << MAX_FUNCTIONS << " functions supported." << std::endl;
        return 0;  // Return invalid ID
    }
    
    FunctionEntry& entry = g_function_table[func_id];
    entry.func_ptr = func_ptr;
    entry.arg_count = arg_count;
    entry.calling_convention = calling_convention;
    entry.flags = 0;
    
    return func_id;
}

void* __lookup_function_fast(uint16_t func_id) {
    if (func_id == 0 || func_id >= g_next_function_id.load()) {
        return nullptr;  // Invalid function ID
    }
    
    return g_function_table[func_id].func_ptr;
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

// Optimized goroutine spawn with direct function IDs - NO string lookups
void* __goroutine_spawn_fast(uint16_t func_id) {
    void* func_ptr = __lookup_function_fast(func_id);
    if (!func_ptr) {
        std::cerr << "ERROR: Invalid function ID: " << func_id << std::endl;
        return nullptr;
    }
    
    // Create a task that calls the function directly - minimal overhead
    auto task = [func_ptr]() {
        typedef int64_t (*FuncType)();
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        return func();
    };
    
    GoroutineScheduler::instance().spawn(task);
    return reinterpret_cast<void*>(1);
}

void* __goroutine_spawn_fast_arg1(uint16_t func_id, int64_t arg1) {
    void* func_ptr = __lookup_function_fast(func_id);
    if (!func_ptr) {
        std::cerr << "ERROR: Invalid function ID: " << func_id << std::endl;
        return nullptr;
    }
    
    // Create a task that calls the function with one argument - minimal overhead
    auto task = [func_ptr, arg1]() {
        typedef int64_t (*FuncType)(int64_t);
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        return func(arg1);
    };
    
    GoroutineScheduler::instance().spawn(task);
    return reinterpret_cast<void*>(1);
}

void* __goroutine_spawn_fast_arg2(uint16_t func_id, int64_t arg1, int64_t arg2) {
    void* func_ptr = __lookup_function_fast(func_id);
    if (!func_ptr) {
        std::cerr << "ERROR: Invalid function ID: " << func_id << std::endl;
        return nullptr;
    }
    
    // Create a task that calls the function with two arguments - minimal overhead
    auto task = [func_ptr, arg1, arg2]() {
        typedef int64_t (*FuncType)(int64_t, int64_t);
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

void __console_log_number(int64_t value) {
    std::lock_guard<std::mutex> lock(g_console_mutex);
    std::cout << value;
    std::cout.flush();
}

void __console_log_auto(int64_t value) {
    // Check if it's a likely heap pointer (string or object)
    if (value > 0x100000) {  // Likely a heap pointer
        // Try to safely read the string by using the __console_log_string function
        // This way we use the existing safe string handling
        void* ptr = reinterpret_cast<void*>(value);
        
        // Try to call the string logger directly and see if it works
        try {
            __console_log_string(ptr);
            return;
        } catch (...) {
            // String printing failed, try other types
        }
        
        // Check if it might be an object ID
        if (object_registry.find(value) != object_registry.end()) {
            __console_log_object(value);
            return;
        }
    }
    
    // Default: treat as number
    std::cout << value;
}

void __console_log_string(void* string_ptr) {
    std::lock_guard<std::mutex> lock(g_console_mutex);
    if (string_ptr) {
        // Handle basic char* strings
        const char* str = static_cast<const char*>(string_ptr);
        std::cout << str;
        std::cout.flush();
    }
}

void __console_log_object(int64_t object_id) {
    auto it = object_registry.find(object_id);
    if (it != object_registry.end()) {
        std::cout << "[object Object]";
    } else {
        std::cout << "Object#" << object_id;
    }
}

// Helper function to extract C string from GoTSString pointer
const char* __gots_string_to_cstr(void* gots_string_ptr) {
    if (!gots_string_ptr) {
        return "";
    }
    GoTSString* str = static_cast<GoTSString*>(gots_string_ptr);
    return str->c_str();
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

// String interning for literals - simple implementation for now
void* __string_intern(const char* str) {
    // For now, just return a copy - could optimize with interning later
    return (void*)strdup(str);
}

void __array_push(void* array, int64_t value) {
    // Basic stub - do nothing for now
    (void)array;
    (void)value;
}

// Timer management functions moved to goroutine_system.cpp

} // extern "C"

// Legacy function removed - use __lookup_function_fast(func_id) instead

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


} // namespace gots

// Ultra-High-Performance Direct Address Goroutine Spawn
namespace gots {
void* __goroutine_spawn_func_ptr(void* func_ptr, void* arg) {
    std::cout << "DEBUG: __goroutine_spawn_func_ptr called with func_ptr: " << func_ptr << std::endl;
    
    // Cast function pointer to proper type and spawn goroutine directly
    // This is the FASTEST possible goroutine spawn - zero overhead, direct address call
    if (func_ptr) {
        // Use the goroutine scheduler to spawn with the function pointer
        typedef void (*func_t)();
        func_t function = reinterpret_cast<func_t>(func_ptr);
        
        std::function<void()> task = [function]() {
            function();
        };
        
        GoroutineScheduler::instance().spawn(task, nullptr);
    } else {
        std::cerr << "ERROR: __goroutine_spawn_func_ptr called with null function pointer" << std::endl;
    }
    
    return nullptr; // TODO: Return actual goroutine handle if needed
}

// Get the executable memory base address for relative offset calculations
extern "C" void* __get_executable_memory_base() {
    std::lock_guard<std::mutex> lock(g_executable_memory.mutex);
    return g_executable_memory.ptr;
}

// Timer system functions moved to goroutine_system.cpp

} // namespace gots
