// Test ultra-high performance path (same types)
var x: int64 = 2;
console.log("Testing typed int64 switch:");
switch (x) {
    case 1:
        console.log("One");
        break;
    case 2:
        console.log("Two (fast path)");
        break;
    case 3:
        console.log("Three");
        break;
}

// Test mixed-type handling (slow path with ANY)
var y = "hello";  // Inferred as string but stored as ANY
console.log("Testing ANY variable with mixed types:");
switch (y) {
    case 1:
        console.log("Number case");
        break;
    case "hello":
        console.log("String case (slow path)");
        break;
    case 3:
        console.log("Another number");
        break;
}

// Test typed string switch (fast path)
var z: string = "world";
console.log("Testing typed string switch:");
switch (z) {
    case "hello":
        console.log("Hello");
        break;
    case "world":
        console.log("World (fast path)");
        break;
    case "foo":
        console.log("Foo");
        break;
}

console.log("Performance test complete");