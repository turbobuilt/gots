// Test basic match
var result = "test".match(/test/)
console.log("Result exists:", result ? "yes" : "no")
if (result) {
    console.log("Result:", result)
} else {
    console.log("Match failed")
}