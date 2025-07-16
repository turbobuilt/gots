#include "compiler.h"
#include <cctype>
#include <stdexcept>
#include <unordered_map>

namespace gots {

static std::unordered_map<std::string, TokenType> keywords = {
    {"function", TokenType::FUNCTION},
    {"go", TokenType::GO},
    {"await", TokenType::AWAIT},
    {"let", TokenType::LET},
    {"var", TokenType::VAR},
    {"const", TokenType::CONST},
    {"if", TokenType::IF},
    {"for", TokenType::FOR},
    {"each", TokenType::EACH},
    {"in", TokenType::IN},
    {"while", TokenType::WHILE},
    {"return", TokenType::RETURN},
    {"switch", TokenType::SWITCH},
    {"case", TokenType::CASE},
    {"default", TokenType::DEFAULT},
    {"break", TokenType::BREAK},
    {"import", TokenType::IMPORT},
    {"export", TokenType::EXPORT},
    {"from", TokenType::FROM},
    {"as", TokenType::AS},
    {"tensor", TokenType::TENSOR},
    {"new", TokenType::NEW},
    {"class", TokenType::CLASS},
    {"extends", TokenType::EXTENDS},
    {"super", TokenType::SUPER},
    {"this", TokenType::THIS},
    {"constructor", TokenType::CONSTRUCTOR},
    {"public", TokenType::PUBLIC},
    {"private", TokenType::PRIVATE},
    {"protected", TokenType::PROTECTED},
    {"static", TokenType::STATIC},
    {"operator", TokenType::OPERATOR},
    {"true", TokenType::BOOLEAN},
    {"false", TokenType::BOOLEAN}
};

char Lexer::current_char() {
    if (pos >= source.length()) return '\0';
    return source[pos];
}

char Lexer::peek_char(int offset) {
    size_t peek_pos = pos + offset;
    if (peek_pos >= source.length()) return '\0';
    return source[peek_pos];
}

void Lexer::advance() {
    if (pos < source.length()) {
        if (source[pos] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
        pos++;
    }
}

void Lexer::skip_whitespace() {
    while (std::isspace(current_char())) {
        advance();
    }
}

void Lexer::skip_comment() {
    if (current_char() == '/' && peek_char() == '/') {
        while (current_char() != '\n' && current_char() != '\0') {
            advance();
        }
    } else if (current_char() == '/' && peek_char() == '*') {
        advance(); // skip '/'
        advance(); // skip '*'
        while (!(current_char() == '*' && peek_char() == '/') && current_char() != '\0') {
            advance();
        }
        if (current_char() == '*') {
            advance(); // skip '*'
            advance(); // skip '/'
        }
    }
}

Token Lexer::make_number() {
    std::string number;
    int start_line = line, start_column = column;
    
    while (std::isdigit(current_char())) {
        number += current_char();
        advance();
    }
    
    if (current_char() == '.') {
        number += current_char();
        advance();
        while (std::isdigit(current_char())) {
            number += current_char();
            advance();
        }
    }
    
    return {TokenType::NUMBER, number, start_line, start_column};
}

Token Lexer::make_string() {
    std::string str;
    char quote = current_char();
    int start_line = line, start_column = column;
    advance(); // skip opening quote
    
    while (current_char() != quote && current_char() != '\0') {
        if (current_char() == '\\') {
            advance();
            switch (current_char()) {
                case 'n': str += '\n'; break;
                case 't': str += '\t'; break;
                case 'r': str += '\r'; break;
                case '\\': str += '\\'; break;
                case '"': str += '"'; break;
                case '\'': str += '\''; break;
                default: str += current_char(); break;
            }
        } else {
            str += current_char();
        }
        advance();
    }
    
    if (current_char() == quote) {
        advance(); // skip closing quote
    }
    
    return {TokenType::STRING, str, start_line, start_column};
}

Token Lexer::make_template_literal() {
    std::string str;
    int start_line = line, start_column = column;
    advance(); // skip opening backtick
    
    while (current_char() != '`' && current_char() != '\0') {
        if (current_char() == '\\') {
            advance();
            switch (current_char()) {
                case 'n': str += '\n'; break;
                case 't': str += '\t'; break;
                case 'r': str += '\r'; break;
                case '\\': str += '\\'; break;
                case '`': str += '`'; break;
                default: str += current_char(); break;
            }
        } else {
            str += current_char();
        }
        advance();
    }
    
    if (current_char() == '`') {
        advance(); // skip closing backtick
    }
    
    return {TokenType::TEMPLATE_LITERAL, str, start_line, start_column};
}

Token Lexer::make_identifier() {
    std::string identifier;
    int start_line = line, start_column = column;
    
    while (std::isalnum(current_char()) || current_char() == '_' || current_char() == '$') {
        identifier += current_char();
        advance();
    }
    
    TokenType type = TokenType::IDENTIFIER;
    auto it = keywords.find(identifier);
    if (it != keywords.end()) {
        type = it->second;
    }
    
    return {type, identifier, start_line, start_column};
}

Token Lexer::make_regex() {
    std::string pattern;
    int start_line = line, start_column = column;
    advance(); // skip opening '/'
    
    while (current_char() != '/' && current_char() != '\0') {
        if (current_char() == '\\') {
            pattern += current_char();
            advance();
            if (current_char() != '\0') {
                pattern += current_char();
                advance();
            }
        } else if (current_char() == '\n') {
            // Regex cannot span multiple lines
            if (error_reporter) {
                error_reporter->report_lexer_error("Unterminated regex literal - regex cannot span multiple lines", line, column, current_char());
            }
            throw std::runtime_error("Unterminated regex literal");
        } else {
            pattern += current_char();
            advance();
        }
    }
    
    if (current_char() != '/') {
        if (error_reporter) {
            error_reporter->report_lexer_error("Unterminated regex literal", line, column, current_char());
        }
        throw std::runtime_error("Unterminated regex literal at position " + std::to_string(pos) + 
                                 ", found: '" + std::string(1, current_char()) + "'");
    }
    advance(); // skip closing '/'
    
    // Parse flags
    std::string flags;
    while (std::isalpha(current_char())) {
        char flag = current_char();
        if (flag == 'g' || flag == 'i' || flag == 'm' || flag == 's' || flag == 'u' || flag == 'y') {
            flags += flag;
            advance();
        } else {
            break;
        }
    }
    
    // Combine pattern and flags into value
    std::string value = pattern;
    if (!flags.empty()) {
        value += "|" + flags; // Use | as separator
    }
    
    return {TokenType::REGEX, value, start_line, start_column};
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    
    while (current_char() != '\0') {
        skip_whitespace();
        
        if (current_char() == '\0') break;
        
        if (current_char() == '/' && (peek_char() == '/' || peek_char() == '*')) {
            skip_comment();
            continue;
        }
        
        int start_line = line, start_column = column;
        
        if (std::isdigit(current_char())) {
            tokens.push_back(make_number());
        } else if (current_char() == '"' || current_char() == '\'') {
            tokens.push_back(make_string());
        } else if (current_char() == '`') {
            tokens.push_back(make_template_literal());
        } else if (std::isalpha(current_char()) || current_char() == '_' || current_char() == '$') {
            tokens.push_back(make_identifier());
        } else {
            char ch = current_char();
            TokenType type;
            std::string value(1, ch);
            
            switch (ch) {
                case '(':
                    type = TokenType::LPAREN;
                    break;
                case ')':
                    type = TokenType::RPAREN;
                    break;
                case '{':
                    type = TokenType::LBRACE;
                    break;
                case '}':
                    type = TokenType::RBRACE;
                    break;
                case '[':
                    advance();
                    if (current_char() == ':' && peek_char() == ']') {
                        advance(); // skip ':'
                        type = TokenType::SLICE_BRACKET;
                        value = "[:]";
                    } else {
                        pos--; column--;
                        type = TokenType::LBRACKET;
                    }
                    break;
                case ']':
                    type = TokenType::RBRACKET;
                    break;
                case ';':
                    type = TokenType::SEMICOLON;
                    break;
                case ',':
                    type = TokenType::COMMA;
                    break;
                case '.':
                    type = TokenType::DOT;
                    break;
                case ':':
                    type = TokenType::COLON;
                    break;
                case '?':
                    type = TokenType::QUESTION;
                    break;
                case '+':
                    advance();
                    if (current_char() == '=') {
                        type = TokenType::PLUS_ASSIGN;
                        value = "+=";
                    } else if (current_char() == '+') {
                        type = TokenType::INCREMENT;
                        value = "++";
                    } else {
                        pos--; column--;
                        type = TokenType::PLUS;
                    }
                    break;
                case '-':
                    advance();
                    if (current_char() == '=') {
                        type = TokenType::MINUS_ASSIGN;
                        value = "-=";
                    } else if (current_char() == '-') {
                        type = TokenType::DECREMENT;
                        value = "--";
                    } else {
                        pos--; column--;
                        type = TokenType::MINUS;
                    }
                    break;
                case '*':
                    advance();
                    if (current_char() == '=') {
                        type = TokenType::MULTIPLY_ASSIGN;
                        value = "*=";
                    } else if (current_char() == '*') {
                        type = TokenType::POWER;
                        value = "**";
                    } else {
                        pos--; column--;
                        type = TokenType::MULTIPLY;
                    }
                    break;
                case '/':
                    // Check if this could be a regex literal
                    // Regex literals can appear after: =, (, [, {, ;, :, !, &, |, ?, +, -, *, /, %, ^, ~, <, >, comma, return, throw
                    // and at the start of statements
                    if (tokens.empty() || 
                        tokens.back().type == TokenType::ASSIGN ||
                        tokens.back().type == TokenType::LPAREN ||
                        tokens.back().type == TokenType::LBRACKET ||
                        tokens.back().type == TokenType::LBRACE ||
                        tokens.back().type == TokenType::SEMICOLON ||
                        tokens.back().type == TokenType::COLON ||
                        tokens.back().type == TokenType::NOT ||
                        tokens.back().type == TokenType::AND ||
                        tokens.back().type == TokenType::OR ||
                        tokens.back().type == TokenType::QUESTION ||
                        tokens.back().type == TokenType::PLUS ||
                        tokens.back().type == TokenType::MINUS ||
                        tokens.back().type == TokenType::MULTIPLY ||
                        tokens.back().type == TokenType::DIVIDE ||
                        tokens.back().type == TokenType::MODULO ||
                        tokens.back().type == TokenType::LESS ||
                        tokens.back().type == TokenType::GREATER ||
                        tokens.back().type == TokenType::EQUAL ||
                        tokens.back().type == TokenType::NOT_EQUAL ||
                        tokens.back().type == TokenType::COMMA ||
                        tokens.back().type == TokenType::RETURN) {
                        // This is likely a regex literal
                        tokens.push_back(make_regex());
                        continue;
                    } else {
                        // This is likely a division operator
                        advance();
                        if (current_char() == '=') {
                            type = TokenType::DIVIDE_ASSIGN;
                            value = "/=";
                        } else {
                            pos--; column--;
                            type = TokenType::DIVIDE;
                        }
                    }
                    break;
                case '%':
                    type = TokenType::MODULO;
                    break;
                case '=':
                    advance();
                    if (current_char() == '=') {
                        advance();
                        if (current_char() == '=') {
                            type = TokenType::STRICT_EQUAL;
                            value = "===";
                        } else {
                            pos--; column--;
                            type = TokenType::EQUAL;
                            value = "==";
                        }
                    } else {
                        pos--; column--;
                        type = TokenType::ASSIGN;
                    }
                    break;
                case '!':
                    advance();
                    if (current_char() == '=') {
                        type = TokenType::NOT_EQUAL;
                        value = "!=";
                    } else {
                        pos--; column--;
                        type = TokenType::NOT;
                    }
                    break;
                case '<':
                    advance();
                    if (current_char() == '=') {
                        type = TokenType::LESS_EQUAL;
                        value = "<=";
                    } else {
                        pos--; column--;
                        type = TokenType::LESS;
                    }
                    break;
                case '>':
                    advance();
                    if (current_char() == '=') {
                        type = TokenType::GREATER_EQUAL;
                        value = ">=";
                    } else {
                        pos--; column--;
                        type = TokenType::GREATER;
                    }
                    break;
                case '&':
                    advance();
                    if (current_char() == '&') {
                        type = TokenType::AND;
                        value = "&&";
                    } else {
                        pos--; column--;
                        continue; // Skip single &
                    }
                    break;
                case '|':
                    advance();
                    if (current_char() == '|') {
                        type = TokenType::OR;
                        value = "||";
                    } else {
                        pos--; column--;
                        type = TokenType::PIPE;
                        value = "|";
                    }
                    break;
                default:
                    if (error_reporter) {
                        error_reporter->report_lexer_error("Unexpected character", line, column, ch);
                    }
                    throw std::runtime_error("Unexpected character: '" + std::string(1, ch) + "'");
            }
            
            tokens.push_back({type, value, start_line, start_column});
            advance();
        }
    }
    
    tokens.push_back({TokenType::EOF_TOKEN, "", line, column});
    return tokens;
}

}