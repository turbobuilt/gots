class Point {
    x: float64;
    y: float64 = 0;

    // typed version for numeric access - super performant
    operator [] (a: Point, b: int64) {
        return new Point{ x: a.x * b, y: a.y * b }
    }

    // typed version for float access
    operator [] (a: Point, b: float64) {
        return new Point{ x: a.x + b, y: a.y + b } 
    }
    
    // string version for slice access and indeterminate types
    operator [] (a: Point, b: string) {
        if (b == "2:6") {
            return new Point{ x: a.x / 2, y: a.y / 2 }
        }
        return a; // default return same point
    }
    
    // untyped version that handles ANY variable type - flexible but slower
    operator [] (a: Point, b) {
        // b is ANY type - coder can do whatever they want with it here
        return new Point{ x: a.x, y: a.y } // Default behavior
    }
}

class Tensor {
    data: array;
    shape: array;
    
    // Comparison operator that returns another tensor (like numpy/pytorch)
    operator > (a: Tensor, b: float64) {
        // Returns a boolean tensor of same shape
        return new Tensor{ data: [], shape: a.shape }
    }
    
    // Array access with tensor indexing (boolean indexing)
    operator [] (a: Tensor, b: Tensor) {
        // Boolean indexing - returns filtered tensor
        return new Tensor{ data: [], shape: [0] }
    }
    
    // Regular numeric indexing
    operator [] (a: Tensor, b: int64) {
        return a.data[b];  // Returns element at index
    }
}

function test() {
    var p = new Point{ x: 10.0, y: 20.0 }
    
    // Test numeric literal priority ordering
    var result1 = p[0];     // Should use int64 operator[]
    var result2 = p[5.5];   // Should use float64 operator[]
    
    // Test slice notation fallback to string
    var result3 = p[2:6];   // Should use string operator[] with "2:6"
    
    // Test complex expression type inference
    var y = new Tensor{ data: [1,2,3,4,5], shape: [5] }
    var mask = y > 0;       // Should infer Tensor type from y > 0 operation
    var filtered = p[mask]; // Should use Tensor operator[] since mask is Tensor type
    
    console.log("Test completed successfully");
}

test();
