#include "compiler.h"
#include "tensor.h"
#include "promise.h"
#include <iostream>
#include <string>

using namespace gots;

void test_tensor_operations() {
    std::cout << "\n=== Testing Tensor Operations ===" << std::endl;
    
    tensor a = {1, 2, 3, 4, 5};
    std::cout << "Created 1D tensor with " << a.size() << " elements" << std::endl;
    
    a.push(6);
    std::cout << "After push: size = " << a.size() << std::endl;
    
    auto b = tensor::zeros({2, 3});
    std::cout << "Created 2D tensor with shape [" << b.shape()[0] << ", " << b.shape()[1] << "]" << std::endl;
    
    auto c = tensor::ones({3, 2});
    auto d = b.transpose().matmul(c);
    std::cout << "Matrix multiplication result shape: [" << d.shape()[0] << ", " << d.shape()[1] << "]" << std::endl;
}

void test_compiler() {
    std::cout << "\n=== Testing GoTS Compiler ===" << std::endl;
    
    std::string simple_program = R"(
        function doSomething(x: int64) {
            return x + 42
        }
        
        let result = doSomething(100)
        go doSomething(200)
        await go doSomething(300)
    )";
    
    std::cout << "Compiling for x86-64:" << std::endl;
    GoTSCompiler compiler_x86(Backend::X86_64);
    compiler_x86.compile(simple_program);
    
    std::cout << "\nCompiling for WebAssembly:" << std::endl;
    GoTSCompiler compiler_wasm(Backend::WASM);
    compiler_wasm.compile(simple_program);
}

void test_promises() {
    std::cout << "\n=== Testing Promise System ===" << std::endl;
    
    auto& scheduler = GoroutineScheduler::instance();
    
    auto promise1 = scheduler.spawn([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return 42;
    });
    
    auto promise2 = scheduler.spawn([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        return 84;
    });
    
    std::vector<std::shared_ptr<Promise>> promises = {promise1, promise2};
    auto all_future = promise_all(promises);
    
    std::cout << "Waiting for promises to resolve..." << std::endl;
    auto results = all_future.get();
    std::cout << "Promise.all results: ";
    for (const auto& result : results) {
        std::cout << result << " ";
    }
    std::cout << std::endl;
    
    std::vector<int> numbers = {1, 2, 3, 4, 5};
    auto map_future = goMap(numbers, [](int x) {
        return x * x;
    });
    
    auto squared = map_future.get();
    std::cout << "goMap results: ";
    for (const auto& result : squared) {
        std::cout << result << " ";
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "GoTS Compiler Demo" << std::endl;
    std::cout << "==================" << std::endl;
    
    try {
        test_tensor_operations();
        test_compiler();
        test_promises();
        
        std::cout << "\n=== Full Integration Test ===" << std::endl;
        
        std::string gots_program = R"(
            function fibonacci(n: int64) {
                if n <= 1
                    return n
                return fibonacci(n - 1) + fibonacci(n - 2)
            }
            
            function main() {
                let numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]
                let results = await Promise.all(numbers.goMap(fibonacci))
                return results
            }
            
            main()
        )";
        
        GoTSCompiler compiler(Backend::X86_64);
        compiler.compile(gots_program);
        
        std::cout << "GoTS program compiled successfully!" << std::endl;
        std::cout << "Generated machine code size: " << compiler.get_machine_code().size() << " bytes" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "\nDemo completed successfully!" << std::endl;
    return 0;
}