class Point {
    x: float64;
    y: float64;
    
    constructor(x: float64, y: float64) {
        this.x = x;
        this.y = y;
    }
    
    operator + (a: Point, b: Point) {
        console.log("Adding points:");
        console.log("First point:", a.x, a.y);
        console.log("Second point:", b.x, b.y);
        return new Point(a.x + b.x, a.y + b.y);
    }
}

let p1 = new Point(1.0, 2.0);
let p2 = new Point(3.0, 4.0);
let result = p1 + p2;