#include <iostream>
#include <regex>
#include <string>

int main() {
    std::string text = "test.user+foo@example.co.uk";
    std::string pattern = "[\\w.+-]+@[\\w-]+\\.[\\w.-]+";
    
    try {
        std::cout << "Testing pattern: " << pattern << std::endl;
        std::regex std_regex(pattern);
        std::cout << "Regex created successfully" << std::endl;
        
        std::smatch match_result;
        if (std::regex_search(text, match_result, std_regex)) {
            std::cout << "Match found: " << match_result[0].str() << std::endl;
        } else {
            std::cout << "No match found" << std::endl;
        }
    } catch (const std::regex_error& e) {
        std::cout << "Regex error: " << e.what() << std::endl;
    }
    
    return 0;
}