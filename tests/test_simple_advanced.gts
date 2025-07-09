// Simple test for advanced goroutine features

// Test 1: Basic shared memory
go function() {
    let shared = __goroutine_alloc_shared(1024)
    console.log("Allocated shared memory")
    
    setTimeout(function() {
        console.log("Releasing shared memory")
        __goroutine_release_shared(shared)
    }, 100)
}

// Test 2: Basic channel communication
let channel = __channel_create(8, 5)

go function() {
    __channel_send_int64(channel, 42)
    console.log("Sent value to channel")
    __channel_close(channel)
}

go function() {
    let value = 0
    if __channel_receive_int64(channel, value) {
        console.log("Received value:", value)
    }
}

// Test 3: Print stats
setTimeout(function() {
    console.log("Test completed")
    __print_scheduler_stats()
}, 500)