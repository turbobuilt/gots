// Benchmark to prove fast path is faster when typed
// This will demonstrate the performance difference between typed and untyped switch statements

console.log("=== Switch Statement Performance Benchmark ===");

// Test 1: Fast path - typed int64 switch
console.log("\n1. Testing FAST PATH (typed int64):");
console.time("typed_int64_switch");

for (let i: int64 = 0; i < 1000000; i++) {
    var x: int64 = i % 4;  // Will be 0, 1, 2, or 3
    switch (x) {
        case 0:
            // Do nothing
            break;
        case 1:
            // Do nothing
            break;
        case 2:
            // Do nothing
            break;
        case 3:
            // Do nothing
            break;
    }
}

console.timeEnd("typed_int64_switch");

// Test 2: Slow path - ANY variable with mixed type cases
console.log("\n2. Testing SLOW PATH (ANY variable with mixed types):");
console.time("any_mixed_switch");

for (let i: int64 = 0; i < 1000000; i++) {
    var y = i % 4;  // Inferred as ANY, will be 0, 1, 2, or 3
    switch (y) {
        case 0:
            // Do nothing
            break;
        case "1":  // String case - forces slow path comparison
            // Do nothing
            break;
        case 2:
            // Do nothing
            break;
        case 3:
            // Do nothing
            break;
    }
}

console.timeEnd("any_mixed_switch");

// Test 3: Fast path - typed string switch
console.log("\n3. Testing FAST PATH (typed string):");
console.time("typed_string_switch");

for (let i: int64 = 0; i < 1000000; i++) {
    var z: string = (i % 4 == 0) ? "zero" : 
                   (i % 4 == 1) ? "one" : 
                   (i % 4 == 2) ? "two" : "three";
    switch (z) {
        case "zero":
            // Do nothing
            break;
        case "one":
            // Do nothing
            break;
        case "two":
            // Do nothing
            break;
        case "three":
            // Do nothing
            break;
    }
}

console.timeEnd("typed_string_switch");

// Test 4: Slow path - ANY string variable with mixed cases
console.log("\n4. Testing SLOW PATH (ANY string with mixed cases):");
console.time("any_string_mixed_switch");

for (let i: int64 = 0; i < 1000000; i++) {
    var w = (i % 4 == 0) ? "zero" : 
           (i % 4 == 1) ? "one" : 
           (i % 4 == 2) ? "two" : "three";  // Inferred as ANY
    switch (w) {
        case 0:  // Number case - forces slow path comparison
            // Do nothing
            break;
        case "one":
            // Do nothing
            break;
        case "two":
            // Do nothing
            break;
        case "three":
            // Do nothing
            break;
    }
}

console.timeEnd("any_string_mixed_switch");

console.log("\n=== Performance Summary ===");
console.log("Fast paths should be significantly faster than slow paths");
console.log("Fast paths use direct memory comparison");
console.log("Slow paths use JavaScript-style type coercion");