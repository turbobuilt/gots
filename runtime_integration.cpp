// Old goroutine system removed
#include "runtime.h"
#include <iostream>
#include <unordered_map>
#include <mutex>

// Function registry (from runtime.cpp)
extern std::unordered_map<std::string, void*> gots::gots_function_registry;

namespace gots {

// Initialize the main goroutine for the main thread
void initialize_main_goroutine() {
    auto main_task = []() {
        std::cout << "DEBUG: Main goroutine placeholder task" << std::endl;
    };
    
    auto main_goroutine = std::make_shared<Goroutine>(0, main_task, nullptr);
    GoroutineScheduler::instance().set_main_goroutine(main_goroutine);
    current_goroutine = main_goroutine;
}

} // namespace gots

extern "C" {

// Replace the old __goroutine_spawn functions with new implementation
void* __goroutine_spawn(const char* function_name) {
    std::cout << "DEBUG: __goroutine_spawn called with function: " << function_name << std::endl;
    
    // Look up function in registry
    auto it = gots::gots_function_registry.find(std::string(function_name));
    if (it == gots::gots_function_registry.end()) {
        std::cerr << "ERROR: Function " << function_name << " not found in registry" << std::endl;
        return nullptr;
    }
    
    void* func_ptr = it->second;
    
    // Create task for goroutine
    auto task = [func_ptr]() {
        std::cout << "DEBUG: Executing goroutine function" << std::endl;
        typedef void (*FuncType)();
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        func();
    };
    
    // Spawn goroutine
    auto goroutine = gots::GoroutineScheduler::instance().spawn(task);
    
    // Return a dummy promise for compatibility
    auto promise = std::make_shared<gots::Promise>();
    promise->resolve(0);
    return promise.get();
}

void* __goroutine_spawn_func_ptr(void* func_ptr, void* arg) {
    std::cout << "DEBUG: __goroutine_spawn_func_ptr called with func_ptr: " << func_ptr << std::endl;
    
    // Create task for goroutine
    auto task = [func_ptr]() {
        std::cout << "DEBUG: Executing goroutine function via pointer" << std::endl;
        typedef void (*FuncType)();
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        func();
    };
    
    // Spawn goroutine
    auto goroutine = gots::GoroutineScheduler::instance().spawn(task);
    
    // Return a dummy promise for compatibility
    auto promise = std::make_shared<gots::Promise>();
    promise->resolve(0);
    return promise.get();
}

// Replace timer functions to use new system
int64_t __runtime_timer_set_timeout(void* callback, int64_t delay) {
    std::cout << "DEBUG: __runtime_timer_set_timeout called with delay=" << delay << "ms" << std::endl;
    return __gots_set_timeout(callback, delay);
}

int64_t __runtime_timer_set_interval(void* callback, int64_t delay) {
    std::cout << "DEBUG: __runtime_timer_set_interval called with delay=" << delay << "ms" << std::endl;
    return __gots_set_interval(callback, delay);
}

bool __runtime_timer_clear_timeout(int64_t id) {
    std::cout << "DEBUG: __runtime_timer_clear_timeout called for id=" << id << std::endl;
    return __gots_clear_timeout(id);
}

bool __runtime_timer_clear_interval(int64_t id) {
    std::cout << "DEBUG: __runtime_timer_clear_interval called for id=" << id << std::endl;
    return __gots_clear_interval(id);
}

// Main program loop
void __runtime_main_loop() {
    std::cout << "DEBUG: Starting main runtime loop" << std::endl;
    
    // Initialize main goroutine
    gots::initialize_main_goroutine();
    
    // Wait for all goroutines to complete
    gots::GoroutineScheduler::instance().wait_all();
    
    std::cout << "DEBUG: Main runtime loop completed" << std::endl;
}

} // extern "C"