function test(x) {
    console.log("INSIDE FUNCTION test, x=", x);
    return x;
}
console.log("BEFORE CALL");
test(5);
console.log("AFTER CALL");