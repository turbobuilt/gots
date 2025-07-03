// Test if the issue is in JIT vs native function calls
function jit_function() {
    return 987;
}

console.log("Direct JIT call:", jit_function());