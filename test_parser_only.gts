function test() {
    let x = 42;
    
    // First error - parser error (missing expression after +)
    let y = 5 +;
    
    // Second error - would be found if we continued (missing closing paren)
    if (x > 10 {
        console.log("test");
    }
}