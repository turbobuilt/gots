#include "compiler.h"
#include "runtime.h"
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>

namespace gots {

GoTSCompiler::GoTSCompiler(Backend backend) : target_backend(backend) {
    set_backend(backend);
}

void GoTSCompiler::set_backend(Backend backend) {
    target_backend = backend;
    
    switch (backend) {
        case Backend::X86_64:
            codegen = std::make_unique<X86CodeGen>();
            break;
        case Backend::WASM:
            codegen = std::make_unique<WasmCodeGen>();
            break;
    }
}

void GoTSCompiler::compile(const std::string& source) {
    try {
        Lexer lexer(source);
        auto tokens = lexer.tokenize();
        
        std::cout << "Tokens generated: " << tokens.size() << std::endl;
        
        Parser parser(std::move(tokens));
        auto ast = parser.parse();
        
        std::cout << "AST nodes: " << ast.size() << std::endl;
        
        codegen->clear();
        
        // Set the compiler context for constructor code generation
        ConstructorDecl::set_compiler_context(this);
        
        // First, register all class declarations and generate default constructors if needed
        for (const auto& node : ast) {
            if (auto class_decl = dynamic_cast<ClassDecl*>(node.get())) {
                // Generate default constructor if none exists
                if (!class_decl->constructor) {
                    class_decl->constructor = std::make_unique<ConstructorDecl>(class_decl->name);
                    std::cout << "Generated default constructor for class: " << class_decl->name << std::endl;
                }
                
                ClassInfo class_info(class_decl->name);
                class_info.fields = class_decl->fields;
                class_info.parent_class = class_decl->parent_class;
                class_info.instance_size = class_decl->fields.size() * 8; // 8 bytes per property
                register_class(class_info);
                std::cout << "Registered class: " << class_decl->name << " with " << class_decl->fields.size() << " fields";
                if (!class_decl->parent_class.empty()) {
                    std::cout << " (extends " << class_decl->parent_class << ")";
                }
                std::cout << std::endl;
            }
        }
        
        // First, generate a jump to main code to skip function declarations
        codegen->emit_jump("__main");
        
        // Generate all function declarations first
        for (const auto& node : ast) {
            if (dynamic_cast<FunctionDecl*>(node.get())) {
                node->generate_code(*codegen, type_system);
            }
        }
        
        // Generate all class constructors and methods before main code
        for (const auto& node : ast) {
            if (auto class_decl = dynamic_cast<ClassDecl*>(node.get())) {
                // Generate constructor first if it exists
                if (class_decl->constructor) {
                    class_decl->constructor->generate_code(*codegen, type_system);
                }
                
                // Generate methods
                for (auto& method : class_decl->methods) {
                    method->generate_code(*codegen, type_system);
                }
            }
        }
        
        // Generate main code label
        codegen->emit_label("__main");
        
        // Calculate stack size for main function based on statement complexity
        size_t non_function_statements = 0;
        for (const auto& node : ast) {
            if (!dynamic_cast<FunctionDecl*>(node.get())) {
                non_function_statements++;
            }
        }
        
        // Estimate stack size: base + (statements * complexity factor) + method call overhead
        int64_t estimated_stack_size = 80 + (non_function_statements * 24) + 64;
        // Ensure 16-byte alignment
        if (estimated_stack_size % 16 != 0) {
            estimated_stack_size += 16 - (estimated_stack_size % 16);
        }
        
        // Set stack size for main function
        if (auto x86_gen = dynamic_cast<X86CodeGen*>(codegen.get())) {
            x86_gen->set_function_stack_size(estimated_stack_size);
        }
        
        codegen->emit_prologue();
        
        // Generate non-function statements
        for (const auto& node : ast) {
            if (!dynamic_cast<FunctionDecl*>(node.get())) {
                node->generate_code(*codegen, type_system);
            }
        }
        
        // Ensure return value is set to 0 for main function
        codegen->emit_mov_reg_imm(0, 0);  // mov rax, 0
        
        // Generate function epilogue
        codegen->emit_epilogue();
        
        std::cout << "Code generation completed. Machine code size: " 
                  << codegen->get_code().size() << " bytes" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Compilation error: " << e.what() << std::endl;
        throw;
    }
}

std::vector<uint8_t> GoTSCompiler::get_machine_code() {
    return codegen->get_code();
}

void GoTSCompiler::execute() {
    if (target_backend == Backend::X86_64) {
        auto machine_code = get_machine_code();
        
        if (machine_code.empty()) {
            std::cerr << "No machine code to execute" << std::endl;
            return;
        }
        
        size_t code_size = machine_code.size();
        // Round up to page size for better memory management
        size_t page_size = sysconf(_SC_PAGESIZE);
        size_t aligned_size = (code_size + page_size - 1) & ~(page_size - 1);
        
        // Use MAP_SHARED to ensure memory is accessible from all threads
        void* exec_mem = mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        
        if (exec_mem == MAP_FAILED) {
            std::cerr << "Failed to allocate executable memory" << std::endl;
            return;
        }
        
        memcpy(exec_mem, machine_code.data(), code_size);
        
        // Make memory executable and readable, but not writable for security
        if (mprotect(exec_mem, aligned_size, PROT_READ | PROT_EXEC) != 0) {
            std::cerr << "Failed to make memory executable" << std::endl;
            munmap(exec_mem, aligned_size);
            return;
        }
        
        // Store the executable memory info globally for thread access
        __set_executable_memory(exec_mem, aligned_size);
        
        __runtime_init();
        
        // Register all functions in the runtime registry
        auto& label_offsets = codegen->get_label_offsets();
        for (const auto& label : label_offsets) {
            const std::string& name = label.first;
            int64_t offset = label.second;
            
            // Skip internal labels like __main, but allow static method labels
            if (name.find("__") == 0 && name.find("__static_") != 0) continue;
            
            // Calculate actual function address
            void* func_addr = reinterpret_cast<void*>(
                reinterpret_cast<uintptr_t>(exec_mem) + offset
            );
            
            __register_function(name.c_str(), func_addr);
        }
        
        // Find and execute main function
        auto main_it = label_offsets.find("__main");
        if (main_it == label_offsets.end()) {
            std::cerr << "Error: __main label not found" << std::endl;
            munmap(exec_mem, code_size);
            return;
        }
        
        std::cout << "DEBUG: About to execute main function at offset " << main_it->second << std::endl;
        
        auto func = reinterpret_cast<int(*)()>(
            reinterpret_cast<uintptr_t>(exec_mem) + main_it->second
        );
        
        std::cout << "DEBUG: Function pointer created, about to call..." << std::endl;
        
        int result = 0;
        try {
            result = func();
            std::cout << "Program executed successfully. Result: " << result << std::endl;
        } catch (...) {
            std::cerr << "Exception caught during program execution" << std::endl;
        }
        
        // Give threads time to complete before cleanup
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        __runtime_cleanup();
        
        // DON'T FREE THE EXECUTABLE MEMORY - it's needed for goroutine function calls
        // The registered functions in the function registry depend on this memory
        // This memory will be freed when the process terminates
        // munmap(exec_mem, aligned_size);
        
    } else if (target_backend == Backend::WASM) {
        std::cout << "WebAssembly execution not implemented in this demo" << std::endl;
        auto machine_code = get_machine_code();
        std::cout << "Generated WASM bytecode size: " << machine_code.size() << " bytes" << std::endl;
    }
}

// Class management methods
void GoTSCompiler::register_class(const ClassInfo& class_info) {
    classes[class_info.name] = class_info;
}

ClassInfo* GoTSCompiler::get_class(const std::string& class_name) {
    auto it = classes.find(class_name);
    return (it != classes.end()) ? &it->second : nullptr;
}

bool GoTSCompiler::is_class_defined(const std::string& class_name) {
    return classes.find(class_name) != classes.end();
}

}