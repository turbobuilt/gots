// Comprehensive Date test for GoTS
// This tests all major JavaScript Date functionality

console.log("=== GoTS Date Implementation Test ===");

// Test 1: Basic Date creation and current time
console.log("\n1. Basic Date Creation:");
var now = new Date();
console.log("Current time (new Date()):", now.toString());
console.log("Current timestamp:", now.getTime());
console.log("Date.now():", Date.now());

// Test 2: Date creation from timestamp
console.log("\n2. Date from Timestamp:");
var date1 = new Date(1640995200000); // 2022-01-01 00:00:00 UTC
console.log("Date from timestamp:", date1.toString());
console.log("ISO String:", date1.toISOString());

// Test 3: Date creation from components
console.log("\n3. Date from Components:");
var date2 = new Date(2023, 11, 25, 10, 30, 45, 123); // Note: month is 0-based
console.log("Date(2023, 11, 25, 10, 30, 45, 123):", date2.toString());
console.log("Year:", date2.getFullYear());
console.log("Month:", date2.getMonth()); // Should be 11 (December)
console.log("Date:", date2.getDate());
console.log("Hours:", date2.getHours());
console.log("Minutes:", date2.getMinutes());
console.log("Seconds:", date2.getSeconds());
console.log("Milliseconds:", date2.getMilliseconds());

// Test 4: UTC methods
console.log("\n4. UTC Methods:");
console.log("UTC Year:", date2.getUTCFullYear());
console.log("UTC Month:", date2.getUTCMonth());
console.log("UTC Date:", date2.getUTCDate());
console.log("UTC Hours:", date2.getUTCHours());
console.log("UTC Minutes:", date2.getUTCMinutes());
console.log("UTC Seconds:", date2.getUTCSeconds());
console.log("UTC Milliseconds:", date2.getUTCMilliseconds());

// Test 5: Day of week
console.log("\n5. Day of Week:");
console.log("Local Day of Week:", date2.getDay()); // 0=Sunday, 1=Monday, etc.
console.log("UTC Day of Week:", date2.getUTCDay());

// Test 6: Timezone offset
console.log("\n6. Timezone:");
console.log("Timezone Offset (minutes):", date2.getTimezoneOffset());

// Test 7: String formatting
console.log("\n7. String Formatting:");
console.log("toString():", date2.toString());
console.log("toISOString():", date2.toISOString());
console.log("toUTCString():", date2.toUTCString());
console.log("toDateString():", date2.toDateString());
console.log("toTimeString():", date2.toTimeString());
console.log("toJSON():", date2.toJSON());

// Test 8: Date parsing
console.log("\n8. Date Parsing:");
var parsed1 = new Date("2023-12-25T10:30:45.123Z");
console.log("Parsed ISO string:", parsed1.toString());
console.log("Parsed timestamp:", parsed1.getTime());

// Test 9: Date.UTC static method
console.log("\n9. Date.UTC Static Method:");
var utcTime = Date.UTC(2023, 11, 25, 10, 30, 45, 123);
console.log("Date.UTC(2023, 11, 25, 10, 30, 45, 123):", utcTime);
var utcDate = new Date(utcTime);
console.log("UTC Date object:", utcDate.toISOString());

// Test 10: Date setters
console.log("\n10. Date Setters:");
var mutableDate = new Date(2023, 0, 1); // January 1, 2023
console.log("Original date:", mutableDate.toString());

mutableDate.setFullYear(2024);
console.log("After setFullYear(2024):", mutableDate.toString());

mutableDate.setMonth(5); // June (0-based)
console.log("After setMonth(5):", mutableDate.toString());

mutableDate.setDate(15);
console.log("After setDate(15):", mutableDate.toString());

mutableDate.setHours(14, 30, 45, 500);
console.log("After setHours(14, 30, 45, 500):", mutableDate.toString());

// Test 11: UTC setters
console.log("\n11. UTC Setters:");
var utcMutableDate = new Date(2023, 0, 1);
console.log("Original UTC date:", utcMutableDate.toISOString());

utcMutableDate.setUTCFullYear(2024);
console.log("After setUTCFullYear(2024):", utcMutableDate.toISOString());

utcMutableDate.setUTCMonth(5);
console.log("After setUTCMonth(5):", utcMutableDate.toISOString());

utcMutableDate.setUTCDate(15);
console.log("After setUTCDate(15):", utcMutableDate.toISOString());

// Test 12: Date comparison
console.log("\n12. Date Comparison:");
var date3 = new Date(2023, 0, 1);
var date4 = new Date(2023, 0, 1);
var date5 = new Date(2023, 0, 2);

console.log("Date3 timestamp:", date3.getTime());
console.log("Date4 timestamp:", date4.getTime());
console.log("Date5 timestamp:", date5.getTime());

console.log("date3.getTime() === date4.getTime():", date3.getTime() === date4.getTime());
console.log("date3.getTime() < date5.getTime():", date3.getTime() < date5.getTime());

// Test 13: Date arithmetic
console.log("\n13. Date Arithmetic:");
var baseDate = new Date(2023, 0, 1, 12, 0, 0, 0);
console.log("Base date:", baseDate.toString());

// Add one day (86400000 milliseconds)
var nextDay = new Date(baseDate.getTime() + 86400000);
console.log("Next day:", nextDay.toString());

// Subtract one hour (3600000 milliseconds)
var hourEarlier = new Date(baseDate.getTime() - 3600000);
console.log("Hour earlier:", hourEarlier.toString());

// Test 14: Edge cases
console.log("\n14. Edge Cases:");

// Leap year test
var leapYear = new Date(2020, 1, 29); // February 29, 2020
console.log("Leap year date (2020-02-29):", leapYear.toString());
console.log("Leap year day:", leapYear.getDate());

// Month overflow
var overflowDate = new Date(2023, 13, 1); // Should become January 2024
console.log("Month overflow (2023, 13, 1):", overflowDate.toString());

// Test 15: Performance test
console.log("\n15. Performance Test:");
console.time("Date creation performance");
for (var i = 0; i < 1000; i++) {
    var perfDate = new Date();
    perfDate.getTime();
    perfDate.getFullYear();
    perfDate.toString();
}
console.timeEnd("Date creation performance");

console.log("\n=== Date Test Complete ===");