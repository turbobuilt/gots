#include "goroutine_aware_gc.h"
#include <algorithm>
#include <unordered_set>
#include <stack>
#include <iostream>
#include <thread>
#include <mutex>

namespace gots {

// ============================================================================
// THREAD-LOCAL ESCAPE ANALYSIS DATA
// ============================================================================

thread_local struct GoroutineEscapeData {
    uint32_t current_goroutine_id = 0;
    std::unordered_map<size_t, GoroutineEscapeAnalyzer::GoroutineAnalysisResult> allocation_results;
    std::unordered_map<size_t, std::vector<uint32_t>> var_goroutine_access;
    std::unordered_map<uint32_t, std::vector<size_t>> goroutine_captured_vars;
    std::unordered_map<size_t, std::vector<size_t>> var_allocation_sites;
    std::unordered_map<size_t, size_t> var_scope_map;
    std::unordered_map<size_t, uint32_t> allocation_site_goroutines;
    std::stack<size_t> scope_stack;
    
    // Cross-goroutine access tracking
    std::mutex cross_access_mutex;
    std::unordered_map<size_t, std::unordered_set<uint32_t>> cross_goroutine_reads;
    std::unordered_map<size_t, std::unordered_set<uint32_t>> cross_goroutine_writes;
    
    // Goroutine spawn tracking
    std::unordered_map<uint32_t, uint32_t> goroutine_parent_map;
    std::unordered_map<uint32_t, std::vector<uint32_t>> goroutine_children_map;
    
    void reset() {
        allocation_results.clear();
        var_goroutine_access.clear();
        goroutine_captured_vars.clear();
        var_allocation_sites.clear();
        var_scope_map.clear();
        allocation_site_goroutines.clear();
        cross_goroutine_reads.clear();
        cross_goroutine_writes.clear();
        goroutine_parent_map.clear();
        goroutine_children_map.clear();
        while (!scope_stack.empty()) scope_stack.pop();
    }
} g_escape_data;

// Global escape analysis coordinator
class EscapeAnalysisCoordinator {
private:
    std::mutex global_mutex_;
    std::unordered_map<uint32_t, std::unique_ptr<GoroutineEscapeData>> goroutine_data_;
    std::unordered_map<size_t, std::vector<uint32_t>> global_var_access_;
    std::unordered_map<size_t, ObjectOwnership> final_ownership_decisions_;
    
public:
    static EscapeAnalysisCoordinator& instance() {
        static EscapeAnalysisCoordinator coordinator;
        return coordinator;
    }
    
    void register_goroutine(uint32_t goroutine_id) {
        std::lock_guard<std::mutex> lock(global_mutex_);
        goroutine_data_[goroutine_id] = std::make_unique<GoroutineEscapeData>();
    }
    
    void unregister_goroutine(uint32_t goroutine_id) {
        std::lock_guard<std::mutex> lock(global_mutex_);
        goroutine_data_.erase(goroutine_id);
    }
    
    void register_global_var_access(size_t var_id, uint32_t goroutine_id) {
        std::lock_guard<std::mutex> lock(global_mutex_);
        global_var_access_[var_id].push_back(goroutine_id);
    }
    
    std::vector<uint32_t> get_var_accessing_goroutines(size_t var_id) {
        std::lock_guard<std::mutex> lock(global_mutex_);
        auto it = global_var_access_.find(var_id);
        if (it != global_var_access_.end()) {
            return it->second;
        }
        return {};
    }
    
    void set_final_ownership(size_t allocation_site, ObjectOwnership ownership) {
        std::lock_guard<std::mutex> lock(global_mutex_);
        final_ownership_decisions_[allocation_site] = ownership;
    }
    
    ObjectOwnership get_final_ownership(size_t allocation_site) {
        std::lock_guard<std::mutex> lock(global_mutex_);
        auto it = final_ownership_decisions_.find(allocation_site);
        if (it != final_ownership_decisions_.end()) {
            return it->second;
        }
        return ObjectOwnership::GOROUTINE_SHARED; // Conservative default
    }
};

// ============================================================================
// GOROUTINE ESCAPE ANALYZER IMPLEMENTATION
// ============================================================================

void GoroutineEscapeAnalyzer::register_goroutine_spawn(
    uint32_t parent_goroutine_id,
    uint32_t child_goroutine_id,
    const std::vector<size_t>& captured_vars
) {
    // Update parent-child relationships
    g_escape_data.goroutine_parent_map[child_goroutine_id] = parent_goroutine_id;
    g_escape_data.goroutine_children_map[parent_goroutine_id].push_back(child_goroutine_id);
    
    // Store captured variables for the child goroutine
    g_escape_data.goroutine_captured_vars[child_goroutine_id] = captured_vars;
    
    // Mark all captured variables as accessed by the child goroutine
    for (size_t var_id : captured_vars) {
        g_escape_data.var_goroutine_access[var_id].push_back(child_goroutine_id);
        
        // Mark all allocation sites for this variable as potentially shared
        if (g_escape_data.var_allocation_sites.find(var_id) != g_escape_data.var_allocation_sites.end()) {
            for (size_t site_id : g_escape_data.var_allocation_sites[var_id]) {
                if (g_escape_data.allocation_results.find(site_id) != g_escape_data.allocation_results.end()) {
                    auto& result = g_escape_data.allocation_results[site_id];
                    result.captured_by_goroutine = true;
                    result.accessing_goroutines.push_back(child_goroutine_id);
                    
                    // If multiple goroutines access it, it needs to be shared
                    if (result.accessing_goroutines.size() > 1) {
                        result.ownership = ObjectOwnership::GOROUTINE_SHARED;
                        result.needs_synchronization = true;
                    }
                }
            }
        }
    }
    
    // Register with global coordinator
    EscapeAnalysisCoordinator::instance().register_goroutine(child_goroutine_id);
    
    std::cout << "[ESCAPE] Goroutine " << child_goroutine_id << " spawned by " 
              << parent_goroutine_id << " with " << captured_vars.size() 
              << " captured variables\n";
}

void GoroutineEscapeAnalyzer::register_cross_goroutine_access(
    uint32_t accessing_goroutine_id,
    size_t var_id,
    size_t allocation_site,
    bool is_write
) {
    std::lock_guard<std::mutex> lock(g_escape_data.cross_access_mutex);
    
    // Track which goroutines access this variable
    if (is_write) {
        g_escape_data.cross_goroutine_writes[var_id].insert(accessing_goroutine_id);
    } else {
        g_escape_data.cross_goroutine_reads[var_id].insert(accessing_goroutine_id);
    }
    
    // Update allocation site analysis - THREAD SAFE ACCESS
    auto result_it = g_escape_data.allocation_results.find(allocation_site);
    if (result_it != g_escape_data.allocation_results.end()) {
        auto& result = result_it->second;
        result.accessed_across_goroutines = true;
        result.needs_synchronization = true;
        
        // Add to accessing goroutines if not already present
        if (std::find(result.accessing_goroutines.begin(), result.accessing_goroutines.end(), 
                     accessing_goroutine_id) == result.accessing_goroutines.end()) {
            result.accessing_goroutines.push_back(accessing_goroutine_id);
        }
        
        // Determine ownership based on access pattern
        if (result.accessing_goroutines.size() > 2) {
            result.ownership = ObjectOwnership::GLOBAL_SHARED;
        } else if (result.accessing_goroutines.size() > 1) {
            result.ownership = ObjectOwnership::GOROUTINE_SHARED;
        }
    }
    
    // Register with global coordinator
    EscapeAnalysisCoordinator::instance().register_global_var_access(var_id, accessing_goroutine_id);
    
    std::cout << "[ESCAPE] Cross-goroutine " << (is_write ? "write" : "read") 
              << " by goroutine " << accessing_goroutine_id << " to var " << var_id 
              << " at site " << allocation_site << "\n";
}

GoroutineEscapeAnalyzer::GoroutineAnalysisResult GoroutineEscapeAnalyzer::analyze_goroutine_allocation(
    const void* jit_context,
    size_t allocation_site,
    size_t allocation_size,
    uint32_t type_id,
    uint32_t current_goroutine_id
) {
    GoroutineAnalysisResult result;
    
    // Check if we already have analysis for this site
    auto existing_it = g_escape_data.allocation_results.find(allocation_site);
    if (existing_it != g_escape_data.allocation_results.end()) {
        return existing_it->second;
    }
    
    // Initialize with optimistic assumptions
    result.ownership = ObjectOwnership::STACK_LOCAL;
    result.accessing_goroutines.push_back(current_goroutine_id);
    result.needs_synchronization = false;
    
    // Store the creating goroutine
    g_escape_data.allocation_site_goroutines[allocation_site] = current_goroutine_id;
    
    // Check size constraints for stack allocation
    if (allocation_size > GCConfig::MAX_STACK_ALLOC_SIZE) {
        result.size_too_large = true;
        result.ownership = ObjectOwnership::GOROUTINE_PRIVATE;
        std::cout << "[ESCAPE] Site " << allocation_site << " too large for stack (" 
                  << allocation_size << " bytes)\n";
    }
    
    // Check for goroutine captures
    for (const auto& [goroutine_id, captured_vars] : g_escape_data.goroutine_captured_vars) {
        for (size_t var_id : captured_vars) {
            if (g_escape_data.var_allocation_sites.find(var_id) != g_escape_data.var_allocation_sites.end()) {
                for (size_t site_id : g_escape_data.var_allocation_sites[var_id]) {
                    if (site_id == allocation_site) {
                        result.captured_by_goroutine = true;
                        result.ownership = ObjectOwnership::GOROUTINE_SHARED;
                        result.needs_synchronization = true;
                        
                        if (std::find(result.accessing_goroutines.begin(), result.accessing_goroutines.end(), 
                                     goroutine_id) == result.accessing_goroutines.end()) {
                            result.accessing_goroutines.push_back(goroutine_id);
                        }
                        
                        std::cout << "[ESCAPE] Site " << allocation_site 
                                  << " captured by goroutine " << goroutine_id << "\n";
                    }
                }
            }
        }
    }
    
    // Check for cross-goroutine access
    {
        std::lock_guard<std::mutex> lock(g_escape_data.cross_access_mutex);
        for (const auto& [var_id, accessing_goroutines] : g_escape_data.cross_goroutine_reads) {
            if (g_escape_data.var_allocation_sites.find(var_id) != g_escape_data.var_allocation_sites.end()) {
                for (size_t site_id : g_escape_data.var_allocation_sites[var_id]) {
                    if (site_id == allocation_site && accessing_goroutines.size() > 1) {
                        result.accessed_across_goroutines = true;
                        result.needs_synchronization = true;
                        
                        if (accessing_goroutines.size() > 2) {
                            result.ownership = ObjectOwnership::GLOBAL_SHARED;
                        } else {
                            result.ownership = ObjectOwnership::GOROUTINE_SHARED;
                        }
                        
                        for (uint32_t gid : accessing_goroutines) {
                            if (std::find(result.accessing_goroutines.begin(), result.accessing_goroutines.end(), 
                                         gid) == result.accessing_goroutines.end()) {
                                result.accessing_goroutines.push_back(gid);
                            }
                        }
                        
                        std::cout << "[ESCAPE] Site " << allocation_site 
                                  << " accessed by " << accessing_goroutines.size() 
                                  << " goroutines\n";
                    }
                }
            }
        }
    }
    
    // Check for returns (forces heap allocation)
    // This would be set by register_return calls during compilation
    
    // Check for global storage (forces global shared)
    // This would be detected during compilation phase
    
    // Store the result
    g_escape_data.allocation_results[allocation_site] = result;
    
    // Register final decision with coordinator
    EscapeAnalysisCoordinator::instance().set_final_ownership(allocation_site, result.ownership);
    
    std::cout << "[ESCAPE] Final analysis for site " << allocation_site 
              << ": ownership=" << static_cast<int>(result.ownership)
              << ", goroutines=" << result.accessing_goroutines.size()
              << ", sync=" << result.needs_synchronization << "\n";
    
    return result;
}

bool GoroutineEscapeAnalyzer::is_captured_by_goroutine(size_t var_id) {
    for (const auto& [goroutine_id, captured_vars] : g_escape_data.goroutine_captured_vars) {
        if (std::find(captured_vars.begin(), captured_vars.end(), var_id) != captured_vars.end()) {
            return true;
        }
    }
    return false;
}

std::vector<uint32_t> GoroutineEscapeAnalyzer::get_accessing_goroutines(size_t var_id) {
    auto it = g_escape_data.var_goroutine_access.find(var_id);
    if (it != g_escape_data.var_goroutine_access.end()) {
        return it->second;
    }
    return {};
}

// ============================================================================
// ADDITIONAL ESCAPE ANALYSIS FUNCTIONS
// ============================================================================

void GoroutineEscapeAnalyzer::register_variable_definition(
    size_t var_id,
    size_t allocation_site,
    size_t scope_id,
    uint32_t goroutine_id
) {
    g_escape_data.var_allocation_sites[var_id].push_back(allocation_site);
    g_escape_data.var_scope_map[var_id] = scope_id;
    g_escape_data.var_goroutine_access[var_id].push_back(goroutine_id);
    
    std::cout << "[ESCAPE] Variable " << var_id << " defined at site " << allocation_site
              << " in scope " << scope_id << " by goroutine " << goroutine_id << "\n";
}

void GoroutineEscapeAnalyzer::register_variable_assignment(
    size_t from_var_id,
    size_t to_var_id,
    uint32_t goroutine_id
) {
    // Propagate allocation sites from source to destination - SAFE ACCESS
    auto from_sites_it = g_escape_data.var_allocation_sites.find(from_var_id);
    if (from_sites_it != g_escape_data.var_allocation_sites.end()) {
        auto& from_sites = from_sites_it->second;
        auto& to_sites = g_escape_data.var_allocation_sites[to_var_id];
        
        for (size_t site : from_sites) {
            if (std::find(to_sites.begin(), to_sites.end(), site) == to_sites.end()) {
                to_sites.push_back(site);
            }
            
            // Update allocation site analysis to reflect assignment - SAFE ACCESS
            auto result_it = g_escape_data.allocation_results.find(site);
            if (result_it != g_escape_data.allocation_results.end()) {
                auto& result = result_it->second;
                if (std::find(result.accessing_goroutines.begin(), result.accessing_goroutines.end(), 
                             goroutine_id) == result.accessing_goroutines.end()) {
                    result.accessing_goroutines.push_back(goroutine_id);
                }
            }
        }
    }
    
    // Propagate goroutine access - SAFE ACCESS  
    auto from_goroutines_it = g_escape_data.var_goroutine_access.find(from_var_id);
    if (from_goroutines_it != g_escape_data.var_goroutine_access.end()) {
        auto& from_goroutines = from_goroutines_it->second;
        auto& to_goroutines = g_escape_data.var_goroutine_access[to_var_id];
        
        for (uint32_t gid : from_goroutines) {
            if (std::find(to_goroutines.begin(), to_goroutines.end(), gid) == to_goroutines.end()) {
                to_goroutines.push_back(gid);
            }
        }
    }
    
    std::cout << "[ESCAPE] Assignment from var " << from_var_id << " to var " << to_var_id
              << " by goroutine " << goroutine_id << "\n";
}

void GoroutineEscapeAnalyzer::register_return_value(
    size_t var_id,
    uint32_t goroutine_id
) {
    // Mark all allocation sites for this variable as returned - SAFE ACCESS
    auto var_sites_it = g_escape_data.var_allocation_sites.find(var_id);
    if (var_sites_it != g_escape_data.var_allocation_sites.end()) {
        for (size_t site : var_sites_it->second) {
            auto result_it = g_escape_data.allocation_results.find(site);
            if (result_it != g_escape_data.allocation_results.end()) {
                auto& result = result_it->second;
                result.returned_from_goroutine = true;
                result.ownership = ObjectOwnership::GOROUTINE_SHARED; // At minimum
                result.needs_synchronization = true;
                
                std::cout << "[ESCAPE] Site " << site << " returned from goroutine " 
                          << goroutine_id << "\n";
            }
        }
    }
}

void GoroutineEscapeAnalyzer::register_global_store(
    size_t var_id,
    uint32_t goroutine_id
) {
    // Mark all allocation sites for this variable as globally stored - SAFE ACCESS
    auto var_sites_it = g_escape_data.var_allocation_sites.find(var_id);
    if (var_sites_it != g_escape_data.var_allocation_sites.end()) {
        for (size_t site : var_sites_it->second) {
            auto result_it = g_escape_data.allocation_results.find(site);
            if (result_it != g_escape_data.allocation_results.end()) {
                auto& result = result_it->second;
                result.stored_in_shared_object = true;
                result.ownership = ObjectOwnership::GLOBAL_SHARED;
                result.needs_synchronization = true;
                
                std::cout << "[ESCAPE] Site " << site << " stored globally by goroutine " 
                          << goroutine_id << "\n";
            }
        }
    }
}

void GoroutineEscapeAnalyzer::register_channel_send(
    size_t var_id,
    uint32_t goroutine_id
) {
    // Mark all allocation sites for this variable as sent to channel - SAFE ACCESS
    auto var_sites_it = g_escape_data.var_allocation_sites.find(var_id);
    if (var_sites_it != g_escape_data.var_allocation_sites.end()) {
        for (size_t site : var_sites_it->second) {
            auto result_it = g_escape_data.allocation_results.find(site);
            if (result_it != g_escape_data.allocation_results.end()) {
                auto& result = result_it->second;
                result.passed_to_channel = true;
                result.ownership = ObjectOwnership::GOROUTINE_SHARED;
                result.needs_synchronization = true;
                
                std::cout << "[ESCAPE] Site " << site << " sent to channel by goroutine " 
                          << goroutine_id << "\n";
            }
        }
    }
}

void GoroutineEscapeAnalyzer::register_scope_entry(size_t scope_id) {
    g_escape_data.scope_stack.push(scope_id);
    std::cout << "[ESCAPE] Entered scope " << scope_id << "\n";
}

void GoroutineEscapeAnalyzer::register_scope_exit(size_t scope_id) {
    if (!g_escape_data.scope_stack.empty() && g_escape_data.scope_stack.top() == scope_id) {
        g_escape_data.scope_stack.pop();
        std::cout << "[ESCAPE] Exited scope " << scope_id << "\n";
    }
}

void GoroutineEscapeAnalyzer::set_current_goroutine(uint32_t goroutine_id) {
    g_escape_data.current_goroutine_id = goroutine_id;
    std::cout << "[ESCAPE] Set current goroutine to " << goroutine_id << "\n";
}

void GoroutineEscapeAnalyzer::reset_analysis() {
    g_escape_data.reset();
    std::cout << "[ESCAPE] Reset analysis data\n";
}

// ============================================================================
// ANALYSIS STATISTICS AND DEBUGGING
// ============================================================================

void GoroutineEscapeAnalyzer::print_analysis_statistics() {
    std::cout << "\n=== ESCAPE ANALYSIS STATISTICS ===\n";
    
    // Count allocations by ownership type
    std::unordered_map<ObjectOwnership, size_t> ownership_counts;
    for (const auto& [site, result] : g_escape_data.allocation_results) {
        ownership_counts[result.ownership]++;
    }
    
    std::cout << "Allocation sites by ownership:\n";
    std::cout << "- Stack Local: " << ownership_counts[ObjectOwnership::STACK_LOCAL] << "\n";
    std::cout << "- Goroutine Private: " << ownership_counts[ObjectOwnership::GOROUTINE_PRIVATE] << "\n";
    std::cout << "- Goroutine Shared: " << ownership_counts[ObjectOwnership::GOROUTINE_SHARED] << "\n";
    std::cout << "- Global Shared: " << ownership_counts[ObjectOwnership::GLOBAL_SHARED] << "\n";
    
    // Count escape reasons
    size_t captured_count = 0;
    size_t cross_access_count = 0;
    size_t returned_count = 0;
    size_t global_count = 0;
    size_t channel_count = 0;
    
    for (const auto& [site, result] : g_escape_data.allocation_results) {
        if (result.captured_by_goroutine) captured_count++;
        if (result.accessed_across_goroutines) cross_access_count++;
        if (result.returned_from_goroutine) returned_count++;
        if (result.stored_in_shared_object) global_count++;
        if (result.passed_to_channel) channel_count++;
    }
    
    std::cout << "\nEscape reasons:\n";
    std::cout << "- Captured by goroutine: " << captured_count << "\n";
    std::cout << "- Accessed across goroutines: " << cross_access_count << "\n";
    std::cout << "- Returned from goroutine: " << returned_count << "\n";
    std::cout << "- Stored globally: " << global_count << "\n";
    std::cout << "- Passed to channel: " << channel_count << "\n";
    
    // Goroutine statistics
    std::cout << "\nGoroutine statistics:\n";
    std::cout << "- Total goroutines: " << g_escape_data.goroutine_captured_vars.size() << "\n";
    std::cout << "- Variables with cross-goroutine access: " << g_escape_data.var_goroutine_access.size() << "\n";
    
    // Performance impact estimate
    size_t total_allocations = g_escape_data.allocation_results.size();
    size_t fast_allocations = ownership_counts[ObjectOwnership::STACK_LOCAL] + 
                             ownership_counts[ObjectOwnership::GOROUTINE_PRIVATE];
    
    if (total_allocations > 0) {
        double fast_percentage = (double)fast_allocations / total_allocations * 100.0;
        std::cout << "\nPerformance impact:\n";
        std::cout << "- Fast allocations: " << fast_percentage << "%\n";
        std::cout << "- Slow allocations: " << (100.0 - fast_percentage) << "%\n";
    }
    
    std::cout << "================================\n\n";
}

} // namespace gots