function test(x: int64) {
    console.log("Received parameter x =", x);
    return x;
}

console.log("Calling test(42)");
let result = test(42);
console.log("test returned:", result);
