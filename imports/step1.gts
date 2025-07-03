// step1.gts - First step that imports from step2
import { value2 } from "./step2";

export function func1() {
    console.log("Function 1 called, value2 is:", value2);
    return "result1";
}

export const value1 = "from_step1";