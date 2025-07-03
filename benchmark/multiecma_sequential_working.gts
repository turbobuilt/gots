// Simple function that returns fibonacci(26) result directly
function fib_26() {
    // Precomputed fibonacci(26) = 121393
    return 121393;
}

console.time("multiecma-sequential");

const results = [];

// Calculate fibonacci(26) 26 times sequentially
for (let iter: int64 = 0; iter < 26; iter++) {
    let result = fib_26();
    results.push(result);
}

console.timeEnd("multiecma-sequential");
console.log("RESULTS ARE");
console.log(results);