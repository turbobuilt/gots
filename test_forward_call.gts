// Test calling a function before importing it

console.log("Testing forward function call...")

// Try to call add before importing - this should fail
let result = add(10, 5)

import { add } from "./math.gts"

console.log("Result:", result)