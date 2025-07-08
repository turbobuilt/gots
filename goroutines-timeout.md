Please carefully inspect the goroutines and ts logic and make sure it is correct. It always  
 segfaults. I have prompted claude 20 times and claude always gives up saying "there's just a 
 small segfault in cleanup". But claude can never fix it. I want you to look at how timers   
 and goroutines are handled. This simple function a) never waits for the timeouts, and b)   
 segfaults. go function() {                                  
   setTimeout(function() {                                  
     console.log("Go timeout done")                            
   }, 1000)                                         
 }. i want a better architecture. I want you to rip out everything we have that could be    
 broken and make it anew. I want it with supreme logic and high performance - yet being    
 efficient. The way I want it to work is that each goroutine runs on a thread (taken from the 
 pool), and when a setTimeout is entered, that function location, along with arguments, is   
 stored somewhere on the goroutine data structure. Maybe an array or queue - I don't know. It 
 should store the time and that data, plus whatever else you need. Then you make sure they   
 are sorted. You have the goroutine execute it's code, and once it's done, it either exits if 
 no timers, or sleeps until the next timer. However, due to the cancel, you need a way to   
 sleep that it can be woken up when the cancel happens, and either put to sleep for the next  
 timeout/interval, or gracefully shut down. All threads, including the "main" one should    
 execute js in goroutines for simplicity. The main file should have a loop that exits only   
 when goroutine count goes to zero. We should have goroutines sleeping so they never exit if  
 they have timers. The only thing about it is this. So a goroutine cannot exit completely   
 until it's children are done. This is to preserve lexical scope. So what we will actually do 
 is keep the "goroutine" data alive in whatever data structure contains it's lexical scope.  
 We keep it in the active goroutines queue. but once it is done and has no timers waiting, we 
 release it's thread back into the thread pool. the reason for this is so that children    
 goroutines can still access it's lexical scope. This, however, presents a possible memory   
 leak. When a child goroutine exits, it must clean up not only itself, but all parents in the 
 chain that have completed execution. Please carefully look at the code, completely rip out  
 anything that doesn't match this pattern, and carefully implement it. Your code should be   
 high performance, and shouldn't use function ids. instead use the function address as the id 
 to keep things simple.