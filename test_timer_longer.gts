console.log("Starting timer test with longer wait");
runtime.timer.setTimeout(function() {
    console.log("Timer executed!")
}, 500);
console.log("Timer scheduled, waiting 2 seconds...");

// Keep the program alive for a bit longer
for let i: int64 = 0; i < 100; i = i + 1
    runtime.time.sleepMillis(20);
console.log("Main script done");