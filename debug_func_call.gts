function fib(n: int64) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}
var result = fib(5);
console.log("done");
