function fib(n: int64) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

go fib(5);
console.log("done");
