#pragma once

#include "unified_event_system.h"
#include <atomic>
#include <memory>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>

namespace gots {

// Forward declaration
class WorkStealingScheduler;

// ============================================================================
// SIMPLIFIED GOROUTINE SCHEDULER - Works with unified event system
// ============================================================================

class GoroutineScheduler {
private:
    std::atomic<uint64_t> next_goroutine_id_{1};
    std::shared_ptr<Goroutine> main_goroutine_;
    WorkStealingScheduler* work_scheduler_{nullptr};
    std::mutex scheduler_mutex_;
    
    // Singleton
    GoroutineScheduler() = default;
    
public:
    static GoroutineScheduler& instance() {
        static GoroutineScheduler instance;
        return instance;
    }
    
    // Initialize with work-stealing scheduler
    void initialize(WorkStealingScheduler* scheduler);
    
    // Spawn goroutine with inherited lexical environment
    std::shared_ptr<Goroutine> spawn(std::function<void()> task, 
                                    std::shared_ptr<LexicalEnvironment> parent_env = nullptr);
    
    // Spawn main goroutine
    std::shared_ptr<Goroutine> spawn_main(std::function<void()> main_task);
    
    // Get current goroutine
    std::shared_ptr<Goroutine> get_current_goroutine();
    
    // Schedule function on current goroutine or work-stealing scheduler
    void schedule_task(std::function<void()> task);
    
    // Shutdown
    void shutdown();
    
    // Statistics
    size_t get_active_count() const;
    
private:
    uint64_t get_next_id() { return next_goroutine_id_.fetch_add(1); }
};

// ============================================================================
// RUNTIME FUNCTIONS - Updated for unified system
// ============================================================================

// Initialize the unified goroutine system
void initialize_unified_goroutine_system();

// Shutdown the unified goroutine system
void shutdown_unified_goroutine_system();

// Spawn goroutine with function pointer (for runtime integration)
void* __goroutine_spawn_unified(void* func_ptr, void* arg = nullptr);

// Timer functions using unified system
int64_t __gots_set_timeout_unified(void* callback, int64_t delay_ms);
int64_t __gots_set_interval_unified(void* callback, int64_t interval_ms);
bool __gots_clear_timeout_unified(int64_t timer_id);
bool __gots_clear_interval_unified(int64_t timer_id);

// Get current goroutine information
uint64_t __get_current_goroutine_id();
void* __get_current_lexical_env();

// Variable access in lexical environment
void* __lexical_env_get_variable(const char* name);
void __lexical_env_set_variable(const char* name, void* value, int type);

// Wait for all goroutines to complete
void __wait_for_all_goroutines();

} // namespace gots