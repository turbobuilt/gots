#include "compiler.h"
#include <iostream>
#include <unordered_set>
#include <cctype>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace gots {

namespace Colors {
    // Color detection
    bool supports_color() {
        // Check environment variables first
        const char* term = std::getenv("TERM");
        const char* colorterm = std::getenv("COLORTERM");
        const char* no_color = std::getenv("NO_COLOR");
        
        // If NO_COLOR is set, disable colors
        if (no_color && std::strlen(no_color) > 0) {
            return false;
        }
        
        // Check if we're outputting to a terminal
#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_ERROR_HANDLE);
        DWORD dwMode = 0;
        if (!GetConsoleMode(hOut, &dwMode)) {
            return false; // Not a console
        }
        
        // Enable ANSI colors on Windows 10+
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        return true;
#else
        if (!isatty(STDERR_FILENO)) {
            return false; // Not a TTY
        }
#endif
        
        // Check TERM environment variable
        if (term) {
            std::string term_str(term);
            if (term_str.find("color") != std::string::npos ||
                term_str.find("xterm") != std::string::npos ||
                term_str.find("screen") != std::string::npos ||
                term_str == "ansi") {
                return true;
            }
        }
        
        // Check COLORTERM
        if (colorterm) {
            return true;
        }
        
        return false;
    }
    
    // Define color codes - these will be empty strings if colors aren't supported
    const char* RESET = "";
    const char* BOLD = "";
    const char* DIM = "";
    
    const char* RED = "";
    const char* GREEN = "";
    const char* YELLOW = "";
    const char* BLUE = "";
    const char* MAGENTA = "";
    const char* CYAN = "";
    const char* WHITE = "";
    const char* GRAY = "";
    
    const char* BRIGHT_RED = "";
    const char* BRIGHT_GREEN = "";
    const char* BRIGHT_YELLOW = "";
    const char* BRIGHT_BLUE = "";
    const char* BRIGHT_MAGENTA = "";
    const char* BRIGHT_CYAN = "";
    
    // Initialize colors based on terminal support
    void init_colors() {
        static bool initialized = false;
        if (initialized) return;
        initialized = true;
        
        if (supports_color()) {
            RESET = "\033[0m";
            BOLD = "\033[1m";
            DIM = "\033[2m";
            
            RED = "\033[31m";
            GREEN = "\033[32m";
            YELLOW = "\033[33m";
            BLUE = "\033[34m";
            MAGENTA = "\033[35m";
            CYAN = "\033[36m";
            WHITE = "\033[37m";
            GRAY = "\033[90m";
            
            BRIGHT_RED = "\033[91m";
            BRIGHT_GREEN = "\033[92m";
            BRIGHT_YELLOW = "\033[93m";
            BRIGHT_BLUE = "\033[94m";
            BRIGHT_MAGENTA = "\033[95m";
            BRIGHT_CYAN = "\033[96m";
        }
    }
}

SyntaxHighlighter::SyntaxHighlighter() {
    Colors::init_colors();
    use_colors = Colors::supports_color();
}

bool SyntaxHighlighter::is_keyword(const std::string& token) const {
    static const std::unordered_set<std::string> keywords = {
        "function", "go", "await", "let", "var", "const",
        "if", "else", "for", "each", "in", "while", "return",
        "switch", "case", "default", "break", "continue",
        "import", "export", "from", "as", "tensor", "new",
        "class", "extends", "super", "this", "constructor",
        "public", "private", "protected", "static",
        "true", "false", "null", "undefined"
    };
    return keywords.find(token) != keywords.end();
}

bool SyntaxHighlighter::is_number(const std::string& token) const {
    if (token.empty()) return false;
    
    size_t i = 0;
    if (token[0] == '-' || token[0] == '+') i++;
    
    bool has_digits = false;
    bool has_dot = false;
    
    for (; i < token.length(); ++i) {
        char ch = token[i];
        if (std::isdigit(ch)) {
            has_digits = true;
        } else if (ch == '.' && !has_dot) {
            has_dot = true;
        } else {
            return false;
        }
    }
    
    return has_digits;
}

bool SyntaxHighlighter::is_string_delimiter(char ch) const {
    return ch == '"' || ch == '\'' || ch == '`';
}

TokenType SyntaxHighlighter::classify_token(const std::string& token) const {
    if (token.empty()) return TokenType::EOF_TOKEN;
    
    if (is_keyword(token)) {
        if (token == "true" || token == "false") {
            return TokenType::BOOLEAN;
        }
        return TokenType::FUNCTION; // Use FUNCTION as general keyword token
    }
    
    if (is_number(token)) {
        return TokenType::NUMBER;
    }
    
    if (is_string_delimiter(token[0])) {
        return TokenType::STRING;
    }
    
    // Check for operators
    if (token == "+" || token == "-" || token == "*" || token == "/" || 
        token == "%" || token == "**") {
        return TokenType::PLUS; // Use PLUS as general operator token
    }
    
    if (token == "=" || token == "==" || token == "===" || token == "!=" ||
        token == "<" || token == ">" || token == "<=" || token == ">=") {
        return TokenType::EQUAL; // Use EQUAL as general comparison token
    }
    
    if (token == "&&" || token == "||" || token == "!") {
        return TokenType::AND; // Use AND as general logical token
    }
    
    // Punctuation
    if (token == "(" || token == ")" || token == "{" || token == "}" ||
        token == "[" || token == "]" || token == ";" || token == "," ||
        token == "." || token == ":") {
        return TokenType::LPAREN; // Use LPAREN as general punctuation token
    }
    
    return TokenType::IDENTIFIER;
}

std::string SyntaxHighlighter::colorize_token(const std::string& token, TokenType type) const {
    if (!use_colors) return token;
    
    switch (type) {
        case TokenType::FUNCTION: // Keywords
            return std::string(Colors::BLUE) + token + Colors::RESET;
        
        case TokenType::NUMBER:
            return std::string(Colors::MAGENTA) + token + Colors::RESET;
        
        case TokenType::STRING:
            return std::string(Colors::GREEN) + token + Colors::RESET;
        
        case TokenType::BOOLEAN:
            return std::string(Colors::YELLOW) + token + Colors::RESET;
        
        case TokenType::PLUS: // Operators
            return std::string(Colors::CYAN) + token + Colors::RESET;
        
        case TokenType::EQUAL: // Comparisons
            return std::string(Colors::CYAN) + token + Colors::RESET;
        
        case TokenType::AND: // Logical operators
            return std::string(Colors::BRIGHT_CYAN) + token + Colors::RESET;
        
        case TokenType::LPAREN: // Punctuation
            return std::string(Colors::WHITE) + token + Colors::RESET;
        
        case TokenType::IDENTIFIER:
            return token; // No color for identifiers
        
        default:
            return token;
    }
}

std::string SyntaxHighlighter::highlight_line(const std::string& line) const {
    if (!use_colors || line.empty()) {
        return line;
    }
    
    std::string result;
    std::string current_token;
    bool in_string = false;
    char string_delimiter = '\0';
    bool in_comment = false;
    
    for (size_t i = 0; i < line.length(); ++i) {
        char ch = line[i];
        
        // Handle comments
        if (!in_string && !in_comment && ch == '/' && i + 1 < line.length() && line[i + 1] == '/') {
            // Flush current token
            if (!current_token.empty()) {
                TokenType type = classify_token(current_token);
                result += colorize_token(current_token, type);
                current_token.clear();
            }
            
            // Color the rest of the line as comment
            result += std::string(Colors::GRAY) + line.substr(i) + Colors::RESET;
            break;
        }
        
        // Handle strings
        if (is_string_delimiter(ch) && !in_comment) {
            if (!in_string) {
                // Starting a string
                if (!current_token.empty()) {
                    TokenType type = classify_token(current_token);
                    result += colorize_token(current_token, type);
                    current_token.clear();
                }
                in_string = true;
                string_delimiter = ch;
                current_token += ch;
            } else if (ch == string_delimiter) {
                // Ending string
                current_token += ch;
                result += colorize_token(current_token, TokenType::STRING);
                current_token.clear();
                in_string = false;
                string_delimiter = '\0';
            } else {
                current_token += ch;
            }
        } else if (in_string) {
            current_token += ch;
        } else if (std::isalnum(ch) || ch == '_' || ch == '$') {
            current_token += ch;
        } else {
            // End of token, flush it
            if (!current_token.empty()) {
                TokenType type = classify_token(current_token);
                result += colorize_token(current_token, type);
                current_token.clear();
            }
            
            // Handle multi-character operators
            if (ch == '+' || ch == '-' || ch == '*' || ch == '/' || 
                ch == '=' || ch == '!' || ch == '<' || ch == '>' ||
                ch == '&' || ch == '|') {
                std::string op(1, ch);
                
                // Check for multi-character operators
                if (i + 1 < line.length()) {
                    char next_ch = line[i + 1];
                    if ((ch == '+' && next_ch == '+') ||
                        (ch == '-' && next_ch == '-') ||
                        (ch == '*' && next_ch == '*') ||
                        (ch == '=' && next_ch == '=') ||
                        (ch == '!' && next_ch == '=') ||
                        (ch == '<' && next_ch == '=') ||
                        (ch == '>' && next_ch == '=') ||
                        (ch == '&' && next_ch == '&') ||
                        (ch == '|' && next_ch == '|')) {
                        op += next_ch;
                        i++; // Skip next character
                    } else if (ch == '=' && i + 2 < line.length() && 
                              line[i + 1] == '=' && line[i + 2] == '=') {
                        op = "===";
                        i += 2; // Skip next two characters
                    }
                }
                
                TokenType type = classify_token(op);
                result += colorize_token(op, type);
            } else {
                // Single character punctuation
                std::string punct(1, ch);
                TokenType type = classify_token(punct);
                result += colorize_token(punct, type);
            }
        }
    }
    
    // Flush any remaining token
    if (!current_token.empty()) {
        TokenType type = classify_token(current_token);
        result += colorize_token(current_token, type);
    }
    
    return result;
}

}