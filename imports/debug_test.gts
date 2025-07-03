// Debug test to see module loading in action
console.log("Starting debug test...");

// This should trigger the circular import detection
import { add } from "./math";  // math.gts imports from utils.gts
console.log("Math import completed");

import { log } from "./utils"; // utils.gts imports from math.gts  
console.log("Utils import completed");

console.log("All imports successful!");