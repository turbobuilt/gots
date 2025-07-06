#include <iostream>
#include <cstring>
#include <cstdint>

// Proposed fix for GoTSString
class GoTSString {
public:
    static constexpr size_t SSO_THRESHOLD = sizeof(void*) + sizeof(size_t) + sizeof(size_t) - 1;
    
private:
    union {
        struct {
            char* data;
            size_t size;
            size_t capacity;
        } large;
        
        struct {
            char buffer[SSO_THRESHOLD + 1];
            uint8_t size;
        } small;
    };
    
    // FIXED: Use a bit pattern that doesn't conflict with string data
    // We can use the highest bit of the first byte after the buffer
    bool is_small() const {
        // Check if we're looking at large.capacity or part of small.buffer
        // For small strings, we never set large.capacity, so it contains
        // whatever is in small.buffer[16-23]. We need a different approach.
        
        // Option 1: Use a magic value in a non-overlapping location
        // Since small.size is at byte 24, we can use a bit there
        return (reinterpret_cast<const uint8_t*>(this)[24] & 0x80) != 0;
    }
    
    void set_small_flag() {
        // Set the high bit of small.size location to indicate small string
        reinterpret_cast<uint8_t*>(this)[24] |= 0x80;
    }
    
    void clear_small_flag() {
        // Clear the high bit for large strings
        reinterpret_cast<uint8_t*>(this)[24] &= 0x7F;
    }
    
    uint8_t get_small_size() const {
        // Mask off the flag bit to get actual size
        return small.size & 0x7F;
    }

public:
    GoTSString(const char* str) {
        if (!str) {
            small.buffer[0] = '\0';
            small.size = 0x80;  // Set flag bit + size 0
            return;
        }
        
        size_t len = strlen(str);
        std::cout << "Creating string with length " << len << ": '" << str << "'" << std::endl;
        
        if (len <= SSO_THRESHOLD) {
            std::cout << "Using small string optimization" << std::endl;
            memcpy(small.buffer, str, len);
            small.buffer[len] = '\0';
            small.size = static_cast<uint8_t>(len) | 0x80;  // Set flag bit + actual size
            std::cout << "Small buffer after copy: '" << small.buffer << "'" << std::endl;
        } else {
            std::cout << "Using large string (heap allocation)" << std::endl;
            large.size = len;
            large.capacity = ((len + 16) & ~15) | 1;
            large.data = new char[large.capacity & ~1];
            memcpy(large.data, str, len + 1);
            clear_small_flag();  // Make sure flag is clear for large strings
        }
    }
    
    const char* c_str() const {
        return is_small() ? small.buffer : large.data;
    }
    
    size_t size() const {
        return is_small() ? get_small_size() : large.size;
    }
    
    ~GoTSString() {
        if (!is_small()) {
            delete[] large.data;
        }
    }
};

int main() {
    std::cout << "Testing fixed GoTSString implementation\n" << std::endl;
    
    const char* test_str = "My IP is 192.168.1.1";
    GoTSString str(test_str);
    
    std::cout << "\nResult:" << std::endl;
    std::cout << "c_str(): '" << str.c_str() << "'" << std::endl;
    std::cout << "size(): " << str.size() << std::endl;
    
    if (strcmp(str.c_str(), test_str) == 0) {
        std::cout << "\nSUCCESS: String created correctly!" << std::endl;
    } else {
        std::cout << "\nERROR: String mismatch!" << std::endl;
        std::cout << "Expected: '" << test_str << "'" << std::endl;
        std::cout << "Got: '" << str.c_str() << "'" << std::endl;
    }
    
    // Test with a longer string
    std::cout << "\n\nTesting with longer string:" << std::endl;
    const char* long_str = "This is a much longer string that exceeds SSO threshold";
    GoTSString long_string(long_str);
    std::cout << "Result: '" << long_string.c_str() << "'" << std::endl;
    std::cout << "Size: " << long_string.size() << std::endl;
    
    return 0;
}