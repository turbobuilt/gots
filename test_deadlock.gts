// Simple test to trigger the deadlock error
console.log("Starting deadlock test")

go function() {
    console.log("Child goroutine started")
    
    setTimeout(function() {
        console.log("Timer callback executed")
    }, 100)
    
    console.log("Child goroutine finished")
}

console.log("Main execution finished")