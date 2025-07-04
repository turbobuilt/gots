// Test with circular imports like in the original crash
import { add } from "./math";
import { log } from "./utils";

console.log("Testing circular imports...");
console.log("About to call add(10, 5)...");
let sum = add(10, 5);
console.log("add(10, 5) =", sum);