// Comprehensive benchmark: Classes with Goroutines
// Tests both object-oriented features and parallel performance

class WorkerPool {
    workerCount: int64;
    taskMultiplier: int64;
    
    constructor(workers: int64, multiplier: int64) {
        this.workerCount = workers;
        this.taskMultiplier = multiplier;
    }
    
    // Heavy computation task
    private computeTask(value: int64): int64 {
        let result: int64 = 0;
        for (let i: int64 = 0; i < value * this.taskMultiplier; i++) {
            result += i % 1000;
        }
        return result;
    }
    
    // Sequential processing
    processSequential(tasks: int64[]): int64[] {
        let results: int64[] = [];
        for (let i: int64 = 0; i < tasks.length; i++) {
            results.push(this.computeTask(tasks[i]));
        }
        return results;
    }
    
    // Parallel processing with goroutines
    processParallel(tasks: int64[]): Promise<int64[]> {
        let promises: Promise<int64>[] = [];
        for (let i: int64 = 0; i < tasks.length; i++) {
            promises.push(go this.computeTask(tasks[i]));
        }
        return Promise.all(promises);
    }
    
    // Mixed workload: some sequential, some parallel
    processMixed(tasks: int64[]): Promise<int64[]> {
        let results: int64[] = [];
        let promises: Promise<int64>[] = [];
        
        // Process half sequentially
        for (let i: int64 = 0; i < tasks.length / 2; i++) {
            results.push(this.computeTask(tasks[i]));
        }
        
        // Process half in parallel
        for (let i: int64 = tasks.length / 2; i < tasks.length; i++) {
            promises.push(go this.computeTask(tasks[i]));
        }
        
        let parallelResults = await Promise.all(promises);
        
        // Combine results
        for (let i: int64 = 0; i < parallelResults.length; i++) {
            results.push(parallelResults[i]);
        }
        
        return results;
    }
}

// Create test data
let tasks: int64[] = [];
for (let i: int64 = 0; i < 20; i++) {
    tasks.push(1000 + i * 100);
}

let pool = new WorkerPool(8, 1000);

// Benchmark sequential processing
console.time("class-sequential");
let sequentialResults = pool.processSequential(tasks);
console.timeEnd("class-sequential");

// Benchmark parallel processing
console.time("class-parallel");
let parallelResults = await pool.processParallel(tasks);
console.timeEnd("class-parallel");

// Benchmark mixed processing
console.time("class-mixed");
let mixedResults = await pool.processMixed(tasks);
console.timeEnd("class-mixed");

// Verify all approaches give same results
let allMatch = true;
for (let i: int64 = 0; i < tasks.length; i++) {
    if (sequentialResults[i] != parallelResults[i]) {
        allMatch = false;
        break;
    }
}

console.log("Results consistency check:", allMatch ? "✓ PASS" : "✗ FAIL");
console.log("Sequential sum:", sequentialResults.reduce((a, b) => a + b));
console.log("Parallel sum:", parallelResults.reduce((a, b) => a + b));