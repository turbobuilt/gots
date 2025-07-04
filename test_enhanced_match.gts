// Test the enhanced match result with JavaScript properties
var result = "bobby brown".match(/bob/)

console.log("=== Basic Array Access (High Performance) ===")
console.log("result[0]:", result[0])
console.log("result.length:", result.length)

console.log("=== JavaScript Properties (Lazy Loaded) ===")
console.log("result.index:", result.index)
console.log("result.input:", result.input)
console.log("result.groups:", result.groups)

console.log("=== Full Compatibility Test ===")
console.log("Complete result:", result)