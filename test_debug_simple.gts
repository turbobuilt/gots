class Point {
    x: float64;
    y: float64 = 0;

    operator [] (a: Point, b: string) {
        console.log("Got string:", b)
    }
}

function test() {
    var p = new Point{ x: 10.0, y: 20.0 }
    p["hello"]
}

test();