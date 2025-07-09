#include "unified_event_system.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    std::cout << "=== UNIFIED EVENT SYSTEM DEMO ===" << std::endl;
    
    // Initialize unified system
    gots::initialize_unified_event_system();
    
    // Create lexical environment
    auto root_env = std::make_shared<gots::LexicalEnvironment>();
    
    // Create a goroutine
    auto goroutine = std::make_shared<gots::Goroutine>(1, root_env);
    
    // Set a simple task
    goroutine->set_main_task([]() {
        std::cout << "Goroutine task executed!" << std::endl;
    });
    
    // Create a timer
    auto timer_id = gots::GlobalTimerSystem::instance().set_timeout(
        1, 
        []() {
            std::cout << "Timer fired!" << std::endl;
        }, 
        100
    );
    
    std::cout << "Set timer " << timer_id << " for 100ms" << std::endl;
    
    // Register with main controller
    gots::MainThreadController::instance().goroutine_started(1, goroutine);
    
    // Run goroutine
    std::thread([goroutine]() {
        goroutine->run();
    }).detach();
    
    // Wait briefly for timer
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Print stats
    std::cout << "\n=== FINAL STATS ===" << std::endl;
    std::cout << "Active goroutines: " << gots::MainThreadController::instance().get_active_goroutines() << std::endl;
    std::cout << "Pending timers: " << gots::MainThreadController::instance().get_pending_timers() << std::endl;
    std::cout << "Timer queue size: " << gots::GlobalTimerSystem::instance().get_pending_count() << std::endl;
    
    // Shutdown
    gots::shutdown_unified_event_system();
    
    std::cout << "=== DEMO COMPLETE ===" << std::endl;
    return 0;
}