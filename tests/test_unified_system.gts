// Test unified event system with proper goroutine lifecycle management

console.log("=== UNIFIED EVENT SYSTEM TEST ===")

// Test 1: Basic goroutine with timer
console.log("Test 1: Basic goroutine with timer")
go function() {
    console.log("Goroutine 1 started")
    
    setTimeout(function() {
        console.log("Goroutine 1 timer callback executed")
    }, 100)
    
    console.log("Goroutine 1 main task completed")
}

// Test 2: Nested goroutines with shared scope
console.log("Test 2: Nested goroutines with shared scope")
let sharedVar = 42
go function() {
    console.log("Parent goroutine, sharedVar:", sharedVar)
    
    go function() {
        console.log("Child goroutine, sharedVar:", sharedVar)
        sharedVar = 100
        console.log("Child goroutine modified sharedVar to:", sharedVar)
    }
    
    setTimeout(function() {
        console.log("Parent goroutine timer, sharedVar:", sharedVar)
    }, 200)
}

// Test 3: Multiple timers
console.log("Test 3: Multiple timers")
go function() {
    console.log("Timer goroutine started")
    
    setTimeout(function() {
        console.log("Timer 1 fired (50ms)")
    }, 50)
    
    setTimeout(function() {
        console.log("Timer 2 fired (150ms)")
    }, 150)
    
    setTimeout(function() {
        console.log("Timer 3 fired (250ms)")
    }, 250)
}

// Test 4: Main thread should wait for all goroutines
console.log("Test 4: Main thread waiting test")
setTimeout(function() {
    console.log("Main thread timer - should execute before exit")
}, 300)

console.log("Main function completed - but should wait for all goroutines")