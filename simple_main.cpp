#include "compiler.h"
#include "runtime.h"
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <filesystem>
#include <thread>
#include <chrono>
#include <signal.h>
#include <atomic>

using namespace gots;

std::atomic<bool> should_restart(false);
std::atomic<bool> watch_mode(false);

void signal_handler(int signal) {
    if (signal == SIGINT) {
        if (watch_mode.load()) {
            std::cout << "\nShutting down watch mode..." << std::endl;
            exit(0);
        }
    }
}

std::string read_file(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file: " + filename);
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

class FileWatcher {
private:
    std::unordered_set<std::string> watched_files;
    std::unordered_map<std::string, std::filesystem::file_time_type> file_times;
    std::chrono::steady_clock::time_point last_change_time;
    static constexpr auto DEBOUNCE_TIME = std::chrono::milliseconds(250);
    
public:
    void add_file(const std::string& filepath) {
        if (std::filesystem::exists(filepath)) {
            watched_files.insert(filepath);
            file_times[filepath] = std::filesystem::last_write_time(filepath);
        }
    }
    
    void collect_imported_files(GoTSCompiler& compiler, const std::string& main_file) {
        // Add the main file
        add_file(main_file);
        
        // We need to analyze the compiled program to find all imported files
        // This is a simplified approach - in a real implementation you'd want
        // to traverse the module dependency graph
        try {
            std::string program = read_file(main_file);
            
            // Parse imports from the source code
            std::istringstream iss(program);
            std::string line;
            
            while (std::getline(iss, line)) {
                // Look for import statements
                if (line.find("import") != std::string::npos && line.find("from") != std::string::npos) {
                    // Extract the module path
                    auto from_pos = line.find("from");
                    if (from_pos != std::string::npos) {
                        auto quote_start = line.find("\"", from_pos);
                        if (quote_start == std::string::npos) {
                            quote_start = line.find("'", from_pos);
                        }
                        if (quote_start != std::string::npos) {
                            auto quote_end = line.find_first_of("\"'", quote_start + 1);
                            if (quote_end != std::string::npos) {
                                std::string import_path = line.substr(quote_start + 1, quote_end - quote_start - 1);
                                
                                // Resolve the path relative to the main file
                                std::string resolved_path = compiler.resolve_module_path(import_path, main_file);
                                add_file(resolved_path);
                                
                                // Recursively collect imports from the imported file
                                if (std::filesystem::exists(resolved_path)) {
                                    collect_imported_files_recursive(compiler, resolved_path);
                                }
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: Could not analyze imports: " << e.what() << std::endl;
        }
    }
    
    void collect_imported_files_recursive(GoTSCompiler& compiler, const std::string& file_path) {
        if (watched_files.find(file_path) != watched_files.end()) {
            return; // Already processed
        }
        
        add_file(file_path);
        
        try {
            std::string program = read_file(file_path);
            std::istringstream iss(program);
            std::string line;
            
            while (std::getline(iss, line)) {
                if (line.find("import") != std::string::npos && line.find("from") != std::string::npos) {
                    auto from_pos = line.find("from");
                    if (from_pos != std::string::npos) {
                        auto quote_start = line.find("\"", from_pos);
                        if (quote_start == std::string::npos) {
                            quote_start = line.find("'", from_pos);
                        }
                        if (quote_start != std::string::npos) {
                            auto quote_end = line.find_first_of("\"'", quote_start + 1);
                            if (quote_end != std::string::npos) {
                                std::string import_path = line.substr(quote_start + 1, quote_end - quote_start - 1);
                                std::string resolved_path = compiler.resolve_module_path(import_path, file_path);
                                
                                if (std::filesystem::exists(resolved_path)) {
                                    collect_imported_files_recursive(compiler, resolved_path);
                                }
                            }
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            // Ignore errors in recursive collection
        }
    }
    
    bool check_for_changes() {
        bool changed = false;
        auto now = std::chrono::steady_clock::now();
        
        for (const auto& file : watched_files) {
            if (std::filesystem::exists(file)) {
                auto current_time = std::filesystem::last_write_time(file);
                if (file_times[file] != current_time) {
                    file_times[file] = current_time;
                    last_change_time = now;
                    changed = true;
                }
            }
        }
        
        // Apply debounce - only return true if enough time has passed since last change
        if (changed && (now - last_change_time) >= DEBOUNCE_TIME) {
            return true;
        }
        
        return false;
    }
    
    void print_watched_files() {
        std::cout << "Watching files:" << std::endl;
        for (const auto& file : watched_files) {
            std::cout << "  " << file << std::endl;
        }
    }
};

void run_program(const std::string& filename) {
    try {
        // Initialize timer manager but defer scheduler connection until after main execution
        auto& scheduler = GoroutineScheduler::instance();
        // Don't connect scheduler during main execution to prevent conflicts
        // gots::g_main_timer_manager->set_scheduler(&scheduler);
        
        // Read and execute program normally on main thread
        std::string program = read_file(filename);
        
        GoTSCompiler compiler(Backend::X86_64);
        compiler.set_current_file(filename);
        compiler.compile(program);
        compiler.execute();
        
        // After main execution, wait for any pending timers
        std::cout << "DEBUG: Main execution completed, checking for active timers..." << std::endl;
        std::cout << "DEBUG: Timer manager pointer: " << gots::g_main_timer_manager << std::endl;
        
        if (gots::g_main_timer_manager) {
            bool has_timers = gots::g_main_timer_manager->has_active_timers();
            std::cout << "DEBUG: has_active_timers() = " << has_timers << std::endl;
            
            if (has_timers) {
                std::cout << "DEBUG: Found active timers, connecting scheduler and starting timer event loop..." << std::endl;
                
                // Now it's safe to connect the scheduler for timer execution
                gots::g_main_timer_manager->set_scheduler(&scheduler);
                
                // Start timer event loop in a separate thread
                std::thread timer_thread([]() {
                    std::cout << "DEBUG: Timer thread started" << std::endl;
                    if (gots::g_main_timer_manager) {
                        std::cout << "DEBUG: About to call run_main_event_loop()" << std::endl;
                        gots::g_main_timer_manager->run_main_event_loop();
                        std::cout << "DEBUG: run_main_event_loop() completed" << std::endl;
                    } else {
                        std::cout << "DEBUG: Timer manager is null in timer thread" << std::endl;
                    }
                });
                
                std::cout << "DEBUG: Timer thread launched, waiting for timers..." << std::endl;
                
                // Wait for all timers to complete
                int wait_count = 0;
                while (gots::g_main_timer_manager && gots::g_main_timer_manager->has_active_timers()) {
                    wait_count++;
                    std::cout << "DEBUG: Waiting for timers (iteration " << wait_count << ")..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (wait_count > 50) {  // 5 second timeout
                        std::cout << "DEBUG: Timer wait timeout, forcing shutdown" << std::endl;
                        break;
                    }
                }
                
                std::cout << "DEBUG: Timer wait completed, shutting down..." << std::endl;
                
                // Shutdown and wait for timer thread
                if (gots::g_main_timer_manager) {
                    std::cout << "DEBUG: Calling timer manager shutdown..." << std::endl;
                    gots::g_main_timer_manager->shutdown();
                    std::cout << "DEBUG: Timer manager shutdown completed" << std::endl;
                }
                
                std::cout << "DEBUG: Joining timer thread..." << std::endl;
                timer_thread.join();
                std::cout << "DEBUG: Timer thread joined successfully" << std::endl;
            } else {
                std::cout << "DEBUG: No active timers found" << std::endl;
            }
        } else {
            std::cout << "DEBUG: Timer manager is null" << std::endl;
        }
        
    } catch (const std::exception& e) {
        // Cleanup on error
        if (gots::g_main_timer_manager) {
            gots::g_main_timer_manager->shutdown();
        }
        std::cerr << "Error: " << e.what() << std::endl;
        throw;
    }
}

int main(int argc, char* argv[]) {
    // Initialize timer manager at program start
    gots::g_main_timer_manager = new gots::MainThreadTimerManager();
    
    bool watch_flag = false;
    std::string filename;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-w" || arg == "--watch") {
            watch_flag = true;
        } else if (arg.find("-") != 0) {
            // This is the filename (not a flag)
            filename = arg;
        }
    }
    
    if (filename.empty()) {
        std::cerr << "Usage: " << argv[0] << " [-w|--watch] <file.gts>" << std::endl;
        std::cerr << "  -w, --watch    Watch for file changes and restart automatically" << std::endl;
        return 1;
    }
    
    // Set up signal handler for clean exit
    signal(SIGINT, signal_handler);
    
    if (watch_flag) {
        watch_mode.store(true);
        std::cout << "Starting GoTS in watch mode..." << std::endl;
        std::cout << "Press Ctrl+C to stop watching" << std::endl;
        
        FileWatcher watcher;
        
        // Main watch loop
        while (true) {
            try {
                std::cout << "\n--- Running " << filename << " ---" << std::endl;
                
                // Set up the compiler to collect import information
                GoTSCompiler compiler(Backend::X86_64);
                compiler.set_current_file(filename);
                
                // Collect all files that should be watched
                watcher.collect_imported_files(compiler, filename);
                watcher.print_watched_files();
                
                // Run the program
                run_program(filename);
                
                std::cout << "\n--- Execution complete. Watching for changes... ---" << std::endl;
                
                // Watch for file changes
                while (true) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    
                    if (watcher.check_for_changes()) {
                        std::cout << "\n🔄 File change detected! Restarting..." << std::endl;
                        std::this_thread::sleep_for(std::chrono::milliseconds(250)); // Final debounce
                        break;
                    }
                }
                
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                std::cerr << "Watching for changes to retry..." << std::endl;
                
                // Continue watching even if there's an error
                FileWatcher error_watcher;
                GoTSCompiler error_compiler(Backend::X86_64);
                error_compiler.set_current_file(filename);
                error_watcher.collect_imported_files(error_compiler, filename);
                
                while (true) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    
                    if (error_watcher.check_for_changes()) {
                        std::cout << "\n🔄 File change detected! Retrying..." << std::endl;
                        std::this_thread::sleep_for(std::chrono::milliseconds(250));
                        break;
                    }
                }
            }
        }
    } else {
        // Normal mode - run once
        try {
            run_program(filename);
        } catch (const std::exception& e) {
            return 1;
        }
    }
    
    // Cleanup global timer manager
    if (gots::g_main_timer_manager) {
        delete gots::g_main_timer_manager;
        gots::g_main_timer_manager = nullptr;
    }
    
    return 0;
}