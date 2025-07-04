// Reproducing exact context from main.gts
console.log("=== GoTS Circular Import Demo ===");
console.log("\n1. Testing simple circular imports (math <-> utils):");
import { add, multiply, PI } from "./math";
import { circle_area, log } from "./utils";

console.log("PI value:", PI);
let sum = add(10, 5);