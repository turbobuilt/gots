const email = "test.user+foo@example.co.uk".match(/[\w.+-]+@[\w-]+\.[\w.-]+/);
console.log("email", email && email[0] === "test.user+foo@example.co.uk" ? "success" : "fail");