// This file contains patches to integrate the new goroutine system
// Replace the problematic goroutine and timer implementations

#include "goroutine_system.h"
#include "runtime.h"
#include <iostream>

namespace gots {

// Override the old timer manager
thread_local std::unique_ptr<GoroutineTimerManager> g_thread_timer_manager = nullptr;

// Replacement for old timer system
GoroutineTimerManager& get_timer_manager() {
    static GoroutineTimerManager dummy;
    return dummy;
}

} // namespace gots

extern "C" {

// Replace __goroutine_spawn with new implementation
void* __goroutine_spawn(const char* function_name) {
    std::cout << "DEBUG: NEW __goroutine_spawn called with function: " << function_name << std::endl;
    
    // Look up function in registry
    auto it = gots::gots_function_registry.find(std::string(function_name));
    if (it == gots::gots_function_registry.end()) {
        std::cerr << "ERROR: Function " << function_name << " not found in registry" << std::endl;
        return nullptr;
    }
    
    void* func_ptr = it->second;
    
    // Create task for goroutine
    auto task = [func_ptr]() {
        typedef void (*FuncType)();
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        func();
    };
    
    // Spawn goroutine
    gots::GoroutineScheduler::instance().spawn(task);
    
    // Return dummy promise for compatibility
    return reinterpret_cast<void*>(1);
}

// Replace __goroutine_spawn_func_ptr
void* __goroutine_spawn_func_ptr(void* func_ptr, void* arg) {
    std::cout << "DEBUG: NEW __goroutine_spawn_func_ptr called" << std::endl;
    
    auto task = [func_ptr]() {
        typedef void (*FuncType)();
        FuncType func = reinterpret_cast<FuncType>(func_ptr);
        func();
    };
    
    gots::GoroutineScheduler::instance().spawn(task);
    return reinterpret_cast<void*>(1);
}

// Timer functions using new system
int64_t create_timer_new(int64_t delay_ms, void* callback, bool is_interval) {
    return is_interval ? 
        __gots_set_interval(callback, delay_ms) : 
        __gots_set_timeout(callback, delay_ms);
}

bool cancel_timer_new(int64_t timer_id) {
    return __gots_clear_timeout(timer_id);
}

bool has_active_timers_new() {
    return gots::GoroutineScheduler::instance().get_active_count() > 0;
}

bool has_active_work_new() {
    return has_active_timers_new();
}

// Main initialization
void __runtime_init() {
    std::cout << "DEBUG: Initializing new goroutine system" << std::endl;
    
    // Create main goroutine for the main thread
    auto main_task = []() {};
    auto main_goroutine = std::make_shared<gots::Goroutine>(0, main_task, nullptr);
    gots::GoroutineScheduler::instance().set_main_goroutine(main_goroutine);
    gots::current_goroutine = main_goroutine;
}

// Main cleanup
void __runtime_cleanup() {
    std::cout << "DEBUG: Cleaning up goroutine system" << std::endl;
    gots::GoroutineScheduler::instance().wait_all();
}

} // extern "C"