class Point {
    x: float64;
    y: float64 = 0;

    // string version for slice access and indeterminate types
    operator [] (a: Point, b: string) {
        console.log("String parameter received")
        console.log("B IS ", b)
        return new Point{ x: a.x / 2, y: a.y / 2 }
    }
}

function test() {
    var p = new Point{ x: 10.0, y: 20.0 }
    
    // Test with direct string literal
    console.log("=== Testing with direct string literal ===")
    var result = p["hello"]
    
    console.log("Test completed successfully");
}

test();