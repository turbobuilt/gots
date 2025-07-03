function fib(n: int64) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

let result = fib(26);
console.log("fib(26) =", result);