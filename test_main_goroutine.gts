function test() {
    console.log("This should run in a goroutine");
}

console.log("Before go statement");
go test();
console.log("After go statement");