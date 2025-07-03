function fib(n: int64) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

var x: string = "hello";
console.log(x + "world");
console.time("multiecma");

const promises = [];
for (let i: int64 = 0; i < 11; i++) {
    // intentional to call with same number every time to test performance better
    promises.push(go fib(11));
}

const results = await Promise.all(promises);
console.timeEnd("multiecma");
console.log("RESULTS ARE");
console.log(results);


var x: string = "hello";
console.log(x + "world");

class Point {
    x: float64 = 0;
    y: float64 = 0;
}

let p = new Point();
p.x += 5;
console.log("p.x", p.x, "p.y", p.y)