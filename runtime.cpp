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
    // Initialize function table
    for (size_t i = 0; i < MAX_FUNCTIONS; i++) {
        g_function_table[i].func_ptr = nullptr;
    }
    __new_goroutine_system_init();
}

// Main cleanup - wait for all goroutines
void __runtime_cleanup() {
    __new_goroutine_system_cleanup();
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

// Simplified Array runtime functions
extern "C" void* __simple_array_create(double* values, int64_t size) {
    Array* arr = new Array();
    for (int64_t i = 0; i < size; i++) {
        arr->push(values[i]);
    }
    return arr;
}

extern "C" void* __simple_array_zeros(int64_t size) {
    if (size <= 0) {
        // Create empty array
        Array* arr = new Array();
        return arr;
    }
    std::vector<size_t> shape = {static_cast<size_t>(size)};
    Array* arr = new Array(Array::zeros(shape));
    return arr;
}

extern "C" void* __simple_array_ones(int64_t size) {
    std::vector<size_t> shape = {static_cast<size_t>(size)};
    Array* arr = new Array(Array::ones(shape));
    return arr;
}

extern "C" void __simple_array_push(void* array, double value) {
    if (array) {
        static_cast<Array*>(array)->push(value);
    }
}

extern "C" double __simple_array_pop(void* array) {
    if (array) {
        return static_cast<Array*>(array)->pop();
    }
    return 0.0;
}

extern "C" double __simple_array_get(void* array, int64_t index) {
    if (array) {
        return (*static_cast<Array*>(array))[index];
    }
    return 0.0;
}

extern "C" void __simple_array_set(void* array, int64_t index, double value) {
    if (array) {
        (*static_cast<Array*>(array))[index] = value;
    }
}

extern "C" int64_t __simple_array_length(void* array) {
    if (array) {
        return static_cast<int64_t>(static_cast<Array*>(array)->length());
    }
    return 0;
}

extern "C" double __simple_array_sum(void* array) {
    if (array) {
        return static_cast<Array*>(array)->sum();
    }
    return 0.0;
}

extern "C" double __simple_array_mean(void* array) {
    if (array) {
        return static_cast<Array*>(array)->mean();
    }
    return 0.0;
}

extern "C" void* __simple_array_shape(void* array) {
    if (array) {
        const std::vector<size_t>& shape = static_cast<Array*>(array)->shape();
        std::vector<double> shape_as_doubles;
        for (size_t dim : shape) {
            shape_as_doubles.push_back(static_cast<double>(dim));
        }
        return new Array({shape_as_doubles.size()}, shape_as_doubles);
    }
    return nullptr;
}

extern "C" const char* __simple_array_tostring(void* array) {
    if (array) {
        std::string str = static_cast<Array*>(array)->toString();
        return strdup(str.c_str());
    }
    return strdup("Array()");
}

extern "C" void* __simple_array_slice(void* array, int64_t start, int64_t end, int64_t step) {
    if (array) {
        Array sliced = static_cast<Array*>(array)->slice(start, end, step);
        return new Array(sliced);
    }
    return nullptr;
}

extern "C" void* __simple_array_slice_all(void* array) {
    if (array) {
        Array sliced = static_cast<Array*>(array)->slice_all();
        return new Array(sliced);
    }
    return nullptr;
}

double __simple_array_max(void* array) {
    if (array) {
        return static_cast<Array*>(array)->max();
    }
    return 0.0;
}

double __simple_array_min(void* array) {
    if (array) {
        return static_cast<Array*>(array)->min();
    }
    return 0.0;
}

// Helper function to get first dimension from shape array
int64_t __simple_array_get_first_dimension(void* shape_array) {
    if (shape_array) {
        Array* arr = static_cast<Array*>(shape_array);
        if (arr->length() > 0) {
            return static_cast<int64_t>((*arr)[0]);
        }
    }
    return 0;
}

// Array static factory methods
void* __simple_array_arange(double start, double stop, double step) {
    Array* arr = new Array(Array::arange(start, stop, step));
    return arr;
}

void* __simple_array_linspace(double start, double stop, int64_t num) {
    Array* arr = new Array(Array::linspace(start, stop, static_cast<size_t>(num)));
    return arr;
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
    
    if (g_is_goroutine_context && !was_goroutine) {
        // Setting up goroutine context
        // Increment active goroutine count
        g_active_goroutine_count.fetch_add(1);
    } else if (!g_is_goroutine_context && was_goroutine) {
        // Cleaning up goroutine context
        // Decrement active goroutine count
        g_active_goroutine_count.fetch_sub(1);
    }
}


} // namespace gots

// Ultra-High-Performance Direct Address Goroutine Spawn
namespace gots {
void* __goroutine_spawn_func_ptr(void* func_ptr, void* arg) {
    
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

extern "C" const char* __dynamic_method_toString(void* obj) {
    // For now, we'll treat this as a simple array toString
    // In a full implementation, this would check object type and call appropriate toString
    if (obj) {
        Array* array = static_cast<Array*>(obj);
        std::string str = array->toString();
        return strdup(str.c_str());
    }
    return strdup("undefined");
}

} // namespace gots
