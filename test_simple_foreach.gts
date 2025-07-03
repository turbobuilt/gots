let data = { x: "hello" };
console.log("About to start for-each");
for each key, value in data {
    console.log("Inside loop: key =", key, "value =", value);
}
console.log("For-each completed");
