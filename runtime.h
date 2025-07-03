#pragma once

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include <functional>
#include <memory>
#include <unordered_map>
#include <type_traits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <chrono>

// Forward declare DataType from compiler.h to avoid circular dependency
namespace gots {
    enum class DataType;
    class LexicalScope;
}

namespace gots {

// High-performance typed arrays - direct templates for maximum performance
template<typename T>
struct TypedArray {
    T* data;
    int64_t size;
    int64_t capacity;
    DataType element_type;
    
    TypedArray(DataType type, int64_t initial_capacity = 8) 
        : data(nullptr), size(0), capacity(initial_capacity), element_type(type) {
        if (capacity > 0) {
            data = new T[capacity];
        }
    }
    
    ~TypedArray() {
        if (data) delete[] data;
    }
    
    // Inline for maximum performance
    inline void ensure_capacity(int64_t new_size) {
        if (__builtin_expect(new_size > capacity, 0)) {
            int64_t new_capacity = capacity == 0 ? 8 : capacity;
            while (new_capacity < new_size) {
                new_capacity <<= 1;  // Bit shift for faster multiplication
            }
            T* new_data = new T[new_capacity];
            if (data) {
                // Use fast memory copy for POD types, move semantics for complex types
                if constexpr (std::is_trivially_copyable_v<T>) {
                    std::memcpy(new_data, data, size * sizeof(T));
                } else {
                    for (int64_t i = 0; i < size; ++i) {
                        new_data[i] = std::move(data[i]);
                    }
                }
                delete[] data;
            }
            data = new_data;
            capacity = new_capacity;
        }
    }
    
    // Inline push for maximum performance - no bounds checking overhead
    inline void push(T value) {
        ensure_capacity(size + 1);
        data[size++] = std::move(value);
    }
    
    // Inline pop for maximum performance
    inline T pop() {
        if (__builtin_expect(size > 0, 1)) {
            return std::move(data[--size]);
        }
        return T{};
    }
    
    // Direct array access - no bounds checking for maximum performance
    inline T& operator[](int64_t index) {
        return data[index];
    }
    
    inline const T& operator[](int64_t index) const {
        return data[index];
    }
    
    // Safe access with bounds checking
    inline T get(int64_t index) const {
        if (__builtin_expect(index >= 0 && index < size, 1)) {
            return data[index];
        }
        return T{};
    }
    
    inline void set(int64_t index, T value) {
        if (__builtin_expect(index >= 0 && index < size, 1)) {
            data[index] = std::move(value);
        }
    }
    
    // Direct access to raw data for maximum performance
    inline T* raw_data() { return data; }
    inline const T* raw_data() const { return data; }
    inline int64_t length() const { return size; }
    inline bool empty() const { return size == 0; }
};

// Specialized arrays for common types - maximum performance
using Int8Array = TypedArray<int8_t>;
using Int16Array = TypedArray<int16_t>;
using Int32Array = TypedArray<int32_t>;
using Int64Array = TypedArray<int64_t>;
using Uint8Array = TypedArray<uint8_t>;
using Uint16Array = TypedArray<uint16_t>;
using Uint32Array = TypedArray<uint32_t>;
using Uint64Array = TypedArray<uint64_t>;
using Float32Array = TypedArray<float>;
using Float64Array = TypedArray<double>;

// High-Performance String Implementation with Small String Optimization (SSO)
// This implements an extremely fast string type optimized for JIT compilation
class GoTSString {
public:
    // SSO threshold - strings up to 22 bytes are stored inline (on 64-bit systems)
    static constexpr size_t SSO_THRESHOLD = sizeof(void*) + sizeof(size_t) + sizeof(size_t) - 1;
    
private:
    union {
        // Large string storage
        struct {
            char* data;
            size_t size;
            size_t capacity;
        } large;
        
        // Small string storage - direct inline storage
        struct {
            char buffer[SSO_THRESHOLD + 1]; // +1 for null terminator
            uint8_t size; // Size fits in 1 byte for small strings
        } small;
    };
    
    // Use the LSB of capacity to indicate if it's a small string
    // Small strings have capacity = 0, large strings have odd capacity
    bool is_small() const {
        return large.capacity == 0;
    }
    
    void set_small_flag() {
        // For small strings, we store size in the last byte
        // and use capacity=0 to indicate small string
        large.capacity = 0;
    }

public:
    // Default constructor - creates empty small string
    GoTSString() noexcept {
        small.buffer[0] = '\0';
        small.size = 0;
        set_small_flag();
    }
    
    // Constructor from C string - optimized for literal strings
    GoTSString(const char* str) {
        if (!str) {
            *this = GoTSString();
            return;
        }
        
        size_t len = strlen(str);
        if (len <= SSO_THRESHOLD) {
            // Small string optimization
            memcpy(small.buffer, str, len);
            small.buffer[len] = '\0';
            small.size = static_cast<uint8_t>(len);
            set_small_flag();
        } else {
            // Large string - allocate on heap
            large.size = len;
            large.capacity = ((len + 16) & ~15) | 1; // Round up to 16-byte boundary, set odd flag
            large.data = new char[large.capacity & ~1]; // Mask off the flag bit
            memcpy(large.data, str, len + 1);
        }
    }
    
    // Copy constructor
    GoTSString(const GoTSString& other) {
        if (other.is_small()) {
            memcpy(&small, &other.small, sizeof(small));
        } else {
            large.size = other.large.size;
            large.capacity = other.large.capacity;
            size_t actual_capacity = large.capacity & ~1;
            large.data = new char[actual_capacity];
            memcpy(large.data, other.large.data, large.size + 1);
        }
    }
    
    // Move constructor
    GoTSString(GoTSString&& other) noexcept {
        memcpy(this, &other, sizeof(GoTSString));
        other.small.buffer[0] = '\0';
        other.small.size = 0;
        other.set_small_flag();
    }
    
    // Assignment operators
    GoTSString& operator=(const GoTSString& other) {
        if (this != &other) {
            this->~GoTSString();
            new (this) GoTSString(other);
        }
        return *this;
    }
    
    GoTSString& operator=(GoTSString&& other) noexcept {
        if (this != &other) {
            this->~GoTSString();
            memcpy(this, &other, sizeof(GoTSString));
            other.small.buffer[0] = '\0';
            other.small.size = 0;
            other.set_small_flag();
        }
        return *this;
    }
    
    // Destructor
    ~GoTSString() {
        if (!is_small()) {
            delete[] large.data;
        }
    }
    
    // Access methods - highly optimized
    inline const char* c_str() const {
        return is_small() ? small.buffer : large.data;
    }
    
    inline size_t size() const {
        return is_small() ? small.size : large.size;
    }
    
    inline size_t length() const {
        return size();
    }
    
    inline bool empty() const {
        return size() == 0;
    }
    
    inline char operator[](size_t index) const {
        return is_small() ? small.buffer[index] : large.data[index];
    }
    
    // Concatenation - extremely optimized
    GoTSString operator+(const GoTSString& other) const {
        size_t total_size = size() + other.size();
        GoTSString result;
        
        if (total_size <= SSO_THRESHOLD) {
            // Result fits in small string
            memcpy(result.small.buffer, c_str(), size());
            memcpy(result.small.buffer + size(), other.c_str(), other.size());
            result.small.buffer[total_size] = '\0';
            result.small.size = static_cast<uint8_t>(total_size);
            result.set_small_flag();
        } else {
            // Need large string
            result.large.size = total_size;
            result.large.capacity = ((total_size + 16) & ~15) | 1;
            result.large.data = new char[result.large.capacity & ~1];
            memcpy(result.large.data, c_str(), size());
            memcpy(result.large.data + size(), other.c_str(), other.size());
            result.large.data[total_size] = '\0';
        }
        
        return result;
    }
    
    // Comparison operators - optimized for JIT
    bool operator==(const GoTSString& other) const {
        size_t len = size();
        if (len != other.size()) return false;
        if (len == 0) return true;
        
        // For small strings, we can use optimized comparison
        if (is_small() && other.is_small()) {
            return memcmp(small.buffer, other.small.buffer, len) == 0;
        }
        
        return memcmp(c_str(), other.c_str(), len) == 0;
    }
    
    bool operator!=(const GoTSString& other) const {
        return !(*this == other);
    }
    
    bool operator<(const GoTSString& other) const {
        int cmp = strcmp(c_str(), other.c_str());
        return cmp < 0;
    }
    
    bool operator>(const GoTSString& other) const {
        return other < *this;
    }
    
    bool operator<=(const GoTSString& other) const {
        return !(other < *this);
    }
    
    bool operator>=(const GoTSString& other) const {
        return !(*this < other);
    }
};

// String interning system for literal optimization
class StringPool {
private:
    std::unordered_map<std::string, GoTSString*> interned_strings;
    std::mutex pool_mutex;
    
public:
    // Get or create an interned string
    GoTSString* intern(const char* str) {
        if (!str) return nullptr;
        
        std::lock_guard<std::mutex> lock(pool_mutex);
        
        std::string key(str);  // Create proper string key
        auto it = interned_strings.find(key);
        if (it != interned_strings.end()) {
            return it->second;
        }
        
        GoTSString* new_string = new GoTSString(str);
        interned_strings[key] = new_string;  // Use string key, not char* key
        return new_string;
    }
    
    // Cleanup - called on program exit
    ~StringPool() {
        for (auto& pair : interned_strings) {
            delete pair.second;
        }
    }
};

// Global string pool instance
extern StringPool global_string_pool;

// Legacy Array structure for backward compatibility
struct Array {
    int64_t* data;
    int64_t size;
    int64_t capacity;
    
    Array() : data(nullptr), size(0), capacity(0) {}
    
    Array(int64_t initial_capacity) : size(0), capacity(initial_capacity) {
        data = new int64_t[capacity];
    }
    
    ~Array() {
        if (data) delete[] data;
    }
    
    void push(int64_t value) {
        if (size >= capacity) {
            int64_t new_capacity = capacity == 0 ? 8 : capacity * 2;
            int64_t* new_data = new int64_t[new_capacity];
            for (int64_t i = 0; i < size; i++) {
                new_data[i] = data[i];
            }
            if (data) delete[] data;
            data = new_data;
            capacity = new_capacity;
        }
        data[size++] = value;
    }
    
    int64_t pop() {
        if (size > 0) {
            return data[--size];
        }
        return 0;
    }
};

struct Promise {
    std::atomic<bool> resolved{false};
    std::shared_ptr<void> value;
    std::vector<std::function<void()>> callbacks;
    std::mutex callback_mutex;
    
    template<typename T>
    void resolve(T&& val) {
        {
            std::lock_guard<std::mutex> lock(callback_mutex);
            using ValueType = typename std::remove_reference<T>::type;
            value = std::make_shared<ValueType>(std::forward<T>(val));
            resolved.store(true);
        }
        
        for (auto& callback : callbacks) {
            callback();
        }
    }
    
    template<typename T>
    T await() {
        while (!resolved.load()) {
            std::this_thread::yield();
        }
        using ValueType = typename std::remove_reference<T>::type;
        return *static_cast<ValueType*>(value.get());
    }
    
    void then(std::function<void()> callback) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        if (resolved.load()) {
            callback();
        } else {
            callbacks.push_back(callback);
        }
    }
};

class ThreadPool {
    
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop{false};
    
public:
    ThreadPool(size_t num_threads = std::thread::hardware_concurrency());
    ~ThreadPool();
    
    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;
    
    // Simple non-template enqueue for basic tasks
    void enqueue_simple(std::function<void()> task);
    
    void shutdown();
};

class GoroutineScheduler {
public:
    ThreadPool thread_pool;
private:
    std::unordered_map<std::thread::id, std::queue<std::function<void()>>> event_loops;
    std::mutex event_loop_mutex;
    std::atomic<uint64_t> next_goroutine_id{1};
    
    struct GoroutineContext {
        uint64_t id;
        std::shared_ptr<Promise> promise;
        std::function<void()> task;
        bool is_main_thread;
        std::shared_ptr<void> captured_scope;  // Lexical environment (void* to avoid circular dependency)
    };
    
    std::unordered_map<uint64_t, GoroutineContext> active_goroutines;
    std::mutex goroutine_mutex;
    
public:
    GoroutineScheduler();
    ~GoroutineScheduler();
    
    template<typename F, typename... Args>
    std::shared_ptr<Promise> spawn(F&& f, Args&&... args);
    
    std::shared_ptr<Promise> spawn_with_scope_impl(std::function<void()> task, std::shared_ptr<void> captured_scope);
    
    template<typename F>
    std::shared_ptr<Promise> spawn_with_scope(F&& f, std::shared_ptr<LexicalScope> captured_scope);
    
    void process_event_loop();
    void add_to_event_loop(std::function<void()> task);
    
    template<typename T>
    std::vector<T> promise_all(const std::vector<std::shared_ptr<Promise>>& promises);
    
    static GoroutineScheduler& instance();
};

class SharedMemory {
private:
    std::unordered_map<std::string, std::shared_ptr<void>> global_vars;
    std::unordered_map<std::string, std::mutex> var_mutexes;
    std::mutex memory_mutex;
    
public:
    template<typename T>
    void set_global(const std::string& name, T&& value);
    
    template<typename T>
    T get_global(const std::string& name);
    
    template<typename T>
    void atomic_update(const std::string& name, std::function<T(const T&)> updater);
};

// Object and class management structures
struct ObjectInstance {
    std::string class_name;
    std::unordered_map<std::string, int64_t> properties;  // Property name -> value
    int64_t* property_data;  // Raw memory for properties
    std::string* property_names;  // Property names for iteration
    int64_t property_count;
    
    ObjectInstance(const std::string& cls_name, int64_t prop_count) 
        : class_name(cls_name), property_count(prop_count) {
        if (prop_count > 0) {
            property_data = new int64_t[prop_count];
            property_names = new std::string[prop_count];
            memset(property_data, 0, prop_count * sizeof(int64_t));
        } else {
            property_data = nullptr;
            property_names = nullptr;
        }
    }
    
    ~ObjectInstance() {
        if (property_data) {
            delete[] property_data;
        }
        if (property_names) {
            delete[] property_names;
        }
    }
};

// Global object registry
extern std::unordered_map<int64_t, std::unique_ptr<ObjectInstance>> object_registry;
extern std::atomic<int64_t> next_object_id;

// Global function registry for goroutines
extern std::unordered_map<std::string, void*> gots_function_registry;

extern "C" {
    void* __goroutine_spawn(const char* function_name);
    void* __goroutine_spawn_with_arg1(const char* function_name, int64_t arg1);
    void* __goroutine_spawn_with_arg2(const char* function_name, int64_t arg1, int64_t arg2);
    void* __goroutine_spawn_with_args(const char* function_name, int64_t* args, int64_t arg_count);
    void* __goroutine_spawn_with_scope(const char* function_name, void* captured_scope);
    void* __goroutine_spawn_func_ptr(void* func_ptr, void* arg);
    void __register_function(const char* name, void* func_ptr);
    void __promise_resolve(void* promise, void* value);
    void* __promise_await(void* promise);
    void __runtime_init();
    void __runtime_cleanup();
    void __set_executable_memory(void* ptr, size_t size);
    
    // Main thread JIT execution queue for thread safety
    struct JITExecutionRequest {
        void* func_ptr;
        std::vector<int64_t> args;
        std::promise<int64_t> result_promise;
    };
    
    void __execute_jit_on_main_thread(void* func_ptr, const std::vector<int64_t>& args, std::promise<int64_t>& result_promise);
    void __process_jit_queue();
    
    // Console functions
    void __console_log(const char* message);
    void __console_log_newline();
    void __console_log_space();
    void __console_log_array(int64_t* array, int64_t size);
    void __console_log_number(int64_t value);
    void __console_log_auto(int64_t value);
    bool __is_array_pointer(int64_t value);
    void __console_time(const char* label);
    void __console_timeEnd(const char* label);
    
    // Promise functions
    void* __promise_all(void* promises_array);
    
    // Legacy Array functions
    void* __array_create(int64_t initial_capacity);
    void __array_push(void* array, int64_t value);
    int64_t __array_pop(void* array);
    int64_t __array_size(void* array);
    int64_t* __array_data(void* array);
    
    // Typed Array functions for maximum performance
    void* __typed_array_create_int32(int64_t initial_capacity);
    void* __typed_array_create_int64(int64_t initial_capacity);
    void* __typed_array_create_float32(int64_t initial_capacity);
    void* __typed_array_create_float64(int64_t initial_capacity);
    void* __typed_array_create_uint8(int64_t initial_capacity);
    void* __typed_array_create_uint16(int64_t initial_capacity);
    void* __typed_array_create_uint32(int64_t initial_capacity);
    void* __typed_array_create_uint64(int64_t initial_capacity);
    
    // Push operations for each type
    void __typed_array_push_int32(void* array, int32_t value);
    void __typed_array_push_int64(void* array, int64_t value);
    void __typed_array_push_float32(void* array, float value);
    void __typed_array_push_float64(void* array, double value);
    void __typed_array_push_uint8(void* array, uint8_t value);
    void __typed_array_push_uint16(void* array, uint16_t value);
    void __typed_array_push_uint32(void* array, uint32_t value);
    void __typed_array_push_uint64(void* array, uint64_t value);
    
    // Pop operations for each type
    int32_t __typed_array_pop_int32(void* array);
    int64_t __typed_array_pop_int64(void* array);
    float __typed_array_pop_float32(void* array);
    double __typed_array_pop_float64(void* array);
    uint8_t __typed_array_pop_uint8(void* array);
    uint16_t __typed_array_pop_uint16(void* array);
    uint32_t __typed_array_pop_uint32(void* array);
    uint64_t __typed_array_pop_uint64(void* array);
    
    // Array access operations for each type (direct indexing)
    int32_t __typed_array_get_int32(void* array, int64_t index);
    int64_t __typed_array_get_int64(void* array, int64_t index);
    float __typed_array_get_float32(void* array, int64_t index);
    double __typed_array_get_float64(void* array, int64_t index);
    uint8_t __typed_array_get_uint8(void* array, int64_t index);
    uint16_t __typed_array_get_uint16(void* array, int64_t index);
    uint32_t __typed_array_get_uint32(void* array, int64_t index);
    uint64_t __typed_array_get_uint64(void* array, int64_t index);
    
    void __typed_array_set_int32(void* array, int64_t index, int32_t value);
    void __typed_array_set_int64(void* array, int64_t index, int64_t value);
    void __typed_array_set_float32(void* array, int64_t index, float value);
    void __typed_array_set_float64(void* array, int64_t index, double value);
    void __typed_array_set_uint8(void* array, int64_t index, uint8_t value);
    void __typed_array_set_uint16(void* array, int64_t index, uint16_t value);
    void __typed_array_set_uint32(void* array, int64_t index, uint32_t value);
    void __typed_array_set_uint64(void* array, int64_t index, uint64_t value);
    
    // Size and data access
    int64_t __typed_array_size(void* array);
    void* __typed_array_raw_data(void* array);
    
    // Console logging for typed arrays
    void __console_log_typed_array_int32(void* array);
    void __console_log_typed_array_int64(void* array);
    void __console_log_typed_array_float32(void* array);
    void __console_log_typed_array_float64(void* array);
    
    // Object management functions
    int64_t __object_create(const char* class_name, int64_t property_count);
    void __object_set_property(int64_t object_id, int64_t property_index, int64_t value);
    int64_t __object_get_property(int64_t object_id, int64_t property_index);
    void __object_destroy(int64_t object_id);
    
    // Property name management for iteration
    void __object_set_property_name(int64_t object_id, int64_t property_index, const char* property_name);
    const char* __object_get_property_name(int64_t object_id, int64_t property_index);
    
    // Method calling
    int64_t __object_call_method(int64_t object_id, const char* method_name, int64_t* args, int64_t arg_count);
    
    // Static property management
    void __static_set_property(const char* class_name, const char* property_name, int64_t value);
    int64_t __static_get_property(const char* class_name, const char* property_name);
    
    // Inheritance support
    void __register_class_inheritance(const char* child_class, const char* parent_class);
    void __super_constructor_call(int64_t object_id, int64_t arg1, int64_t arg2, int64_t arg3, int64_t arg4, int64_t arg5);
    
    // Mathematical functions
    int64_t __runtime_pow(int64_t base, int64_t exponent);
    int64_t __runtime_js_equal(int64_t left_value, int64_t left_type, int64_t right_value, int64_t right_type);
    
    // High-Performance String Runtime Functions
    void* __string_create(const char* str);
    void* __string_create_empty();
    void __string_destroy(void* string_ptr);
    
    // String operations - extremely optimized
    void* __string_concat(void* str1, void* str2);
    void* __string_concat_cstr(void* str1, const char* str2);
    void* __string_concat_cstr_left(const char* str1, void* str2);
    
    // String comparison - JIT optimized
    bool __string_equals(void* str1, void* str2);
    bool __string_equals_cstr(void* str1, const char* str2);
    int64_t __string_compare(void* str1, void* str2);
    
    // String access
    int64_t __string_length(void* string_ptr);
    const char* __string_c_str(void* string_ptr);
    char __string_char_at(void* string_ptr, int64_t index);
    
    // String pool functions for literal optimization
    void* __string_intern(const char* str);
    void __string_pool_cleanup();
    
    // Console logging optimized for strings
    void __console_log_string(void* string_ptr);
}

template<typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;
    
    // Create a simple task wrapper instead of packaged_task
    auto promise = std::make_shared<std::promise<return_type>>();
    auto future = promise->get_future();
    
    // Fix: Create a bound function to avoid problematic variadic capture
    auto bound_func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    
    // Create a simpler task that uses std::bind instead of lambda capture
    auto simple_task = [promise, bound_func = std::move(bound_func)]() mutable {
        try {
            if constexpr (std::is_void_v<return_type>) {
                bound_func();
                promise->set_value();
            } else {
                auto result = bound_func();
                promise->set_value(result);
            }
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    };
    
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        
        if (stop.load()) {
            throw std::runtime_error("ThreadPool is stopped");
        }
        
        tasks.emplace(std::move(simple_task));
    }
    
    condition.notify_one();
    return future;
}

template<typename F, typename... Args>
std::shared_ptr<Promise> GoroutineScheduler::spawn(F&& f, Args&&... args) {
    auto promise = std::make_shared<Promise>();
    
    // Create a simple task that avoids std::bind and complex lambda captures
    std::function<void()> simple_task = [promise, f]() mutable {
        try {
            auto result = f();
            promise->resolve(result);
        } catch (...) {
            promise->resolve(static_cast<int64_t>(0));
        }
    };
    
    thread_pool.enqueue_simple(simple_task);
    return promise;
}

template<typename F>
std::shared_ptr<Promise> GoroutineScheduler::spawn_with_scope(F&& f, std::shared_ptr<LexicalScope> captured_scope) {
    auto task = [f = std::forward<F>(f)]() {
        f();
    };
    
    std::shared_ptr<void> scope_ptr = nullptr;
    if (captured_scope) {
        scope_ptr = std::static_pointer_cast<void>(captured_scope);
    }
    
    return spawn_with_scope_impl(task, scope_ptr);
}

// Template implementation moved to runtime.cpp to avoid circular dependency

template<typename T>
std::vector<T> GoroutineScheduler::promise_all(const std::vector<std::shared_ptr<Promise>>& promises) {
    std::vector<T> results;
    results.reserve(promises.size());
    
    for (const auto& promise : promises) {
        results.push_back(promise->await<T>());
    }
    
    return results;
}

template<typename T>
void SharedMemory::set_global(const std::string& name, T&& value) {
    std::lock_guard<std::mutex> lock(memory_mutex);
    global_vars[name] = std::make_shared<T>(std::forward<T>(value));
}

template<typename T>
T SharedMemory::get_global(const std::string& name) {
    std::lock_guard<std::mutex> lock(memory_mutex);
    auto it = global_vars.find(name);
    if (it != global_vars.end()) {
        return *static_cast<T*>(it->second.get());
    }
    return T{};
}

template<typename T>
void SharedMemory::atomic_update(const std::string& name, std::function<T(const T&)> updater) {
    std::lock_guard<std::mutex> lock(memory_mutex);
    std::lock_guard<std::mutex> var_lock(var_mutexes[name]);
    
    auto it = global_vars.find(name);
    if (it != global_vars.end()) {
        T current_value = *static_cast<T*>(it->second.get());
        T new_value = updater(current_value);
        it->second = std::make_shared<T>(new_value);
    }
}

}