#include "compiler.h"
#include "runtime.h"
#include <iostream>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>

using namespace gots;

int main() {
    std::cout << "=== Testing JIT function call from C++ ===" << std::endl;
    
    try {
        // Compile a simple function
        std::string program = R"(
function add(x: int64) {
    return x + 10;
}
)";
        
        GoTSCompiler compiler(Backend::X86_64);
        compiler.compile(program);
        
        auto machine_code = compiler.get_machine_code();
        
        if (machine_code.empty()) {
            std::cerr << "No machine code generated" << std::endl;
            return 1;
        }
        
        size_t code_size = machine_code.size();
        size_t page_size = sysconf(_SC_PAGESIZE);
        size_t aligned_size = (code_size + page_size - 1) & ~(page_size - 1);
        
        void* exec_mem = mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        
        if (exec_mem == MAP_FAILED) {
            std::cerr << "Failed to allocate memory" << std::endl;
            return 1;
        }
        
        memcpy(exec_mem, machine_code.data(), code_size);
        
        if (mprotect(exec_mem, aligned_size, PROT_READ | PROT_EXEC) != 0) {
            std::cerr << "Failed to make memory executable" << std::endl;
            munmap(exec_mem, aligned_size);
            return 1;
        }
        
        __set_executable_memory(exec_mem, aligned_size);
        __runtime_init();
        
        // For this test, let's assume the add function is at offset 0
        // This is a simplified test
        std::cout << "Assuming add function is at start of memory..." << std::endl;
        
        typedef int64_t (*AddFunc)(int64_t);
        AddFunc add_func = reinterpret_cast<AddFunc>(exec_mem);
        
        std::cout << "About to call add(5)..." << std::endl;
        
        int64_t result = add_func(5);
        std::cout << "add(5) = " << result << std::endl;
        
        // Cleanup
        munmap(exec_mem, aligned_size);
        
        std::cout << "\n✅ Test completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}