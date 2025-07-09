#include "goroutine_system_unified.h"
#include "goroutine_advanced.h"
#include <iostream>
#include <algorithm>

// Forward declaration for global scheduler
// extern gots::WorkStealingScheduler* g_work_stealing_scheduler;

namespace gots {

// ============================================================================
// GOROUTINE SCHEDULER IMPLEMENTATION
// ============================================================================

void GoroutineScheduler::initialize(WorkStealingScheduler* scheduler) {
    std::lock_guard<std::mutex> lock(scheduler_mutex_);
    work_scheduler_ = scheduler;
    
    // Initialize unified event system
    initialize_unified_event_system();
    
    std::cout << "DEBUG: GoroutineScheduler initialized with unified event system" << std::endl;
}

std::shared_ptr<Goroutine> GoroutineScheduler::spawn(std::function<void()> task, 
                                                    std::shared_ptr<LexicalEnvironment> parent_env) {
    uint64_t goroutine_id = get_next_id();
    
    // Create lexical environment (inherit from parent if provided)
    std::shared_ptr<LexicalEnvironment> env;
    if (parent_env) {
        env = std::make_shared<LexicalEnvironment>(parent_env);
    } else {
        env = std::make_shared<LexicalEnvironment>();
    }
    
    // Create goroutine
    auto goroutine = std::make_shared<Goroutine>(goroutine_id, env);
    goroutine->set_main_task(task);
    
    // Register with systems
    GoroutineManager::instance().register_goroutine(goroutine_id, goroutine);
    MainThreadController::instance().goroutine_started(goroutine_id, goroutine);
    
    // Schedule on work-stealing scheduler
    if (work_scheduler_) {
        work_scheduler_->schedule([goroutine]() {
            goroutine->run();
        });
    } else {
        // Fallback: run in new thread
        std::thread([goroutine]() {
            goroutine->run();
        }).detach();
    }
    
    std::cout << "DEBUG: Spawned goroutine " << goroutine_id << std::endl;
    return goroutine;
}

std::shared_ptr<Goroutine> GoroutineScheduler::spawn_main(std::function<void()> main_task) {
    uint64_t main_id = get_next_id();
    
    // Create root lexical environment
    auto root_env = std::make_shared<LexicalEnvironment>();
    
    // Create main goroutine
    main_goroutine_ = std::make_shared<Goroutine>(main_id, root_env);
    main_goroutine_->set_main_task(main_task);
    
    // Register with systems
    GoroutineManager::instance().register_goroutine(main_id, main_goroutine_);
    MainThreadController::instance().goroutine_started(main_id, main_goroutine_);
    
    std::cout << "DEBUG: Created main goroutine " << main_id << std::endl;
    return main_goroutine_;
}

std::shared_ptr<Goroutine> GoroutineScheduler::get_current_goroutine() {
    return ::gots::get_current_goroutine();
}

void GoroutineScheduler::schedule_task(std::function<void()> task) {
    if (work_scheduler_) {
        work_scheduler_->schedule(std::move(task));
    } else {
        // Fallback: execute immediately
        task();
    }
}

void GoroutineScheduler::shutdown() {
    std::cout << "DEBUG: Shutting down GoroutineScheduler" << std::endl;
    
    // Clean up main goroutine
    if (main_goroutine_) {
        main_goroutine_.reset();
    }
    
    // Shutdown unified event system
    shutdown_unified_event_system();
    
    std::cout << "DEBUG: GoroutineScheduler shut down" << std::endl;
}

size_t GoroutineScheduler::get_active_count() const {
    return GoroutineManager::instance().get_active_count();
}

// ============================================================================
// RUNTIME FUNCTIONS IMPLEMENTATION
// ============================================================================

void initialize_unified_goroutine_system() {
    std::cout << "DEBUG: Initializing unified goroutine system" << std::endl;
    
    // Initialize work-stealing scheduler
    WorkStealingScheduler* scheduler = new WorkStealingScheduler();
    
    // Initialize goroutine scheduler
    GoroutineScheduler::instance().initialize(scheduler);
    
    std::cout << "DEBUG: Unified goroutine system initialized" << std::endl;
}

void shutdown_unified_goroutine_system() {
    std::cout << "DEBUG: Shutting down unified goroutine system" << std::endl;
    
    // Shutdown scheduler
    GoroutineScheduler::instance().shutdown();
    
    // Clean up work-stealing scheduler
    // (Will be managed by GoroutineScheduler)
    
    std::cout << "DEBUG: Unified goroutine system shut down" << std::endl;
}

void* __goroutine_spawn_unified(void* func_ptr, void* arg) {
    if (!func_ptr) {
        std::cerr << "ERROR: __goroutine_spawn_unified called with null function pointer" << std::endl;
        return nullptr;
    }
    
    // Get current goroutine's lexical environment for inheritance
    auto current_env = get_current_lexical_env();
    
    // Create task wrapper
    auto task = [func_ptr, arg]() {
        typedef void (*func_t)(void*);
        func_t function = reinterpret_cast<func_t>(func_ptr);
        
        try {
            function(arg);
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Goroutine function exception: " << e.what() << std::endl;
        }
    };
    
    // Spawn goroutine with inherited environment
    auto goroutine = GoroutineScheduler::instance().spawn(task, current_env);
    
    return reinterpret_cast<void*>(goroutine->get_id());
}

int64_t __gots_set_timeout_unified(void* callback, int64_t delay_ms) {
    if (!callback) {
        std::cerr << "ERROR: __gots_set_timeout_unified called with null callback" << std::endl;
        return -1;
    }
    
    auto current_goroutine = get_current_goroutine();
    if (!current_goroutine) {
        std::cerr << "ERROR: __gots_set_timeout_unified called outside goroutine context" << std::endl;
        return -1;
    }
    
    // Create callback wrapper
    auto callback_wrapper = [callback]() {
        typedef void (*callback_t)();
        callback_t func = reinterpret_cast<callback_t>(callback);
        
        try {
            func();
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Timer callback exception: " << e.what() << std::endl;
        }
    };
    
    // Set timeout using unified timer system
    return GlobalTimerSystem::instance().set_timeout(
        current_goroutine->get_id(), 
        std::move(callback_wrapper), 
        delay_ms
    );
}

int64_t __gots_set_interval_unified(void* callback, int64_t interval_ms) {
    if (!callback) {
        std::cerr << "ERROR: __gots_set_interval_unified called with null callback" << std::endl;
        return -1;
    }
    
    auto current_goroutine = get_current_goroutine();
    if (!current_goroutine) {
        std::cerr << "ERROR: __gots_set_interval_unified called outside goroutine context" << std::endl;
        return -1;
    }
    
    // Create callback wrapper
    auto callback_wrapper = [callback]() {
        typedef void (*callback_t)();
        callback_t func = reinterpret_cast<callback_t>(callback);
        
        try {
            func();
        } catch (const std::exception& e) {
            std::cerr << "ERROR: Interval callback exception: " << e.what() << std::endl;
        }
    };
    
    // Set interval using unified timer system
    return GlobalTimerSystem::instance().set_interval(
        current_goroutine->get_id(), 
        std::move(callback_wrapper), 
        interval_ms
    );
}

bool __gots_clear_timeout_unified(int64_t timer_id) {
    return GlobalTimerSystem::instance().clear_timer(timer_id);
}

bool __gots_clear_interval_unified(int64_t timer_id) {
    return GlobalTimerSystem::instance().clear_timer(timer_id);
}

uint64_t __get_current_goroutine_id() {
    auto current_goroutine = get_current_goroutine();
    return current_goroutine ? current_goroutine->get_id() : 0;
}

void* __get_current_lexical_env() {
    auto env = get_current_lexical_env();
    return env.get();
}

void* __lexical_env_get_variable(const char* name) {
    if (!name) return nullptr;
    
    auto env = get_current_lexical_env();
    if (!env) return nullptr;
    
    auto var = env->get_variable(std::string(name));
    return var ? var->get_pointer() : nullptr;
}

void __lexical_env_set_variable(const char* name, void* value, int type) {
    if (!name) return;
    
    auto env = get_current_lexical_env();
    if (!env) return;
    
    auto var = env->create_variable(std::string(name), static_cast<Variable::Type>(type));
    if (var) {
        var->set_pointer(value);
    }
}

void __wait_for_all_goroutines() {
    std::cout << "DEBUG: Waiting for all goroutines to complete..." << std::endl;
    
    // Wait for main thread controller to signal completion
    MainThreadController::instance().wait_for_completion();
    
    std::cout << "DEBUG: All goroutines completed" << std::endl;
}

} // namespace gots