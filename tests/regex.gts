// Test 1: Email validation
const email = "test.user+foo@example.co.uk".match(/[\w.+-]+@[\w-]+\.[\w.-]+/);
console.log("email", email && email[0] === "test.user+foo@example.co.uk" ? "success" : "fail");

// Test 2: Extract all numbers from a string
const numbers = "Order 123: $45.67, Qty: 8".match(/\d+(\.\d+)?/g);
console.log("numbers", JSON.stringify(numbers) === JSON.stringify(['123', '45.67', '8']) ? "success" : "fail");

// Test 3: Match repeated words (case-insensitive)
const repeated = "Hello hello world".match(/\b(\w+)\b\s+\1\b/i);
console.log("repeated", repeated && repeated[0] === "Hello hello" ? "success" : "fail");

// Test 4: Extract quoted strings (single or double quotes)
const quotes = `He said, "Hello" and 'Goodbye'`.match(/(["'])(?:(?=(\\?))\2.)*?\1/g);
console.log("quotes", JSON.stringify(quotes) === JSON.stringify(['"Hello"', "'Goodbye'"]) ? "success" : "fail");

// Test 5: Validate IPv4 address
const ipv4 = "My IP is 192.168.1.1".match(/\b(?:\d{1,3}\.){3}\d{1,3}\b/);
console.log("ipv4", ipv4 && ipv4[0] === "192.168.1.1" ? "success" : "fail", ipv4);

// Test 6: Find all hashtags in a tweet
const hashtags = "Loving #JavaScript and #regex!".match(/#\w+/g);
console.log("hashtags", JSON.stringify(hashtags) === JSON.stringify(['#JavaScript', '#regex']) ? "success" : "fail");