setTimeout(function() {
    console.log("First timeout");
    setTimeout(function() {
        console.log("Nested timeout");
    }, 1000);
}, 1000);

setTimeout(function() {
    console.log("Second timeout");
}, 2000);