function test(a, b, c) {
    console.log("a:", a);
    console.log("b:", b);
    console.log("c:", c);
}

// Test positional arguments
console.log("=== Positional arguments ===");
test(1, 2, 3);

// Test keyword arguments
console.log("=== Keyword arguments ===");
test(5, c=10);

// Test mixed positional and keyword arguments
console.log("=== Mixed arguments ===");
test(7, c=8, b=9);

// Test all keyword arguments
console.log("=== All keyword arguments ===");
test(c=30, a=10, b=20);