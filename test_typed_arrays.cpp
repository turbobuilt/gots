#include "runtime.h"
#include "compiler.h"
#include <iostream>

using namespace gots;

int main() {
    std::cout << "Testing GoTS Typed Arrays (Maximum Performance)\n";
    
    // Test Int32Array
    std::cout << "\n=== Testing Int32Array ===\n";
    auto* int32_arr = static_cast<Int32Array*>(__typed_array_create_int32(4));
    
    __typed_array_push_int32(int32_arr, 10);
    __typed_array_push_int32(int32_arr, 20);
    __typed_array_push_int32(int32_arr, 30);
    
    std::cout << "Int32Array contents: ";
    __console_log_typed_array_int32(int32_arr);
    
    std::cout << "Size: " << __typed_array_size(int32_arr) << std::endl;
    std::cout << "Element [1]: " << __typed_array_get_int32(int32_arr, 1) << std::endl;
    
    // Test Float64Array
    std::cout << "\n=== Testing Float64Array ===\n";
    auto* float64_arr = static_cast<Float64Array*>(__typed_array_create_float64(4));
    
    __typed_array_push_float64(float64_arr, 3.14159);
    __typed_array_push_float64(float64_arr, 2.71828);
    __typed_array_push_float64(float64_arr, 1.41421);
    
    std::cout << "Float64Array contents: ";
    __console_log_typed_array_float64(float64_arr);
    
    // Test performance with direct access
    std::cout << "\n=== Performance Test ===\n";
    auto* perf_arr = static_cast<Int64Array*>(__typed_array_create_int64(1000));
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Push 1000 elements
    for (int64_t i = 0; i < 1000; i++) {
        __typed_array_push_int64(perf_arr, i * i);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    
    std::cout << "Pushed 1000 elements in " << duration.count() << " microseconds\n";
    std::cout << "Array size: " << __typed_array_size(perf_arr) << std::endl;
    std::cout << "Last element: " << __typed_array_get_int64(perf_arr, 999) << std::endl;
    
    // Test direct array access (maximum performance)
    std::cout << "\n=== Direct Access Test ===\n";
    auto* direct_arr = static_cast<Uint32Array*>(__typed_array_create_uint32(10));
    
    for (uint32_t i = 0; i < 10; i++) {
        __typed_array_push_uint32(direct_arr, i * 100);
    }
    
    // Direct access using operator[] (no bounds checking for max performance)
    std::cout << "Direct access test: ";
    for (int i = 0; i < 10; i++) {
        std::cout << (*direct_arr)[i] << " ";
    }
    std::cout << std::endl;
    
    std::cout << "\nTyped Arrays test completed successfully!\n";
    return 0;
}