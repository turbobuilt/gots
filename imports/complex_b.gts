// complex_b.gts - Part of complex circular import chain A->B->C->A
import { func_c } from "./complex_c";
import { value_a } from "./complex_a";

export function func_b() {
    console.log("Function B executing");
    console.log("Value from A: " + value_a);
    return "result_b";
}

export function call_c() {
    console.log("B calling C");
    return func_c();
}

export const value_b = "from_module_b";