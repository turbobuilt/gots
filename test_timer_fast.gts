console.log("Starting fast timer test");
runtime.timer.setTimeout(function() {
    console.log("Timer executed!")
}, 50);
console.log("Timer scheduled, sleeping 200ms...");
runtime.time.sleepMillis(200);
console.log("Main script done");