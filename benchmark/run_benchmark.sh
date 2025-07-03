#!/bin/bash

echo "=== GoTS Benchmark ==="
echo ""

echo "Running Node.js Fibonacci benchmark..."
/usr/bin/time -f "Node.js time: %es" node node_test.js
echo ""

echo "Running GoTS benchmark (with goroutines)..."
/usr/bin/time -f "GoTS time: %es" ../gots multiecma_test.gts
echo ""

echo "Running GoTS sequential benchmark..."
/usr/bin/time -f "GoTS sequential time: %es" ../gots multiecma_sequential.gts
