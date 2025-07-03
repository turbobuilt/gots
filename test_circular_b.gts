// Test file B for circular import testing
import { funcA, valueA } from "./test_circular_a";

export function funcB() {
    console.log("Function B called");
    console.log("Value from A:", valueA);
    return "B";
}

export function callA() {
    console.log("B is calling A");
    return funcA();
}

export const valueB = "from B";