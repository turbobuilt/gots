// main.gts - Main demo file that tests all circular import scenarios
console.log("=== GoTS Circular Import Demo ===");

// Test 1: Simple circular imports (math <-> utils)
console.log("\n1. Testing simple circular imports (math <-> utils):");
import { add, multiply, PI } from "./math";
import { circle_area, log } from "./utils";

console.log("PI value:", PI);
let sum = add(10, 5);
console.log("10 + 5 =", sum);

let area = circle_area(3);
console.log("Circle area (radius 3):", area);

// Test 2: Calculator using both modules
console.log("\n2. Testing calculator with multiple imports:");
import { advanced_calc } from "./calculator";
let result = advanced_calc();
console.log("Advanced calculation result:", result);

// Test 3: Complex circular imports (A->B->C->A)
console.log("\n3. Testing complex circular imports (A->B->C->A):");
import { func_a, call_b, value_a } from "./complex_a";
import { func_b, call_c, value_b } from "./complex_b";
import { func_c, call_a, value_c } from "./complex_c";

console.log("Values from all modules:");
console.log("value_a:", value_a);
console.log("value_b:", value_b);
console.log("value_c:", value_c);

console.log("\nTesting function calls:");
let result_a = func_a();
console.log("func_a result:", result_a);

let result_b = func_b();
console.log("func_b result:", result_b);

let result_c = func_c();
console.log("func_c result:", result_c);

console.log("\nTesting cross-calls:");
let cross_b = call_b();
console.log("call_b result:", cross_b);

let cross_c = call_c();
console.log("call_c result:", cross_c);

let cross_a = call_a();
console.log("call_a result:", cross_a);

console.log("\n=== Demo completed successfully! ===");