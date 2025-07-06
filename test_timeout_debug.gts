console.log("Before setTimeout");

function timerCallback() {
    console.log("Timer executed!");
}

console.log("About to call setTimeout");
var result = setTimeout(timerCallback, 100);
console.log("setTimeout returned:", result);

console.log("After setTimeout");