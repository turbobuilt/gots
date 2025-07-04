function fib(n: int64) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

var x: string = "hello";
console.log(x + "world");
console.time("multiecma");

const promises = [];
for (let i: int64 = 0; i < 31; i++) {
    // intentional to call with same number every time to test performance better
    promises.push(go fib(31));
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

var y = "bob";

switch (y) {
    case 1:
        console.log("is 1");
        break;
    case "bob":
        console.log("is bob");
        break;
    case 2: 
        console.log("is 2");
        break;
    case 3: 
        console.log("is 3");
        break;
}

import { bobby } from "./testb.gts";

console.log("bobby is ", bobby)

let data = { x: "x", y: "y val", z: "z val" }
console.log("data is", data)
for each key, value in data {
    console.log("key is", key, "value is", value)
}

console.log("regex", "bobby brown".match(/bob/))