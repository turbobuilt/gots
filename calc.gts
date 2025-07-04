// Calculator module that imports from math module
import { add, multiply } from "./math.gts"

export function calculate(x: int64, y: int64): int64 {
    // Do some complex calculation using imported functions
    let sum = add(x, y)
    let product = multiply(x, y)
    return add(sum, product)
}