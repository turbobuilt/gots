#include "compiler.h"
#include <regex>
#include <algorithm>
#include <iostream>
#include <cmath>

namespace gots {

DataType TypeInference::infer_type(const std::string& expression) {
    if (variable_types.find(expression) != variable_types.end()) {
        return variable_types[expression];
    }
    
    // For JavaScript compatibility, all numeric literals default to number (float64)
    if (std::regex_match(expression, std::regex(R"(\d+)"))) {
        return DataType::NUMBER;  // JavaScript compatibility: integer literals are number (float64)
    }
    
    if (std::regex_match(expression, std::regex(R"(\d+\.\d+)"))) {
        return DataType::NUMBER;  // JavaScript compatibility: decimal literals are number (float64)
    }
    
    if (expression == "true" || expression == "false") {
        return DataType::BOOLEAN;
    }
    
    if (expression.front() == '"' && expression.back() == '"') {
        return DataType::STRING;
    }
    
    return DataType::UNKNOWN;
}

DataType TypeInference::infer_operator_index_type(const std::string& class_name, const std::string& index_expression) {
    std::cout << "DEBUG: infer_operator_index_type called with class=" << class_name << ", expression='" << index_expression << "'" << std::endl;
    
    auto* compiler = get_current_compiler();
    if (!compiler) {
        std::cout << "DEBUG: No compiler available" << std::endl;
        return DataType::UNKNOWN;
    }
    
    // Check if the index expression is deterministic
    bool is_deterministic = is_deterministic_expression(index_expression);
    std::cout << "DEBUG: Expression is deterministic: " << (is_deterministic ? "YES" : "NO") << std::endl;
    
    if (is_deterministic) {
        // For deterministic expressions, infer the type based on the expression
        DataType inferred_type = infer_expression_type(index_expression);
        std::cout << "DEBUG: Inferred type from expression: " << static_cast<int>(inferred_type) << std::endl;
        
        if (inferred_type != DataType::UNKNOWN) {
            // For numeric literals, try priority ordering
            if (is_numeric_literal(index_expression)) {
                std::cout << "DEBUG: Expression is numeric literal" << std::endl;
                DataType best_numeric_type = get_best_numeric_operator_type(class_name, index_expression);
                std::cout << "DEBUG: Best numeric type: " << static_cast<int>(best_numeric_type) << std::endl;
                if (best_numeric_type != DataType::UNKNOWN) {
                    std::vector<DataType> operand_types = {best_numeric_type};
                    const auto* overload = compiler->find_best_operator_overload(class_name, TokenType::LBRACKET, operand_types);
                    std::cout << "DEBUG: Found operator overload: " << (overload ? "YES" : "NO") << std::endl;
                    if (overload) {
                        std::cout << "DEBUG: Returning overload return type: " << static_cast<int>(overload->return_type) << std::endl;
                        return overload->return_type;
                    }
                } else {
                    std::cout << "DEBUG: Best numeric type is UNKNOWN" << std::endl;
                }
            } else {
                // For other deterministic expressions, use the inferred type directly
                std::vector<DataType> operand_types = {inferred_type};
                const auto* best_overload = compiler->find_best_operator_overload(class_name, TokenType::LBRACKET, operand_types);
                
                if (best_overload) {
                    return best_overload->return_type;
                }
            }
        }
    }
    
    // For non-deterministic expressions, try fallback priorities:
    // 1. String type (for slice notation like "2:6")
    // 2. ANY/UNKNOWN type (for flexible operator overloads)
    
    // Try string type first
    std::vector<DataType> string_operand_types = {DataType::STRING};
    const auto* string_overload = compiler->find_best_operator_overload(class_name, TokenType::LBRACKET, string_operand_types);
    
    if (string_overload) {
        return string_overload->return_type;
    }
    
    // Try ANY/UNKNOWN type for flexible overloads (user left type blank)
    std::vector<DataType> any_operand_types = {DataType::ANY};
    const auto* any_overload = compiler->find_best_operator_overload(class_name, TokenType::LBRACKET, any_operand_types);
    
    if (any_overload) {
        return any_overload->return_type;
    }
    
    return DataType::UNKNOWN;
}

bool TypeInference::is_numeric_literal(const std::string& expression) {
    // Check if the expression is a simple numeric literal
    return std::regex_match(expression, std::regex(R"(\d+(\.\d+)?)"));
}

bool TypeInference::is_deterministic_expression(const std::string& expression) {
    // Check for slice notation patterns that indicate non-deterministic types
    if (expression.find(':') != std::string::npos) {
        return false; // e.g., "2:6" - slice notation
    }
    
    if (expression.find("...") != std::string::npos) {
        return false; // e.g., "..." - spread operator
    }
    
    // Check for comparison expressions that result in arrays
    if (expression.find('>') != std::string::npos || 
        expression.find('<') != std::string::npos ||
        expression.find("==") != std::string::npos ||
        expression.find("!=") != std::string::npos) {
        // This could be an array comparison like "y > 0" where y is an array
        return is_array_comparison_expression(expression);
    }
    
    // For simple numeric literals or variables, it's deterministic
    return true;
}

bool TypeInference::is_array_comparison_expression(const std::string& expression) {
    // Extract the left-hand side variable name from comparison expressions
    std::regex comparison_regex(R"(([a-zA-Z_][a-zA-Z0-9_]*)\s*([><=!]+))");
    std::smatch match;
    
    if (std::regex_search(expression, match, comparison_regex)) {
        std::string var_name = match[1].str();
        std::string operator_str = match[2].str();
        
        DataType var_type = get_variable_type(var_name);
        
        // If the variable is a class instance, check for operator overloading
        if (var_type == DataType::CLASS_INSTANCE) {
            std::string class_name = get_variable_class_name(var_name);
            TokenType op_token = string_to_operator_token(operator_str);
            
            if (op_token != TokenType::EOF_TOKEN) {
                return can_use_operator_overload(class_name, op_token, {});
            }
        }
        
        // If the variable is an array/tensor, then the comparison returns an array
        return var_type == DataType::TENSOR;
    }
    
    return false;
}

TokenType TypeInference::string_to_operator_token(const std::string& op_str) {
    if (op_str == ">") return TokenType::GREATER;
    if (op_str == "<") return TokenType::LESS;
    if (op_str == ">=") return TokenType::GREATER_EQUAL;
    if (op_str == "<=") return TokenType::LESS_EQUAL;
    if (op_str == "==") return TokenType::EQUAL;
    if (op_str == "!=") return TokenType::NOT_EQUAL;
    return TokenType::EOF_TOKEN;
}

DataType TypeInference::infer_expression_type(const std::string& expression) {
    std::cout << "DEBUG: infer_expression_type called with: '" << expression << "'" << std::endl;
    
    // Handle numeric literals with priority ordering
    // First check for integer without decimal point
    if (std::regex_match(expression, std::regex(R"(\d+)"))) {
        // Integer literal without decimal point
        int64_t value = std::stoll(expression);
        
        // Priority ordering: int64 -> int32 -> float64 -> float32
        if (value >= INT32_MIN && value <= INT32_MAX) {
            return DataType::INT32; // Fits in int32
        } else {
            return DataType::INT64; // Requires int64
        }
    }
    
    // Check for decimal literal (including .000000 formatted integers)
    if (std::regex_match(expression, std::regex(R"(\d+\.\d+)"))) {
        std::cout << "DEBUG: Matched decimal literal pattern for: " << expression << std::endl;
        double value = std::stod(expression);
        
        // Check if it's really an integer value (like 0.000000)
        if (std::floor(value) == value && value >= INT32_MIN && value <= INT32_MAX) {
            // It's an integer value formatted as float - treat as integer
            std::cout << "DEBUG: Detected integer value in float format, returning INT64" << std::endl;
            return DataType::INT64; // Use int64 for compatibility
        } else {
            // It's a real decimal value
            std::cout << "DEBUG: Detected real decimal value, returning FLOAT64" << std::endl;
            return DataType::FLOAT64; // Default to float64 for decimal literals
        }
    } else {
        std::cout << "DEBUG: Expression '" << expression << "' does not match decimal literal pattern" << std::endl;
    }
    
    // Handle slice notation - convert to string
    if (expression.find(':') != std::string::npos) {
        return DataType::STRING;
    }
    
    // Handle complex expressions with operators
    DataType complex_type = infer_complex_expression_type(expression);
    if (complex_type != DataType::UNKNOWN) {
        return complex_type;
    }
    
    // Try to infer from existing variable types
    return infer_type(expression);
}

DataType TypeInference::infer_complex_expression_type(const std::string& expression) {
    // Parse binary operations like "y > 0", "tensor == 5", etc.
    std::regex binary_op_regex(R"(([a-zA-Z_][a-zA-Z0-9_]*)\s*([><=!+\-*/]+)\s*(.+))");
    std::smatch match;
    
    if (std::regex_search(expression, match, binary_op_regex)) {
        std::string left_var = match[1].str();
        std::string operator_str = match[2].str();
        std::string right_operand = match[3].str();
        
        DataType left_type = get_variable_type(left_var);
        
        // If left operand is a class instance, check for operator overloading
        if (left_type == DataType::CLASS_INSTANCE) {
            std::string class_name = get_variable_class_name(left_var);
            TokenType op_token = string_to_operator_token(operator_str);
            
            if (op_token != TokenType::EOF_TOKEN) {
                // Infer the type of the right operand
                DataType right_type = infer_expression_type(right_operand);
                
                // Use operator overloading to determine result type
                std::vector<DataType> operand_types = {left_type, right_type};
                return infer_operator_result_type(class_name, op_token, operand_types);
            }
        }
        
        // Handle standard operations for built-in types
        if (left_type == DataType::TENSOR) {
            // Tensor operations typically return tensors
            return DataType::TENSOR;
        }
    }
    
    return DataType::UNKNOWN;
}

DataType TypeInference::get_best_numeric_operator_type(const std::string& class_name, const std::string& numeric_literal) {
    std::cout << "DEBUG: get_best_numeric_operator_type called with: '" << numeric_literal << "'" << std::endl;
    
    auto* compiler = get_current_compiler();
    if (!compiler) {
        return DataType::UNKNOWN;
    }
    
    // Parse the numeric literal
    bool has_decimal = numeric_literal.find('.') != std::string::npos;
    std::cout << "DEBUG: Has decimal: " << (has_decimal ? "YES" : "NO") << std::endl;
    
    if (!has_decimal) {
        // Integer literal - try in priority order: int64, int32, float64, float32, ANY
        std::vector<DataType> priority_types = {DataType::INT64, DataType::INT32, DataType::FLOAT64, DataType::FLOAT32, DataType::ANY};
        
        for (DataType type : priority_types) {
            std::vector<DataType> operand_types = {type};
            const auto* overload = compiler->find_best_operator_overload(class_name, TokenType::LBRACKET, operand_types);
            if (overload) {
                return type;
            }
        }
        
        // FALLBACK: If no exact type match found, but we know there are operator overloads,
        // just return INT64 since we're dealing with integer literals
        if (compiler->has_operator_overload(class_name, TokenType::LBRACKET)) {
            return DataType::INT64;
        }
    } else {
        // Decimal literal - try in priority order: float64, float32, ANY
        std::cout << "DEBUG: Trying decimal literal priority ordering" << std::endl;
        std::vector<DataType> priority_types = {DataType::FLOAT64, DataType::FLOAT32, DataType::ANY};
        
        for (DataType type : priority_types) {
            std::cout << "DEBUG: Trying type: " << static_cast<int>(type) << std::endl;
            std::vector<DataType> operand_types = {type};
            const auto* overload = compiler->find_best_operator_overload(class_name, TokenType::LBRACKET, operand_types);
            std::cout << "DEBUG: Found overload for type " << static_cast<int>(type) << ": " << (overload ? "YES" : "NO") << std::endl;
            if (overload) {
                std::cout << "DEBUG: Returning type: " << static_cast<int>(type) << std::endl;
                return type;
            }
        }
        
        // FALLBACK: If no exact type match found, but we know there are operator overloads,
        // just return FLOAT64 since we're dealing with decimal literals
        if (compiler->has_operator_overload(class_name, TokenType::LBRACKET)) {
            std::cout << "DEBUG: Fallback - returning FLOAT64 for decimal literal" << std::endl;
            return DataType::FLOAT64;
        }
    }
    
    return DataType::UNKNOWN;
}

DataType TypeInference::get_cast_type(DataType t1, DataType t2) {
    if (t1 == DataType::UNKNOWN || t2 == DataType::UNKNOWN) {
        return DataType::UNKNOWN;
    }
    
    if (t1 == t2) {
        return t1;
    }
    
    std::vector<DataType> integer_hierarchy = {
        DataType::INT8, DataType::UINT8, DataType::INT16, DataType::UINT16,
        DataType::INT32, DataType::UINT32, DataType::INT64, DataType::UINT64
    };
    
    std::vector<DataType> float_hierarchy = {
        DataType::FLOAT32, DataType::FLOAT64
    };
    
    auto is_integer = [&](DataType t) {
        return std::find(integer_hierarchy.begin(), integer_hierarchy.end(), t) != integer_hierarchy.end();
    };
    
    auto is_float = [&](DataType t) {
        // NUMBER is identical to FLOAT64 for performance
        return t == DataType::NUMBER || std::find(float_hierarchy.begin(), float_hierarchy.end(), t) != float_hierarchy.end();
    };
    
    auto get_integer_rank = [&](DataType t) {
        auto it = std::find(integer_hierarchy.begin(), integer_hierarchy.end(), t);
        return it != integer_hierarchy.end() ? it - integer_hierarchy.begin() : -1;
    };
    
    auto get_float_rank = [&](DataType t) -> int {
        // NUMBER is identical to FLOAT64 for performance - same rank
        if (t == DataType::NUMBER) return 1; // Same rank as FLOAT64
        auto it = std::find(float_hierarchy.begin(), float_hierarchy.end(), t);
        return it != float_hierarchy.end() ? static_cast<int>(it - float_hierarchy.begin()) : -1;
    };
    
    if (is_float(t1) || is_float(t2)) {
        if (is_float(t1) && is_float(t2)) {
            return get_float_rank(t1) > get_float_rank(t2) ? t1 : t2;
        }
        return is_float(t1) ? t1 : t2;
    }
    
    if (is_integer(t1) && is_integer(t2)) {
        return get_integer_rank(t1) > get_integer_rank(t2) ? t1 : t2;
    }
    
    if (t1 == DataType::STRING || t2 == DataType::STRING) {
        return DataType::STRING;
    }
    
    return DataType::UNKNOWN;
}

bool TypeInference::needs_casting(DataType from, DataType to) {
    if (from == to) return false;
    if (from == DataType::UNKNOWN || to == DataType::UNKNOWN) return true;
    
    std::vector<DataType> widening_casts[] = {
        {DataType::INT8, DataType::INT16, DataType::INT32, DataType::INT64},
        {DataType::UINT8, DataType::UINT16, DataType::UINT32, DataType::UINT64},
        {DataType::FLOAT32, DataType::FLOAT64},
        {DataType::INT8, DataType::FLOAT32, DataType::FLOAT64},
        {DataType::INT16, DataType::FLOAT32, DataType::FLOAT64},
        {DataType::INT32, DataType::FLOAT64},
        {DataType::INT64, DataType::FLOAT64}
    };
    
    for (const auto& cast_path : widening_casts) {
        auto from_it = std::find(cast_path.begin(), cast_path.end(), from);
        auto to_it = std::find(cast_path.begin(), cast_path.end(), to);
        
        if (from_it != cast_path.end() && to_it != cast_path.end() && from_it < to_it) {
            return false;
        }
    }
    
    return true;
}

void TypeInference::set_variable_type(const std::string& name, DataType type) {
    variable_types[name] = type;
}

DataType TypeInference::get_variable_type(const std::string& name) {
    auto it = variable_types.find(name);
    return it != variable_types.end() ? it->second : DataType::UNKNOWN;
}

void TypeInference::set_variable_offset(const std::string& name, int64_t offset) {
    variable_offsets[name] = offset;
}

int64_t TypeInference::get_variable_offset(const std::string& name) {
    auto it = variable_offsets.find(name);
    return it != variable_offsets.end() ? it->second : -8; // Default to -8 if not found
}

bool TypeInference::variable_exists(const std::string& name) {
    return variable_offsets.find(name) != variable_offsets.end();
}

int64_t TypeInference::allocate_variable(const std::string& name, DataType type) {
    // Check if variable already exists
    auto it = variable_offsets.find(name);
    if (it != variable_offsets.end()) {
        // Variable already allocated, just update type
        variable_types[name] = type;
        return it->second;
    }
    
    // Allocate new variable using instance offset
    int64_t offset = current_offset;
    current_offset -= 8; // Next variable gets the next slot
    
    variable_offsets[name] = offset;
    variable_types[name] = type;
    return offset;
}

void TypeInference::enter_scope() {
    // For now, we don't implement nested scopes - just track current offset
}

void TypeInference::exit_scope() {
    // For now, we don't clean up variables on scope exit
}

void TypeInference::reset_for_function() {
    // Reset offset for new function but preserve global variables
    // Start after parameter space (parameters use -8, -16, -24, etc)
    current_offset = -48;  // Start local variables after parameter space
}

void TypeInference::reset_for_function_with_params(int param_count) {
    // Reset offset for new function with known parameter count
    // Parameters use -8, -16, -24, etc. for the first param_count slots
    // Local variables start after all parameter slots
    current_offset = -(param_count + 1) * 8 - 8;  // Leave extra space for safety
}

void TypeInference::set_variable_class_type(const std::string& name, const std::string& class_name) {
    variable_types[name] = DataType::CLASS_INSTANCE;
    variable_class_names[name] = class_name;
}

std::string TypeInference::get_variable_class_name(const std::string& name) {
    auto it = variable_class_names.find(name);
    return (it != variable_class_names.end()) ? it->second : "";
}

void TypeInference::register_function_params(const std::string& func_name, const std::vector<std::string>& param_names) {
    function_param_names[func_name] = param_names;
}

std::vector<std::string> TypeInference::get_function_params(const std::string& func_name) const {
    auto it = function_param_names.find(func_name);
    return (it != function_param_names.end()) ? it->second : std::vector<std::string>();
}

DataType TypeInference::infer_operator_result_type(const std::string& class_name, TokenType operator_type, 
                                                   const std::vector<DataType>& operand_types) {
    auto* compiler = get_current_compiler();
    if (!compiler) {
        return DataType::UNKNOWN;
    }
    
    const auto* overloads = compiler->get_operator_overloads(class_name, operator_type);
    if (!overloads || overloads->empty()) {
        return DataType::UNKNOWN;
    }
    
    // Find the best matching overload based on parameter types
    const auto* best_overload = compiler->find_best_operator_overload(class_name, operator_type, operand_types);
    if (best_overload) {
        return best_overload->return_type;
    }
    
    return DataType::UNKNOWN;
}

bool TypeInference::can_use_operator_overload(const std::string& class_name, TokenType operator_type, 
                                              const std::vector<DataType>& operand_types) {
    auto* compiler = get_current_compiler();
    if (!compiler) {
        return false;
    }
    
    return compiler->has_operator_overload(class_name, operator_type) &&
           compiler->find_best_operator_overload(class_name, operator_type, operand_types) != nullptr;
}

std::string TypeInference::extract_expression_string(ExpressionNode* node) {
    if (!node) {
        return "";
    }
    
    // Handle different expression types
    if (auto* literal = dynamic_cast<gots::NumberLiteral*>(node)) {
        return std::to_string(literal->value);
    }
    
    if (auto* ident = dynamic_cast<gots::Identifier*>(node)) {
        return ident->name;
    }
    
    if (auto* binary_op = dynamic_cast<gots::BinaryOp*>(node)) {
        std::string left_str = extract_expression_string(binary_op->left.get());
        std::string right_str = extract_expression_string(binary_op->right.get());
        std::string op_str = token_type_to_string(binary_op->op);
        
        return left_str + " " + op_str + " " + right_str;
    }
    
    // For other complex expressions, return a placeholder
    return "complex_expression";
}

std::string TypeInference::token_type_to_string(TokenType token) {
    switch (token) {
        case TokenType::GREATER: return ">";
        case TokenType::LESS: return "<";
        case TokenType::GREATER_EQUAL: return ">=";
        case TokenType::LESS_EQUAL: return "<=";
        case TokenType::EQUAL: return "==";
        case TokenType::NOT_EQUAL: return "!=";
        case TokenType::PLUS: return "+";
        case TokenType::MINUS: return "-";
        case TokenType::MULTIPLY: return "*";
        case TokenType::DIVIDE: return "/";
        case TokenType::MODULO: return "%";
        default: return "unknown_op";
    }
}

}