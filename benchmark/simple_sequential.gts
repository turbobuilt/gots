console.time("simple-sequential");

const results = [];

// Simple calculation 26 times
for (let iter: int64 = 0; iter < 26; iter++) {
    results.push(121393);
}

console.timeEnd("simple-sequential");
console.log("RESULTS ARE");
console.log(results);