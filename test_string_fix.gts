// Test that string literals are not truncated
const ip_text = "My IP is 192.168.1.1";
console.log("Testing string literal truncation fix:");
console.log("String: '" + ip_text + "'");
console.log("Length: " + ip_text.length);

// Test various strings around the problematic length
const test1 = "1234567890123456";     // 16 chars - exactly at truncation point
const test2 = "12345678901234567";    // 17 chars
const test3 = "123456789012345678";   // 18 chars
const test4 = "1234567890123456789"; // 19 chars
const test5 = "12345678901234567890"; // 20 chars

console.log("\nTesting strings around truncation point:");
console.log("16 chars: '" + test1 + "' (length: " + test1.length + ")");
console.log("17 chars: '" + test2 + "' (length: " + test2.length + ")");
console.log("18 chars: '" + test3 + "' (length: " + test3.length + ")");
console.log("19 chars: '" + test4 + "' (length: " + test4.length + ")");
console.log("20 chars: '" + test5 + "' (length: " + test5.length + ")");

// Test with IP addresses
const ip1 = "192.168.1.1";
const ip2 = "10.0.0.1";
const ip3 = "255.255.255.255";

console.log("\nTesting IP addresses:");
console.log("IP1: '" + ip1 + "' (length: " + ip1.length + ")");
console.log("IP2: '" + ip2 + "' (length: " + ip2.length + ")");
console.log("IP3: '" + ip3 + "' (length: " + ip3.length + ")");