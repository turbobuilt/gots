#include <iostream>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

int main() {
    // Minimal JIT function that just returns 42
    unsigned char code[] = {
        0x55,                   // push rbp
        0x48, 0x89, 0xe5,      // mov rbp, rsp
        0x48, 0xc7, 0xc0, 0x2a, 0x00, 0x00, 0x00,  // mov rax, 42
        0x5d,                   // pop rbp
        0xc3                    // ret
    };
    
    size_t code_size = sizeof(code);
    size_t page_size = sysconf(_SC_PAGESIZE);
    size_t aligned_size = (code_size + page_size - 1) & ~(page_size - 1);
    
    void* exec_mem = mmap(nullptr, aligned_size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (exec_mem == MAP_FAILED) {
        std::cerr << "mmap failed" << std::endl;
        return 1;
    }
    
    memcpy(exec_mem, code, code_size);
    
    if (mprotect(exec_mem, aligned_size, PROT_READ | PROT_EXEC) != 0) {
        std::cerr << "mprotect failed" << std::endl;
        return 1;
    }
    
    auto func = reinterpret_cast<int(*)()>(exec_mem);
    std::cout << "Calling JIT function at " << exec_mem << std::endl;
    
    int result = func();
    std::cout << "Result: " << result << std::endl;
    
    munmap(exec_mem, aligned_size);
    return 0;
}