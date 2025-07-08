// Test that setTimeout callbacks work correctly
console.log("Starting timer test")

go function() {
    console.log("Goroutine started")
    
    setTimeout(function() {
        console.log("Timer 1 executed")
    }, 500)
    
    setTimeout(function() {
        console.log("Timer 2 executed")
    }, 1000)
    
    setTimeout(function() {
        console.log("Timer 3 executed")
    }, 1500)
}

console.log("Timer test setup complete")