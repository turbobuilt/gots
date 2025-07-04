function test(x) {
    console.log("got:", x);
    return x;
}
let msg = "hello " + 42;  
console.log("msg:", msg);
let result = test(999);   // Use simple literal, not expression
console.log("result:", result);
