// Test advanced goroutine features: shared memory, channels, work stealing

// Test 1: Shared memory between goroutines
go function() {
    // Allocate shared memory in first goroutine
    let shared = __goroutine_alloc_shared(1024)
    console.log("Goroutine 1: Allocated shared memory")
    
    // Write some data
    // In real implementation, we'd have typed access
    
    // Spawn another goroutine that accesses the same memory
    go function() {
        console.log("Goroutine 2: Accessing shared memory")
        // Read from shared memory
        
        // Release when done
        __goroutine_release_shared(shared)
    }
    
    // Simulate some work
    setTimeout(function() {
        console.log("Goroutine 1: Releasing shared memory")
        __goroutine_release_shared(shared)
    }, 100)
}

// Test 2: Channel communication
let channel = __channel_create(8, 10) // int64 channel with capacity 10

// Producer goroutine
go function() {
    for let i = 0; i < 5; i++ {
        __channel_send_int64(channel, i)
        console.log("Producer sent:", i)
    }
    __channel_close(channel)
}

// Consumer goroutine
go function() {
    let value = 0
    for let i = 0; i < 10; i++ {
        if __channel_receive_int64(channel, value) {
            console.log("Consumer received:", value)
        } else {
            console.log("Channel closed, consumer done")
            break
        }
    }
}

// Test 3: Work stealing with many goroutines
for let i = 0; i < 10; i++ {
    go function() {
        console.log("Work stealing goroutine", i, "started")
        
        // Simulate work
        let sum = 0
        for let j = 0; j < 1000000; j++ {
            sum += j
        }
        
        console.log("Work stealing goroutine", i, "completed with sum:", sum)
    }
}

// Give time for all goroutines to complete
setTimeout(function() {
    console.log("All tests completed")
    __print_scheduler_stats()
}, 2000)