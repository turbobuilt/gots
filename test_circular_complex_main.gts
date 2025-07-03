// Main test for complex circular imports (A->C->B->A)
import { funcA, callC, valueA } from "./test_circular_complex_a";
import { funcB, callA, valueB } from "./test_circular_complex_b";
import { funcC, callB, valueC } from "./test_circular_complex_c";

console.log("Testing complex circular imports (A->C->B->A)...");

// Test basic function calls
console.log("Direct calls:");
console.log("funcA():", funcA());
console.log("funcB():", funcB());
console.log("funcC():", funcC());

// Test cross-calls through the circular chain
console.log("\nComplex cross calls:");
console.log("callC() from A:", callC());
console.log("callA() from B:", callA());
console.log("callB() from C:", callB());

// Test value imports
console.log("\nValues:");
console.log("valueA:", valueA);
console.log("valueB:", valueB);
console.log("valueC:", valueC);

console.log("\nComplex circular import test completed successfully!");