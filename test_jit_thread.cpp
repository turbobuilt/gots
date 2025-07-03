#include <iostream>
#include <thread>
#include <sys/mman.h>
#include <cstring>
#include <unistd.h>

// Simple JIT function that returns 5
uint8_t jit_code[] = {
    0x48, 0xc7, 0xc0, 0x05, 0x00, 0x00, 0x00,  // mov rax, 5
    0xc3                                         // ret
};

void test_jit_in_thread(void* func_ptr) {
    std::cout << "Thread: About to call JIT function..." << std::endl;
    
    typedef int64_t (*FuncType)();
    FuncType func = reinterpret_cast<FuncType>(func_ptr);
    
    int64_t result = func();
    std::cout << "Thread: JIT function returned: " << result << std::endl;
}

int main() {
    std::cout << "=== Testing JIT code execution in threads ===" << std::endl;
    
    // Allocate executable memory
    size_t code_size = sizeof(jit_code);
    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t aligned_size = (code_size + page_size - 1) & ~(page_size - 1);
    
    void* exec_mem = mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    
    if (exec_mem == MAP_FAILED) {
        std::cerr << "Failed to allocate memory" << std::endl;
        return 1;
    }
    
    // Copy JIT code
    memcpy(exec_mem, jit_code, code_size);
    
    // Make executable
    if (mprotect(exec_mem, aligned_size, PROT_READ | PROT_EXEC) != 0) {
        std::cerr << "Failed to make memory executable" << std::endl;
        munmap(exec_mem, aligned_size);
        return 1;
    }
    
    // Test 1: Call from main thread
    std::cout << "\n--- Test 1: Main thread ---" << std::endl;
    typedef int64_t (*FuncType)();
    FuncType func = reinterpret_cast<FuncType>(exec_mem);
    int64_t result = func();
    std::cout << "Main: JIT function returned: " << result << std::endl;
    
    // Test 2: Call from worker thread
    std::cout << "\n--- Test 2: Worker thread ---" << std::endl;
    std::thread worker(test_jit_in_thread, exec_mem);
    worker.join();
    
    // Cleanup
    munmap(exec_mem, aligned_size);
    
    std::cout << "\n✅ All tests passed!" << std::endl;
    return 0;
}