#include "goroutine_event_system.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

using namespace gots;

// ============================================================================
// TEST 1: Basic Timer Functionality
// ============================================================================

void test_basic_timers() {
    std::cout << "\n=== TEST 1: Basic Timer Functionality ===" << std::endl;
    
    MainProgramController::instance().run_main_goroutine([]() {
        std::cout << "Main goroutine started - testing timers" << std::endl;
        
        // Test setTimeout
        setTimeout([]() {
            std::cout << "✅ setTimeout fired after 100ms" << std::endl;
        }, 100);
        
        // Test setInterval
        auto count_ptr = std::make_shared<int>(0);
        auto interval_id_ptr = std::make_shared<uint64_t>(0);
        
        *interval_id_ptr = setInterval([count_ptr, interval_id_ptr]() {
            (*count_ptr)++;
            std::cout << "✅ setInterval fired #" << *count_ptr << std::endl;
            if (*count_ptr >= 3) {
                std::cout << "✅ Clearing interval after 3 executions" << std::endl;
                clearInterval(*interval_id_ptr);
            }
        }, 200);
        
        // Test timer cancellation
        auto timeout_id = setTimeout([]() {
            std::cout << "❌ This timer should have been cancelled!" << std::endl;
        }, 50);
        
        clearTimeout(timeout_id);
        std::cout << "✅ Cancelled timer " << timeout_id << std::endl;
        
        // Wait for timers to complete
        setTimeout([]() {
            std::cout << "✅ Final timer - test complete" << std::endl;
        }, 1000);
    });
    
    MainProgramController::instance().wait_for_completion();
    std::cout << "✅ Test 1 completed" << std::endl;
}

// ============================================================================
// TEST 2: Parent-Child Goroutine Coordination
// ============================================================================

void test_parent_child_coordination() {
    std::cout << "\n=== TEST 2: Parent-Child Goroutine Coordination ===" << std::endl;
    
    MainProgramController::instance().run_main_goroutine([]() {
        std::cout << "Main goroutine - spawning children" << std::endl;
        
        // Spawn child goroutine 1
        spawn_goroutine([]() {
            std::cout << "Child 1 started" << std::endl;
            
            setTimeout([]() {
                std::cout << "✅ Child 1 timer fired" << std::endl;
            }, 300);
            
            // Spawn grandchild
            spawn_goroutine([]() {
                std::cout << "Grandchild started" << std::endl;
                setTimeout([]() {
                    std::cout << "✅ Grandchild timer fired" << std::endl;
                }, 400);
                std::cout << "Grandchild main task completed" << std::endl;
            });
            
            std::cout << "Child 1 main task completed" << std::endl;
        });
        
        // Spawn child goroutine 2
        spawn_goroutine([]() {
            std::cout << "Child 2 started" << std::endl;
            
            setTimeout([]() {
                std::cout << "✅ Child 2 timer fired" << std::endl;
            }, 200);
            
            std::cout << "Child 2 main task completed" << std::endl;
        });
        
        std::cout << "Main goroutine main task completed - waiting for children" << std::endl;
    });
    
    MainProgramController::instance().wait_for_completion();
    std::cout << "✅ Test 2 completed" << std::endl;
}

// ============================================================================
// TEST 3: Server Functionality
// ============================================================================

void test_server_functionality() {
    std::cout << "\n=== TEST 3: Server Functionality ===" << std::endl;
    
    MainProgramController::instance().run_main_goroutine([]() {
        std::cout << "Main goroutine - starting server test" << std::endl;
        
        // Start a simple echo server
        auto server_id = createServer(8080, [](int client_fd) {
            std::cout << "✅ Server handler called for client " << client_fd << std::endl;
            
            // Simple echo - read and write back
            char buffer[1024];
            ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                std::cout << "Server received: " << buffer << std::endl;
                (void)write(client_fd, buffer, bytes_read);
            }
            close(client_fd);
        });
        
        std::cout << "✅ Server started with ID " << server_id << std::endl;
        
        // Simulate client connections after a delay
        setTimeout([server_id]() {
            std::cout << "Simulating client connections..." << std::endl;
            
            // Create a few client connections
            for (int i = 0; i < 3; i++) {
                std::thread([i]() {
                    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
                    if (client_fd >= 0) {
                        struct sockaddr_in addr;
                        addr.sin_family = AF_INET;
                        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                        addr.sin_port = htons(8080);
                        
                        if (connect(client_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                            std::string message = "Hello from client " + std::to_string(i);
                            (void)write(client_fd, message.c_str(), message.length());
                            
                            char buffer[1024];
                            ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
                            if (bytes_read > 0) {
                                buffer[bytes_read] = '\0';
                                std::cout << "✅ Client " << i << " received echo: " << buffer << std::endl;
                            }
                        }
                        close(client_fd);
                    }
                }).detach();
                
                // Small delay between connections
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            
            // Stop server after processing connections
            setTimeout([server_id]() {
                std::cout << "Stopping server..." << std::endl;
                closeServer(server_id);
                std::cout << "✅ Server stopped" << std::endl;
            }, 500);
            
        }, 500);
        
        std::cout << "Main goroutine main task completed" << std::endl;
    });
    
    MainProgramController::instance().wait_for_completion();
    std::cout << "✅ Test 3 completed" << std::endl;
}

// ============================================================================
// TEST 4: Early Timer Wake-up
// ============================================================================

void test_early_timer_wakeup() {
    std::cout << "\n=== TEST 4: Early Timer Wake-up ===" << std::endl;
    
    MainProgramController::instance().run_main_goroutine([]() {
        std::cout << "Testing early timer wake-up mechanism" << std::endl;
        
        // Set a long timer first
        setTimeout([]() {
            std::cout << "✅ Long timer (1000ms) fired" << std::endl;
        }, 1000);
        
        // After a short delay, set a shorter timer that should wake up the event loop early
        setTimeout([]() {
            std::cout << "Setting early timer that should wake up event loop..." << std::endl;
            
            setTimeout([]() {
                std::cout << "✅ Early timer (100ms) fired - event loop woke up early!" << std::endl;
            }, 100);
            
        }, 200);
        
        std::cout << "Main task completed - timers should fire in order: early, then long" << std::endl;
    });
    
    MainProgramController::instance().wait_for_completion();
    std::cout << "✅ Test 4 completed" << std::endl;
}

// ============================================================================
// TEST 5: Complex Nested Scenario
// ============================================================================

void test_complex_nested_scenario() {
    std::cout << "\n=== TEST 5: Complex Nested Scenario ===" << std::endl;
    
    MainProgramController::instance().run_main_goroutine([]() {
        std::cout << "Complex scenario: goroutines + timers + async events" << std::endl;
        
        // Spawn a goroutine that starts a server
        spawn_goroutine([]() {
            std::cout << "Server goroutine started" << std::endl;
            
            auto server_id = createServer(8081, [](int client_fd) {
                std::cout << "Server handling client in nested goroutine" << std::endl;
                
                // Spawn another goroutine to handle the client
                spawn_goroutine([client_fd]() {
                    std::cout << "Client handler goroutine started" << std::endl;
                    
                    // Set a timer in the handler
                    setTimeout([client_fd]() {
                        const char* response = "Hello from nested handler!";
                        (void)write(client_fd, response, strlen(response));
                        close(client_fd);
                        std::cout << "✅ Nested handler completed" << std::endl;
                    }, 100);
                    
                    std::cout << "Client handler main task completed" << std::endl;
                });
            });
            
            // Set interval to create periodic events
            auto tick_ptr = std::make_shared<int>(0);
            auto interval_id_ptr = std::make_shared<uint64_t>(0);
            
            *interval_id_ptr = setInterval([tick_ptr, server_id, interval_id_ptr]() {
                (*tick_ptr)++;
                std::cout << "Server goroutine tick #" << *tick_ptr << std::endl;
                
                if (*tick_ptr >= 5) {
                    clearInterval(*interval_id_ptr);
                    closeServer(server_id);
                    std::cout << "✅ Server goroutine shutting down" << std::endl;
                }
            }, 300);
            
            std::cout << "Server goroutine main task completed" << std::endl;
        });
        
        // Create client connections
        setTimeout([]() {
            for (int i = 0; i < 2; i++) {
                std::thread([i]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(i * 200));
                    
                    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
                    if (client_fd >= 0) {
                        struct sockaddr_in addr;
                        addr.sin_family = AF_INET;
                        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                        addr.sin_port = htons(8081);
                        
                        if (connect(client_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                            char buffer[1024];
                            ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
                            if (bytes_read > 0) {
                                buffer[bytes_read] = '\0';
                                std::cout << "✅ Complex client " << i << " received: " << buffer << std::endl;
                            }
                        } else {
                            close(client_fd);
                        }
                    }
                }).detach();
            }
        }, 500);
        
        std::cout << "Main goroutine main task completed" << std::endl;
    });
    
    MainProgramController::instance().wait_for_completion();
    std::cout << "✅ Test 5 completed" << std::endl;
}

// ============================================================================
// MAIN TEST RUNNER
// ============================================================================

int main() {
    std::cout << "Starting Goroutine Event System Tests" << std::endl;
    initialize_goroutine_system();
    
    try {
        test_basic_timers();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        test_parent_child_coordination();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        test_server_functionality();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        test_early_timer_wakeup();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        test_complex_nested_scenario();
        
        std::cout << "\n🎉 ALL TESTS COMPLETED SUCCESSFULLY! 🎉" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    shutdown_goroutine_system();
    return 0;
}