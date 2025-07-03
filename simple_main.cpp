#include "compiler.h"
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>

using namespace gots;

std::string read_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <file.gts>" << std::endl;
        return 1;
    }
    
    std::string filename = argv[1];
    
    try {
        // Read the GoTS file
        std::string program = read_file(filename);
        
        // Compile and execute the program
        GoTSCompiler compiler(Backend::X86_64);
        compiler.compile(program);
        compiler.execute();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}