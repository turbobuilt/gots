// complex_a.gts - Part of complex circular import chain A->B->C->A
import { func_b } from "./complex_b";
import { value_c } from "./complex_c";

export function func_a() {
    console.log("Function A executing");
    console.log("Value from C: " + value_c);
    return "result_a";
}

export function call_b() {
    console.log("A calling B");
    return func_b();
}

export const value_a = "from_module_a";