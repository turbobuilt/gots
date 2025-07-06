// Test runtime optimization - runtime.time.now() should compile to direct call

console.log("Testing runtime.time.now() optimization");

// This should compile to a direct __runtime_time_now_millis() call
// with no property lookups or object access
let start = runtime.time.now();

console.log("Current time:", start);

// Test other runtime calls
console.log("Process PID:", runtime.process.pid());
console.log("Current directory:", runtime.process.cwd());