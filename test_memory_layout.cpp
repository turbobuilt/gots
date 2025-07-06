#include <iostream>
#include <cstring>
#include <cstdint>
#include <cstddef>

static constexpr size_t SSO_THRESHOLD = sizeof(void*) + sizeof(size_t) + sizeof(size_t) - 1;

union TestUnion {
    struct {
        char* data;      // bytes 0-7
        size_t size;     // bytes 8-15
        size_t capacity; // bytes 16-23
    } large;
    
    struct {
        char buffer[SSO_THRESHOLD + 1];  // bytes 0-23 (24 bytes)
        uint8_t size;                     // byte 24
    } small;
};

int main() {
    std::cout << "Memory Layout Analysis:" << std::endl;
    std::cout << "=======================" << std::endl;
    
    TestUnion u;
    
    // Show offsets
    std::cout << "large.data offset: " << offsetof(TestUnion, large.data) << " (bytes 0-7)" << std::endl;
    std::cout << "large.size offset: " << offsetof(TestUnion, large.size) << " (bytes 8-15)" << std::endl;
    std::cout << "large.capacity offset: " << offsetof(TestUnion, large.capacity) << " (bytes 16-23)" << std::endl;
    std::cout << "small.buffer offset: " << offsetof(TestUnion, small.buffer) << " (bytes 0-23)" << std::endl;
    std::cout << "small.size offset: " << offsetof(TestUnion, small.size) << " (byte 24)" << std::endl;
    
    std::cout << "\nThe problem: large.capacity (bytes 16-23) overlaps with small.buffer[16-23]!" << std::endl;
    
    // Demonstrate the issue
    const char* test_str = "My IP is 192.168.1.1";
    memcpy(u.small.buffer, test_str, strlen(test_str));
    u.small.buffer[strlen(test_str)] = '\0';
    
    std::cout << "\nBefore set_small_flag(): '" << u.small.buffer << "'" << std::endl;
    
    // This is what set_small_flag() does - it zeros out bytes 16-23!
    u.large.capacity = 0;
    
    std::cout << "After set_small_flag():  '" << u.small.buffer << "'" << std::endl;
    
    return 0;
}