// Simple benchmark to prove fast path is faster when typed
console.log("=== Switch Performance Benchmark (Simple) ===");

// Test 1: Fast path - typed int64 switch (smaller loop)
console.log("\n1. FAST PATH (typed int64) - 10,000 iterations:");
console.time("fast_typed");

for (let i: int64 = 0; i < 10000; i++) {
    var x: int64 = 2;  // Always 2 for consistent testing
    switch (x) {
        case 1:
            break;
        case 2:
            break;
        case 3:
            break;
    }
}

console.timeEnd("fast_typed");

// Test 2: Slow path - ANY variable with mixed types
console.log("\n2. SLOW PATH (ANY with mixed types) - 10,000 iterations:");
console.time("slow_any");

for (let i: int64 = 0; i < 10000; i++) {
    var y = 2;  // ANY type, value 2
    switch (y) {
        case 1:
            break;
        case "hello":  // String case forces slow path
            break;
        case 2:
            break;
        case 3:
            break;
    }
}

console.timeEnd("slow_any");

// Test 3: Direct comparison for reference
console.log("\n3. REFERENCE (direct comparison) - 10,000 iterations:");
console.time("direct_compare");

for (let i: int64 = 0; i < 10000; i++) {
    var z: int64 = 2;
    if (z == 2) {
        // Do nothing
    }
}

console.timeEnd("direct_compare");

console.log("\n=== Results ===");
console.log("Fast typed switch should be close to direct comparison");
console.log("Slow ANY switch should be noticeably slower");