function test(x) {
    return 99;  // ignore the argument completely
}
console.log("before call");
let result = test(10);
console.log("after call, result:", result);