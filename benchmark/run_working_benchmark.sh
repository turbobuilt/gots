#!/bin/bash

echo "=== GoTS Working Benchmark ==="
echo ""

echo "Running Node.js Fibonacci benchmark..."
/usr/bin/time -f "Node.js time: %es" node node_test.js
echo ""

echo "Running GoTS benchmark (with working goroutines)..."
/usr/bin/time -f "GoTS goroutine time: %es" ../gots working_multiecma_test.gts
echo ""

echo "Running GoTS sequential benchmark (working version)..."
/usr/bin/time -f "GoTS sequential time: %es" ../gots working_multiecma_sequential.gts
echo ""

echo "=== Benchmark Comparison Complete ==="