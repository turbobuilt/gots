// Comprehensive Date Test Suite for GoTS
// Testing all ECMAScript Date features and moment.js-like extensions

console.log("Starting comprehensive Date tests...");

// Test 1: Basic Date construction
console.log("\n=== Test 1: Basic Date Construction ===");
let now = new Date();
console.log("new Date():", now.toString());

let specificDate = new Date(2023, 11, 25, 10, 30, 45, 123);
console.log("new Date(2023, 11, 25, 10, 30, 45, 123):", specificDate.toString());

let fromMillis = new Date(1640995200000); // Jan 1, 2022 UTC
console.log("new Date(1640995200000):", fromMillis.toString());

// Test 2: Two-digit year handling
console.log("\n=== Test 2: Two-digit Year Handling ===");
let year99 = new Date(99, 0, 1);
console.log("new Date(99, 0, 1) should be 1999:", year99.getFullYear());

let year00 = new Date(0, 0, 1);
console.log("new Date(0, 0, 1) should be 1900:", year00.getFullYear());

let year50 = new Date(50, 0, 1);
console.log("new Date(50, 0, 1) should be 1950:", year50.getFullYear());

// Test 3: Date function call (not constructor)
console.log("\n=== Test 3: Date Function Call ===");
// This should return a string, not a Date object
// Note: In GoTS, this would be implemented as Date() without 'new'

// Test 4: Getters
console.log("\n=== Test 4: Getters ===");
let testDate = new Date(2023, 5, 15, 14, 30, 45, 500); // June 15, 2023
console.log("Date:", testDate.toString());
console.log("getFullYear():", testDate.getFullYear());
console.log("getMonth():", testDate.getMonth()); // Should be 5 (June, 0-indexed)
console.log("getDate():", testDate.getDate());
console.log("getDay():", testDate.getDay());
console.log("getHours():", testDate.getHours());
console.log("getMinutes():", testDate.getMinutes());
console.log("getSeconds():", testDate.getSeconds());
console.log("getMilliseconds():", testDate.getMilliseconds());
console.log("getTime():", testDate.getTime());

// Test 5: UTC Getters
console.log("\n=== Test 5: UTC Getters ===");
console.log("getUTCFullYear():", testDate.getUTCFullYear());
console.log("getUTCMonth():", testDate.getUTCMonth());
console.log("getUTCDate():", testDate.getUTCDate());
console.log("getUTCDay():", testDate.getUTCDay());
console.log("getUTCHours():", testDate.getUTCHours());
console.log("getUTCMinutes():", testDate.getUTCMinutes());
console.log("getUTCSeconds():", testDate.getUTCSeconds());
console.log("getUTCMilliseconds():", testDate.getUTCMilliseconds());

// Test 6: Setters
console.log("\n=== Test 6: Setters ===");
let mutableDate = new Date(2023, 0, 1);
console.log("Initial date:", mutableDate.toString());

mutableDate.setFullYear(2024);
console.log("After setFullYear(2024):", mutableDate.toString());

mutableDate.setMonth(11); // December
console.log("After setMonth(11):", mutableDate.toString());

mutableDate.setDate(25);
console.log("After setDate(25):", mutableDate.toString());

mutableDate.setHours(18, 30, 45, 250);
console.log("After setHours(18, 30, 45, 250):", mutableDate.toString());

// Test 7: Deprecated methods
console.log("\n=== Test 7: Deprecated Methods ===");
let deprecatedTest = new Date(2023, 5, 15);
console.log("getYear():", deprecatedTest.getYear()); // Should be 123 (2023 - 1900)

deprecatedTest.setYear(95); // Should set to 1995
console.log("After setYear(95):", deprecatedTest.getFullYear());

console.log("toGMTString():", deprecatedTest.toGMTString());

// Test 8: String methods
console.log("\n=== Test 8: String Methods ===");
let stringTest = new Date(2023, 5, 15, 14, 30, 45);
console.log("toString():", stringTest.toString());
console.log("toISOString():", stringTest.toISOString());
console.log("toUTCString():", stringTest.toUTCString());
console.log("toDateString():", stringTest.toDateString());
console.log("toTimeString():", stringTest.toTimeString());
console.log("toLocaleDateString():", stringTest.toLocaleDateString());
console.log("toLocaleTimeString():", stringTest.toLocaleTimeString());
console.log("toLocaleString():", stringTest.toLocaleString());
console.log("toJSON():", stringTest.toJSON());

// Test 9: Static methods
console.log("\n=== Test 9: Static Methods ===");
console.log("Date.now():", Date.now());
console.log("Date.UTC(2023, 5, 15, 12, 0, 0):", Date.UTC(2023, 5, 15, 12, 0, 0));
console.log("Date.parse('2023-06-15T12:00:00Z'):", Date.parse('2023-06-15T12:00:00Z'));

// Test 10: Comparison operators
console.log("\n=== Test 10: Comparison Operators ===");
let date1 = new Date(2023, 5, 15);
let date2 = new Date(2023, 5, 16);
let date3 = new Date(2023, 5, 15);

console.log("date1 < date2:", date1 < date2);
console.log("date1 > date2:", date1 > date2);
console.log("date1 == date3:", date1.getTime() == date3.getTime());

// Test 11: Arithmetic operators
console.log("\n=== Test 11: Arithmetic Operations ===");
let baseDate = new Date(2023, 5, 15, 12, 0, 0);
console.log("Base date:", baseDate.toString());

// These would need operator overloading support in the compiler
// For now, we test the underlying methods

// Test 12: Moment.js-like methods
console.log("\n=== Test 12: Moment.js-like Methods ===");
let momentTest = new Date(2023, 5, 15, 12, 0, 0);
console.log("Original date:", momentTest.toString());

let addedDay = momentTest.add(1, "day");
console.log("Add 1 day:", addedDay.toString());

let addedMonth = momentTest.add(2, "months");
console.log("Add 2 months:", addedMonth.toString());

let addedYear = momentTest.add(1, "year");
console.log("Add 1 year:", addedYear.toString());

let subtracted = momentTest.subtract(5, "days");
console.log("Subtract 5 days:", subtracted.toString());

let cloned = momentTest.clone();
console.log("Cloned date:", cloned.toString());

// Test 13: Date formatting
console.log("\n=== Test 13: Date Formatting ===");
let formatTest = new Date(2023, 5, 15, 14, 30, 45, 123);
console.log("Format YYYY-MM-DD:", formatTest.format("YYYY-MM-DD"));
console.log("Format MM/DD/YYYY:", formatTest.format("MM/DD/YYYY"));
console.log("Format DD-MM-YY HH:mm:ss:", formatTest.format("DD-MM-YY HH:mm:ss"));
console.log("Format YYYY-MM-DD HH:mm:ss.SSS:", formatTest.format("YYYY-MM-DD HH:mm:ss.SSS"));

// Test 14: Date comparison methods
console.log("\n=== Test 14: Date Comparison Methods ===");
let earlierDate = new Date(2023, 5, 14);
let laterDate = new Date(2023, 5, 16);
let testDateForComparison = new Date(2023, 5, 15);

console.log("testDate.isBefore(laterDate):", testDateForComparison.isBefore(laterDate));
console.log("testDate.isAfter(earlierDate):", testDateForComparison.isAfter(earlierDate));
console.log("testDate.isBefore(earlierDate):", testDateForComparison.isBefore(earlierDate));
console.log("testDate.isAfter(laterDate):", testDateForComparison.isAfter(laterDate));

// Test 15: Edge cases
console.log("\n=== Test 15: Edge Cases ===");

// Leap year test
let leapYear = new Date(2024, 1, 29); // Feb 29, 2024 (leap year)
console.log("Leap year date (2024-02-29):", leapYear.toString());

let nonLeapYear = new Date(2023, 1, 28); // Feb 28, 2023 (non-leap year)
console.log("Non-leap year date (2023-02-28):", nonLeapYear.toString());

// Month overflow
let monthOverflow = new Date(2023, 13, 1); // Should wrap to next year
console.log("Month overflow (2023, 13, 1):", monthOverflow.toString());

// Day overflow
let dayOverflow = new Date(2023, 1, 31); // Feb 31 should adjust
console.log("Day overflow (2023, 1, 31):", dayOverflow.toString());

console.log("\nAll Date tests completed!");