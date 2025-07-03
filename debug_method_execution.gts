// Simple test to debug method execution
class TestClass {
    value: number;
    
    constructor(val: number) {
        this.value = val;
    }
    
    getValue(): number {
        console.log("Inside getValue method");
        return this.value;
    }
}

console.log("Creating object...");
let obj = new TestClass(42);
console.log("Calling method...");
let result = obj.getValue();
console.log("Method returned:", result);