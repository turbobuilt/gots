console.log("Testing function expression timer");

runtime.timer.setTimeout(function() {
    console.log("Timer fired!");
}, 500);

console.log("Timer set");