#include <iostream>
#include <cstdint>

extern "C" void* get_return_address() {
    void* ret_addr;
    asm volatile("mov 8(%%rbp), %0" : "=r"(ret_addr));
    return ret_addr;
}

void test_function() {
    void* ret_addr = get_return_address();
    std::cout << "Return address in test_function: " << ret_addr << std::endl;
}

int main() {
    std::cout << "main function at: " << (void*)main << std::endl;
    std::cout << "test_function at: " << (void*)test_function << std::endl;
    test_function();
    return 0;
}