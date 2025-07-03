function fib(n: int64) {
    console.log("fib called with", n);
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

console.log("Testing direct call...");
var result = fib(5);
console.log("Direct call result:", result);

console.log("Done");