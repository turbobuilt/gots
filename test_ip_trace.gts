// Debug IP with simple test
const text = "192.168.1.1";
console.log("Parsing IP:", text);
const match = text.match(/\b(?:\d{1,3}\.){3}\d{1,3}\b/);
console.log("Result:", match);