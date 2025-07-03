// Test various import scenarios
console.log("=== GoTS Import System Test ===");

// Test 1: Named imports from module with only named exports (should create synthetic default)
import { add, multiply, PI } from "./math_utils";

console.log("Named imports test:");
console.log("add(5, 3) =", add(5, 3));
console.log("multiply(4, 7) =", multiply(4, 7));
console.log("PI =", PI);

// Test 2: Namespace import (import all as object)
import * as MathUtils from "./math_utils";

console.log("Namespace import test:");
console.log("MathUtils should contain all exports");

// Test 3: Default import from module with default export
import config from "./constants";

console.log("Default import test:");
console.log("config =", config);

// Test 4: Mixed import (default + named) - would be: import config, { add } from "./module"
// For now, testing with renamed import
import { add as addNumbers } from "./math_utils";

console.log("Renamed import test:");
console.log("addNumbers(10, 20) =", addNumbers(10, 20));

console.log("✓ Import system test complete!");