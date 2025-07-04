function test(x) {
    console.log("x is:", x);
    let result = x * 2;
    console.log("result is:", result);
    return result;
}
console.log("BEFORE CALL");
let val = test(5);
console.log("AFTER CALL, val=", val);