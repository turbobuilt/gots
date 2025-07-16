// Test unified Array class - 1D operations
var arr = [1, 2, 3];
console.log("Initial array:", arr.toString());
console.log("Length:", arr.length);

// 1D operations
arr.push(4);
console.log("After push(4):", arr.toString());
console.log("Length:", arr.length);

var popped = arr.pop();
console.log("Popped value:", popped);
console.log("After pop:", arr.toString());

// Element access
console.log("First element:", arr[0]);
console.log("Second element:", arr[1]);

// Slice operations
var slice1 = arr.slice(1, 3);
console.log("arr.slice(1, 3):", slice1.toString());

var slice2 = arr.slice_all();
console.log("arr.slice_all():", slice2.toString());

// Statistical operations
console.log("Sum:", arr.sum());
console.log("Mean:", arr.mean());
console.log("Max:", arr.max());
console.log("Min:", arr.min());

// Static methods
var zeros = Array.zeros([3]);
console.log("Array.zeros([3]):", zeros.toString());

var ones = Array.ones([2, 2]);
console.log("Array.ones([2, 2]):", ones.toString());

var range = Array.arange(0, 5);
console.log("Array.arange(0, 5):", range.toString());

var linear = Array.linspace(0, 1, 5);
console.log("Array.linspace(0, 1, 5):", linear.toString());