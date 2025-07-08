function test1() {
    console.log("Test 1");
}

function test2() {
    console.log("Test 2");
}

setTimeout(function() {
    test1();
    test2();
}, 1000);