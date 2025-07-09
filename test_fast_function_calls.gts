// Test the new fast function call optimizations
console.log("Testing fast function calls");

function testFunction() {
    console.log("Fast function called!");
    return 42;
}

// Test basic function call
let result = testFunction();
console.log("Result:", result);

// Test fast goroutine spawn (if implemented in compiler)
go function() {
    console.log("Fast goroutine executed");
}

console.log("Fast function call test complete");