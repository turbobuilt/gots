// calculator.gts - Advanced calculator that imports from both math and utils
import { add, multiply } from "./math";
import { circle_area, log } from "./utils";

export function advanced_calc() {
    log("Calculator: Starting advanced calculation");
    
    let sum = add(5, 3);
    let product = multiply(4, 6);
    let area = circle_area(2);
    
    log("Sum: " + sum);
    log("Product: " + product);  
    log("Circle area: " + area);
    
    return sum + product + area;
}