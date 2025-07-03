#include "compiler.h"
#include "runtime.h"
#include "lexical_scope.h"
#include <iostream>

using namespace gots;

int main() {
    std::cout << "=== Testing real JIT memory in threads ===\n";
    
    try {
        // Compile a simple function first
        std::string program = R"(
function add(x: int64) {
    return x + 5;
}
)";
        
        GoTSCompiler compiler(Backend::X86_64);
        compiler.compile(program);
        
        // Execute to register functions and set up memory
        compiler.execute();
        
        std::cout << "JIT compilation and execution completed\n";
        
        // Get the function from the registry
        auto it = gots_function_registry.find("add");
        if (it == gots_function_registry.end()) {
            std::cout << "❌ Function 'add' not found in registry\n";
            return 1;
        }
        
        void* func_ptr = it->second;
        int64_t arg1 = 10;
        
        std::cout << "JIT Function pointer: " << func_ptr << ", arg: " << arg1 << std::endl;
        
        auto& scheduler = GoroutineScheduler::instance();
        
        // Test the exact same lambda pattern as in __goroutine_spawn_with_arg1
        auto lambda_with_jit = [func_ptr, arg1]() -> int64_t {
            std::cout << "Lambda executing with JIT func_ptr: " << func_ptr << ", arg: " << arg1 << std::endl;
            
            // Initialize scope chain for this thread
            ScopeChain::initialize_thread_local_chain();
            
            typedef int64_t (*FuncType1)(int64_t);
            FuncType1 func = reinterpret_cast<FuncType1>(func_ptr);
            
            std::cout << "About to call JIT function...\n";
            auto result = func(arg1);
            std::cout << "JIT function returned: " << result << std::endl;
            
            // Cleanup scope chain
            ScopeChain::cleanup_thread_local_chain();
            
            return result;
        };
        
        std::cout << "About to spawn lambda with JIT function pointer\n";
        auto promise = scheduler.spawn(lambda_with_jit);
        std::cout << "JIT lambda spawned\n";
        
        auto result = promise->await<int64_t>();
        std::cout << "JIT lambda result: " << result << std::endl;
        
        std::cout << "✅ Real JIT threading test passed!\n";
        
    } catch (const std::exception& e) {
        std::cout << "❌ Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cout << "❌ Unknown exception\n";
        return 1;
    }
    
    return 0;
}