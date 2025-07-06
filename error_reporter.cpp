#include "compiler.h"
#include <iostream>
#include <sstream>
#include <vector>

namespace gots {

std::string ErrorReporter::get_line_content(int line_number) const {
    if (line_number <= 0) return "";
    
    std::istringstream iss(source_code);
    std::string line;
    int current_line = 1;
    
    while (std::getline(iss, line) && current_line <= line_number) {
        if (current_line == line_number) {
            return line;
        }
        current_line++;
    }
    
    return "";
}

std::string ErrorReporter::format_error_context(const std::string& message, int line, int column, const std::string& line_content, char problematic_char) const {
    std::ostringstream oss;
    
    // File path (if available)
    if (!file_path.empty()) {
        oss << file_path << ":";
    }
    
    // Line and column
    oss << line << ":" << column << ": error: " << message << "\n";
    
    // Show a few lines around the error for context
    int start_line = std::max(1, line - 2);
    int end_line = line + 2;
    
    std::istringstream iss(source_code);
    std::string current_line_content;
    int current_line_num = 1;
    std::vector<std::pair<int, std::string>> context_lines;
    
    // Collect context lines
    while (std::getline(iss, current_line_content) && current_line_num <= end_line) {
        if (current_line_num >= start_line) {
            context_lines.emplace_back(current_line_num, current_line_content);
        }
        current_line_num++;
    }
    
    // Display context lines with line numbers and syntax highlighting
    for (const auto& [line_num, content] : context_lines) {
        bool is_error_line = (line_num == line);
        
        // Line number with padding
        std::string line_num_str = std::to_string(line_num);
        std::string padding(5 - line_num_str.length(), ' ');
        
        // Apply syntax highlighting to the line content
        std::string highlighted_content = highlighter.highlight_line(content);
        
        if (is_error_line) {
            oss << " --> " << padding << line_num_str << "│ " << highlighted_content << "\n";
            
            // Add pointer to the exact character
            if (column > 0 && column <= (int)content.length()) {
                std::string pointer_line = "     " + padding + " │ ";
                for (int i = 1; i < column; ++i) {
                    if (i <= (int)content.length() && content[i-1] == '\t') {
                        pointer_line += "\t";
                    } else {
                        pointer_line += " ";
                    }
                }
                pointer_line += "^";
                
                if (problematic_char != '\0') {
                    pointer_line += " unexpected character: '";
                    if (problematic_char == '\n') {
                        pointer_line += "\\n";
                    } else if (problematic_char == '\t') {
                        pointer_line += "\\t";
                    } else if (problematic_char == '\0') {
                        pointer_line += "EOF";
                    } else {
                        pointer_line += problematic_char;
                    }
                    pointer_line += "'";
                }
                
                oss << pointer_line << "\n";
            }
        } else {
            oss << "     " << padding << line_num_str << "│ " << highlighted_content << "\n";
        }
    }
    
    return oss.str();
}

void ErrorReporter::report_error(const std::string& message, int line, int column) const {
    std::string line_content = get_line_content(line);
    std::string formatted_error = format_error_context(message, line, column, line_content);
    std::cerr << formatted_error << std::endl;
}

void ErrorReporter::report_parse_error(const std::string& message, const Token& token) const {
    std::string line_content = get_line_content(token.line);
    std::string enhanced_message = message;
    
    if (!token.value.empty()) {
        enhanced_message += " (found: '" + token.value + "')";
    }
    
    std::string formatted_error = format_error_context(enhanced_message, token.line, token.column, line_content);
    std::cerr << formatted_error << std::endl;
}

void ErrorReporter::report_lexer_error(const std::string& message, int line, int column, char unexpected_char) const {
    std::string line_content = get_line_content(line);
    std::string formatted_error = format_error_context(message, line, column, line_content, unexpected_char);
    std::cerr << formatted_error << std::endl;
}

}