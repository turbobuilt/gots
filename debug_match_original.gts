// Test original pattern
var result = "bobby brown".match(/bob/)
console.log("Result exists:", result ? "yes" : "no")
if (result) {
    console.log("Result:", result)
} else {
    console.log("Match failed")
}