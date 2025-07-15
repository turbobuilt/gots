class Point {
    x: float64;
    y: float64 = 0;

    // string version for slice access and indeterminate types
    operator [] (a: Point, b: string) {
        console.log("B IS ", b)
        return new Point{ x: a.x / 2, y: a.y / 2 }
    }
}

function test() {
    var p = new Point{ x: 10.0, y: 20.0 }
    
    // Test simple slice notation
    p[2:6]
    
    console.log("Test completed successfully");
}

test();