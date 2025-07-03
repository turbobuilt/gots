// Debug the switch loop issue step by step
console.log("Starting debug test");

// Test 1: Simple for loop
console.log("Test 1: Simple for loop");
for (let i: int64 = 0; i < 5; i++) {
    console.log("Loop iteration", i);
}

console.log("Test 2: For loop with local variable");
// Test 2: For loop with local variable assignment  
for (let i: int64 = 0; i < 3; i++) {
    var x: int64 = i % 2;
    console.log("x =", x);
}

console.log("Test 3: For loop with simple switch");
// Test 3: For loop with simple switch
for (let i: int64 = 0; i < 3; i++) {
    var x: int64 = i % 2;
    switch (x) {
        case 0:
            console.log("Even");
            break;
        case 1:
            console.log("Odd");
            break;
    }
}

console.log("All debug tests complete");