function fib(n: int64) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

var x: string = "hello";
console.log(x + "world");

console.time("sequential");

const results = [];
for (let i: int64 = 0; i < 10; i++) {
    // Calculate fib(30) for each iteration to demonstrate computational load
    results.push(fib(30));
}

console.timeEnd("sequential");
console.log("RESULTS ARE");
console.log(results);

var x2: string = "hello";
console.log(x2 + "world");

class Point {
    x: float64 = 0;
    y: float64 = 0;
}

let p = new Point();
p.x += 5;
console.log("p.x", p.x, "p.y", p.y);