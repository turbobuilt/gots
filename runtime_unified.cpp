#include "runtime.h"
#include "goroutine_system_unified.h"
#include "unified_event_system.h"
#include "goroutine_advanced.h"
#include <iostream>
#include <cstring>

// Forward declaration for global scheduler
// extern gots::WorkStealingScheduler* g_work_stealing_scheduler;

namespace gots {

// ============================================================================
// UNIFIED RUNTIME FUNCTIONS - Replace old timer/goroutine functions
// ============================================================================

// Initialize unified runtime system
void __init_unified_runtime() {
    std::cout << "DEBUG: Initializing unified runtime system" << std::endl;
    initialize_unified_goroutine_system();
}

// Shutdown unified runtime system
void __shutdown_unified_runtime() {
    std::cout << "DEBUG: Shutting down unified runtime system" << std::endl;
    shutdown_unified_goroutine_system();
}

// Main goroutine execution with unified system
void __execute_main_with_unified_system(void* main_func_ptr) {
    if (!main_func_ptr) {
        std::cerr << "ERROR: __execute_main_with_unified_system called with null function" << std::endl;
        return;
    }
    
    // Create main task wrapper
    auto main_task = [main_func_ptr]() {
        typedef void (*main_func_t)();
        main_func_t main_func = reinterpret_cast<main_func_t>(main_func_ptr);
        
        try {
            std::cout << "DEBUG: Executing main function in unified system" << std::endl;
            main_func();
            std::cout << "DEBUG: Main function completed" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Main function exception: " << e.what() << std::endl;
        }
    };
    
    // Spawn main goroutine
    auto main_goroutine = GoroutineScheduler::instance().spawn_main(main_task);
    
    // Run main goroutine in current thread
    main_goroutine->run();
    
    // Wait for all child goroutines and timers to complete
    MainThreadController::instance().wait_for_completion();
    
    std::cout << "DEBUG: Main execution completed with unified system" << std::endl;
}

// ============================================================================
// TIMER FUNCTIONS - Use unified system
// ============================================================================

int64_t __gots_set_timeout(void* callback, int64_t delay_ms) {
    return __gots_set_timeout_unified(callback, delay_ms);
}

int64_t __gots_set_interval(void* callback, int64_t interval_ms) {
    return __gots_set_interval_unified(callback, interval_ms);
}

bool __gots_clear_timeout(int64_t timer_id) {
    return __gots_clear_timeout_unified(timer_id);
}

bool __gots_clear_interval(int64_t timer_id) {
    return __gots_clear_interval_unified(timer_id);
}

// ============================================================================
// GOROUTINE FUNCTIONS - Use unified system
// ============================================================================

void* __goroutine_spawn_func_ptr(void* func_ptr, void* arg) {
    return __goroutine_spawn_unified(func_ptr, arg);
}

// ============================================================================
// ENHANCED CONSOLE FUNCTIONS - Thread-safe
// ============================================================================

void __console_log_string_unified(const char* str) {
    if (!str) return;
    
    static std::mutex console_mutex;
    std::lock_guard<std::mutex> lock(console_mutex);
    
    auto current_goroutine = get_current_goroutine();
    if (current_goroutine) {
        std::cout << "[G" << current_goroutine->get_id() << "] " << str;
    } else {
        std::cout << "[MAIN] " << str;
    }
}

void __console_log_int64_unified(int64_t value) {
    static std::mutex console_mutex;
    std::lock_guard<std::mutex> lock(console_mutex);
    
    auto current_goroutine = get_current_goroutine();
    if (current_goroutine) {
        std::cout << "[G" << current_goroutine->get_id() << "] " << value;
    } else {
        std::cout << "[MAIN] " << value;
    }
}

void __console_log_float64_unified(double value) {
    static std::mutex console_mutex;
    std::lock_guard<std::mutex> lock(console_mutex);
    
    auto current_goroutine = get_current_goroutine();
    if (current_goroutine) {
        std::cout << "[G" << current_goroutine->get_id() << "] " << value;
    } else {
        std::cout << "[MAIN] " << value;
    }
}

void __console_log_newline_unified() {
    static std::mutex console_mutex;
    std::lock_guard<std::mutex> lock(console_mutex);
    std::cout << std::endl;
}

// ============================================================================
// LEXICAL ENVIRONMENT FUNCTIONS - For scope chain access
// ============================================================================

void* __create_lexical_env(void* parent_env) {
    std::shared_ptr<LexicalEnvironment> parent;
    if (parent_env) {
        parent = std::shared_ptr<LexicalEnvironment>(
            reinterpret_cast<LexicalEnvironment*>(parent_env), 
            [](LexicalEnvironment*){} // Don't delete, managed by shared_ptr
        );
    }
    
    auto env = std::make_shared<LexicalEnvironment>(parent);
    env->add_ref(); // Keep alive for C interface
    return env.get();
}

void __destroy_lexical_env(void* env_ptr) {
    if (env_ptr) {
        auto env = reinterpret_cast<LexicalEnvironment*>(env_ptr);
        env->release();
    }
}

void __lexical_env_set_int64(void* env_ptr, const char* name, int64_t value) {
    if (!env_ptr || !name) return;
    
    auto env = reinterpret_cast<LexicalEnvironment*>(env_ptr);
    auto var = env->create_variable(std::string(name), Variable::INT64);
    if (var) {
        var->set_int64(value);
    }
}

int64_t __lexical_env_get_int64(void* env_ptr, const char* name) {
    if (!env_ptr || !name) return 0;
    
    auto env = reinterpret_cast<LexicalEnvironment*>(env_ptr);
    auto var = env->get_variable(std::string(name));
    return var ? var->get_int64() : 0;
}

void __lexical_env_set_string(void* env_ptr, const char* name, const char* value) {
    if (!env_ptr || !name || !value) return;
    
    auto env = reinterpret_cast<LexicalEnvironment*>(env_ptr);
    auto var = env->create_variable(std::string(name), Variable::STRING);
    if (var) {
        var->set_string(std::string(value));
    }
}

const char* __lexical_env_get_string(void* env_ptr, const char* name) {
    if (!env_ptr || !name) return nullptr;
    
    auto env = reinterpret_cast<LexicalEnvironment*>(env_ptr);
    auto var = env->get_variable(std::string(name));
    if (var && var->get_type() == Variable::STRING) {
        return var->get_string().c_str();
    }
    return nullptr;
}

// ============================================================================
// SYSTEM STATISTICS - For debugging
// ============================================================================

void __print_unified_system_stats() {
    static std::mutex stats_mutex;
    std::lock_guard<std::mutex> lock(stats_mutex);
    
    auto& main_controller = MainThreadController::instance();
    auto& timer_system = GlobalTimerSystem::instance();
    
    std::cout << "\n=== UNIFIED SYSTEM STATISTICS ===" << std::endl;
    std::cout << "Active goroutines: " << main_controller.get_active_goroutines() << std::endl;
    std::cout << "Pending timers: " << main_controller.get_pending_timers() << std::endl;
    std::cout << "Active I/O operations: " << main_controller.get_active_io_operations() << std::endl;
    std::cout << "Timer queue size: " << timer_system.get_pending_count() << std::endl;
    std::cout << "Goroutine manager active: " << GoroutineManager::instance().get_active_count() << std::endl;
    
    std::cout << "Work-stealing scheduler active: Integrated" << std::endl;
    
    std::cout << "Global event loop running: " << (GlobalEventLoop::instance().is_running() ? "Yes" : "No") << std::endl;
    std::cout << "=================================" << std::endl;
}

// ============================================================================
// COMPATIBILITY FUNCTIONS - For existing code
// ============================================================================

// Wrapper for existing console functions
void __console_log_string(const char* str) {
    __console_log_string_unified(str);
}

void __console_log_int64(int64_t value) {
    __console_log_int64_unified(value);
}

void __console_log_float64(double value) {
    __console_log_float64_unified(value);
}

void __console_log_newline() {
    __console_log_newline_unified();
}

// Wait for completion
void __wait_for_completion() {
    __wait_for_all_goroutines();
}

// Force system shutdown
void __force_shutdown() {
    MainThreadController::instance().force_exit();
}

} // namespace gots