// utils.gts - Utilities that import from math (creating circular dependency)
import { PI } from "./math";

export function log(message) {
    console.log("[LOG] " + message);
}

export function circle_area(radius) {
    log("Utils: Calculating circle area with radius " + radius);
    return PI * radius * radius;
}