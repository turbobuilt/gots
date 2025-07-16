class Point {
    x: float64;
    y: float64 = 0;

    // typed version for numeric access - super performant
    operator [] (a: Point, b: int64) {
        console.log("Int here")
        return new Point{ x: a.x * b, y: a.y * b }
    }

    // typed version for float access
    operator [] (a: Point, b: float64) {
        return new Point{ x: a.x + b, y: a.y + b } 
    }
    
    // string version for slice access and indeterminate types
    operator [] (a: Point, b: string) {
        console.log("B IS ", b) // should print "anything goes here"
        return new Point{ x: a.x / 2, y: a.y / 2 }
    }
    
    // untyped version that handles ANY variable type - flexible but slower
    operator [] (a: Point, b) {
        return new Point{ x: a.x, y: a.y } // Default behavior
    }
}

function test() {
    var p = new Point{ x: 10.0, y: 20.0 }
    
    // Test numeric literal priority ordering
    var result1 = p[0];     // Should use int64 operator[]
    var result2 = p[5.5];   // Should use float64 operator[]
    p[anything goes heredd]
    p[0:,:,:]
    
    console.log("Test completed successfully");
}

test();
