console.log("Testing all timer functions");

function timeoutCallback() {
    console.log("setTimeout executed!");
}

function intervalCallback() {
    console.log("setInterval executed!");
}

function immediateCallback() {
    console.log("setImmediate executed!");
}

console.log("Setting setTimeout...");
var timeoutId = setTimeout(timeoutCallback, 100);
console.log("setTimeout ID:", timeoutId);

console.log("Setting setInterval...");
var intervalId = setInterval(intervalCallback, 200);
console.log("setInterval ID:", intervalId);

console.log("Setting setImmediate...");
var immediateId = setImmediate(immediateCallback);
console.log("setImmediate ID:", immediateId);

console.log("All timers set - main execution complete");