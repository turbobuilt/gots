#include <iostream>
#include <cstring>
#include <cstdint>

// SSO_THRESHOLD must be constexpr for array size
static constexpr size_t SSO_THRESHOLD = sizeof(void*) + sizeof(size_t) + sizeof(size_t) - 1;

// Create a union similar to GoTSString
union TestUnion {
    struct {
        char* data;
        size_t size;
        size_t capacity;
    } large;
    
    struct {
        char buffer[SSO_THRESHOLD + 1];  // 24 bytes
        uint8_t size;                     // 1 byte
    } small;
};

int main() {
    std::cout << "SSO_THRESHOLD = " << SSO_THRESHOLD << std::endl;
    
    std::cout << "sizeof(TestUnion): " << sizeof(TestUnion) << std::endl;
    std::cout << "sizeof(large): " << sizeof(TestUnion::large) << std::endl;
    std::cout << "sizeof(small): " << sizeof(TestUnion::small) << std::endl;
    
    // Test with the IP string
    TestUnion u;
    const char* test_str = "My IP is 192.168.1.1";
    size_t len = strlen(test_str);
    
    // Copy to small buffer
    memcpy(u.small.buffer, test_str, len);
    u.small.buffer[len] = '\0';
    u.small.size = static_cast<uint8_t>(len);
    
    std::cout << "\nAfter copying to small buffer:" << std::endl;
    std::cout << "small.buffer: '" << u.small.buffer << "'" << std::endl;
    std::cout << "small.size: " << static_cast<int>(u.small.size) << std::endl;
    
    // Check what's in the large struct view
    std::cout << "\nViewing as large struct:" << std::endl;
    std::cout << "large.capacity: " << u.large.capacity << std::endl;
    
    // The issue: when we set large.capacity = 0 to indicate small string,
    // we might be overwriting part of the buffer!
    u.large.capacity = 0;
    
    std::cout << "\nAfter setting large.capacity = 0:" << std::endl;
    std::cout << "small.buffer: '" << u.small.buffer << "'" << std::endl;
    
    // Print byte-by-byte to see what happened
    std::cout << "\nByte-by-byte analysis:" << std::endl;
    for (int i = 0; i < 24; i++) {
        char c = u.small.buffer[i];
        if (c >= 32 && c <= 126) {
            std::cout << "[" << i << "] = '" << c << "' (ASCII " << static_cast<int>(c) << ")" << std::endl;
        } else {
            std::cout << "[" << i << "] = '\\x" << std::hex << static_cast<int>(static_cast<unsigned char>(c)) << std::dec << "'" << std::endl;
        }
    }
    
    return 0;
}