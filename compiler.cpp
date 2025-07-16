#include "compiler.h"
#include "runtime.h"
#include "runtime_syscalls.h"
#include "goroutine_system.h"
#include "function_compilation_manager.h"

// External console mutex for thread safety
extern std::mutex g_console_mutex;
#include <fstream>
#include <iostream>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>

// New goroutine system functions
extern "C" void __runtime_spawn_main_goroutine(void* func_ptr);
extern "C" void __runtime_wait_for_main_goroutine();

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
        // Create error reporter with source code and file path
        ErrorReporter error_reporter(source, current_file_path);
        
        Lexer lexer(source, &error_reporter);
        auto tokens = lexer.tokenize();
        
        std::cout << "Tokens generated: " << tokens.size() << std::endl;
        
        Parser parser(std::move(tokens), &error_reporter);
        auto ast = parser.parse();
        
        std::cout << "AST nodes: " << ast.size() << std::endl;
        
        codegen->clear();
        
        // Runtime functions will be registered during runtime initialization
        // to avoid double registration and potential memory corruption
        
        // Set the compiler context for constructor code generation
        ConstructorDecl::set_compiler_context(this);
        
        // Set the compiler context for function registration
        set_current_compiler(this);
        
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
                
                // CRITICAL: Register operator overloads BEFORE any code generation that might use them
                for (const auto& op_overload : class_decl->operator_overloads) {
                    // Generate parameter signature for unique function naming
                    std::string param_signature = "";
                    for (size_t i = 0; i < op_overload->parameters.size(); ++i) {
                        if (i > 0) param_signature += "_";
                        if (op_overload->parameters[i].type == DataType::UNKNOWN) {
                            param_signature += "any";
                        } else {
                            param_signature += std::to_string(static_cast<int>(op_overload->parameters[i].type));
                        }
                    }
                    
                    std::string op_function_name = class_decl->name + "::__op_" + std::to_string(static_cast<int>(op_overload->operator_type)) + "_" + param_signature + "__";
                    OperatorOverload overload(op_overload->operator_type, op_overload->parameters, op_overload->return_type);
                    overload.function_name = op_function_name;
                    register_operator_overload(class_decl->name, overload);
                    std::cout << "Pre-registered operator overload " << op_function_name 
                              << " for class " << class_decl->name << " with operator type " << static_cast<int>(op_overload->operator_type) << std::endl;
                }
            }
        }
        
        // NEW THREE-PHASE COMPILATION SYSTEM
        FunctionCompilationManager::instance().clear();
        FunctionCompilationManager::instance().discover_functions(ast);
        
        // PHASE 2: FUNCTION COMPILATION
        // Compile all functions to the beginning of the code section
        FunctionCompilationManager::instance().compile_all_functions(*codegen, type_system);
        
        // Check if we have any function declarations or class definitions
        bool has_functions = false;
        bool has_classes = false;
        for (const auto& node : ast) {
            if (dynamic_cast<FunctionDecl*>(node.get())) {
                has_functions = true;
            }
            if (dynamic_cast<ClassDecl*>(node.get())) {
                has_classes = true;
            }
        }
        
        // Only generate a jump to main if we have function declarations or classes to skip
        if (has_functions || has_classes) {
            codegen->emit_jump("__main");
        }
        
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
                
                // Generate operator overloads
                for (auto& op_overload : class_decl->operator_overloads) {
                    op_overload->generate_code(*codegen, type_system);
                }
            }
        }
        
        // PHASE 2.5: PREPARE FOR MAIN CODE GENERATION
        // At this point, we need to create executable memory and assign function addresses
        // before generating main code, so function expressions can use direct addresses
        
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
        
        // Process imports first (they are hoisted like in JavaScript/TypeScript)
        for (const auto& node : ast) {
            if (dynamic_cast<ImportStatement*>(node.get())) {
                node->generate_code(*codegen, type_system);
            }
        }
        
        // Generate non-function, non-import statements
        for (const auto& node : ast) {
            if (!dynamic_cast<FunctionDecl*>(node.get()) && !dynamic_cast<ImportStatement*>(node.get())) {
                node->generate_code(*codegen, type_system);
            }
        }
        
        // Add explicit jump to epilogue to prevent fall-through
        codegen->emit_jump("__main_epilogue");
        
        // Mark epilogue location  
        codegen->emit_label("__main_epilogue");
        
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
        
        // Use MAP_PRIVATE for proper JIT memory isolation
        void* exec_mem = mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        
        if (exec_mem == MAP_FAILED) {
            std::cerr << "Failed to allocate executable memory" << std::endl;
            return;
        }
        
        memcpy(exec_mem, machine_code.data(), code_size);
        
        // Store the executable memory info globally for thread access
        __set_executable_memory(exec_mem, aligned_size);
        
        __runtime_init();
        
        // Runtime registration happens automatically in new system
        
        // PRODUCTION FIX: Resolve any unresolved runtime function calls now that the registry is populated
        // We need to patch the code while it's still writable
        if (auto x86_gen = dynamic_cast<X86CodeGen*>(codegen.get())) {
            x86_gen->resolve_runtime_function_calls();
            
            // Apply the patches to the executable memory
            auto updated_code = x86_gen->get_code();
            memcpy(exec_mem, updated_code.data(), updated_code.size());
        }
        
        // PRODUCTION FIX: Compile all deferred function expressions AFTER stubs are generated
        // This ensures function expressions are placed after stubs at the correct offset
        compile_deferred_function_expressions(*codegen, type_system);
        
        // Update the executable memory with the function expressions
        if (auto x86_gen = dynamic_cast<X86CodeGen*>(codegen.get())) {
            auto updated_code = x86_gen->get_code();
            memcpy(exec_mem, updated_code.data(), updated_code.size());
        }
        
        // Make memory executable and readable, but not writable for security
        if (mprotect(exec_mem, aligned_size, PROT_READ | PROT_EXEC) != 0) {
            std::cerr << "Failed to make memory executable" << std::endl;
            munmap(exec_mem, aligned_size);
            return;
        }
        
        // PHASE 2.5: ASSIGN FUNCTION ADDRESSES
        // Now that we have executable memory, assign addresses to all functions
        FunctionCompilationManager::instance().assign_function_addresses(exec_mem, aligned_size);
        FunctionCompilationManager::instance().register_function_in_runtime();
        FunctionCompilationManager::instance().print_function_registry();
        
        // Register all functions in the runtime registry
        auto& label_offsets = codegen->get_label_offsets();
        for (const auto& label : label_offsets) {
            std::cout << "  " << label.first << " -> " << label.second << std::endl;
        }
        
        for (const auto& label : label_offsets) {
            const std::string& name = label.first;
            int64_t offset = label.second;
            
            // Skip internal labels like __main, but allow static method labels and function expressions
            if (name.find("__") == 0 && name.find("__static_") != 0 && name.find("__func_expr_") != 0) continue;
            
            // Calculate actual function address
            void* func_addr = reinterpret_cast<void*>(
                reinterpret_cast<uintptr_t>(exec_mem) + offset
            );
            
            __register_function_fast(func_addr, 0, 0);
        }
        
        // Find and execute main function
        auto main_it = label_offsets.find("__main");
        if (main_it == label_offsets.end()) {
            std::cerr << "Error: __main label not found" << std::endl;
            munmap(exec_mem, aligned_size);
            return;
        }
        
        
        auto func = reinterpret_cast<int(*)()>(
            reinterpret_cast<uintptr_t>(exec_mem) + main_it->second
        );
        
        
        // Spawn the main function as the main goroutine - ALL JS runs in goroutines
        int result = 0;
        try {
            std::cout.flush();
            
            // Spawn main function as the top-level goroutine
            __runtime_spawn_main_goroutine(reinterpret_cast<void*>(func));
            {
                std::lock_guard<std::mutex> lock(g_console_mutex);
                std::cout.flush();
            }
            
            // With simplified timer system, no need to mark execution complete
            {
                std::lock_guard<std::mutex> lock(g_console_mutex);
            }
            
            // Timer processing is now handled by the main goroutine's event loop
            
            // If we have timers, start the timer scheduler
            // For now, just exit cleanly since timer execution is complex
        } catch (const std::exception& e) {
            std::cerr << "Exception caught during program execution: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception caught during program execution" << std::endl;
        }
        
        // Wait for main goroutine to complete (which will wait for all its children and timers)
        // This is the ONLY wait the main loop should do - never wait for timers directly
        __runtime_wait_for_main_goroutine();
        
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

// Function management methods

// Module system methods
std::string GoTSCompiler::resolve_module_path(const std::string& module_path, const std::string& current_file) {
    // Handle relative and absolute paths
    std::string resolved_path = module_path;
    
    // If it's a relative path and we have a current file, resolve relative to it
    bool is_relative = (module_path.length() >= 2 && module_path.substr(0, 2) == "./") ||
                      (module_path.length() >= 3 && module_path.substr(0, 3) == "../");
    
    if (!current_file.empty() && is_relative) {
        // Extract directory from current file
        size_t last_slash = current_file.find_last_of("/\\");
        if (last_slash != std::string::npos) {
            std::string current_dir = current_file.substr(0, last_slash + 1);
            resolved_path = current_dir + module_path;
        }
    }
    
    // Try different extensions: .gts, .ts, .js (in order of preference)
    std::vector<std::string> extensions = {".gts", ".ts", ".js"};
    
    // First try the path as-is (might already have extension)
    std::ifstream file(resolved_path);
    if (file.good()) {
        file.close();
        return resolved_path;
    }
    
    // Try with different extensions
    for (const auto& ext : extensions) {
        std::string path_with_ext = resolved_path + ext;
        std::ifstream test_file(path_with_ext);
        if (test_file.good()) {
            test_file.close();
            return path_with_ext;
        }
    }
    
    // If no file found, return original path (will cause error later)
    return resolved_path;
}

Module* GoTSCompiler::load_module(const std::string& module_path) {
    // Check if module is already loaded
    auto it = modules.find(module_path);
    if (it != modules.end() && it->second.loaded) {
        return &it->second;
    }
    
    // Resolve the actual file path using current file context
    std::string resolved_path = resolve_module_path(module_path, current_file_path);
    
    // Read the file
    std::ifstream file(resolved_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open module file: " + resolved_path);
    }
    
    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    file.close();
    
    // Parse the module
    Lexer lexer(source);
    auto tokens = lexer.tokenize();
    Parser parser(std::move(tokens));
    auto ast = parser.parse();
    
    // Create module entry
    Module& module = modules[module_path];
    module.path = resolved_path;
    module.ast = std::move(ast);
    module.loaded = true;
    
    // Analyze exports in the module
    bool has_named_exports = false;
    for (const auto& stmt : module.ast) {
        if (auto export_stmt = dynamic_cast<ExportStatement*>(stmt.get())) {
            if (export_stmt->is_default) {
                module.has_default_export = true;
                module.default_export_name = "default";
            } else {
                has_named_exports = true;
                // Add named exports to module
                for (const auto& spec : export_stmt->specifiers) {
                    // For now, just track that we have named exports
                    // Full implementation would analyze the actual exported values
                }
            }
        }
    }
    
    // Create synthetic default export if no default but has named exports
    if (!module.has_default_export && has_named_exports) {
        create_synthetic_default_export(module);
    }
    
    return &module;
}

void GoTSCompiler::create_synthetic_default_export(Module& module) {
    // Create a synthetic default export that is an object containing all named exports
    module.has_default_export = true;
    module.default_export_name = "__synthetic_default";
    
    // The synthetic default will be created at runtime by collecting all named exports
    // into a single object. This allows: import module from "./file" when file only
    // has named exports like: export const foo = 1; export function bar() {}
    // The result would be: module = { foo: 1, bar: [Function] }
}

// Enhanced lazy loading system implementation
Module* GoTSCompiler::load_module_lazy(const std::string& module_path) {
    // Check if module is already in cache
    auto it = modules.find(module_path);
    if (it != modules.end()) {
        Module& module = it->second;
        
        // If already loaded, return it
        if (module.is_ready()) {
            return &module;
        }
        
        // If currently loading, we have a circular import
        if (module.is_loading()) {
            handle_circular_import(module_path);
            return &module;  // Return partial module
        }
        
        // If has error, throw with stack trace
        if (module.has_error()) {
            throw std::runtime_error("Module load failed: " + module_path + "\n" + 
                                      module.load_info.error_message + "\n" + 
                                      get_import_stack_trace());
        }
    }
    
    // Check for circular import before starting load
    if (is_circular_import(module_path)) {
        std::cerr << "CIRCULAR IMPORT DETECTED: " << module_path << std::endl;
        std::cerr << get_import_stack_trace() << std::endl;
        return handle_circular_import_and_return(module_path);
    }
    
    // Start loading the module
    Module& module = modules[module_path];
    module.path = module_path;
    module.state = ModuleState::LOADING;
    module.load_info.import_stack = current_loading_stack;
    current_loading_stack.push_back(module_path);
    
    std::cerr << "LOADING MODULE: " << module_path << " (stack depth: " << current_loading_stack.size() << ")" << std::endl;
    
    try {
        // Resolve the actual file path using current file context
        std::string resolved_path = resolve_module_path(module_path, current_file_path);
        module.path = resolved_path;
        
        // Read the file
        std::ifstream file(resolved_path);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open module file: " + resolved_path);
        }
        
        std::string source((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        file.close();
        
        // Parse the module AST (but don't execute yet - that's the lazy part)
        Lexer lexer(source);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens));
        module.ast = parser.parse();
        
        // Analyze exports (but don't execute code yet)
        prepare_partial_exports(module);
        
        // Mark as loaded
        module.state = ModuleState::LOADED;
        module.loaded = true;  // Backward compatibility
        
        // Remove from loading stack
        current_loading_stack.pop_back();
        
        std::cerr << "MODULE LOADED SUCCESSFULLY: " << module_path << std::endl;
        
        return &module;
        
    } catch (const std::exception& e) {
        // Handle loading error
        module.state = ModuleState::ERROR;
        module.load_info.error_message = e.what();
        current_loading_stack.pop_back();
        throw;
    }
}

bool GoTSCompiler::is_circular_import(const std::string& module_path) {
    // Check if module_path is already in the loading stack
    for (const auto& loading_module : current_loading_stack) {
        if (loading_module == module_path) {
            return true;
        }
    }
    return false;
}

Module* GoTSCompiler::handle_circular_import_and_return(const std::string& module_path) {
    // Find the module in cache (it should exist since we're loading it)
    auto it = modules.find(module_path);
    if (it != modules.end()) {
        Module& module = it->second;
        
        // Mark as partial if not already
        if (module.state == ModuleState::LOADING) {
            module.state = ModuleState::PARTIAL_LOADED;
            module.exports_partial = true;
        }
        
        return &module;
    }
    
    // Create new partial module
    Module& module = modules[module_path];
    module.path = module_path;
    module.state = ModuleState::PARTIAL_LOADED;
    module.exports_partial = true;
    module.load_info.import_stack = current_loading_stack;
    
    return &module;
}

void GoTSCompiler::handle_circular_import(const std::string& module_path) {
    // Log the circular import for debugging
    std::string stack_trace = get_import_stack_trace();
    
    // For now, just continue with partial loading
    // In production, you might want to emit a warning
    // std::cerr << "Warning: Circular import detected: " << module_path << std::endl;
    // std::cerr << "Import stack: " << stack_trace << std::endl;
}

std::string GoTSCompiler::get_import_stack_trace() const {
    std::string trace = "Import stack:\n";
    for (int i = current_loading_stack.size() - 1; i >= 0; --i) {
        trace += "  " + std::to_string(current_loading_stack.size() - i) + ". " + 
                 current_loading_stack[i] + "\n";
    }
    return trace;
}

void GoTSCompiler::execute_module_code(Module& module) {
    // Only execute if not already executed
    if (module.code_executed) {
        return;
    }
    
    // Execute the module's AST
    for (const auto& stmt : module.ast) {
        // This would execute the statements in the module
        // For now, just mark as executed
        // In real implementation, would call stmt->generate_code()
    }
    
    module.code_executed = true;
}

void GoTSCompiler::prepare_partial_exports(Module& module) {
    // Analyze exports in the module without executing code
    bool has_named_exports = false;
    
    for (const auto& stmt : module.ast) {
        if (auto export_stmt = dynamic_cast<ExportStatement*>(stmt.get())) {
            if (export_stmt->is_default) {
                module.has_default_export = true;
                module.default_export_name = "default";
            } else {
                has_named_exports = true;
                // Add named exports to module
                for (const auto& spec : export_stmt->specifiers) {
                    // Create placeholder variables for now
                    Variable placeholder;
                    placeholder.name = spec.exported_name;
                    placeholder.type = DataType::UNKNOWN;  // Will be determined later
                    module.exports[spec.exported_name] = placeholder;
                }
            }
        }
    }
    
    // Create synthetic default export if no default but has named exports
    if (!module.has_default_export && has_named_exports) {
        create_synthetic_default_export(module);
    }
}

void GoTSCompiler::compile_file(const std::string& file_path) {
    // Read and compile a file directly
    std::ifstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + file_path);
    }
    
    std::string source((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
    file.close();
    
    compile(source);
}

// Function management methods
void GoTSCompiler::register_function(const std::string& name, const Function& func) {
    functions[name] = func;
}

Function* GoTSCompiler::get_function(const std::string& name) {
    auto it = functions.find(name);
    if (it != functions.end()) {
        return &it->second;
    }
    return nullptr;
}

bool GoTSCompiler::is_function_defined(const std::string& name) const {
    return functions.find(name) != functions.end();
}

void GoTSCompiler::register_operator_overload(const std::string& class_name, const OperatorOverload& overload) {
    auto it = classes.find(class_name);
    if (it == classes.end()) {
        throw std::runtime_error("Cannot register operator overload for undefined class: " + class_name);
    }
    
    it->second.operator_overloads[overload.operator_type].push_back(overload);
}

const std::vector<OperatorOverload>* GoTSCompiler::get_operator_overloads(const std::string& class_name, TokenType operator_type) {
    auto class_it = classes.find(class_name);
    if (class_it == classes.end()) {
        return nullptr;
    }
    
    auto op_it = class_it->second.operator_overloads.find(operator_type);
    if (op_it == class_it->second.operator_overloads.end()) {
        return nullptr;
    }
    
    return &op_it->second;
}

bool GoTSCompiler::has_operator_overload(const std::string& class_name, TokenType operator_type) {
    auto class_it = classes.find(class_name);
    if (class_it == classes.end()) {
        return false;
    }
    
    return class_it->second.operator_overloads.find(operator_type) != 
           class_it->second.operator_overloads.end();
}

const OperatorOverload* GoTSCompiler::find_best_operator_overload(const std::string& class_name, TokenType operator_type, 
                                                                  const std::vector<DataType>& arg_types) {
    const auto* overloads = get_operator_overloads(class_name, operator_type);
    if (!overloads) {
        return nullptr;
    }
    
    // Find the best matching overload
    const OperatorOverload* best_match = nullptr;
    int best_score = -1;
    
    for (const auto& overload : *overloads) {
        if (overload.parameters.size() != arg_types.size()) {
            continue;
        }
        
        int score = 0;
        bool match = true;
        
        for (size_t i = 0; i < arg_types.size(); ++i) {
            DataType param_type = overload.parameters[i].type;
            DataType arg_type = arg_types[i];
            
            if (param_type == DataType::UNKNOWN) {
                // Untyped parameter matches anything
                score += 1;
            } else if (param_type == arg_type) {
                // Exact match
                score += 10;
            } else if (type_system.get_cast_type(arg_type, param_type) == param_type) {
                // Can be cast to parameter type
                score += 5;
            } else {
                match = false;
                break;
            }
        }
        
        if (match && score > best_score) {
            best_score = score;
            best_match = &overload;
        }
    }
    
    return best_match;
}

// Global compiler context for AST generation
static GoTSCompiler* current_compiler = nullptr;

void set_current_compiler(GoTSCompiler* compiler) {
    current_compiler = compiler;
}

GoTSCompiler* get_current_compiler() {
    return current_compiler;
}

// Function to compile all deferred function expressions
void compile_deferred_function_expressions(CodeGenerator& gen, TypeInference& types) {
    // Stub implementation - in a full implementation this would compile
    // any function expressions that were deferred during the initial pass
    (void)gen;
    (void)types;
}

}