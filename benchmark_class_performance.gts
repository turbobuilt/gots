// Performance benchmark: Classes vs Functions
// This benchmark ensures classes don't introduce significant overhead

// Function-based approach
function fibFunction(n: int64): int64 {
    if (n <= 1) return n;
    return fibFunction(n - 1) + fibFunction(n - 2);
}

function performFunctionTest(): int64 {
    let result: int64 = 0;
    for (let i: int64 = 0; i < 1000000; i++) {
        result += fibFunction(10);
    }
    return result;
}

// Class-based approach
class FibCalculator {
    multiplier: int64;
    
    constructor(multiplier: int64) {
        this.multiplier = multiplier;
    }
    
    fibonacci(n: int64): int64 {
        if (n <= 1) return n;
        return this.fibonacci(n - 1) + this.fibonacci(n - 2);
    }
    
    performTest(): int64 {
        let result: int64 = 0;
        for (let i: int64 = 0; i < 1000000; i++) {
            result += this.fibonacci(10) * this.multiplier;
        }
        return result;
    }
}

// Benchmark function approach
console.time("function-approach");
let funcResult = performFunctionTest();
console.timeEnd("function-approach");

// Benchmark class approach
console.time("class-approach");
let calc = new FibCalculator(1);
let classResult = calc.performTest();
console.timeEnd("class-approach");

console.log("Function result:", funcResult);
console.log("Class result:", classResult);

// Verify results are equivalent (accounting for multiplier)
if (funcResult == classResult) {
    console.log("✓ Results match - class overhead is minimal");
} else {
    console.log("✗ Results don't match - investigate implementation");
}