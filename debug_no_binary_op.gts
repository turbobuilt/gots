function test(x) {
    console.log("about to return", x);
    return x;  // No binary operation, just return the argument
}
console.log("calling function");
test(10);