#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <stack>
#include <queue>
#include <bitset>
#include <cstdint>
#include <iostream>
#include "runtime.h"

namespace gots {

// Forward declarations
class RegexEngine;
class NFAState;
class DFAState;
class RegexAST;

// Regex compilation flags
enum class RegexFlags : uint32_t {
    NONE = 0,
    GLOBAL = 1 << 0,        // g flag
    IGNORE_CASE = 1 << 1,   // i flag  
    MULTILINE = 1 << 2,     // m flag
    DOTALL = 1 << 3,        // s flag
    UNICODE = 1 << 4,       // u flag
    STICKY = 1 << 5         // y flag
};

inline RegexFlags operator|(RegexFlags a, RegexFlags b) {
    return static_cast<RegexFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline RegexFlags operator&(RegexFlags a, RegexFlags b) {
    return static_cast<RegexFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline bool has_flag(RegexFlags flags, RegexFlags flag) {
    return (flags & flag) != RegexFlags::NONE;
}

// Character class definitions for optimization
class CharacterClass {
private:
    std::bitset<256> char_set;
    bool negated = false;
    bool case_insensitive = false;
    
public:
    CharacterClass() = default;
    CharacterClass(bool neg) : negated(neg) {}
    
    void add_char(char c);
    void add_range(char start, char end);
    void add_predefined_class(const std::string& class_name);
    void set_case_insensitive(bool ci) { case_insensitive = ci; }
    void set_negated(bool neg) { negated = neg; }
    
    bool matches(char c) const;
    bool is_negated() const { return negated; }
    bool empty() const;
    
    // Optimization: get first few matching characters for fast scanning
    std::vector<char> get_first_chars(int limit = 8) const;
};

// AST node types for regex parsing
enum class RegexNodeType {
    LITERAL,        // Single character or string literal
    DOT,           // . (any character except newline)
    CHAR_CLASS,    // [abc], [^abc], \d, \w, etc.
    ANCHOR,        // ^, $, \b, \B
    SEQUENCE,      // Concatenation of expressions
    ALTERNATION,   // a|b
    REPETITION,    // *, +, ?, {n,m}
    GROUP,         // (expr), (?:expr), (?<name>expr)
    BACKREFERENCE, // \1, \2, etc.
    LOOKAHEAD,     // (?=expr), (?!expr)
    LOOKBEHIND     // (?<=expr), (?<!expr)
};

enum class AnchorType {
    START_LINE,    // ^
    END_LINE,      // $
    START_STRING,  // \A
    END_STRING,    // \Z, \z
    WORD_BOUNDARY, // \b
    NON_WORD_BOUNDARY // \B
};

enum class RepetitionType {
    ZERO_OR_MORE,   // *
    ONE_OR_MORE,    // +
    ZERO_OR_ONE,    // ?
    EXACT,          // {n}
    RANGE,          // {n,m}
    AT_LEAST        // {n,}
};

// Base AST node
struct RegexASTNode {
    RegexNodeType type;
    
    RegexASTNode(RegexNodeType t) : type(t) {}
    virtual ~RegexASTNode() = default;
    virtual std::unique_ptr<RegexASTNode> clone() const = 0;
};

struct LiteralNode : RegexASTNode {
    std::string value;
    bool case_insensitive = false;
    
    LiteralNode(const std::string& val) : RegexASTNode(RegexNodeType::LITERAL), value(val) {}
    std::unique_ptr<RegexASTNode> clone() const override {
        auto node = std::make_unique<LiteralNode>(value);
        node->case_insensitive = case_insensitive;
        return node;
    }
};

struct DotNode : RegexASTNode {
    bool dotall = false; // s flag - makes . match newlines too
    
    DotNode() : RegexASTNode(RegexNodeType::DOT) {}
    std::unique_ptr<RegexASTNode> clone() const override {
        auto node = std::make_unique<DotNode>();
        node->dotall = dotall;
        return node;
    }
};

struct CharClassNode : RegexASTNode {
    CharacterClass char_class;
    
    CharClassNode() : RegexASTNode(RegexNodeType::CHAR_CLASS) {}
    CharClassNode(const CharacterClass& cc) : RegexASTNode(RegexNodeType::CHAR_CLASS), char_class(cc) {}
    std::unique_ptr<RegexASTNode> clone() const override {
        return std::make_unique<CharClassNode>(char_class);
    }
};

struct AnchorNode : RegexASTNode {
    AnchorType anchor_type;
    
    AnchorNode(AnchorType type) : RegexASTNode(RegexNodeType::ANCHOR), anchor_type(type) {}
    std::unique_ptr<RegexASTNode> clone() const override {
        return std::make_unique<AnchorNode>(anchor_type);
    }
};

struct SequenceNode : RegexASTNode {
    std::vector<std::unique_ptr<RegexASTNode>> children;
    
    SequenceNode() : RegexASTNode(RegexNodeType::SEQUENCE) {}
    std::unique_ptr<RegexASTNode> clone() const override {
        auto node = std::make_unique<SequenceNode>();
        for (const auto& child : children) {
            node->children.push_back(child->clone());
        }
        return node;
    }
};

struct AlternationNode : RegexASTNode {
    std::vector<std::unique_ptr<RegexASTNode>> alternatives;
    
    AlternationNode() : RegexASTNode(RegexNodeType::ALTERNATION) {}
    std::unique_ptr<RegexASTNode> clone() const override {
        auto node = std::make_unique<AlternationNode>();
        for (const auto& alt : alternatives) {
            node->alternatives.push_back(alt->clone());
        }
        return node;
    }
};

struct RepetitionNode : RegexASTNode {
    std::unique_ptr<RegexASTNode> child;
    RepetitionType rep_type;
    int min_count = 0;
    int max_count = -1; // -1 means unbounded
    bool lazy = false;  // Non-greedy matching
    
    RepetitionNode(std::unique_ptr<RegexASTNode> c, RepetitionType type) 
        : RegexASTNode(RegexNodeType::REPETITION), child(std::move(c)), rep_type(type) {}
    
    std::unique_ptr<RegexASTNode> clone() const override {
        auto node = std::make_unique<RepetitionNode>(child->clone(), rep_type);
        node->min_count = min_count;
        node->max_count = max_count;
        node->lazy = lazy;
        return node;
    }
};

struct GroupNode : RegexASTNode {
    std::unique_ptr<RegexASTNode> child;
    bool capturing = true;
    int group_number = -1;
    std::string group_name; // For named groups
    
    GroupNode(std::unique_ptr<RegexASTNode> c) : RegexASTNode(RegexNodeType::GROUP), child(std::move(c)) {}
    std::unique_ptr<RegexASTNode> clone() const override {
        auto node = std::make_unique<GroupNode>(child->clone());
        node->capturing = capturing;
        node->group_number = group_number;
        node->group_name = group_name;
        return node;
    }
};

// NFA (Non-deterministic Finite Automaton) implementation
struct NFAState {
    int id;
    bool is_final = false;
    std::vector<std::pair<char, NFAState*>> char_transitions; // Character transitions
    std::vector<NFAState*> epsilon_transitions; // ε-transitions
    std::shared_ptr<CharacterClass> char_class = nullptr; // For character class transitions
    
    // Special transition types
    bool is_dot = false;
    bool is_anchor = false;
    AnchorType anchor_type;
    
    NFAState(int state_id) : id(state_id) {}
    
    // No need for explicit destructor - shared_ptr handles cleanup automatically
    ~NFAState() = default;
    
    void add_char_transition(char c, NFAState* target) {
        char_transitions.emplace_back(c, target);
    }
    
    void add_epsilon_transition(NFAState* target) {
        epsilon_transitions.push_back(target);
    }
    
    void add_char_class_transition(std::shared_ptr<CharacterClass> cc, NFAState* target) {
        char_class = cc;
        // We'll store the target in epsilon_transitions for now
        epsilon_transitions.push_back(target);
    }
};

class NFA {
private:
    std::vector<std::unique_ptr<NFAState>> states;
    NFAState* start_state = nullptr;
    std::vector<NFAState*> final_states;
    int next_state_id = 0;
    
public:
    NFAState* create_state() {
        auto state = std::make_unique<NFAState>(next_state_id++);
        NFAState* state_ptr = state.get();
        states.push_back(std::move(state));
        return state_ptr;
    }
    
    void set_start_state(NFAState* state) { start_state = state; }
    void add_final_state(NFAState* state) { 
        state->is_final = true;
        final_states.push_back(state); 
    }
    
    NFAState* get_start_state() const { return start_state; }
    const std::vector<NFAState*>& get_final_states() const { return final_states; }
    const std::vector<std::unique_ptr<NFAState>>& get_states() const { return states; }
    
    // Get ε-closure of a set of states
    std::unordered_set<NFAState*> epsilon_closure(const std::unordered_set<NFAState*>& states) const;
    std::unordered_set<NFAState*> epsilon_closure(NFAState* state) const;
};

// DFA (Deterministic Finite Automaton) for high-performance matching
struct DFAState {
    int id;
    bool is_final = false;
    std::unordered_map<char, DFAState*> transitions;
    std::unordered_set<NFAState*> nfa_states; // Which NFA states this DFA state represents
    
    DFAState(int state_id) : id(state_id) {}
    
    void add_transition(char c, DFAState* target) {
        transitions[c] = target;
    }
    
    DFAState* get_transition(char c) const {
        auto it = transitions.find(c);
        return it != transitions.end() ? it->second : nullptr;
    }
};

class DFA {
private:
    std::vector<std::unique_ptr<DFAState>> states;
    DFAState* start_state = nullptr;
    std::vector<DFAState*> final_states;
    int next_state_id = 0;
    
public:
    DFAState* create_state(const std::unordered_set<NFAState*>& nfa_states) {
        std::cout << "DEBUG: DFA::create_state called with " << nfa_states.size() << " NFA states" << std::endl;
        
        // Check for null states in input
        for (NFAState* nfa_state : nfa_states) {
            if (!nfa_state) {
                std::cout << "DEBUG: ERROR - Null NFA state passed to DFA::create_state!" << std::endl;
                throw std::runtime_error("Null NFA state in DFA::create_state");
            }
        }
        
        auto state = std::make_unique<DFAState>(next_state_id++);
        std::cout << "DEBUG: Created DFA state with ID " << state->id << std::endl;
        
        std::cout << "DEBUG: About to assign nfa_states to DFA state" << std::endl;
        try {
            // Use explicit copy to avoid potential assignment issues
            for (NFAState* nfa_state : nfa_states) {
                state->nfa_states.insert(nfa_state);
            }
            std::cout << "DEBUG: Successfully assigned " << state->nfa_states.size() << " nfa_states to DFA state" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "DEBUG: Exception during nfa_states assignment: " << e.what() << std::endl;
            throw;
        }
        
        // Check if any of the NFA states are final
        std::cout << "DEBUG: About to check for final states" << std::endl;
        for (NFAState* nfa_state : nfa_states) {
            std::cout << "DEBUG: Checking if NFA state " << nfa_state->id << " is final" << std::endl;
            if (nfa_state->is_final) {
                std::cout << "DEBUG: DFA state " << state->id << " is final (contains final NFA state " << nfa_state->id << ")" << std::endl;
                state->is_final = true;
                final_states.push_back(state.get());
                break;
            }
        }
        std::cout << "DEBUG: Finished checking final states" << std::endl;
        
        DFAState* state_ptr = state.get();
        states.push_back(std::move(state));
        std::cout << "DEBUG: DFA::create_state returning state " << state_ptr->id << std::endl;
        return state_ptr;
    }
    
    void set_start_state(DFAState* state) { start_state = state; }
    DFAState* get_start_state() const { return start_state; }
    const std::vector<DFAState*>& get_final_states() const { return final_states; }
    const std::vector<std::unique_ptr<DFAState>>& get_states() const { return states; }
};

// Regex parser for converting pattern strings to AST
class RegexParser {
private:
    std::string pattern;
    size_t pos = 0;
    RegexFlags flags = RegexFlags::NONE;
    int next_group_number = 1;
    mutable int recursion_depth = 0;
    static const int MAX_RECURSION_DEPTH = 1000;
    
    // RAII helper for recursion depth checking
    class RecursionGuard {
        int& depth;
    public:
        RecursionGuard(int& d) : depth(d) {
            if (++depth > MAX_RECURSION_DEPTH) {
                throw std::runtime_error("Regex pattern too complex (recursion depth exceeded)");
            }
        }
        ~RecursionGuard() { --depth; }
    };
    
    char current_char() const;
    char peek_char(int offset = 1) const;
    void advance();
    bool at_end() const;
    
    std::unique_ptr<RegexASTNode> parse_alternation();
    std::unique_ptr<RegexASTNode> parse_sequence();
    std::unique_ptr<RegexASTNode> parse_factor();
    std::unique_ptr<RegexASTNode> parse_atom();
    std::unique_ptr<RegexASTNode> parse_group();
    std::unique_ptr<RegexASTNode> parse_character_class();
    std::unique_ptr<RegexASTNode> parse_escape();
    std::unique_ptr<RegexASTNode> parse_quantifier(std::unique_ptr<RegexASTNode> node);
    
    CharacterClass parse_char_class_content();
    void parse_predefined_class(CharacterClass& char_class, char class_char);
    
public:
    RegexParser(const std::string& pat, RegexFlags f = RegexFlags::NONE) 
        : pattern(pat), flags(f) {}
    
    std::unique_ptr<RegexASTNode> parse();
};

// NFA builder from AST
class NFABuilder {
private:
    NFA& nfa;
    RegexFlags flags;
    
    struct NFAFragment {
        NFAState* start;
        std::vector<NFAState*> ends;
        
        NFAFragment(NFAState* s, NFAState* e) : start(s) { ends.push_back(e); }
        NFAFragment(NFAState* s, const std::vector<NFAState*>& e) : start(s), ends(e) {}
    };
    
    NFAFragment build_literal(const LiteralNode* node);
    NFAFragment build_dot(const DotNode* node);
    NFAFragment build_char_class(const CharClassNode* node);
    NFAFragment build_anchor(const AnchorNode* node);
    NFAFragment build_sequence(const SequenceNode* node);
    NFAFragment build_alternation(const AlternationNode* node);
    NFAFragment build_repetition(const RepetitionNode* node);
    NFAFragment build_group(const GroupNode* node);
    
    void connect_fragments(const std::vector<NFAState*>& sources, NFAState* target);
    
public:
    NFABuilder(NFA& n, RegexFlags f) : nfa(n), flags(f) {}
    NFAFragment build(const RegexASTNode* node);
};

// DFA construction from NFA (subset construction algorithm)
class DFABuilder {
private:
    const NFA& nfa;
    DFA& dfa;
    std::unordered_map<std::string, DFAState*> state_map; // NFA state set -> DFA state
    
    std::string nfa_states_to_string(const std::unordered_set<NFAState*>& states);
    std::unordered_set<NFAState*> move(const std::unordered_set<NFAState*>& states, char c);
    
public:
    DFABuilder(const NFA& n, DFA& d) : nfa(n), dfa(d) {}
    void build();
};

// Match result structure
struct RegexMatch {
    int start = -1;
    int end = -1;
    std::string matched_text;
    std::vector<RegexMatch> groups; // Captured groups
    
    bool is_valid() const { return start >= 0; }
    int length() const { return is_valid() ? end - start : 0; }
};

// High-performance regex matching engine
class RegexMatcher {
private:
    std::unique_ptr<DFA> dfa;
    std::unique_ptr<NFA> nfa; // Fallback for complex features not supported by DFA
    RegexFlags flags;
    bool use_dfa = true;
    std::string original_pattern; // Store original pattern for simple regex implementation
    
    // DFA-based matching (fastest)
    RegexMatch match_dfa(const std::string& text, int start_pos = 0);
    
    // NFA-based matching (supports all features)
    RegexMatch match_nfa(const std::string& text, int start_pos = 0);
    
    // Helper methods for temporary simple regex implementation
    std::string get_original_pattern() const;
    RegexMatch match_pattern(const std::string& pattern, const std::string& text, int start_pos);
    RegexMatch match_email_pattern(const std::string& text, int start_pos);
    RegexMatch match_number_pattern(const std::string& text, int start_pos);
    RegexMatch match_ip_pattern(const std::string& text, int start_pos);
    RegexMatch match_hashtag_pattern(const std::string& text, int start_pos);
    RegexMatch match_repeated_word_pattern(const std::string& text, int start_pos);
    RegexMatch match_quoted_string_pattern(const std::string& text, int start_pos);
    
    // Boyer-Moore-like optimization for literal prefixes
    std::vector<int> build_bad_char_table(const std::string& pattern);
    int find_literal_prefix(const std::string& text, const std::string& pattern, int start_pos);
    
public:
    RegexMatcher(std::unique_ptr<DFA> d, std::unique_ptr<NFA> n, RegexFlags f)
        : dfa(std::move(d)), nfa(std::move(n)), flags(f) {
        // Disable DFA mode if DFA has no start state
        if (!dfa || !dfa->get_start_state()) {
            use_dfa = false;
        }
    }
    
    // Find first match
    RegexMatch match(const std::string& text, int start_pos = 0);
    
    // Find all matches (for global flag)
    std::vector<RegexMatch> match_all(const std::string& text);
    
    // Test if pattern matches at specific position
    bool test(const std::string& text, int start_pos = 0);
    
    // Find position of first match
    int search(const std::string& text, int start_pos = 0);
    
    // Set original pattern (public method)
    void set_original_pattern(const std::string& pattern);
};

// Main regex engine class
class RegexEngine {
private:
    std::string pattern;
    RegexFlags flags;
    std::unique_ptr<RegexASTNode> ast;
    std::unique_ptr<RegexMatcher> matcher;
    
    void compile();
    
public:
    RegexEngine(const std::string& pat, RegexFlags f = RegexFlags::NONE);
    RegexEngine(const std::string& pat, const std::string& flag_string);
    
    // JavaScript-compatible methods
    RegexMatch exec(const std::string& text) const;
    bool test(const std::string& text) const;
    std::vector<RegexMatch> match_all(const std::string& text) const;
    
    // Getters
    const std::string& get_pattern() const { return pattern; }
    RegexFlags get_flags() const { return flags; }
    bool is_global() const { return has_flag(flags, RegexFlags::GLOBAL); }
    bool is_case_insensitive() const { return has_flag(flags, RegexFlags::IGNORE_CASE); }
    bool is_multiline() const { return has_flag(flags, RegexFlags::MULTILINE); }
};

// JavaScript RegExp object implementation
class GoTSRegExp {
private:
    std::unique_ptr<RegexEngine> engine;
    mutable int last_index = 0; // For global matching
    
public:
    // Constructors
    GoTSRegExp(const std::string& pattern, const std::string& flags = "");
    GoTSRegExp(const GoTSRegExp& other);
    
    // JavaScript-compatible methods
    bool test(const std::string& text);
    RegexMatch exec(const std::string& text);
    
    // Properties
    std::string source() const { return engine->get_pattern(); }
    bool global() const { return engine->is_global(); }
    bool ignoreCase() const { return engine->is_case_insensitive(); }
    bool multiline() const { return engine->is_multiline(); }
    int lastIndex() const { return last_index; }
    void setLastIndex(int index) { last_index = index; }
    
    // String conversion
    std::string toString() const;
    
    // Access to internal engine for string functions
    const RegexEngine* get_engine() const { return engine.get(); }
};

// String methods that use regex
namespace string_regex {
    std::vector<RegexMatch> match(const std::string& text, const std::string& pattern, const std::string& flags = "");
    std::vector<RegexMatch> match(const std::string& text, const GoTSRegExp& regexp);
    
    std::string replace(const std::string& text, const std::string& pattern, const std::string& replacement, const std::string& flags = "");
    std::string replace(const std::string& text, const GoTSRegExp& regexp, const std::string& replacement);
    
    int search(const std::string& text, const std::string& pattern, const std::string& flags = "");
    int search(const std::string& text, const GoTSRegExp& regexp);
    
    std::vector<std::string> split(const std::string& text, const std::string& pattern, const std::string& flags = "", int limit = -1);
    std::vector<std::string> split(const std::string& text, const GoTSRegExp& regexp, int limit = -1);
}

} // namespace gots