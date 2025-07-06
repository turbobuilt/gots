// Simple Date Test for GoTS

console.log("=== Basic Date Test ===");

// Test basic constructor
let d1 = new Date();
console.log("Current date created");

// Test with specific values
let d2 = new Date(2023, 5, 15);
console.log("Specific date created: 2023-06-15");

// Test getters
console.log("Year:", d2.getFullYear());
console.log("Month:", d2.getMonth());
console.log("Date:", d2.getDate());

console.log("Test completed!");