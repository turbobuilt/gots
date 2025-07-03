// Complex circular import test - Module C
import { funcB } from "./test_circular_complex_b";
import { valueA } from "./test_circular_complex_a";

export function funcC() {
    console.log("Complex C called");
    return "C-" + valueA;
}

export function callB() {
    return funcB();
}

export const valueC = "complex-C";