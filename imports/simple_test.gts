// simple_test.gts - Minimal test to show circular imports working
console.log("Testing circular imports...");

import { add } from "./math";
import { log } from "./utils";

console.log("Imported successfully!");
log("System is working");

let result = add(2, 3);
console.log("2 + 3 =", result);