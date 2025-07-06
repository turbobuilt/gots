// Comprehensive GoTS Regex Testing
// Testing all ECMAScript regex features for compliance and performance

console.log("=== GoTS Regex Comprehensive Test Suite ===");

// ========== Basic Pattern Matching ==========
console.log("\n--- Basic Pattern Matching ---");

// Simple literal matching
let result1 = /hello/.test("hello world");
console.log("Literal match:", result1); // Should be true

// Case sensitivity
let result2 = /Hello/.test("hello world");
console.log("Case sensitive:", result2); // Should be false

// Case insensitive flag
let result3 = /Hello/i.test("hello world");
console.log("Case insensitive:", result3); // Should be true

// ========== Character Classes ==========
console.log("\n--- Character Classes ---");

// Digit class
let digitTest = /\d+/.exec("abc123def");
console.log("Digit class:", digitTest); // Should match "123"

// Word class
let wordTest = /\w+/.exec("hello_world123!");
console.log("Word class:", wordTest); // Should match "hello_world123"

// Whitespace class
let spaceTest = /\s+/.exec("hello   world");
console.log("Whitespace class:", spaceTest); // Should match "   "

// Negated classes
let nonDigitTest = /\D+/.exec("123abc456");
console.log("Non-digit class:", nonDigitTest); // Should match "abc"

// Custom character class
let customTest = /[aeiou]+/.exec("hello world");
console.log("Custom vowels:", customTest); // Should match "e"

// Negated custom class
let negCustomTest = /[^aeiou]+/.exec("hello world");
console.log("Non-vowels:", negCustomTest); // Should match "h"

// Character ranges
let rangeTest = /[a-z]+/.exec("Hello123");
console.log("Range [a-z]:", rangeTest); // Should match "ello"

// Multiple ranges
let multiRangeTest = /[a-zA-Z0-9]+/.exec("Hello123!");
console.log("Multi-range:", multiRangeTest); // Should match "Hello123"

// ========== Anchors and Boundaries ==========
console.log("\n--- Anchors and Boundaries ---");

// Start anchor
let startTest = /^hello/.test("hello world");
console.log("Start anchor (match):", startTest); // Should be true

let startTest2 = /^hello/.test("say hello");
console.log("Start anchor (no match):", startTest2); // Should be false

// End anchor
let endTest = /world$/.test("hello world");
console.log("End anchor (match):", endTest); // Should be true

let endTest2 = /world$/.test("world peace");
console.log("End anchor (no match):", endTest2); // Should be false

// Word boundary
let boundaryTest = /\bword\b/.test("a word here");
console.log("Word boundary (match):", boundaryTest); // Should be true

let boundaryTest2 = /\bword\b/.test("password");
console.log("Word boundary (no match):", boundaryTest2); // Should be false

// Non-word boundary
let nonBoundaryTest = /\Bord\B/.test("password");
console.log("Non-word boundary:", nonBoundaryTest); // Should be true

// ========== Quantifiers ==========
console.log("\n--- Quantifiers ---");

// Zero or more
let zeroMoreTest = /a*/.exec("baaab");
console.log("Zero or more:", zeroMoreTest); // Should match ""

// One or more
let oneMoreTest = /a+/.exec("baaab");
console.log("One or more:", oneMoreTest); // Should match "aaa"

// Zero or one
let zeroOneTest = /colou?r/.exec("color");
console.log("Zero or one:", zeroOneTest); // Should match "color"

let zeroOneTest2 = /colou?r/.exec("colour");
console.log("Zero or one (with u):", zeroOneTest2); // Should match "colour"

// Exact count
let exactTest = /a{3}/.exec("aaab");
console.log("Exact count {3}:", exactTest); // Should match "aaa"

// Range count
let rangeCountTest = /a{2,4}/.exec("aaaaab");
console.log("Range count {2,4}:", rangeCountTest); // Should match "aaaa"

// At least count
let atLeastTest = /a{2,}/.exec("aaaaab");
console.log("At least {2,}:", atLeastTest); // Should match "aaaaa"

// ========== Greedy vs Lazy Quantifiers ==========
console.log("\n--- Greedy vs Lazy Quantifiers ---");

// Greedy matching
let greedyTest = /<.*>/.exec("<tag>content</tag>");
console.log("Greedy quantifier:", greedyTest); // Should match entire string

// Lazy matching
let lazyTest = /<.*?>/.exec("<tag>content</tag>");
console.log("Lazy quantifier:", lazyTest); // Should match "<tag>"

// ========== Groups and Capturing ==========
console.log("\n--- Groups and Capturing ---");

// Basic capturing group
let captureTest = /(hello) (world)/.exec("say hello world");
console.log("Capturing groups:", captureTest); // Should have captured groups

// Non-capturing group
let nonCaptureTest = /(?:hello) (world)/.exec("say hello world");
console.log("Non-capturing group:", nonCaptureTest); // Should only capture "world"

// Backreferences
let backrefTest = /(hello) \1/.test("hello hello");
console.log("Backreference (match):", backrefTest); // Should be true

let backrefTest2 = /(hello) \1/.test("hello world");
console.log("Backreference (no match):", backrefTest2); // Should be false

// ========== Alternation ==========
console.log("\n--- Alternation ---");

// Simple alternation
let altTest = /cat|dog/.exec("I have a dog");
console.log("Alternation:", altTest); // Should match "dog"

// Grouped alternation
let groupAltTest = /(cat|dog)s?/.exec("I have dogs");
console.log("Grouped alternation:", groupAltTest); // Should match "dogs"

// ========== Escape Sequences ==========
console.log("\n--- Escape Sequences ---");

// Special characters
let escapeTest = /\$\d+\.\d{2}/.exec("Price: $12.34");
console.log("Escaped special chars:", escapeTest); // Should match "$12.34"

// Control characters
let tabTest = /hello\tworld/.test("hello\tworld");
console.log("Tab escape:", tabTest); // Should be true

let newlineTest = /line1\nline2/.test("line1\nline2");
console.log("Newline escape:", newlineTest); // Should be true

// ========== Dot Metacharacter ==========
console.log("\n--- Dot Metacharacter ---");

// Dot matches any character except newline
let dotTest = /h.llo/.exec("hello world");
console.log("Dot metachar:", dotTest); // Should match "hello"

let dotTest2 = /h.llo/.test("h\nllo");
console.log("Dot vs newline:", dotTest2); // Should be false

// Dot with dotall flag (s flag)
let dotallTest = /h.llo/s.test("h\nllo");
console.log("Dot with s flag:", dotallTest); // Should be true if s flag supported

// ========== Global Flag Testing ==========
console.log("\n--- Global Flag ---");

// Global matching
let globalRegex = /\d+/g;
let text = "There are 123 and 456 numbers";
let globalMatches = [];
let match;
while ((match = globalRegex.exec(text)) !== null) {
    globalMatches.push(match[0]);
    if (match.index === globalRegex.lastIndex) {
        break; // Prevent infinite loop on zero-length matches
    }
}
console.log("Global matches:", globalMatches); // Should be ["123", "456"]

// ========== Multiline Flag Testing ==========
console.log("\n--- Multiline Flag ---");

let multiText = "line1\nline2\nline3";

// Without multiline flag
let noMultiTest = /^line2/.test(multiText);
console.log("No multiline flag:", noMultiTest); // Should be false

// With multiline flag
let multiTest = /^line2/m.test(multiText);
console.log("With multiline flag:", multiTest); // Should be true

// ========== String Methods with Regex ==========
console.log("\n--- String Methods ---");

// String.match()
let matchResult = "hello 123 world 456".match(/\d+/g);
console.log("String.match():", matchResult); // Should be ["123", "456"]

// String.replace()
let replaceResult = "hello world".replace(/world/, "universe");
console.log("String.replace():", replaceResult); // Should be "hello universe"

// String.replace() with captures
let captureReplace = "John Doe".replace(/(\w+) (\w+)/, "$2, $1");
console.log("Replace with captures:", captureReplace); // Should be "Doe, John"

// String.search()
let searchResult = "hello world".search(/world/);
console.log("String.search():", searchResult); // Should be 6

// String.split()
let splitResult = "one,two,three".split(/,/);
console.log("String.split():", splitResult); // Should be ["one", "two", "three"]

// ========== Edge Cases and Error Handling ==========
console.log("\n--- Edge Cases ---");

// Empty regex
let emptyTest = //.test("anything");
console.log("Empty regex:", emptyTest); // Should be true

// Empty string matching
let emptyStringTest = /^$/.test("");
console.log("Empty string match:", emptyStringTest); // Should be true

// Unicode characters (basic)
let unicodeTest = /café/.test("café");
console.log("Unicode test:", unicodeTest); // Should be true

// Hex escapes
let hexTest = /\x41/.test("A");
console.log("Hex escape:", hexTest); // Should be true

// Unicode escapes
let unicodeEscapeTest = /\u0041/.test("A");
console.log("Unicode escape:", unicodeEscapeTest); // Should be true

// ========== Performance Tests ==========
console.log("\n--- Performance Tests ---");

// Large string matching
let largeString = "a".repeat(10000) + "target" + "b".repeat(10000);
let perfStart = Date.now();
let perfResult = /target/.test(largeString);
let perfTime = Date.now() - perfStart;
console.log("Large string performance:", perfResult, "Time:", perfTime, "ms");

// Complex pattern performance
let complexPattern = /^(?:[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,})$/;
let emailTest = complexPattern.test("user@example.com");
console.log("Complex pattern (email):", emailTest);

// ========== Error Cases (should handle gracefully) ==========
console.log("\n--- Error Handling ---");

try {
    // Invalid quantifier
    let invalidRegex = new RegExp("a{,}");
    console.log("Invalid quantifier handled");
} catch (e) {
    console.log("Invalid quantifier error:", e.message);
}

try {
    // Invalid character class
    let invalidClass = new RegExp("[z-a]");
    console.log("Invalid range handled");
} catch (e) {
    console.log("Invalid range error:", e.message);
}

try {
    // Invalid flags
    let invalidFlags = new RegExp("test", "xyz");
    console.log("Invalid flags handled");
} catch (e) {
    console.log("Invalid flags error:", e.message);
}

console.log("\n=== Regex Test Suite Complete ===");