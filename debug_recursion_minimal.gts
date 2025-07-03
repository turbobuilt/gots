function countdown(n: int64) {
    if (n <= 0) return 0;
    return countdown(n - 1);
}

let result = countdown(3);
console.log("countdown(3) =", result);