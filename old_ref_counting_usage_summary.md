# Old Reference Counting System Usage Summary

This document lists all the files that are still using the old reference counting system from `memory_manager.h/cpp` and need to be updated to use the new GC system.

## Files Using Old Reference Counting

### 1. **lexical_scope.h**
- **VariableBinding** struct:
  - Uses `std::atomic<int64_t> ref_count{1}` (line 27)
  - Implements `add_ref()` method (line 90-92)
  - Implements `release()` method (line 94-98)
  
- **LexicalScope** class:
  - Uses `std::atomic<int64_t> ref_count{1}` (line 131)
  - Implements `add_ref()` method (line 171-173)
  - Implements `release()` method (line 175-179)

### 2. **unified_event_system.h**
- **Variable** class:
  - Uses `std::atomic<int> ref_count_{1}` (line 42)
  - Implements `add_ref()` method (line 47)
  - Implements `release()` method (line 48-52)
  
- **LexicalEnvironment** class:
  - Uses `std::atomic<int> ref_count_{0}` (line 94)
  - Implements `add_ref()` method (line 105-107)
  - Implements `release()` method (line 109-113)

### 3. **gc_integration_example.cpp**
- Contains commented examples showing old vs new allocation:
  - References to `__mem_alloc_managed` (line 64)
  - References to `__mem_add_ref` (line 73)
  - References to `__mem_release` (line 74)
  - These are in comments/documentation only

### 4. **memory_manager.h/cpp**
- Contains the old reference counting implementation:
  - `ManagedObject` base class
  - `ManagedPtr` smart pointer template
  - `ManagedScope` and `ManagedClosure` classes
  - C API functions: `__mem_alloc_managed`, `__mem_add_ref`, `__mem_release`, etc.

## Files Checked - No Old Reference Counting Found
These files were initially flagged but after inspection, they do not use the old reference counting system:
- demo_advanced_goroutines.cpp - No usage found
- goroutine_advanced.cpp - No usage found
- goroutine_coordinated_gc.cpp - No usage found
- goroutine_system.cpp - No usage found
- goroutine_system.h - Only has `release_shared_memory()` which is unrelated
- goroutine_write_barriers.cpp - No usage found
- parser.cpp - No usage found
- runtime_goroutine_advanced.cpp - No usage found
- runtime_syscalls.cpp - No usage found
- runtime_unified.cpp - No usage found
- x86_codegen.cpp - No usage found
- goroutine_advanced.h - No usage found
- goroutine_aware_gc.h - Uses new GC system
- jit_gc_integration.h - Uses new GC system
- revised_jit_integration.h - No usage found
- runtime_object.h - No usage found
- runtime_syscalls.h - No usage found

## Confirmed Files Using Old Reference Counting
Only these files actually use the old reference counting system:
1. **memory_manager.h/cpp** - The old implementation itself
2. **lexical_scope.h** - Manual ref counting in VariableBinding and LexicalScope
3. **unified_event_system.h** - Manual ref counting in Variable and LexicalEnvironment
4. **gc_integration_example.cpp** - Only in comments/documentation

## Recommended Action Plan

1. **Remove/Archive Old Files**: 
   - Move `memory_manager.h` and `memory_manager.cpp` to an archive directory or remove them entirely

2. **Update lexical_scope.h**:
   - Remove manual reference counting from `VariableBinding` and `LexicalScope`
   - Integrate with the new GC system using appropriate GC-managed allocation

3. **Update unified_event_system.h**:
   - Remove manual reference counting from `Variable` and `LexicalEnvironment`
   - Use GC-managed objects instead

4. **Clean up gc_integration_example.cpp**:
   - Remove or update the old allocation examples in comments

5. **Verify Other Files**:
   - Check each file in the "Files to Check Further" list to ensure they're not using the old API
   - Update any remaining usages to the new GC system

## New GC System APIs to Use Instead

Replace old reference counting with:
- `__gc_allocate()` for allocation
- `__gc_write_barrier()` for pointer updates
- No manual `add_ref()`/`release()` needed
- Use TLAB allocation for thread-local objects
- Stack allocation for non-escaping objects