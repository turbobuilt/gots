// Switch Performance Demo
console.log("=== Switch Performance Demo ===");

// Test 1: Fast path demonstration
console.log("\n1. FAST PATH: Typed int64 switch");
var x: int64 = 2;
switch (x) {
    case 1:
        console.log("One");
        break;
    case 2:
        console.log("Two (fast path - direct comparison)");
        break;
    case 3:
        console.log("Three");
        break;
}

// Test 2: Slow path demonstration  
console.log("\n2. SLOW PATH: ANY variable with mixed types");
var y = 2;  // ANY type
switch (y) {
    case 1:
        console.log("One");
        break;
    case "string":  // String case forces slow path
        console.log("String");
        break;
    case 2:
        console.log("Two (slow path - type coercion)");
        break;
}

console.log("\n✓ Both cases work correctly!");
console.log("✓ Fast path: Same types = direct comparison");
console.log("✓ Slow path: Mixed types = JavaScript-style equality");