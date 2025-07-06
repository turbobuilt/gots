#include <iostream>
#include <cstring>
#include <cstdint>
#include <cstddef>

// Reproduce the GoTSString structure
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
    
    bool is_small() const {
        return large.capacity == 0;
    }
    
    void set_small_flag() {
        large.capacity = 0;
    }

public:
    GoTSString(const char* str) {
        if (!str) {
            small.buffer[0] = '\0';
            small.size = 0;
            set_small_flag();
            return;
        }
        
        size_t len = strlen(str);
        std::cout << "Creating string with length " << len << ": '" << str << "'" << std::endl;
        
        if (len <= SSO_THRESHOLD) {
            std::cout << "Using small string optimization (SSO_THRESHOLD=" << SSO_THRESHOLD << ")" << std::endl;
            std::cout << "Copying " << len << " characters" << std::endl;
            
            // Debug: check buffer before and after copy
            for (size_t i = 0; i <= len; i++) {
                std::cout << "  str[" << i << "] = '" << (str[i] ? str[i] : '\\0') << "' (ASCII " << static_cast<int>(str[i]) << ")" << std::endl;
            }
            
            memcpy(small.buffer, str, len);
            small.buffer[len] = '\0';
            small.size = static_cast<uint8_t>(len);
            set_small_flag();
            
            std::cout << "After copy:" << std::endl;
            for (size_t i = 0; i <= len; i++) {
                std::cout << "  small.buffer[" << i << "] = '" << (small.buffer[i] ? small.buffer[i] : '\\0') << "' (ASCII " << static_cast<int>(small.buffer[i]) << ")" << std::endl;
            }
            
            std::cout << "Small buffer contents: '" << small.buffer << "'" << std::endl;
            std::cout << "Small size: " << static_cast<int>(small.size) << std::endl;
        } else {
            std::cout << "Using large string (heap allocation)" << std::endl;
            large.size = len;
            large.capacity = ((len + 16) & ~15) | 1;
            large.data = new char[large.capacity & ~1];
            memcpy(large.data, str, len + 1);
        }
    }
    
    const char* c_str() const {
        return is_small() ? small.buffer : large.data;
    }
    
    size_t size() const {
        return is_small() ? small.size : large.size;
    }
    
    ~GoTSString() {
        if (!is_small()) {
            delete[] large.data;
        }
    }
};

int main() {
    std::cout << "sizeof(void*): " << sizeof(void*) << std::endl;
    std::cout << "sizeof(size_t): " << sizeof(size_t) << std::endl;
    std::cout << "SSO_THRESHOLD: " << GoTSString::SSO_THRESHOLD << std::endl;
    std::cout << "sizeof(GoTSString): " << sizeof(GoTSString) << std::endl;
    std::cout << "offsetof small.buffer: " << offsetof(GoTSString, small.buffer) << std::endl;
    std::cout << "offsetof small.size: " << offsetof(GoTSString, small.size) << std::endl;
    std::cout << std::endl;
    
    const char* test_str = "My IP is 192.168.1.1";
    GoTSString str(test_str);
    
    std::cout << "\nResult:" << std::endl;
    std::cout << "c_str(): '" << str.c_str() << "'" << std::endl;
    std::cout << "size(): " << str.size() << std::endl;
    
    // Check if the string matches
    if (strcmp(str.c_str(), test_str) == 0) {
        std::cout << "String created correctly!" << std::endl;
    } else {
        std::cout << "ERROR: String mismatch!" << std::endl;
        std::cout << "Expected: '" << test_str << "'" << std::endl;
        std::cout << "Got: '" << str.c_str() << "'" << std::endl;
    }
    
    return 0;
}
