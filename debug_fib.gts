function fib(n: int64) {
    console.log("fib called with n =", n);
    if (n <= 1) {
        console.log("base case, returning", n);
        return n;
    }
    console.log("recursive case for n =", n);
    let a = fib(n - 1);
    console.log("fib(n-1) returned", a);
    let b = fib(n - 2);
    console.log("fib(n-2) returned", b);
    let result = a + b;
    console.log("returning", result);
    return result;
}

console.log("Testing fib(3)");
let result = fib(3);
console.log("Final result:", result);
