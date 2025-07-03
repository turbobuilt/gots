function fib(n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

console.time("node");

const results = [];
for (let i = 0; i < 31; i++) {
    // intentional to call with same number every time to test performance better
    results.push(fib(31));
}

console.timeEnd("node");
console.log(results);
