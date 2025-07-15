function testStringParam(s: string) {
    console.log("String param is: ", s)
}

function test() {
    console.log("=== Testing normal string parameter ===")
    testStringParam("hello world")
    console.log("Test completed successfully");
}

test();