function fib(n: int64) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

console.time("multiecma-sequential");

const results = [];

// Calculate fibonacci(31) recursively 31 times sequentially
for (let iter: int64 = 0; iter < 31; iter++) {
    let result = fib(31);
    results.push(result);
}

console.timeEnd("multiecma-sequential");
console.log("RESULTS ARE");
console.log(results);