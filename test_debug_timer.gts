console.log("Test 1: Basic console.log");

function namedFunction() {
    console.log("Named function called");
}

console.log("Test 2: About to call setTimeout");
runtime.timer.setTimeout(namedFunction, 100);
console.log("Test 3: setTimeout called");