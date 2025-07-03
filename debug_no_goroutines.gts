function fib(n: int64) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

var x: string = "hello";
console.log(x + "world");

class Point {
    x: float64 = 0;
    y: float64 = 0;
}

let p = new Point();
p.x += 5;
console.log("p.x", p.x, "p.y", p.y);
