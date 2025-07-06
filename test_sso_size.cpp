#include <iostream>
#include <cstring>

int main() {
    const char* test_str = "My IP is 192.168.1.1";
    size_t len = strlen(test_str);
    
    std::cout << "Test string: " << test_str << std::endl;
    std::cout << "String length: " << len << std::endl;
    
    size_t SSO_THRESHOLD = sizeof(void*) + sizeof(size_t) + sizeof(size_t) - 1;
    std::cout << "SSO_THRESHOLD: " << SSO_THRESHOLD << std::endl;
    std::cout << "sizeof(void*): " << sizeof(void*) << std::endl;
    std::cout << "sizeof(size_t): " << sizeof(size_t) << std::endl;
    
    // Check if it would fit in SSO
    if (len <= SSO_THRESHOLD) {
        std::cout << "String SHOULD fit in SSO" << std::endl;
    } else {
        std::cout << "String would NOT fit in SSO" << std::endl;
    }
    
    // Test copying with exact buffer size
    char buffer[SSO_THRESHOLD + 1];
    memcpy(buffer, test_str, len);
    buffer[len] = '\0';
    std::cout << "Buffer after copy: " << buffer << std::endl;
    
    return 0;
}
