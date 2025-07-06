#include <iostream>
#include <string>
#include <cctype>

int main() {
    std::string text = "My IP is 192.168.1.1";
    std::string pattern = "(?:\\d{1,3}\\.){3}\\d{1,3}";
    
    std::cout << "Text: " << text << std::endl;
    std::cout << "Pattern: " << pattern << std::endl;
    
    // Simulate the matching logic
    for (size_t i = 0; i < text.length(); ++i) {
        if (std::isdigit(text[i])) {
            size_t start = i;
            int octet_count = 0;
            bool valid_ip = true;
            size_t j = i;
            
            std::cout << "\nStarting match at position " << i << " ('" << text[i] << "')" << std::endl;
            
            // Try to match 4 octets separated by dots
            while (octet_count < 4 && j < text.length()) {
                // Match digits (1-3 digits)
                int digit_count = 0;
                size_t octet_start = j;
                while (j < text.length() && std::isdigit(text[j]) && digit_count < 3) {
                    digit_count++;
                    j++;
                }
                
                if (digit_count == 0) {
                    valid_ip = false;
                    break;
                }
                
                std::cout << "  Octet " << (octet_count+1) << ": " << text.substr(octet_start, digit_count) 
                          << " (positions " << octet_start << "-" << (j-1) << ")" << std::endl;
                
                octet_count++;
                
                // If we need more octets, expect a dot
                if (octet_count < 4) {
                    if (j < text.length() && text[j] == '.') {
                        std::cout << "  Found dot at position " << j << std::endl;
                        j++;
                    } else {
                        std::cout << "  Expected dot but found '" << (j < text.length() ? text[j] : '?') << "' at position " << j << std::endl;
                        valid_ip = false;
                        break;
                    }
                }
            }
            
            // Check if we found a valid IP (4 octets)
            if (valid_ip && octet_count == 4) {
                std::string match = text.substr(start, j - start);
                std::cout << "MATCH FOUND: '" << match << "' (positions " << start << "-" << (j-1) << ")" << std::endl;
                std::cout << "Match length: " << (j - start) << std::endl;
                return 0;
            } else {
                std::cout << "  Not a valid IP (valid=" << valid_ip << ", octets=" << octet_count << ")" << std::endl;
            }
        }
    }
    
    std::cout << "\nNo match found" << std::endl;
    return 0;
}
