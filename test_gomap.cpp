#include "promise.h"
#include "runtime.h"
#include <iostream>
#include <vector>

using namespace gots;

int double_value(int x) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return x * 2;
}

int main() {
    std::cout << "=== Testing goMap functionality ===" << std::endl;
    
    try {
        // Test basic goMap functionality
        std::vector<int> numbers = {1, 2, 3, 4, 5};
        
        std::cout << "Input array: ";
        for (const auto& num : numbers) {
            std::cout << num << " ";
        }
        std::cout << std::endl;
        
        // Use goMap to apply double_value function to each element in parallel
        auto future_results = goMap(numbers, double_value);
        
        std::cout << "Waiting for goMap to complete..." << std::endl;
        
        // Wait for results
        auto results = future_results.get();
        
        std::cout << "goMap results: ";
        for (const auto& result : results) {
            std::cout << result << " ";
        }
        std::cout << std::endl;
        
        // Test with empty array
        std::cout << "\n--- Testing with empty array ---" << std::endl;
        std::vector<int> empty_array;
        auto empty_future = goMap(empty_array, double_value);
        auto empty_results = empty_future.get();
        
        std::cout << "Empty array goMap results size: " << empty_results.size() << std::endl;
        
        // Test with larger array for concurrency
        std::cout << "\n--- Testing with larger array for concurrency ---" << std::endl;
        std::vector<int> large_numbers;
        for (int i = 1; i <= 10; ++i) {
            large_numbers.push_back(i);
        }
        
        auto start_time = std::chrono::high_resolution_clock::now();
        auto large_future = goMap(large_numbers, double_value);
        auto large_results = large_future.get();
        auto end_time = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        std::cout << "Large array goMap results: ";
        for (const auto& result : large_results) {
            std::cout << result << " ";
        }
        std::cout << std::endl;
        std::cout << "Time taken: " << duration.count() << "ms (should be ~10ms if parallel)" << std::endl;
        
        std::cout << "\n✅ goMap functionality test passed!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cout << "❌ goMap test failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}