function test(x) {
    let result = x * 2;
    console.log("about to return", result);
    return result;
}
console.log("calling function");
test(10);