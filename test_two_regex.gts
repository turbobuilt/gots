// Test 1: Email validation
const email = "test.user+foo@example.co.uk".match(/[\w.+-]+@[\w-]+\.[\w.-]+/);
console.log("email", email && email[0] === "test.user+foo@example.co.uk" ? "success" : "fail");

// Test 2: Extract all numbers from a string
const numbers = "Order 123: $45.67, Qty: 8".match(/\d+(\.\d+)?/g);
console.log("numbers", JSON.stringify(numbers) === JSON.stringify(['123', '45.67', '8']) ? "success" : "fail");