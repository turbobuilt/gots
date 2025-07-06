#include <iostream>
#include <cstring>
#include <cstdint>

// Test simple version to understand the issue
int main() {
    const char* test_str = "My IP is 192.168.1.1";
    std::cout << "Test string: '" << test_str << "'" << std::endl;
    std::cout << "Length: " << strlen(test_str) << std::endl;
    
    // Check each character
    for (size_t i = 0; i < strlen(test_str); i++) {
        std::cout << "[" << i << "] = '" << test_str[i] << "' (ASCII " << static_cast<int>(test_str[i]) << ")" << std::endl;
    }
    
    // Test memcpy
    char buffer[25];
    memcpy(buffer, test_str, strlen(test_str));
    buffer[strlen(test_str)] = '\0';
    
    std::cout << "\nAfter memcpy:" << std::endl;
    std::cout << "Buffer: '" << buffer << "'" << std::endl;
    
    // Analyze the truncation point
    std::cout << "\nChecking around index 16:" << std::endl;
    for (int i = 14; i <= 20; i++) {
        std::cout << "[" << i << "] = '" << buffer[i] << "' (ASCII " << static_cast<int>(buffer[i]) << ")" << std::endl;
    }
    
    return 0;
}