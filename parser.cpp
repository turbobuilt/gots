#include "compiler.h"
#include <stdexcept>
#include <iostream>

namespace gots {

Token& Parser::current_token() {
    if (pos >= tokens.size()) {
        static Token eof_token = {TokenType::EOF_TOKEN, "", 0, 0};
        return eof_token;
    }
    return tokens[pos];
}

Token& Parser::peek_token(int offset) {
    size_t peek_pos = pos + offset;
    if (peek_pos >= tokens.size()) {
        static Token eof_token = {TokenType::EOF_TOKEN, "", 0, 0};
        return eof_token;
    }
    return tokens[peek_pos];
}

void Parser::advance() {
    if (pos < tokens.size()) {
        pos++;
    }
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::check(TokenType type) {
    return current_token().type == type;
}

std::unique_ptr<ExpressionNode> Parser::parse_expression() {
    return parse_assignment_expression();
}

std::unique_ptr<ExpressionNode> Parser::parse_assignment_expression() {
    auto expr = parse_ternary();
    
    if (match(TokenType::ASSIGN) || match(TokenType::PLUS_ASSIGN) ||
        match(TokenType::MINUS_ASSIGN) || match(TokenType::MULTIPLY_ASSIGN) ||
        match(TokenType::DIVIDE_ASSIGN)) {
        
        auto identifier = dynamic_cast<Identifier*>(expr.get());
        auto property_access = dynamic_cast<PropertyAccess*>(expr.get());
        
        if (identifier) {
            std::string var_name = identifier->name;
            auto value = parse_assignment_expression();
            
            expr.release();
            auto assignment = std::make_unique<Assignment>(var_name, std::move(value));
            return assignment;
        } else if (property_access) {
            std::string obj_name = property_access->object_name;
            std::string prop_name = property_access->property_name;
            auto value = parse_assignment_expression();
            
            expr.release();
            auto prop_assignment = std::make_unique<PropertyAssignment>(obj_name, prop_name, std::move(value));
            return prop_assignment;
        } else {
            throw std::runtime_error("Invalid assignment target");
        }
    }
    
    return expr;
}

std::unique_ptr<ExpressionNode> Parser::parse_ternary() {
    auto expr = parse_logical_or();
    
    if (match(TokenType::QUESTION)) {
        auto true_expr = parse_expression();
        
        if (!match(TokenType::COLON)) {
            throw std::runtime_error("Expected ':' in ternary operator");
        }
        
        auto false_expr = parse_ternary(); // Right associative
        
        return std::make_unique<TernaryOperator>(std::move(expr), std::move(true_expr), std::move(false_expr));
    }
    
    return expr;
}

std::unique_ptr<ExpressionNode> Parser::parse_logical_or() {
    auto expr = parse_logical_and();
    
    while (match(TokenType::OR)) {
        TokenType op = tokens[pos - 1].type;
        auto right = parse_logical_and();
        expr = std::make_unique<BinaryOp>(std::move(expr), op, std::move(right));
    }
    
    return expr;
}

std::unique_ptr<ExpressionNode> Parser::parse_logical_and() {
    auto expr = parse_equality();
    
    while (match(TokenType::AND)) {
        TokenType op = tokens[pos - 1].type;
        auto right = parse_equality();
        expr = std::make_unique<BinaryOp>(std::move(expr), op, std::move(right));
    }
    
    return expr;
}

std::unique_ptr<ExpressionNode> Parser::parse_equality() {
    auto expr = parse_comparison();
    
    while (match(TokenType::EQUAL) || match(TokenType::NOT_EQUAL) || match(TokenType::STRICT_EQUAL)) {
        TokenType op = tokens[pos - 1].type;
        auto right = parse_comparison();
        expr = std::make_unique<BinaryOp>(std::move(expr), op, std::move(right));
    }
    
    return expr;
}

std::unique_ptr<ExpressionNode> Parser::parse_comparison() {
    auto expr = parse_addition();
    
    while (match(TokenType::GREATER) || match(TokenType::GREATER_EQUAL) ||
           match(TokenType::LESS) || match(TokenType::LESS_EQUAL)) {
        TokenType op = tokens[pos - 1].type;
        auto right = parse_addition();
        expr = std::make_unique<BinaryOp>(std::move(expr), op, std::move(right));
    }
    
    return expr;
}

std::unique_ptr<ExpressionNode> Parser::parse_addition() {
    auto expr = parse_multiplication();
    
    while (match(TokenType::PLUS) || match(TokenType::MINUS)) {
        TokenType op = tokens[pos - 1].type;
        auto right = parse_multiplication();
        expr = std::make_unique<BinaryOp>(std::move(expr), op, std::move(right));
    }
    
    return expr;
}

std::unique_ptr<ExpressionNode> Parser::parse_multiplication() {
    auto expr = parse_exponentiation();
    
    while (match(TokenType::MULTIPLY) || match(TokenType::DIVIDE) || match(TokenType::MODULO)) {
        TokenType op = tokens[pos - 1].type;
        auto right = parse_exponentiation();
        expr = std::make_unique<BinaryOp>(std::move(expr), op, std::move(right));
    }
    
    return expr;
}

std::unique_ptr<ExpressionNode> Parser::parse_exponentiation() {
    auto expr = parse_unary();
    
    // Exponentiation is right-associative, so use recursion instead of loop
    if (match(TokenType::POWER)) {
        TokenType op = tokens[pos - 1].type;
        auto right = parse_exponentiation(); // Right-associative
        expr = std::make_unique<BinaryOp>(std::move(expr), op, std::move(right));
    }
    
    return expr;
}

std::unique_ptr<ExpressionNode> Parser::parse_unary() {
    if (match(TokenType::NOT) || match(TokenType::MINUS)) {
        TokenType op = tokens[pos - 1].type;
        auto right = parse_unary();
        return std::make_unique<BinaryOp>(nullptr, op, std::move(right));
    }
    
    if (match(TokenType::GO)) {
        // Parse go functionCall() - the function call should be the next expression
        auto expr = parse_call();
        
        // The expression should be a function call - mark it as a goroutine
        if (auto func_call = dynamic_cast<FunctionCall*>(expr.get())) {
            func_call->is_goroutine = true;
            return expr;
        } else if (auto method_call = dynamic_cast<MethodCall*>(expr.get())) {
            method_call->is_goroutine = true;
            return expr;
        } else {
            throw std::runtime_error("'go' can only be used with function calls");
        }
    }
    
    return parse_call();
}

std::unique_ptr<ExpressionNode> Parser::parse_call() {
    auto expr = parse_primary();
    
    while (true) {
        if (match(TokenType::LPAREN)) {
            auto identifier = dynamic_cast<Identifier*>(expr.get());
            if (!identifier) {
                throw std::runtime_error("Invalid function call");
            }
            
            std::string func_name = identifier->name;
            expr.release();
            
            auto call = std::make_unique<FunctionCall>(func_name);
            
            if (!check(TokenType::RPAREN)) {
                do {
                    call->arguments.push_back(parse_expression());
                } while (match(TokenType::COMMA));
            }
            
            if (!match(TokenType::RPAREN)) {
                throw std::runtime_error("Expected ')' after function arguments");
            }
            
            expr = std::move(call);
        } else if (match(TokenType::DOT)) {
            if (!match(TokenType::IDENTIFIER)) {
                throw std::runtime_error("Expected property name after '.'");
            }
            
            std::string property = tokens[pos - 1].value;
            
            // Check if this is a method call (has parentheses after the property)
            if (check(TokenType::LPAREN)) {
                auto identifier = dynamic_cast<Identifier*>(expr.get());
                auto this_expr = dynamic_cast<ThisExpression*>(expr.get());
                auto super_call = dynamic_cast<SuperCall*>(expr.get());
                
                if (super_call) {
                    // This is super.methodName() - create SuperMethodCall
                    expr.release();
                    
                    advance(); // consume LPAREN
                    auto super_method_call = std::make_unique<SuperMethodCall>(property);
                    
                    if (!check(TokenType::RPAREN)) {
                        do {
                            super_method_call->arguments.push_back(parse_expression());
                        } while (match(TokenType::COMMA));
                    }
                    
                    if (!match(TokenType::RPAREN)) {
                        throw std::runtime_error("Expected ')' after super method arguments");
                    }
                    
                    expr = std::move(super_method_call);
                    continue; // Continue to check for more chained calls
                }
                
                std::string object_name;
                if (identifier) {
                    object_name = identifier->name;
                } else if (this_expr) {
                    object_name = "this";
                } else {
                    throw std::runtime_error("Invalid method call");
                }
                
                expr.release();
                
                // Parse the method call like a function call
                advance(); // consume LPAREN
                auto method_call = std::make_unique<MethodCall>(object_name, property);
                
                if (!check(TokenType::RPAREN)) {
                    do {
                        method_call->arguments.push_back(parse_expression());
                    } while (match(TokenType::COMMA));
                }
                
                if (!match(TokenType::RPAREN)) {
                    throw std::runtime_error("Expected ')' after method arguments");
                }
                
                expr = std::move(method_call);
            } else {
                // This is property access, not a method call
                auto identifier = dynamic_cast<Identifier*>(expr.get());
                auto this_expr = dynamic_cast<ThisExpression*>(expr.get());
                auto super_call = dynamic_cast<SuperCall*>(expr.get());
                
                if (identifier) {
                    std::string object_name = identifier->name;
                    expr.release();
                    expr = std::make_unique<PropertyAccess>(object_name, property);
                } else if (this_expr) {
                    expr.release();
                    expr = std::make_unique<PropertyAccess>("this", property);
                } else if (super_call) {
                    expr.release();
                    expr = std::make_unique<PropertyAccess>("super", property);
                } else {
                    throw std::runtime_error("Invalid property access");
                }
            }
        } else if (match(TokenType::INCREMENT)) {
            // Handle postfix increment
            auto identifier = dynamic_cast<Identifier*>(expr.get());
            if (!identifier) {
                throw std::runtime_error("Invalid increment operation");
            }
            
            std::string var_name = identifier->name;
            expr.release();
            expr = std::make_unique<PostfixIncrement>(var_name);
        } else if (match(TokenType::DECREMENT)) {
            // Handle postfix decrement
            auto identifier = dynamic_cast<Identifier*>(expr.get());
            if (!identifier) {
                throw std::runtime_error("Invalid decrement operation");
            }
            
            std::string var_name = identifier->name;
            expr.release();
            expr = std::make_unique<PostfixDecrement>(var_name);
        } else {
            break;
        }
    }
    
    return expr;
}

std::unique_ptr<ExpressionNode> Parser::parse_primary() {
    if (match(TokenType::NUMBER)) {
        double value = std::stod(tokens[pos - 1].value);
        return std::make_unique<NumberLiteral>(value);
    }
    
    if (match(TokenType::STRING)) {
        return std::make_unique<StringLiteral>(tokens[pos - 1].value);
    }
    
    if (match(TokenType::BOOLEAN)) {
        double value = (tokens[pos - 1].value == "true") ? 1.0 : 0.0;
        return std::make_unique<NumberLiteral>(value);
    }
    
    if (match(TokenType::IDENTIFIER)) {
        return std::make_unique<Identifier>(tokens[pos - 1].value);
    }
    
    if (match(TokenType::LBRACKET)) {
        auto array_literal = std::make_unique<ArrayLiteral>();
        
        if (!check(TokenType::RBRACKET)) {
            do {
                array_literal->elements.push_back(parse_expression());
            } while (match(TokenType::COMMA));
        }
        
        if (!match(TokenType::RBRACKET)) {
            throw std::runtime_error("Expected ']' after array elements");
        }
        
        return array_literal;
    }
    
    if (match(TokenType::LPAREN)) {
        auto expr = parse_expression();
        if (!match(TokenType::RPAREN)) {
            throw std::runtime_error("Expected ')' after expression");
        }
        return expr;
    }
    
    if (match(TokenType::GO)) {
        auto expr = parse_call();
        auto func_call = dynamic_cast<FunctionCall*>(expr.get());
        if (func_call) {
            func_call->is_goroutine = true;
        }
        return expr;
    }
    
    if (match(TokenType::AWAIT)) {
        auto expr = parse_call();
        if (auto func_call = dynamic_cast<FunctionCall*>(expr.get())) {
            func_call->is_awaited = true;
        } else if (auto method_call = dynamic_cast<MethodCall*>(expr.get())) {
            method_call->is_awaited = true;
        }
        return expr;
    }
    
    if (match(TokenType::THIS)) {
        return std::make_unique<ThisExpression>();
    }
    
    if (match(TokenType::SUPER)) {
        auto super_call = std::make_unique<SuperCall>();
        
        if (match(TokenType::LPAREN)) {
            // Parse super constructor arguments
            if (!check(TokenType::RPAREN)) {
                do {
                    super_call->arguments.push_back(parse_expression());
                } while (match(TokenType::COMMA));
            }
            
            if (!match(TokenType::RPAREN)) {
                throw std::runtime_error("Expected ')' after super arguments");
            }
        }
        
        return super_call;
    }
    
    if (match(TokenType::NEW)) {
        if (!check(TokenType::IDENTIFIER)) {
            throw std::runtime_error("Expected class name after 'new'");
        }
        
        std::string class_name = current_token().value;
        advance();
        
        auto new_expr = std::make_unique<NewExpression>(class_name);
        
        if (match(TokenType::LBRACE)) {
            // Dart-style: new Person{name: "bob", age: 25}
            new_expr->is_dart_style = true;
            
            while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
                if (!check(TokenType::IDENTIFIER)) {
                    throw std::runtime_error("Expected property name");
                }
                
                std::string prop_name = current_token().value;
                advance();
                
                if (!match(TokenType::COLON)) {
                    throw std::runtime_error("Expected ':' after property name");
                }
                
                auto value = parse_expression();
                new_expr->dart_args.push_back(std::make_pair(prop_name, std::move(value)));
                
                if (!match(TokenType::COMMA)) {
                    break;
                }
            }
            
            if (!match(TokenType::RBRACE)) {
                throw std::runtime_error("Expected '}' after object properties");
            }
        } else if (match(TokenType::LPAREN)) {
            // Regular style: new Person("bob", 25)
            while (!check(TokenType::RPAREN) && !check(TokenType::EOF_TOKEN)) {
                new_expr->arguments.push_back(parse_expression());
                if (!match(TokenType::COMMA)) {
                    break;
                }
            }
            
            if (!match(TokenType::RPAREN)) {
                throw std::runtime_error("Expected ')' after constructor arguments");
            }
        }
        
        return new_expr;
    }
    
    throw std::runtime_error("Unexpected token: " + current_token().value);
}

std::unique_ptr<ASTNode> Parser::parse_statement() {
    if (check(TokenType::FUNCTION)) {
        return parse_function_declaration();
    }
    
    if (check(TokenType::CLASS)) {
        return parse_class_declaration();
    }
    
    if (check(TokenType::VAR) || check(TokenType::LET) || check(TokenType::CONST)) {
        return parse_variable_declaration();
    }
    
    if (check(TokenType::IF)) {
        return parse_if_statement();
    }
    
    if (check(TokenType::FOR)) {
        return parse_for_statement();
    }
    
    if (check(TokenType::RETURN)) {
        return parse_return_statement();
    }
    
    return parse_expression_statement();
}

std::unique_ptr<ASTNode> Parser::parse_function_declaration() {
    if (!match(TokenType::FUNCTION)) {
        throw std::runtime_error("Expected 'function'");
    }
    
    if (!match(TokenType::IDENTIFIER)) {
        throw std::runtime_error("Expected function name");
    }
    
    std::string func_name = tokens[pos - 1].value;
    auto func_decl = std::make_unique<FunctionDecl>(func_name);
    
    if (!match(TokenType::LPAREN)) {
        throw std::runtime_error("Expected '(' after function name");
    }
    
    if (!check(TokenType::RPAREN)) {
        do {
            if (!match(TokenType::IDENTIFIER)) {
                throw std::runtime_error("Expected parameter name");
            }
            
            std::string param_name = tokens[pos - 1].value;
            Variable param;
            param.name = param_name;
            param.type = DataType::UNKNOWN;
            
            if (match(TokenType::COLON)) {
                param.type = parse_type();
            }
            
            func_decl->parameters.push_back(param);
        } while (match(TokenType::COMMA));
    }
    
    if (!match(TokenType::RPAREN)) {
        throw std::runtime_error("Expected ')' after parameters");
    }
    
    if (match(TokenType::COLON)) {
        func_decl->return_type = parse_type();
    }
    
    if (!match(TokenType::LBRACE)) {
        throw std::runtime_error("Expected '{' to start function body");
    }
    
    while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
        func_decl->body.push_back(parse_statement());
    }
    
    if (!match(TokenType::RBRACE)) {
        throw std::runtime_error("Expected '}' to end function body");
    }
    
    return std::move(func_decl);
}

std::unique_ptr<ASTNode> Parser::parse_variable_declaration() {
    TokenType decl_type = current_token().type;
    advance();
    
    if (!match(TokenType::IDENTIFIER)) {
        throw std::runtime_error("Expected variable name");
    }
    
    std::string var_name = tokens[pos - 1].value;
    DataType type = DataType::UNKNOWN;
    
    if (match(TokenType::COLON)) {
        type = parse_type();
    }
    
    std::unique_ptr<ExpressionNode> value = nullptr;
    if (match(TokenType::ASSIGN)) {
        value = parse_expression();
    }
    
    auto assignment = std::make_unique<Assignment>(var_name, std::move(value));
    assignment->declared_type = type;
    
    if (match(TokenType::SEMICOLON)) {
        // Optional semicolon
    }
    
    return std::move(assignment);
}

std::unique_ptr<ASTNode> Parser::parse_if_statement() {
    if (!match(TokenType::IF)) {
        throw std::runtime_error("Expected 'if'");
    }
    
    auto if_stmt = std::make_unique<IfStatement>();
    
    bool has_parens = false;
    if (check(TokenType::LPAREN)) {
        has_parens = true;
        advance();
    }
    
    if_stmt->condition = parse_expression();
    
    if (has_parens && !match(TokenType::RPAREN)) {
        throw std::runtime_error("Expected ')' after if condition");
    }
    
    if (match(TokenType::LBRACE)) {
        while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
            if_stmt->then_body.push_back(parse_statement());
        }
        
        if (!match(TokenType::RBRACE)) {
            throw std::runtime_error("Expected '}' after if body");
        }
    } else {
        if_stmt->then_body.push_back(parse_statement());
    }
    
    // Handle else clause
    if (current_token().type == TokenType::IDENTIFIER && current_token().value == "else") {
        advance(); // consume "else"
        
        if (match(TokenType::LBRACE)) {
            while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
                if_stmt->else_body.push_back(parse_statement());
            }
            
            if (!match(TokenType::RBRACE)) {
                throw std::runtime_error("Expected '}' after else body");
            }
        } else {
            if_stmt->else_body.push_back(parse_statement());
        }
    }
    
    return std::move(if_stmt);
}

std::unique_ptr<ASTNode> Parser::parse_for_statement() {
    if (!match(TokenType::FOR)) {
        throw std::runtime_error("Expected 'for'");
    }
    
    auto for_loop = std::make_unique<ForLoop>();
    
    bool has_parens = false;
    if (check(TokenType::LPAREN)) {
        has_parens = true;
        advance();
    }
    
    // Parse init if present
    if (!check(TokenType::SEMICOLON)) {
        // For variable declarations in for loops, we need to parse them specially
        // to avoid consuming the semicolon
        if (check(TokenType::VAR) || check(TokenType::LET) || check(TokenType::CONST)) {
            TokenType decl_type = current_token().type;
            advance();
            
            if (!match(TokenType::IDENTIFIER)) {
                throw std::runtime_error("Expected variable name");
            }
            
            std::string var_name = tokens[pos - 1].value;
            DataType type = DataType::UNKNOWN;
            
            if (match(TokenType::COLON)) {
                type = parse_type();
            }
            
            std::unique_ptr<ExpressionNode> value = nullptr;
            if (match(TokenType::ASSIGN)) {
                value = parse_expression();
            }
            
            auto assignment = std::make_unique<Assignment>(var_name, std::move(value));
            assignment->declared_type = type;
            for_loop->init = std::move(assignment);
        } else {
            for_loop->init = parse_statement();
        }
    }
    
    if (match(TokenType::SEMICOLON)) {
        // Parse condition if present
        if (!check(TokenType::SEMICOLON)) {
            for_loop->condition = parse_expression();
        }
        
        if (!match(TokenType::SEMICOLON)) {
            throw std::runtime_error("Expected ';' after for condition");
        }
        
        // For parenthesized for loops, always try to parse the update part
        // unless we're at the closing paren (which means no update statement)
        if (has_parens && !check(TokenType::RPAREN)) {
            for_loop->update = parse_expression();
        } else if (!has_parens && !check(TokenType::RBRACE)) {
            for_loop->update = parse_statement();
        }
    }
    
    if (has_parens && !match(TokenType::RPAREN)) {
        throw std::runtime_error("Expected ')' after for header");
    }
    
    if (match(TokenType::LBRACE)) {
        while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
            for_loop->body.push_back(parse_statement());
        }
        
        if (!match(TokenType::RBRACE)) {
            throw std::runtime_error("Expected '}' after for body");
        }
    } else {
        for_loop->body.push_back(parse_statement());
    }
    
    return std::move(for_loop);
}

std::unique_ptr<ASTNode> Parser::parse_return_statement() {
    if (!match(TokenType::RETURN)) {
        throw std::runtime_error("Expected 'return'");
    }
    
    std::unique_ptr<ExpressionNode> value = nullptr;
    if (!check(TokenType::SEMICOLON) && !check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
        value = parse_expression();
    }
    
    if (match(TokenType::SEMICOLON)) {
        // Optional semicolon
    }
    
    return std::make_unique<ReturnStatement>(std::move(value));
}

std::unique_ptr<ASTNode> Parser::parse_expression_statement() {
    auto expr = parse_expression();
    
    if (match(TokenType::SEMICOLON)) {
        // Optional semicolon
    }
    
    return std::move(expr);
}

DataType Parser::parse_type() {
    if (!match(TokenType::IDENTIFIER)) {
        throw std::runtime_error("Expected type name");
    }
    
    std::string type_name = tokens[pos - 1].value;
    
    if (type_name == "int8") return DataType::INT8;
    if (type_name == "int16") return DataType::INT16;
    if (type_name == "int32") return DataType::INT32;
    if (type_name == "int64") return DataType::INT64;
    if (type_name == "uint8") return DataType::UINT8;
    if (type_name == "uint16") return DataType::UINT16;
    if (type_name == "uint32") return DataType::UINT32;
    if (type_name == "uint64") return DataType::UINT64;
    if (type_name == "float32") return DataType::FLOAT32;
    if (type_name == "float64") return DataType::FLOAT64;
    if (type_name == "number") return DataType::FLOAT64;
    if (type_name == "boolean") return DataType::BOOLEAN;
    if (type_name == "string") return DataType::STRING;
    if (type_name == "tensor") return DataType::TENSOR;
    if (type_name == "void") return DataType::VOID;
    if (type_name == "any") return DataType::ANY;
    
    return DataType::UNKNOWN;
}

std::unique_ptr<ASTNode> Parser::parse_class_declaration() {
    if (!match(TokenType::CLASS)) {
        throw std::runtime_error("Expected 'class'");
    }
    
    if (!check(TokenType::IDENTIFIER)) {
        throw std::runtime_error("Expected class name");
    }
    
    std::string class_name = current_token().value;
    advance();
    
    auto class_decl = std::make_unique<ClassDecl>(class_name);
    
    // Handle inheritance
    if (match(TokenType::EXTENDS)) {
        if (!check(TokenType::IDENTIFIER)) {
            throw std::runtime_error("Expected parent class name");
        }
        class_decl->parent_class = current_token().value;
        advance();
    }
    
    if (!match(TokenType::LBRACE)) {
        throw std::runtime_error("Expected '{' after class name");
    }
    
    // Parse class body
    while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
        // Check for access modifiers
        bool is_private = false;
        bool is_protected = false;
        bool is_static = false;
        
        if (match(TokenType::PRIVATE)) {
            is_private = true;
        } else if (match(TokenType::PROTECTED)) {
            is_protected = true;
        } else if (match(TokenType::PUBLIC)) {
            // public is default, just consume token
        }
        
        if (match(TokenType::STATIC)) {
            is_static = true;
        }
        
        if (check(TokenType::CONSTRUCTOR)) {
            if (class_decl->constructor) {
                throw std::runtime_error("Class can only have one constructor");
            }
            class_decl->constructor = parse_constructor_declaration(class_decl->name);
        } else if (check(TokenType::IDENTIFIER)) {
            // Could be field or method
            std::string member_name = current_token().value;
            advance();
            
            if (check(TokenType::COLON)) {
                // Field declaration: name: type [= defaultValue];
                advance(); // consume ':'
                DataType field_type = parse_type();
                
                Variable field;
                field.name = member_name;
                field.type = field_type;
                field.is_mutable = true;
                field.is_static = is_static;
                
                // Check for default value
                if (match(TokenType::ASSIGN)) {
                    // Parse the default value expression
                    field.default_value = parse_expression();
                }
                
                class_decl->fields.push_back(field);
                
                if (match(TokenType::SEMICOLON)) {
                    // Optional semicolon
                }
            } else if (check(TokenType::LPAREN)) {
                // Method declaration
                pos--; // Go back to method name
                auto method = parse_method_declaration();
                method->is_static = is_static;
                method->is_private = is_private;
                method->is_protected = is_protected;
                class_decl->methods.push_back(std::move(method));
            } else {
                throw std::runtime_error("Expected ':' for field or '(' for method");
            }
        } else {
            throw std::runtime_error("Expected constructor, field, or method declaration");
        }
    }
    
    if (!match(TokenType::RBRACE)) {
        throw std::runtime_error("Expected '}' after class body");
    }
    
    return std::move(class_decl);
}

std::unique_ptr<MethodDecl> Parser::parse_method_declaration() {
    if (!check(TokenType::IDENTIFIER)) {
        throw std::runtime_error("Expected method name");
    }
    
    std::string method_name = current_token().value;
    advance();
    
    auto method = std::make_unique<MethodDecl>(method_name);
    
    if (!match(TokenType::LPAREN)) {
        throw std::runtime_error("Expected '(' after method name");
    }
    
    // Parse parameters
    while (!check(TokenType::RPAREN) && !check(TokenType::EOF_TOKEN)) {
        if (!check(TokenType::IDENTIFIER)) {
            throw std::runtime_error("Expected parameter name");
        }
        
        Variable param;
        param.name = current_token().value;
        advance();
        
        if (match(TokenType::COLON)) {
            param.type = parse_type();
        } else {
            param.type = DataType::UNKNOWN;
        }
        
        method->parameters.push_back(param);
        
        if (!match(TokenType::COMMA)) {
            break;
        }
    }
    
    if (!match(TokenType::RPAREN)) {
        throw std::runtime_error("Expected ')' after parameters");
    }
    
    // Parse return type
    if (match(TokenType::COLON)) {
        method->return_type = parse_type();
    }
    
    if (!match(TokenType::LBRACE)) {
        throw std::runtime_error("Expected '{' before method body");
    }
    
    // Parse method body
    while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
        method->body.push_back(parse_statement());
    }
    
    if (!match(TokenType::RBRACE)) {
        throw std::runtime_error("Expected '}' after method body");
    }
    
    return method;
}

std::unique_ptr<ConstructorDecl> Parser::parse_constructor_declaration(const std::string& class_name) {
    if (!match(TokenType::CONSTRUCTOR)) {
        throw std::runtime_error("Expected 'constructor'");
    }
    
    auto constructor = std::make_unique<ConstructorDecl>(class_name);
    
    if (!match(TokenType::LPAREN)) {
        throw std::runtime_error("Expected '(' after constructor");
    }
    
    // Parse parameters
    while (!check(TokenType::RPAREN) && !check(TokenType::EOF_TOKEN)) {
        if (!check(TokenType::IDENTIFIER)) {
            throw std::runtime_error("Expected parameter name");
        }
        
        Variable param;
        param.name = current_token().value;
        advance();
        
        if (match(TokenType::COLON)) {
            param.type = parse_type();
        } else {
            param.type = DataType::UNKNOWN;
        }
        
        constructor->parameters.push_back(param);
        
        if (!match(TokenType::COMMA)) {
            break;
        }
    }
    
    if (!match(TokenType::RPAREN)) {
        throw std::runtime_error("Expected ')' after constructor parameters");
    }
    
    if (!match(TokenType::LBRACE)) {
        throw std::runtime_error("Expected '{' before constructor body");
    }
    
    // Parse constructor body
    while (!check(TokenType::RBRACE) && !check(TokenType::EOF_TOKEN)) {
        constructor->body.push_back(parse_statement());
    }
    
    if (!match(TokenType::RBRACE)) {
        throw std::runtime_error("Expected '}' after constructor body");
    }
    
    return constructor;
}

std::vector<std::unique_ptr<ASTNode>> Parser::parse() {
    std::vector<std::unique_ptr<ASTNode>> statements;
    
    while (!check(TokenType::EOF_TOKEN)) {
        statements.push_back(parse_statement());
    }
    
    return statements;
}

}