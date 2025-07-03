// Simple test to debug threading issues
function simple_test() {
    return 42;
}

// Test calling function directly
console.log("Direct call result:", simple_test());

// Test calling function from goroutine 
const result = await go simple_test();
console.log("Goroutine result:", result);