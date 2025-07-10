#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <any>
#include <iostream>
#include <condition_variable>

namespace gots {

// Forward declarations
class Goroutine;
class LexicalEnvironment;
class WorkStealingScheduler;

// ============================================================================
// VARIABLE SYSTEM - For lexical scope chain
// ============================================================================

class Variable {
public:
    enum Type { INT64, FLOAT64, STRING, POINTER, FUNCTION };
    
private:
    Type type_;
    union {
        int64_t int_val;
        double float_val;
        void* ptr_val;
    } value_;
    std::string string_val_;
public:
    Variable(Type type) : type_(type) {}
    
    // Note: Memory managed by GC, no manual reference counting needed
    
    // Type-safe getters/setters
    void set_int64(int64_t val) { 
        if (type_ == INT64) value_.int_val = val; 
    }
    int64_t get_int64() const { 
        return (type_ == INT64) ? value_.int_val : 0; 
    }
    
    void set_float64(double val) { 
        if (type_ == FLOAT64) value_.float_val = val; 
    }
    double get_float64() const { 
        return (type_ == FLOAT64) ? value_.float_val : 0.0; 
    }
    
    void set_string(const std::string& val) { 
        if (type_ == STRING) string_val_ = val; 
    }
    const std::string& get_string() const { 
        return string_val_; 
    }
    
    void set_pointer(void* val) { 
        if (type_ == POINTER) value_.ptr_val = val; 
    }
    void* get_pointer() const { 
        return (type_ == POINTER) ? value_.ptr_val : nullptr; 
    }
    
    Type get_type() const { return type_; }
};

// ============================================================================
// LEXICAL ENVIRONMENT - Scope chain management
// ============================================================================

class LexicalEnvironment {
private:
    std::unordered_map<std::string, std::shared_ptr<Variable>> variables_;
    std::shared_ptr<LexicalEnvironment> parent_;
    mutable std::mutex mutex_;
    
public:
    LexicalEnvironment(std::shared_ptr<LexicalEnvironment> parent = nullptr) 
        : parent_(parent) {}
    
    ~LexicalEnvironment() {
        std::cout << "DEBUG: LexicalEnvironment destroyed" << std::endl;
    }
    
    // Note: Memory managed by GC, no manual reference counting needed
    
    void set_variable(const std::string& name, std::shared_ptr<Variable> var) {
        std::lock_guard<std::mutex> lock(mutex_);
        variables_[name] = var;
    }
    
    std::shared_ptr<Variable> get_variable(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = variables_.find(name);
        if (it != variables_.end()) {
            return it->second;
        }
        
        // Walk up scope chain
        if (parent_) {
            return parent_->get_variable(name);
        }
        
        return nullptr;
    }
    
    std::shared_ptr<Variable> create_variable(const std::string& name, Variable::Type type) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto var = std::make_shared<Variable>(type);
        variables_[name] = var;
        return var;
    }
    
    // Removed ref_count accessor - no longer needed with GC
    std::shared_ptr<LexicalEnvironment> get_parent() const { return parent_; }
};

// ============================================================================
// TIMER SYSTEM - Global unified timer management
// ============================================================================

struct Timer {
    uint64_t timer_id;
    std::chrono::steady_clock::time_point expiry;
    uint64_t goroutine_id;
    std::function<void()> callback;
    bool is_interval;
    std::chrono::milliseconds interval_duration;
    
    bool operator<(const Timer& other) const {
        return expiry > other.expiry;  // Min-heap (earliest first)
    }
};

class GlobalTimerSystem {
private:
    std::priority_queue<Timer> timers_;
    std::atomic<uint64_t> next_timer_id_{1};
    std::mutex timer_mutex_;
    std::condition_variable timer_cv_;  // For early wake-up
    std::unordered_map<uint64_t, uint64_t> timer_to_goroutine_;  // timer_id -> goroutine_id
    std::unordered_set<uint64_t> cancelled_timers_;  // Track cancelled timers
    std::unordered_map<uint64_t, std::chrono::milliseconds> intervals_;  // Track intervals
    
public:
    static GlobalTimerSystem& instance() {
        static GlobalTimerSystem instance;
        return instance;
    }
    
    uint64_t set_timeout(uint64_t goroutine_id, 
                        std::function<void()> callback, 
                        int64_t delay_ms);
    
    uint64_t set_interval(uint64_t goroutine_id, 
                         std::function<void()> callback, 
                         int64_t interval_ms);
    
    bool clear_timer(uint64_t timer_id);
    
    void process_expired_timers();
    
    // Efficient version that returns sleep duration
    std::chrono::milliseconds process_expired_timers_and_get_sleep_duration();
    
    size_t get_pending_count() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(timer_mutex_));
        return timers_.size();
    }
    
    void clear_all_timers_for_goroutine(uint64_t goroutine_id);
};

// ============================================================================
// MAIN THREAD CONTROLLER - Lifecycle management
// ============================================================================

class MainThreadController {
private:
    std::atomic<int> active_goroutines_{0};
    std::atomic<int> pending_timers_{0};
    std::atomic<int> active_io_operations_{0};
    std::atomic<bool> should_exit_{false};
    std::condition_variable exit_cv_;
    std::mutex exit_mutex_;
    
    // Track goroutine references for GC
    std::unordered_map<uint64_t, std::shared_ptr<Goroutine>> goroutine_refs_;
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> pending_timers_per_goroutine_;
    std::mutex refs_mutex_;
    
public:
    static MainThreadController& instance() {
        static MainThreadController instance;
        return instance;
    }
    
    // Goroutine lifecycle
    void goroutine_started(uint64_t goroutine_id, std::shared_ptr<Goroutine> goroutine);
    void goroutine_completed(uint64_t goroutine_id);
    
    // Timer lifecycle
    void timer_started(uint64_t goroutine_id, uint64_t timer_id);
    void timer_completed(uint64_t goroutine_id, uint64_t timer_id);
    
    // I/O lifecycle
    void io_operation_started();
    void io_operation_completed();
    
    // Main thread control
    void wait_for_completion();
    void force_exit();
    
    // Statistics
    int get_active_goroutines() const { return active_goroutines_.load(); }
    int get_pending_timers() const { return pending_timers_.load(); }
    int get_active_io_operations() const { return active_io_operations_.load(); }
    
private:
    void check_exit_condition();
    void cleanup_goroutine_references(uint64_t goroutine_id);
};

// ============================================================================
// GLOBAL EVENT LOOP - Single unified event system
// ============================================================================

class GlobalEventLoop {
private:
    std::thread event_thread_;
    std::atomic<bool> running_{false};
    GlobalTimerSystem& timer_system_;
    MainThreadController& main_controller_;
    WorkStealingScheduler* scheduler_{nullptr};
    
public:
    GlobalEventLoop() : timer_system_(GlobalTimerSystem::instance()),
                       main_controller_(MainThreadController::instance()) {}
    
    static GlobalEventLoop& instance() {
        static GlobalEventLoop instance;
        return instance;
    }
    
    void start(WorkStealingScheduler* scheduler);
    void stop();
    
    bool is_running() const { return running_.load(); }
    
private:
    void event_loop();
};

// ============================================================================
// GOROUTINE MANAGER - Reference counting and GC
// ============================================================================

class GoroutineManager {
private:
    std::unordered_map<uint64_t, std::shared_ptr<Goroutine>> active_goroutines_;
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> pending_timers_per_goroutine_;
    std::mutex mutex_;
    
public:
    static GoroutineManager& instance() {
        static GoroutineManager instance;
        return instance;
    }
    
    void register_goroutine(uint64_t goroutine_id, std::shared_ptr<Goroutine> goroutine);
    void unregister_goroutine(uint64_t goroutine_id);
    
    void add_timer_reference(uint64_t goroutine_id, uint64_t timer_id);
    void remove_timer_reference(uint64_t goroutine_id, uint64_t timer_id);
    
    std::shared_ptr<Goroutine> get_goroutine(uint64_t goroutine_id);
    
    bool is_goroutine_active(uint64_t goroutine_id);
    size_t get_active_count() const;
    
    void cleanup_completed_goroutines();
};

// ============================================================================
// ENHANCED GOROUTINE - Simplified without individual event loops
// ============================================================================

enum class GoroutineState {
    CREATED,
    RUNNING,
    SUSPENDED,
    COMPLETED,
    FAILED
};

class Goroutine : public std::enable_shared_from_this<Goroutine> {
private:
    uint64_t id_;
    std::function<void()> main_task_;
    std::shared_ptr<LexicalEnvironment> lexical_env_;
    std::atomic<GoroutineState> state_{GoroutineState::CREATED};
    std::vector<std::shared_ptr<Goroutine>> child_goroutines_;
    std::weak_ptr<Goroutine> parent_;
    std::atomic<int> child_count_{0};
    std::mutex children_mutex_;
    
    // Result storage for suspended goroutines
    std::any suspended_result_;
    std::mutex result_mutex_;
    
public:
    Goroutine(uint64_t id, std::shared_ptr<LexicalEnvironment> env);
    ~Goroutine();
    
    // Basic properties
    uint64_t get_id() const { return id_; }
    GoroutineState get_state() const { return state_.load(); }
    std::shared_ptr<LexicalEnvironment> get_lexical_env() const { return lexical_env_; }
    
    // Task management
    void set_main_task(std::function<void()> task) { main_task_ = task; }
    void run();
    
    // Child goroutine management
    std::shared_ptr<Goroutine> spawn_child(std::function<void()> task);
    void child_completed();
    int get_child_count() const { return child_count_.load(); }
    
    // Suspension/resumption for async operations
    void suspend();
    void resume();
    
    template<typename T>
    void set_result(T result) {
        std::lock_guard<std::mutex> lock(result_mutex_);
        suspended_result_ = result;
    }
    
    template<typename T>
    T get_result() {
        std::lock_guard<std::mutex> lock(result_mutex_);
        return std::any_cast<T>(suspended_result_);
    }
    
    // State management
    void set_state(GoroutineState state) { state_.store(state); }
    bool is_completed() const { 
        auto state = state_.load();
        return state == GoroutineState::COMPLETED || state == GoroutineState::FAILED;
    }
};

// ============================================================================
// GLOBAL FUNCTIONS - Thread-local storage
// ============================================================================

extern thread_local std::shared_ptr<Goroutine> current_goroutine;
extern thread_local std::shared_ptr<LexicalEnvironment> current_lexical_env;

// Helper functions
void set_current_goroutine(std::shared_ptr<Goroutine> goroutine);
std::shared_ptr<Goroutine> get_current_goroutine();

void set_current_lexical_env(std::shared_ptr<LexicalEnvironment> env);
std::shared_ptr<LexicalEnvironment> get_current_lexical_env();

// Thread cleanup functions
void register_thread_cleanup_hooks();
void cleanup_thread_local_resources();

// Initialization
void initialize_unified_event_system();
void shutdown_unified_event_system();

} // namespace gots