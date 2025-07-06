// Debug IP address matching
const text = "My IP is 192.168.1.1";
const pattern = /\b(?:\d{1,3}\.){3}\d{1,3}\b/;
const match = text.match(pattern);
console.log("Input text:", text);
console.log("Pattern:", pattern);
console.log("Match result:", match);
console.log("Expected:", "192.168.1.1");
console.log("Actual:", match ? match[0] : "null");