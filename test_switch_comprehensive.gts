// Test default case
var x: int64 = 5;

console.log("Testing x =", x);
switch (x) {
    case 1:
        console.log("One");
        break;
    case 2:
        console.log("Two");
        break;
    case 3:
        console.log("Three");
        break;
    default:
        console.log("Default case: Other");
        break;
}

// Test fall-through behavior (without break)
var y: int64 = 2;
console.log("Testing fall-through with y =", y);
switch (y) {
    case 1:
        console.log("One - will fall through");
    case 2:
        console.log("Two - will fall through");  
    case 3:
        console.log("Three - end here");
        break;
    default:
        console.log("Default");
        break;
}

console.log("All tests complete");