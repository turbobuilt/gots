// Math utilities module with named exports
export function add(a: int64, b: int64) {
    return a + b;
}

export function multiply(a: int64, b: int64) {
    return a * b;
}

export var PI: float64 = 3.14159;

function privateHelper() {
    console.log("This is a private function");
}

export function calculate() {
    privateHelper();
    return add(5, 3);
}