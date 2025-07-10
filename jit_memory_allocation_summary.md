# JIT Compiler Memory Allocation in GoTS

Based on my analysis of the codebase, here's how the JIT compiler handles memory allocation:

## Key Files and Components

### 1. Memory Management System (`memory_manager.h/cpp`)
- Implements a reference-counted memory management system with cycle detection
- Provides `__mem_alloc_managed()` function for managed allocations
- Key functions exposed to JIT:
  - `__mem_alloc_managed(size_t size, const char* type_name)` - Allocates managed memory
  - `__mem_add_ref(void* ptr)` - Increments reference count
  - `__mem_release(void* ptr)` - Decrements reference count
  - `__mem_set_parent_scope(void* child, void* parent)` - Sets lexical scope relationships

### 2. Object Creation (`ast_codegen.cpp`)
- NewExpression generates code to create objects via `__object_create()`
- Process:
  1. Determines property count from class registry
  2. Emits code to call `__object_create(class_name, property_count)`
  3. Object ID is returned in RAX register
  4. Constructor is called with object ID as 'this'

### 3. X86 Code Generation (`x86_codegen.cpp`)
- Registers runtime functions including:
  - `__goroutine_alloc_shared()` - For shared memory allocation in goroutines
  - String allocation functions like `__string_create_empty()` and `__string_intern()`
- Stack allocation is used for small strings (SSO - Small String Optimization)
- Heap allocation falls back to runtime functions

### 4. WebAssembly Backend (`wasm_codegen.cpp`)
- Currently implements basic opcodes but memory allocation specifics not fully implemented
- Designed to write to SharedArrayBuffer for multi-threaded access

## Memory Allocation Patterns

### 1. Object Allocation
```cpp
// Generated code for: new MyClass()
gen.emit_mov_reg_imm(7, class_name_ptr);  // RDI = class name
gen.emit_mov_reg_imm(6, property_count);  // RSI = property count  
gen.emit_call("__object_create");         // Returns object ID in RAX
```

### 2. String Allocation
- Empty strings: `__string_create_empty()`
- String literals: `__string_intern()` for deduplication
- String concatenation: Stack allocation for small strings, heap for large

### 3. Array/TypedArray Allocation
- Uses template-based `TypedArray<T>` classes
- Direct memory allocation with `new T[capacity]`
- Automatic capacity management with doubling strategy

### 4. Managed Memory
- All objects inherit from `ManagedObject` base class
- Reference counting with atomic operations
- Cycle detection runs on separate thread
- Parent-child scope relationships for lexical scoping

## Key Observations

1. **No direct __mem_alloc_managed calls in JIT**: The JIT compiler doesn't directly emit calls to `__mem_alloc_managed`. Instead, it uses higher-level functions like `__object_create` which likely handle managed allocation internally.

2. **Runtime function table**: X86 backend maintains a table of runtime functions that can be called from JIT code.

3. **Stack vs Heap**: The compiler optimizes by using stack allocation for small/temporary objects (like SSO strings) and heap for larger/persistent objects.

4. **Thread safety**: Uses atomic operations for reference counting and mutex protection for shared data structures.

5. **Missing implementations**: Several allocation-related functions are declared but implementations weren't found in the searched files, suggesting they may be in other runtime files or not yet implemented.

## Areas for Further Investigation

1. The actual implementation of `__object_create()` 
2. How SharedArrayBuffer allocation works for WebAssembly
3. Tensor type allocation (mentioned in CLAUDE.md but not found in code)
4. Integration between managed memory system and JIT-generated code