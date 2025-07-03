# GoTS (Go TypeScript) Compiler

## (Work In Progress!)

Many features do not work yet

A high-performance programming language compiler that combines the best of Go's concurrency model with TypeScript's syntax and static typing, generating direct machine code for maximum performance.

## Features

### Core Language Features
- **JavaScript/TypeScript Syntax Compatibility**: Familiar syntax for web developers
- **Static Typing**: Optional but powerful type system with automatic type inference
- **Goroutines**: Lightweight concurrency using thread pools
- **Promises & Async/Await**: JavaScript-style asynchronous programming
- **Tensor Operations**: Built-in PyTorch-compatible tensor support

### Compiler Features
- **Direct Machine Code Generation**: No intermediate representation for maximum speed
- **Dual Backend Support**: Both x86-64 and WebAssembly targets
- **JIT Compilation**: Runtime code generation and optimization
- **Type-driven Optimization**: Uses static type information for performance

### Performance Features
- **Zero-cost Abstractions**: High-level features compile to efficient machine code
- **Memory-optimized Dynamic Code**: When types are unknown, trades memory for speed
- **Atomic Operations**: Thread-safe operations without locks where possible
- **SharedArrayBuffer Support**: WebAssembly backend uses shared memory

## Syntax Examples

### Basic Function Declaration
```gots
function doSomething(x: int64): int64 {
    return x + 42;
}
```

### Goroutines and Concurrency
```gots
// Spawn a goroutine
go doSomething(100);

// Await a goroutine result
let result = await go doSomething(200);

// Parallel execution with goMap
let numbers = [1, 2, 3, 4, 5];
let results = await Promise.all(numbers.goMap(doSomething));
```

### Type System
```gots
// Explicit typing
let x: int64 = 42;
let y: float64 = 3.14;

// Type inference
let z = 100; // inferred as int64
let w = 2.5; // inferred as float64
```

### Tensor Operations
```gots
var x: tensor = [];
x.push(1);
x.push(2);
x.shape; // returns [2]

var y = tensor.zeros([10, 4, 5]);
var z = y.transpose().matmul(x);
```

### Control Flow (No Parentheses Required)
```gots
if x == 5
    console.log("x is 5")

for let i: int64 = 0; i < 100; ++i
    console.log("i is", i)

for let item of list
    print item
```

## Compilation Targets

### x86-64 Backend
- Direct assembly generation
- Register allocation optimization
- Function calling conventions
- Memory management

### WebAssembly Backend
- WASM bytecode generation
- SharedArrayBuffer for goroutines
- Browser compatibility
- Node.js support

## Type System

### Primitive Types
- `int8`, `int16`, `int32`, `int64`
- `uint8`, `uint16`, `uint32`, `uint64`  
- `float32`, `float64`
- `boolean`, `string`
- `tensor`

### Type Casting Rules
- Types "cast up" to prevent precision loss
- `float32 * int32` → `float64`
- `int64 * float64` → `float64`
- Automatic casting when safe
- Explicit casting when needed

## Concurrency Model

### Goroutines
- Lightweight threads using thread pool
- Automatic scheduling
- Promise-based results
- Exception handling

### Promise System
- `Promise.all()` for parallel execution
- `goMap()` for parallel array processing
- `Promise.race()` for competitive execution
- Async/await syntax support

## Build and Usage

### Building the Compiler
```bash
make clean && make
```

### Running Examples
```bash
./gots_compiler
```

### Makefile Targets
- `make all` - Build compiler
- `make debug` - Debug build
- `make test` - Run tests
- `make clean` - Clean build files

## Architecture

### Compiler Pipeline
1. **Lexer**: Tokenizes source code
2. **Parser**: Builds Abstract Syntax Tree (AST)
3. **Type Inference**: Analyzes and infers types
4. **Code Generation**: Emits machine code directly
5. **Runtime**: Provides goroutine scheduling and memory management

### Key Components
- `compiler.h/cpp` - Main compiler interface
- `lexer.cpp` - Tokenization
- `parser.cpp` - AST construction
- `type_inference.cpp` - Type analysis
- `x86_codegen.cpp` - x86-64 code generation
- `wasm_codegen.cpp` - WebAssembly code generation
- `runtime.h/cpp` - Goroutine scheduler and memory management
- `tensor.h` - Tensor operations

## Performance Characteristics

### Compilation Speed
- Direct code generation (no IR)
- Parallel compilation phases
- Incremental compilation support

### Runtime Performance
- Near-C performance for statically typed code
- Optimized dynamic dispatch for untyped code
- Efficient goroutine scheduling
- Lock-free data structures where possible

### Memory Usage
- Stack-allocated by default
- Shared memory for goroutines
- Garbage collection for dynamic objects
- Memory pooling for frequent allocations

## Future Enhancements

### Planned Features
- Module system
- Package manager
- IDE integration
- Debugging support
- Profiling tools
- Standard library expansion

### Optimization Opportunities
- LLVM backend option
- GPU computation support
- Advanced type inference
- Link-time optimization
- Profile-guided optimization

## License

This implementation is a demonstration of compiler design principles and high-performance language features.# gots
