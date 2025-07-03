#include "compiler.h"
#include <cctype>
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
    {"while", TokenType::WHILE},
    {"return", TokenType::RETURN},
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
                    type = TokenType::LBRACKET;
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
                    advance();
                    if (current_char() == '=') {
                        type = TokenType::DIVIDE_ASSIGN;
                        value = "/=";
                    } else {
                        pos--; column--;
                        type = TokenType::DIVIDE;
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
                        continue; // Skip single |
                    }
                    break;
                default:
                    advance();
                    continue; // Skip unknown characters
            }
            
            tokens.push_back({type, value, start_line, start_column});
            advance();
        }
    }
    
    tokens.push_back({TokenType::EOF_TOKEN, "", line, column});
    return tokens;
}

}