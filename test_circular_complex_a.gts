// Complex circular import test - Module A
import { funcC } from "./test_circular_complex_c";
import { valueB } from "./test_circular_complex_b";

export function funcA() {
    console.log("Complex A called");
    return "A-" + valueB;
}

export function callC() {
    return funcC();
}

export const valueA = "complex-A";