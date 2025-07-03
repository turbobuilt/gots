// Test file A for circular import testing
import { funcB } from "./test_circular_b";

export function funcA() {
    console.log("Function A called");
    return "A";
}

export function callB() {
    console.log("A is calling B");
    return funcB();
}

export const valueA = "from A";