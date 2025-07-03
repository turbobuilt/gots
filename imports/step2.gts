// step2.gts - Second step that imports from step1 (creating direct circular dependency)
import { value1 } from "./step1";

export function func2() {
    console.log("Function 2 called, value1 is:", value1);
    return "result2";
}

export const value2 = "from_step2";