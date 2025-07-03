// math.gts - Basic math utilities that will import from utils
import { log } from "./utils";

export function add(a, b) {
    log("Math: Adding " + a + " + " + b);
    return a + b;
}

export function multiply(a, b) {
    log("Math: Multiplying " + a + " * " + b);
    return a * b;
}

export const PI = 3.14159;