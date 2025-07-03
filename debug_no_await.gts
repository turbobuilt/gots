function fib(n: int64) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

console.log("Testing normal function call");
const result = fib(10);
console.log("Result:", result);