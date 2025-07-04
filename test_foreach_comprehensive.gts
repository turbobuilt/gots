// Test comprehensive foreach functionality with objects

console.log("Testing foreach with object literals...")

let data = { 
    name: "GoTS", 
    version: "1.0", 
    status: "working",
    description: "High-performance language"
}

console.log("Object data:", data)
console.log()

console.log("Iterating through object properties:")
for each key, value in data {
    console.log("  Property:", key, "=>", value)
}

console.log()
console.log("Testing with different string lengths:")

let testObj = {
    short: "hi",
    medium: "medium length string", 
    long: "this is a much longer string with more characters to test edge cases"
}

for each prop, val in testObj {
    console.log("Length test -", prop, ":", val)
}

console.log()
console.log("All foreach tests completed successfully!")