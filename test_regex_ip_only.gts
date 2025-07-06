// Test 5: Validate IPv4 address
console.log("Starting IP test");
const text = "My IP is 192.168.1.1";
console.log("Text:", text);
const pattern = /\b(?:\d{1,3}\.){3}\d{1,3}\b/;
console.log("About to match");
const ipv4 = text.match(pattern);
console.log("Match result:", ipv4);
if (ipv4) {
    console.log("Match found:", ipv4[0]);
} else {
    console.log("No match found");
}