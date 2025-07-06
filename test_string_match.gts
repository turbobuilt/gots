// Test string match functionality
const text = "test.user+foo@example.co.uk";
const email = text.match(/[\w.+-]+@[\w-]+\.[\w.-]+/);
console.log("email", email ? "success" : "fail");