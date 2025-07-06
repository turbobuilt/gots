// Basic GoTS Regex Testing
console.log("=== Basic Regex Tests ===");

// Test 1: Simple literal matching
console.log("Test 1: Literal matching");
let regex1 = /hello/;
let result1 = regex1.test("hello world");
console.log("Result:", result1);

// Test 2: Case insensitive flag
console.log("Test 2: Case insensitive");
let regex2 = /Hello/i;
let result2 = regex2.test("hello world");
console.log("Result:", result2);

// Test 3: Character classes
console.log("Test 3: Digit class");
let regex3 = /\d+/;
let result3 = regex3.exec("abc123def");
console.log("Result:", result3);

// Test 4: Word boundaries
console.log("Test 4: Word boundary");
let regex4 = /\bword\b/;
let result4 = regex4.test("a word here");
console.log("Result:", result4);

// Test 5: Quantifiers
console.log("Test 5: One or more quantifier");
let regex5 = /a+/;
let result5 = regex5.exec("baaab");
console.log("Result:", result5);

// Test 6: Groups
console.log("Test 6: Capturing groups");
let regex6 = /(hello) (world)/;
let result6 = regex6.exec("say hello world");
console.log("Result:", result6);

// Test 7: Alternation
console.log("Test 7: Alternation");
let regex7 = /cat|dog/;
let result7 = regex7.exec("I have a dog");
console.log("Result:", result7);

// Test 8: Anchors
console.log("Test 8: Start anchor");
let regex8 = /^hello/;
let result8 = regex8.test("hello world");
console.log("Result:", result8);

console.log("=== Basic tests complete ===");