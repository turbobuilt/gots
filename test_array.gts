// Test comprehensive Array functionality with PyTorch-style operations and slice syntax

// Basic Array creation and properties
var a: array = [1, 2, 3, 4, 5];
console.log("1D Array:", a.toString());
console.log("Length:", a.length); // 5
console.log("Shape:", a.shape); // [5]
console.log("Size:", a.size()); // 5

// Multi-dimensional arrays
var b = Array.zeros([3, 4]);
console.log("2D Zeros:", b.toString());
console.log("Shape:", b.shape); // [3, 4]

var c = Array.ones([2, 3, 2]);
console.log("3D Ones:", c.toString());
console.log("Shape:", c.shape); // [2, 3, 2]

// Array creation with specific values
var d = Array.full([3, 3], 7);
console.log("3x3 filled with 7:", d.toString());

// Identity matrix
var eye = Array.eye(4);
console.log("4x4 Identity:", eye.toString());

// Range and linspace
var range1 = Array.arange(0, 10, 2); // [0, 2, 4, 6, 8]
console.log("Arange 0-10 step 2:", range1.toString());

var linear = Array.linspace(0, 1, 11); // [0, 0.1, 0.2, ..., 1.0]
console.log("Linspace 0-1 (11 points):", linear.toString());

var logspace = Array.logspace(0, 2, 5); // [1, 10, 100]
console.log("Logspace 10^0 to 10^2:", logspace.toString());

// Random arrays
var random_uniform = Array.random([2, 3], 0, 10);
console.log("Random uniform [0,10]:", random_uniform.toString());

var random_normal = Array.randn([2, 3], 0, 1);
console.log("Random normal (mean=0, std=1):", random_normal.toString());

// Basic slicing
var x = Array.arange(0, 10); // [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]
console.log("Original array:", x.toString());

// PyTorch-style slicing
var slice1 = x[:];           // All elements
console.log("x[:]:", slice1.toString());

var slice2 = x[1:5];         // Elements 1-4
console.log("x[1:5]:", slice2.toString());

var slice3 = x[::2];         // Every 2nd element
console.log("x[::2]:", slice3.toString());

var slice4 = x[1:8:2];       // From 1 to 8, step 2
console.log("x[1:8:2]:", slice4.toString());

var slice5 = x[::-1];        // Reverse
console.log("x[::-1]:", slice5.toString());

var slice6 = x[2:];          // From index 2 to end
console.log("x[2:]:", slice6.toString());

var slice7 = x[:5];          // From start to index 5
console.log("x[:5]:", slice7.toString());

// Multi-dimensional slicing
var matrix = Array.arange(0, 12).reshape([3, 4]);
console.log("3x4 matrix:", matrix.toString());

var row_slice = matrix[1, :];      // Second row
console.log("matrix[1, :]:", row_slice.toString());

var col_slice = matrix[:, 2];      // Third column
console.log("matrix[:, 2]:", col_slice.toString());

var submatrix = matrix[1:3, 1:3];  // 2x2 submatrix
console.log("matrix[1:3, 1:3]:", submatrix.toString());

// Array operations
var arr1 = Array.arange(1, 6);     // [1, 2, 3, 4, 5]
var arr2 = Array.arange(2, 7);     // [2, 3, 4, 5, 6]

console.log("arr1:", arr1.toString());
console.log("arr2:", arr2.toString());

// Element-wise operations
var add_result = arr1 + arr2;
console.log("arr1 + arr2:", add_result.toString());

var sub_result = arr2 - arr1;
console.log("arr2 - arr1:", sub_result.toString());

var mul_result = arr1 * arr2;
console.log("arr1 * arr2:", mul_result.toString());

var div_result = arr2 / arr1;
console.log("arr2 / arr1:", div_result.toString());

// Scalar operations
var scalar_add = arr1 + 10;
console.log("arr1 + 10:", scalar_add.toString());

var scalar_mul = arr1 * 2;
console.log("arr1 * 2:", scalar_mul.toString());

// Dot product and matrix multiplication
var vec1 = Array.arange(1, 4);     // [1, 2, 3]
var vec2 = Array.arange(2, 5);     // [2, 3, 4]
var dot_product = vec1.dot(vec2);  // 1*2 + 2*3 + 3*4 = 20
console.log("Dot product:", dot_product.toString());

// Matrix multiplication
var mat1 = Array.arange(1, 7).reshape([2, 3]);  // [[1,2,3], [4,5,6]]
var mat2 = Array.arange(1, 7).reshape([3, 2]);  // [[1,2], [3,4], [5,6]]
var matmul_result = mat1.dot(mat2);
console.log("Matrix multiplication:", matmul_result.toString());

// Statistical operations
var stats_array = Array.arange(1, 11);  // [1, 2, 3, ..., 10]
console.log("Stats array:", stats_array.toString());
console.log("Sum:", stats_array.sum());
console.log("Mean:", stats_array.mean());
console.log("Max:", stats_array.max());
console.log("Min:", stats_array.min());
console.log("Std:", stats_array.std());

// Shape manipulation
var original = Array.arange(1, 13);      // 12 elements
console.log("Original (12 elements):", original.toString());

var reshaped_2d = original.reshape([3, 4]);
console.log("Reshaped to 3x4:", reshaped_2d.toString());

var reshaped_3d = original.reshape([2, 2, 3]);
console.log("Reshaped to 2x2x3:", reshaped_3d.toString());

var transposed = reshaped_2d.transpose();
console.log("Transposed (4x3):", transposed.toString());

var flattened = reshaped_3d.flatten();
console.log("Flattened:", flattened.toString());

// Advanced slicing with negative indices
var neg_slice_array = Array.arange(0, 10);
console.log("Array for negative slicing:", neg_slice_array.toString());

var neg1 = neg_slice_array[-3:];     // Last 3 elements
console.log("arr[-3:]:", neg1.toString());

var neg2 = neg_slice_array[:-2];     // All but last 2
console.log("arr[:-2]:", neg2.toString());

var neg3 = neg_slice_array[-5:-1];   // Elements -5 to -1
console.log("arr[-5:-1]:", neg3.toString());

// Complex slicing patterns
var complex_array = Array.arange(0, 20);
console.log("Complex array:", complex_array.toString());

var pattern1 = complex_array[2:15:3];    // Start 2, end 15, step 3
console.log("arr[2:15:3]:", pattern1.toString());

var pattern2 = complex_array[10:2:-2];   // Reverse with step
console.log("arr[10:2:-2]:", pattern2.toString());

// Array modification
var mutable_array = Array.arange(1, 6);
console.log("Before modification:", mutable_array.toString());

mutable_array.push(6);
console.log("After push(6):", mutable_array.toString());

var popped = mutable_array.pop();
console.log("Popped value:", popped);
console.log("After pop:", mutable_array.toString());

// Typed arrays
var int_array: [int32] = [1, 2, 3, 4, 5];
var float_array: [float32] = [1.1, 2.2, 3.3, 4.4, 5.5];

console.log("Int32 array:", int_array.toString());
console.log("Float32 array:", float_array.toString());

// Chaining operations
var chained_result = Array.arange(1, 11)
    .reshape([2, 5])
    .transpose()
    .slice([1:4, :])
    .flatten();
console.log("Chained operations result:", chained_result.toString());