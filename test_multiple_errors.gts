function test() {
    let x = 42;
    const message = "Hello World";
    
    // First error - unexpected character
    let y = 5 + @;
    
    // Second error - would be found if we continued  
    let z = 10 +;
    
    // Third error - would be found if we continued
    if (x > 10 {
        console.log("missing closing paren");
    }
}