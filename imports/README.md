# GoTS Circular Import Demo

This folder contains a comprehensive demonstration of GoTS's Node.js-like lazy loading system for handling circular imports.

## Test Files

### Simple Circular Import (math ↔ utils)
- `math.gts` - Math utilities that import logging from utils
- `utils.gts` - Utilities that import PI constant from math
- Creates a simple A↔B circular dependency

### Complex Circular Import (A→B→C→A)
- `complex_a.gts` - Imports from B and C
- `complex_b.gts` - Imports from C and A  
- `complex_c.gts` - Imports from A and B
- Creates a three-way circular dependency chain

### Integration
- `calculator.gts` - Uses both math and utils modules
- `main.gts` - Main demo that tests all scenarios

## Expected Behavior

If the lazy loading system works correctly, you should see:

1. **No infinite loops** during module loading
2. **Partial exports** available during circular loading
3. **Stack traces** for debugging circular imports
4. **All functions** eventually executable once loading completes
5. **Values** from all modules accessible despite circular dependencies

## Running the Demo

```bash
cd /home/me/gots
./gots imports/main.gts
```

This will test:
- Simple circular imports
- Complex three-way circular imports  
- Cross-module function calls
- Value imports across circular dependencies
- Integration scenarios

## Key Features Demonstrated

- **Lazy Loading**: Modules parsed but not executed until needed
- **Circular Detection**: System detects and handles import cycles
- **Partial Exports**: Returns incomplete modules during circular loading
- **Stack Traces**: Detailed import chain debugging
- **Performance**: Module caching prevents re-loading