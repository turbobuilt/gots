#include "runtime.h"
#include "goroutine_advanced.h"
#include "goroutine_system.h"
#include <iostream>

namespace gots {

// C interface for advanced goroutine features

// Initialize advanced goroutine system
void __init_advanced_goroutine_system() {
    initialize_advanced_goroutine_system();
}

// Shutdown advanced goroutine system
void __shutdown_advanced_goroutine_system() {
    shutdown_advanced_goroutine_system();
}

// Allocate shared memory
void* __goroutine_alloc_shared(int64_t size) {
    // Use global pool directly for now
    return g_shared_memory_pool.allocate(size);
}

// Share memory with another goroutine
void __goroutine_share_memory(void* ptr, int64_t target_id) {
    if (!ptr) return;
    
    g_shared_memory_pool.add_ref(ptr);
    std::cout << "DEBUG: Shared memory " << ptr << " with goroutine " << target_id << std::endl;
}

// Release shared memory
void __goroutine_release_shared(void* ptr) {
    if (ptr) {
        g_shared_memory_pool.release(ptr);
    }
}

// Create a channel (returns opaque handle)
void* __channel_create(int64_t element_size, int64_t capacity) {
    // For now, only support int64_t channels
    auto channel = new Channel<int64_t>(capacity);
    std::cout << "DEBUG: Created channel with capacity " << capacity << std::endl;
    return channel;
}

// Send to channel
bool __channel_send_int64(void* channel_ptr, int64_t value) {
    if (!channel_ptr) return false;
    
    auto channel = static_cast<Channel<int64_t>*>(channel_ptr);
    bool result = channel->send(value);
    
    if (result) {
        std::cout << "DEBUG: Sent value " << value << " to channel" << std::endl;
    }
    
    return result;
}

// Receive from channel
bool __channel_receive_int64(void* channel_ptr, int64_t* value) {
    if (!channel_ptr || !value) return false;
    
    auto channel = static_cast<Channel<int64_t>*>(channel_ptr);
    bool result = channel->receive(*value);
    
    if (result) {
        std::cout << "DEBUG: Received value " << *value << " from channel" << std::endl;
    }
    
    return result;
}

// Try receive (non-blocking)
bool __channel_try_receive_int64(void* channel_ptr, int64_t* value) {
    if (!channel_ptr || !value) return false;
    
    auto channel = static_cast<Channel<int64_t>*>(channel_ptr);
    return channel->try_receive(*value);
}

// Close channel
void __channel_close(void* channel_ptr) {
    if (!channel_ptr) return;
    
    auto channel = static_cast<Channel<int64_t>*>(channel_ptr);
    channel->close();
    std::cout << "DEBUG: Channel closed" << std::endl;
}

// Delete channel
void __channel_delete(void* channel_ptr) {
    if (!channel_ptr) return;
    
    auto channel = static_cast<Channel<int64_t>*>(channel_ptr);
    delete channel;
}

// Get work stealing stats
void __print_scheduler_stats() {
    // Would need to expose stats from the global scheduler
    std::cout << "DEBUG: Work-stealing scheduler statistics:" << std::endl;
    std::cout << "  - Shared memory allocations: " << g_shared_memory_pool.get_allocation_count() << std::endl;
    std::cout << "  - Total shared memory: " << g_shared_memory_pool.get_total_memory() << " bytes" << std::endl;
}

} // namespace gots