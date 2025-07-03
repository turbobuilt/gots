// Complex circular import test - Module B
import { funcA } from "./test_circular_complex_a";
import { valueC } from "./test_circular_complex_c";

export function funcB() {
    console.log("Complex B called");
    return "B-" + valueC;
}

export function callA() {
    return funcA();
}

export const valueB = "complex-B";