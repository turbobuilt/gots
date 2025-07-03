// Main test file to test circular imports
import { funcA, callB, valueA } from "./test_circular_a";
import { funcB, callA, valueB } from "./test_circular_b";

console.log("Testing circular imports...");

// Test basic function calls
console.log("Direct calls:");
console.log("funcA():", funcA());
console.log("funcB():", funcB());

// Test cross-calls (these should work with proper circular import handling)
console.log("\nCross calls:");
console.log("callB() from A:", callB());
console.log("callA() from B:", callA());

// Test value imports
console.log("\nValues:");
console.log("valueA:", valueA);
console.log("valueB:", valueB);

console.log("\nCircular import test completed successfully!");