First and foremost we will do static analysis. Since this is a jit, static analysis can yield assembly that is as fast as raw c.

let x = 0;
for (let i = 0; i < 10; ++i)
    x += i; // i is not used outside the loop. static analysis will generate jit code that handles everything i needs to accomplish here. it will not be part of gc/ reference counting.


var obj = { y: 0, z: 1 };
go function() {
    for var i = 0; i < 10; ++i
        obj.y += i;
}
console.log(obj)

In this example obj.y is accessed by multiple scopes AND multiple goroutines (threads). Therefore property y must be created as "atomic". the jit compiler must get the memory address of y