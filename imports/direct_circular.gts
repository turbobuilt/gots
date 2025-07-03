// direct_circular.gts - Test direct circular imports
console.log("Testing direct circular imports...");

import { func1, value1 } from "./step1";
import { func2, value2 } from "./step2";

console.log("value1:", value1);
console.log("value2:", value2);

let result1 = func1();
let result2 = func2();

console.log("func1 result:", result1);
console.log("func2 result:", result2);