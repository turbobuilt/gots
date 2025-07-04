// Minimal test for imported function call
import { add } from "./math";

console.log("About to call add(10, 5)...");
let sum = add(10, 5);
console.log("Result:", sum);