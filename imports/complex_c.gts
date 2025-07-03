// complex_c.gts - Part of complex circular import chain A->B->C->A
import { func_a } from "./complex_a";
import { value_b } from "./complex_b";

export function func_c() {
    console.log("Function C executing");
    console.log("Value from B: " + value_b);
    return "result_c";
}

export function call_a() {
    console.log("C calling A");
    return func_a();
}

export const value_c = "from_module_c";