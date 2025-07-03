// Debug power parsing
function testPowerParsing() {
    console.log("Testing basic arithmetic first");
    
    var a = 2;
    var b = 3;
    var result = a * b;  // Should work
    console.log("2 * 3 =");
    console.log(result);
    
    // Now try with constant power
    console.log("Now testing power with constants");
    var power_result = 2;
    console.log("Before power operation");
    power_result = 2 ** 3;  // This might be causing issues
    console.log("After power operation");
    console.log(power_result);
    
    return result;
}

testPowerParsing();
console.log("Done!");