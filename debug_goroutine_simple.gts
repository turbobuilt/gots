function fib(n: int64) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

console.log("Testing single goroutine");
const result = await go fib(10);
console.log("Result:", result);