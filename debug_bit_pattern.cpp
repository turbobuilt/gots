#include <iostream>
#include <cmath>

int main() {
    union { double d; int64_t i; } converter;
    
    converter.d = 3.14159;
    std::cout << "3.14159 as bits: " << converter.i << std::endl;
    
    converter.i = 4614256650576692846;
    std::cout << "Bit pattern 4614256650576692846 as double: " << converter.d << std::endl;
    
    return 0;
}