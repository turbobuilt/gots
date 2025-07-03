function fib(n: int64) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

const promises = [];
promises.push(go fib(10));
console.log("done");
