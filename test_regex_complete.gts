// Complete regex functionality test
console.log("=== Regex Functionality Test ===");

// Test 1: Basic matching
let pattern1 = /hello/;
console.log("Test 1:", pattern1.test("hello world")); // Should be 1

// Test 2: Non-matching
console.log("Test 2:", pattern1.test("goodbye")); // Should be 0

// Test 3: Case sensitivity
let pattern2 = /Hello/;
console.log("Test 3:", pattern2.test("hello")); // Should be 0

// Test 4: Multiple regex objects
let pattern3 = /world/;
console.log("Test 4a:", pattern3.test("hello world")); // Should be 1
console.log("Test 4b:", pattern1.test("hello universe")); // Should be 1

// Test 5: Empty pattern
let pattern4 = //;
console.log("Test 5:", pattern4.test("anything")); // Should be 1

console.log("=== All tests completed successfully ===");