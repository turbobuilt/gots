// Test array access with regex match
let text = "hello world";
let regex = /hello/;
let match = text.match(regex);
console.log("Match result:", match);
console.log("Match type:", typeof match);
if (match) {
    console.log("Match length:", match.length);
    console.log("Match[0]:", match[0]);
}