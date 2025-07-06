console.log("Before setTimeout");

function testCallback() {
    console.log("Timer fired!");
}

let timerId = runtime.timer.setTimeout(testCallback, 500);
console.log("Timer scheduled with ID:", timerId);
console.log("After setTimeout");