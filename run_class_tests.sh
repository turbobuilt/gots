#!/bin/bash

echo "=== GoTS Class Implementation Test Suite ==="
echo ""

# Array of test files in order of complexity
tests=(
    "test_class_basic.gts"
    "test_class_dart_style.gts" 
    "test_class_this_context.gts"
    "test_class_access_modifiers.gts"
    "test_class_static.gts"
    "test_class_inheritance.gts"
    "test_class_goroutines.gts"
)

# Array of benchmark files
benchmarks=(
    "benchmark_class_performance.gts"
    "benchmark_class_goroutines.gts"
)

echo "Running Class Feature Tests..."
echo "=========================="

for test in "${tests[@]}"; do
    if [ -f "$test" ]; then
        echo "Testing: $test"
        echo "----------------------------------------"
        
        # Try to run the test
        if timeout 30s ./gots "$test" 2>&1; then
            echo "✓ $test: PASSED"
        else
            exit_code=$?
            if [ $exit_code -eq 124 ]; then
                echo "✗ $test: TIMEOUT (>30s)"
            else
                echo "✗ $test: FAILED (exit code $exit_code)"
            fi
        fi
        echo ""
    else
        echo "✗ $test: FILE NOT FOUND"
        echo ""
    fi
done

echo "Running Performance Benchmarks..."
echo "================================"

for benchmark in "${benchmarks[@]}"; do
    if [ -f "$benchmark" ]; then
        echo "Benchmarking: $benchmark"
        echo "----------------------------------------"
        
        # Run benchmark with timing
        if timeout 60s /usr/bin/time -f "Wall time: %es, CPU: %Us, Memory: %MkB" ./gots "$benchmark" 2>&1; then
            echo "✓ $benchmark: COMPLETED"
        else
            exit_code=$?
            if [ $exit_code -eq 124 ]; then
                echo "✗ $benchmark: TIMEOUT (>60s)"
            else
                echo "✗ $benchmark: FAILED (exit code $exit_code)"
            fi
        fi
        echo ""
    else
        echo "✗ $benchmark: FILE NOT FOUND"
        echo ""
    fi
done

echo "=== Test Summary ==="
echo "Class tests created: ${#tests[@]}"
echo "Benchmarks created: ${#benchmarks[@]}"
echo ""
echo "Next steps:"
echo "1. Implement TokenType::CLASS in lexer"
echo "2. Add ClassDecl AST node in compiler.h"  
echo "3. Implement class parsing in parser.cpp"
echo "4. Add class code generation in codegen"
echo "5. Test with: bash run_class_tests.sh"