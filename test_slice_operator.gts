// Test slice operator [:] implementation
var arr = Array.arange(0, 10);  // [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]

// Basic slice tests
var all = arr[:];           // Get all elements
var first_half = arr[:5];   // Get first 5 elements  
var second_half = arr[5:];  // Get elements from index 5 onwards
var middle = arr[2:8];      // Get elements from index 2 to 7
var evens = arr[::2];       // Get every 2nd element
var odds = arr[1::2];       // Get every 2nd element starting from index 1
var reverse = arr[::-1];    // Reverse the array

// Matrix slicing
var matrix = Array.arange(0, 12).reshape([3, 4]);
var row = matrix[1, :];        // Get second row
var col = matrix[:, 2];        // Get third column
var submatrix = matrix[1:3, 1:3];  // Get 2x2 submatrix

console.log("Original array:", arr.toString());
console.log("arr[:]:", all.toString());
console.log("arr[:5]:", first_half.toString());
console.log("arr[5:]:", second_half.toString());
console.log("arr[2:8]:", middle.toString());
console.log("arr[::2]:", evens.toString());
console.log("arr[1::2]:", odds.toString());
console.log("arr[::-1]:", reverse.toString());

console.log("Matrix:", matrix.toString());
console.log("matrix[1, :]:", row.toString());
console.log("matrix[:, 2]:", col.toString());
console.log("matrix[1:3, 1:3]:", submatrix.toString());