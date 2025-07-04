// Test nested imports and multiple function calls
import { add, multiply } from "./math.gts"
import { calculate } from "./calc.gts"

console.log("Testing nested imports...")

let a = add(10, 5)
console.log("add(10, 5) =", a)

let b = multiply(3, 7)
console.log("multiply(3, 7) =", b)

let c = calculate(a, b)
console.log("calculate(", a, ",", b, ") =", c)

console.log("All tests passed!")