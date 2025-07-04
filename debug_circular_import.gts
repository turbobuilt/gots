import { add } from "./math";
import { log } from "./utils";

console.log("Testing circular imports");
let result = add(10, 5);
console.log("Result:", result);