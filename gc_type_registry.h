#pragma once

#include <atomic>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <cstdint>
#include <functional>

namespace gots {

// ============================================================================
// TYPE INFORMATION - For GC object traversal
// ============================================================================

struct TypeInfo {
    uint32_t type_id;
    size_t size;
    void* vtable;
    std::vector<size_t> ref_offsets;  // Offsets of reference fields
    std::function<void(void*)> finalizer;  // Optional finalizer
    bool is_array;
    bool has_weak_refs;
    
    // For array types
    size_t element_size;
    bool elements_are_refs;
};

// ============================================================================
// TYPE REGISTRY - Central registry for all types
// ============================================================================

class TypeRegistry {
private:
    std::unordered_map<uint32_t, TypeInfo> types_;
    mutable std::shared_mutex mutex_;
    std::atomic<uint32_t> next_type_id_{1};
    
    // Type ID cache for common types
    struct CommonTypes {
        uint32_t string_type;
        uint32_t array_type;
        uint32_t object_type;
        uint32_t closure_type;
        uint32_t promise_type;
        uint32_t goroutine_type;
    } common_types_;
    
public:
    TypeRegistry() = default;
    ~TypeRegistry() = default;
    
    // Register a new type
    uint32_t register_type(const TypeInfo& info) {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        uint32_t id = info.type_id > 0 ? info.type_id : next_type_id_.fetch_add(1);
        types_[id] = info;
        types_[id].type_id = id;
        return id;
    }
    
    // Get type information
    const TypeInfo* get_type(uint32_t type_id) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = types_.find(type_id);
        return it != types_.end() ? &it->second : nullptr;
    }
    
    // Register common types
    void register_common_types() {
        // String type
        TypeInfo string_info;
        string_info.size = sizeof(void*) + sizeof(size_t);  // ptr + length
        string_info.ref_offsets = {};  // No GC refs in string
        string_info.is_array = false;
        common_types_.string_type = register_type(string_info);
        
        // Array type (generic)
        TypeInfo array_info;
        array_info.size = sizeof(void*) + sizeof(size_t) * 2;  // ptr + length + capacity
        array_info.is_array = true;
        array_info.elements_are_refs = true;  // Conservative
        common_types_.array_type = register_type(array_info);
        
        // Object type (generic)
        TypeInfo object_info;
        object_info.size = sizeof(void*) * 4;  // Placeholder
        object_info.is_array = false;
        common_types_.object_type = register_type(object_info);
        
        // Closure type
        TypeInfo closure_info;
        closure_info.size = sizeof(void*) * 3;  // function + env + captures
        closure_info.ref_offsets = {sizeof(void*), sizeof(void*) * 2};  // env and captures are refs
        common_types_.closure_type = register_type(closure_info);
        
        // Promise type
        TypeInfo promise_info;
        promise_info.size = sizeof(void*) * 4;  // state + value + callbacks + next
        promise_info.ref_offsets = {sizeof(void*), sizeof(void*) * 2, sizeof(void*) * 3};
        common_types_.promise_type = register_type(promise_info);
        
        // Goroutine type
        TypeInfo goroutine_info;
        goroutine_info.size = sizeof(void*) * 8;  // Complex structure
        goroutine_info.ref_offsets = {0, sizeof(void*), sizeof(void*) * 2};  // Various refs
        common_types_.goroutine_type = register_type(goroutine_info);
    }
    
    // Get common type IDs
    uint32_t get_string_type() const { return common_types_.string_type; }
    uint32_t get_array_type() const { return common_types_.array_type; }
    uint32_t get_object_type() const { return common_types_.object_type; }
    uint32_t get_closure_type() const { return common_types_.closure_type; }
    uint32_t get_promise_type() const { return common_types_.promise_type; }
    uint32_t get_goroutine_type() const { return common_types_.goroutine_type; }
    
    // Helper for array types
    uint32_t register_array_type(size_t element_size, bool elements_are_refs) {
        TypeInfo info;
        info.is_array = true;
        info.element_size = element_size;
        info.elements_are_refs = elements_are_refs;
        info.size = sizeof(size_t) + sizeof(void*);  // length + data ptr
        return register_type(info);
    }
    
    // Clear all types (for shutdown)
    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        types_.clear();
    }
};

// ============================================================================
// OBJECT LAYOUT HELPERS
// ============================================================================

// Get array length from object
inline size_t get_array_length(void* array_obj) {
    return *reinterpret_cast<size_t*>(array_obj);
}

// Get array data pointer
inline void* get_array_data(void* array_obj) {
    return reinterpret_cast<void**>(static_cast<uint8_t*>(array_obj) + sizeof(size_t));
}

// Iterate over array elements that are references
template<typename Callback>
inline void iterate_array_refs(void* array_obj, const TypeInfo* type_info, Callback callback) {
    if (!type_info->is_array || !type_info->elements_are_refs) return;
    
    size_t length = get_array_length(array_obj);
    void** elements = reinterpret_cast<void**>(get_array_data(array_obj));
    
    for (size_t i = 0; i < length; ++i) {
        if (elements[i]) {
            callback(elements[i]);
        }
    }
}

// Iterate over object references
template<typename Callback>
inline void iterate_object_refs(void* obj, const TypeInfo* type_info, Callback callback) {
    uint8_t* obj_bytes = static_cast<uint8_t*>(obj);
    
    for (size_t offset : type_info->ref_offsets) {
        void** ref_ptr = reinterpret_cast<void**>(obj_bytes + offset);
        if (*ref_ptr) {
            callback(*ref_ptr);
        }
    }
}

// Combined iterator for any object
template<typename Callback>
inline void iterate_refs(void* obj, const TypeInfo* type_info, Callback callback) {
    if (!type_info) return;
    
    if (type_info->is_array) {
        iterate_array_refs(obj, type_info, callback);
    } else {
        iterate_object_refs(obj, type_info, callback);
    }
}

} // namespace gots