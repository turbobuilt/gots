// Test various edge cases
console.log("Test 1 - Match found:", "hello world".match(/world/))
console.log("Test 2 - No match:", "hello world".match(/xyz/))
console.log("Test 3 - Case sensitive:", "Hello World".match(/hello/))
console.log("Test 4 - Empty string:", "".match(/test/))
console.log("Edge case tests completed")