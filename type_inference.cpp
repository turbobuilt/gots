#include "compiler.h"
#include <regex>
#include <algorithm>

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

}