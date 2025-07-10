// GoTS Lock Performance Demonstration
// This shows how Lock operations are JIT compiled to assembly

function testBasicLocking() {
    // Lock creation - JIT emits optimized allocation
    let lock = new Lock();
    
    // Lock acquisition - JIT emits direct x86-64 atomic instructions
    lock.lock();
    
    // Critical section
    console.log("In critical section");
    
    // Lock release - JIT emits direct atomic release
    lock.unlock();
}

function testTryLock() {
    let lock = new Lock();
    
    // Try lock - JIT emits atomic compare-exchange
    if (lock.try_lock()) {
        console.log("Got lock immediately");
        lock.unlock();
    } else {
        console.log("Lock was contended");
    }
}

function testRecursiveLocking() {
    let lock = new Lock();
    
    lock.lock();
    console.log("First lock acquired");
    
    // Recursive lock - JIT optimizes to increment counter
    lock.lock();
    console.log("Recursive lock acquired");
    
    lock.unlock(); // Decrement counter
    lock.unlock(); // Final release
}

function testLockTimeout() {
    let lock = new Lock();
    
    // Try lock with timeout - JIT emits optimized timeout handling
    if (lock.try_lock_for(1000)) { // 1 second timeout
        console.log("Got lock within timeout");
        lock.unlock();
    } else {
        console.log("Timeout waiting for lock");
    }
}

// High-performance producer-consumer using locks
function producerConsumer() {
    let dataLock = new Lock();
    let data = [];
    
    // Producer goroutine
    go function producer() {
        for (let i = 0; i < 100; i++) {
            dataLock.lock();
            data.push(i);
            console.log("Produced:", i);
            dataLock.unlock();
            
            // Yield to allow consumer to run
            await new Promise(resolve => setTimeout(resolve, 1));
        }
    }();
    
    // Consumer goroutine
    go function consumer() {
        while (true) {
            dataLock.lock();
            if (data.length > 0) {
                let item = data.shift();
                console.log("Consumed:", item);
            }
            dataLock.unlock();
            
            // Yield to allow producer to run
            await new Promise(resolve => setTimeout(resolve, 1));
        }
    }();
}

// RAII-style lock guard pattern (would be optimized by JIT)
function criticalSection(lock: Lock, work: () => void) {
    lock.lock();
    try {
        work();
    } finally {
        lock.unlock();
    }
}

// Test all lock operations
function main() {
    console.log("Testing GoTS Lock Performance...");
    
    testBasicLocking();
    testTryLock();
    testRecursiveLocking();
    testLockTimeout();
    
    // Run producer-consumer demo
    producerConsumer();
    
    console.log("Lock tests completed");
}

main();