#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <chrono>

namespace gots {

enum class TokenType {
    IDENTIFIER, NUMBER, STRING, BOOLEAN,
    FUNCTION, GO, AWAIT, LET, VAR, CONST,
    IF, FOR, WHILE, RETURN,
    SWITCH, CASE, DEFAULT, BREAK,
    IMPORT, EXPORT, FROM, AS, DEFAULT_EXPORT,
    TENSOR, NEW,
    CLASS, EXTENDS, SUPER, THIS, CONSTRUCTOR,
    PUBLIC, PRIVATE, PROTECTED, STATIC,
    EACH, IN, PIPE,  // Added for for-each syntax
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACKET, RBRACKET,
    SEMICOLON, COMMA, DOT, COLON, QUESTION,
    ASSIGN, PLUS, MINUS, MULTIPLY, DIVIDE, MODULO, POWER,
    EQUAL, NOT_EQUAL, STRICT_EQUAL, LESS, GREATER, LESS_EQUAL, GREATER_EQUAL,
    AND, OR, NOT,
    PLUS_ASSIGN, MINUS_ASSIGN, MULTIPLY_ASSIGN, DIVIDE_ASSIGN,
    INCREMENT, DECREMENT,
    EOF_TOKEN
};

enum class DataType {
    UNKNOWN, VOID,
    INT8, INT16, INT32, INT64,
    UINT8, UINT16, UINT32, UINT64,
    FLOAT32, FLOAT64,
    BOOLEAN, STRING,
    TENSOR, PROMISE, FUNCTION,
    CLASS_INSTANCE,  // For class instances
    NUMBER = FLOAT64,  // JavaScript compatibility: number is float64
    ANY = UNKNOWN     // ANY is an alias for UNKNOWN (untyped variables)
};

struct Token {
    TokenType type;
    std::string value;
    int line, column;
};

// Forward declarations
struct ExpressionNode;
class GoTSCompiler;

struct Variable {
    std::string name;
    DataType type;
    int64_t stack_offset;
    bool is_global;
    bool is_mutable;
    bool is_static = false;
    std::string class_name;  // For CLASS_INSTANCE type, stores the class name
    std::shared_ptr<ExpressionNode> default_value;  // Default value for class fields
};

struct Function {
    std::string name;
    DataType return_type;
    std::vector<Variable> parameters;
    std::vector<uint8_t> machine_code;
    int64_t stack_size;
};

struct ClassInfo {
    std::string name;
    std::string parent_class;
    std::vector<Variable> fields;
    std::unordered_map<std::string, Function> methods;
    Function* constructor;
    int64_t instance_size;  // Total size needed for an instance
    
    ClassInfo() : constructor(nullptr), instance_size(0) {}
    ClassInfo(const std::string& n) : name(n), constructor(nullptr), instance_size(0) {}
};

enum class Backend {
    X86_64,
    WASM
};

class CodeGenerator {
public:
    virtual ~CodeGenerator() = default;
    virtual void emit_prologue() = 0;
    virtual void emit_epilogue() = 0;
    virtual void emit_mov_reg_imm(int reg, int64_t value) = 0;
    virtual void emit_mov_reg_reg(int dst, int src) = 0;
    virtual void emit_mov_mem_reg(int64_t offset, int reg) = 0;
    virtual void emit_mov_reg_mem(int reg, int64_t offset) = 0;
    virtual void emit_add_reg_imm(int reg, int64_t value) = 0;
    virtual void emit_add_reg_reg(int dst, int src) = 0;
    virtual void emit_sub_reg_imm(int reg, int64_t value) = 0;
    virtual void emit_sub_reg_reg(int dst, int src) = 0;
    virtual void emit_mul_reg_reg(int dst, int src) = 0;
    virtual void emit_div_reg_reg(int dst, int src) = 0;
    virtual void emit_mod_reg_reg(int dst, int src) = 0;
    virtual void emit_call(const std::string& label) = 0;
    virtual void emit_ret() = 0;
    virtual void emit_function_return() = 0;
    virtual void emit_jump(const std::string& label) = 0;
    virtual void emit_jump_if_zero(const std::string& label) = 0;
    virtual void emit_jump_if_not_zero(const std::string& label) = 0;
    virtual void emit_compare(int reg1, int reg2) = 0;
    virtual void emit_setl(int reg) = 0;
    virtual void emit_setg(int reg) = 0;
    virtual void emit_sete(int reg) = 0;
    virtual void emit_setne(int reg) = 0;
    virtual void emit_setle(int reg) = 0;
    virtual void emit_setge(int reg) = 0;
    virtual void emit_and_reg_imm(int reg, int64_t value) = 0;
    virtual void emit_xor_reg_reg(int dst, int src) = 0;
    virtual void emit_call_reg(int reg) = 0;
    virtual void emit_label(const std::string& label) = 0;
    virtual void emit_goroutine_spawn(const std::string& function_name) = 0;
    virtual void emit_goroutine_spawn_with_args(const std::string& function_name, int arg_count) = 0;
    virtual void emit_goroutine_spawn_with_func_ptr() = 0;
    virtual void emit_promise_resolve(int value_reg) = 0;
    virtual void emit_promise_await(int promise_reg) = 0;
    virtual std::vector<uint8_t> get_code() const = 0;
    virtual void clear() = 0;
    virtual const std::unordered_map<std::string, int64_t>& get_label_offsets() const = 0;
};

class X86CodeGen : public CodeGenerator {
private:
    std::vector<uint8_t> code;
    std::unordered_map<std::string, int64_t> label_offsets;
    std::vector<std::pair<std::string, int64_t>> unresolved_jumps;
    int64_t current_stack_offset;
    int64_t function_stack_size;
    
public:
    X86CodeGen() : current_stack_offset(0), function_stack_size(0) {}
    void emit_prologue() override;
    void emit_epilogue() override;
    void emit_mov_reg_imm(int reg, int64_t value) override;
    void emit_mov_reg_reg(int dst, int src) override;
    void emit_mov_mem_reg(int64_t offset, int reg) override;
    void emit_mov_reg_mem(int reg, int64_t offset) override;
    void emit_add_reg_imm(int reg, int64_t value) override;
    void emit_add_reg_reg(int dst, int src) override;
    void emit_sub_reg_imm(int reg, int64_t value) override;
    void emit_sub_reg_reg(int dst, int src) override;
    void emit_mul_reg_reg(int dst, int src) override;
    void emit_div_reg_reg(int dst, int src) override;
    void emit_mod_reg_reg(int dst, int src) override;
    void emit_call(const std::string& label) override;
    void emit_ret() override;
    void emit_function_return() override;
    void emit_jump(const std::string& label) override;
    void emit_jump_if_zero(const std::string& label) override;
    void emit_jump_if_not_zero(const std::string& label) override;
    void emit_compare(int reg1, int reg2) override;
    void emit_setl(int reg) override;
    void emit_setg(int reg) override;
    void emit_sete(int reg) override;
    void emit_setne(int reg) override;
    void emit_setle(int reg) override;
    void emit_setge(int reg) override;
    void emit_and_reg_imm(int reg, int64_t value) override;
    void emit_xor_reg_reg(int dst, int src) override;
    void emit_call_reg(int reg) override;
    void emit_jump_if_equal(const std::string& label);
    void emit_jump_if_greater(const std::string& label);
    void emit_jump_if_less(const std::string& label);
    void emit_label(const std::string& label) override;
    void emit_goroutine_spawn(const std::string& function_name) override;
    void emit_goroutine_spawn_with_args(const std::string& function_name, int arg_count) override;
    void emit_goroutine_spawn_with_func_ptr() override;
    void emit_promise_resolve(int value_reg) override;
    void emit_promise_await(int promise_reg) override;
    std::vector<uint8_t> get_code() const override { return code; }
    void clear() override { code.clear(); label_offsets.clear(); unresolved_jumps.clear(); }
    const std::unordered_map<std::string, int64_t>& get_label_offsets() const override { return label_offsets; }
    void set_function_stack_size(int64_t size) { function_stack_size = size; }
    int64_t get_function_stack_size() const { return function_stack_size; }
    void emit_mov_reg_mem_rsp(int reg, int64_t offset);  // RSP-relative version
    void emit_mov_mem_rsp_reg(int64_t offset, int reg);  // RSP-relative store version
    
    // High-Performance String Assembly Optimizations
    void emit_string_length_fast(int string_reg, int dest_reg);
    void emit_string_concat_fast(int str1_reg, int str2_reg, int dest_reg);
    void emit_string_equals_fast(int str1_reg, int str2_reg, int dest_reg);
    void emit_fast_memcpy();  // RDI=dest, RSI=src, RDX=len
    void emit_fast_memcmp();  // RDI=ptr1, RSI=ptr2, RDX=len
};

class WasmCodeGen : public CodeGenerator {
private:
    std::vector<uint8_t> code;
    std::unordered_map<std::string, int64_t> label_offsets;
    std::vector<std::pair<std::string, int64_t>> unresolved_jumps;
    int64_t current_local_count;
    
    void emit_leb128(int64_t value);
    void emit_opcode(uint8_t opcode);
    
public:
    void emit_prologue() override;
    void emit_epilogue() override;
    void emit_mov_reg_imm(int reg, int64_t value) override;
    void emit_mov_reg_reg(int dst, int src) override;
    void emit_mov_mem_reg(int64_t offset, int reg) override;
    void emit_mov_reg_mem(int reg, int64_t offset) override;
    void emit_add_reg_imm(int reg, int64_t value) override;
    void emit_add_reg_reg(int dst, int src) override;
    void emit_sub_reg_imm(int reg, int64_t value) override;
    void emit_sub_reg_reg(int dst, int src) override;
    void emit_mul_reg_reg(int dst, int src) override;
    void emit_div_reg_reg(int dst, int src) override;
    void emit_mod_reg_reg(int dst, int src) override;
    void emit_call(const std::string& label) override;
    void emit_ret() override;
    void emit_function_return() override;
    void emit_jump(const std::string& label) override;
    void emit_jump_if_zero(const std::string& label) override;
    void emit_jump_if_not_zero(const std::string& label) override;
    void emit_compare(int reg1, int reg2) override;
    void emit_setl(int reg) override;
    void emit_setg(int reg) override;
    void emit_sete(int reg) override;
    void emit_setne(int reg) override;
    void emit_setle(int reg) override;
    void emit_setge(int reg) override;
    void emit_and_reg_imm(int reg, int64_t value) override;
    void emit_xor_reg_reg(int dst, int src) override;
    void emit_call_reg(int reg) override;
    void emit_label(const std::string& label) override;
    void emit_goroutine_spawn(const std::string& function_name) override;
    void emit_goroutine_spawn_with_args(const std::string& function_name, int arg_count) override;
    void emit_goroutine_spawn_with_func_ptr() override;
    void emit_promise_resolve(int value_reg) override;
    void emit_promise_await(int promise_reg) override;
    std::vector<uint8_t> get_code() const override { return code; }
    void clear() override { code.clear(); label_offsets.clear(); unresolved_jumps.clear(); }
    const std::unordered_map<std::string, int64_t>& get_label_offsets() const override { return label_offsets; }
};

class TypeInference {
private:
    std::unordered_map<std::string, DataType> variable_types;
    std::unordered_map<std::string, std::string> variable_class_names;  // For CLASS_INSTANCE variables
    std::unordered_map<std::string, int64_t> variable_offsets;
    int64_t current_offset = -8; // Start at -8 (RBP-8)
    
public:
    DataType infer_type(const std::string& expression);
    DataType get_cast_type(DataType t1, DataType t2);
    bool needs_casting(DataType from, DataType to);
    void set_variable_type(const std::string& name, DataType type);
    void set_variable_class_type(const std::string& name, const std::string& class_name);
    DataType get_variable_type(const std::string& name);
    std::string get_variable_class_name(const std::string& name);
    
    // Variable storage management
    void set_variable_offset(const std::string& name, int64_t offset);
    int64_t get_variable_offset(const std::string& name);
    int64_t allocate_variable(const std::string& name, DataType type);
    bool variable_exists(const std::string& name);
    void enter_scope();
    void exit_scope();
    void reset_for_function();
    void reset_for_function_with_params(int param_count);
};

struct ASTNode {
    virtual ~ASTNode() = default;
    virtual void generate_code(CodeGenerator& gen, TypeInference& types) = 0;
};

struct ExpressionNode : ASTNode {
    DataType result_type = DataType::UNKNOWN;
};

struct NumberLiteral : ExpressionNode {
    double value;
    NumberLiteral(double v) : value(v) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct StringLiteral : ExpressionNode {
    std::string value;
    StringLiteral(const std::string& v) : value(v) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct Identifier : ExpressionNode {
    std::string name;
    Identifier(const std::string& n) : name(n) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct BinaryOp : ExpressionNode {
    std::unique_ptr<ExpressionNode> left, right;
    TokenType op;
    BinaryOp(std::unique_ptr<ExpressionNode> l, TokenType o, std::unique_ptr<ExpressionNode> r)
        : op(o), left(std::move(l)), right(std::move(r)) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct TernaryOperator : ExpressionNode {
    std::unique_ptr<ExpressionNode> condition, true_expr, false_expr;
    TernaryOperator(std::unique_ptr<ExpressionNode> cond, std::unique_ptr<ExpressionNode> true_val, std::unique_ptr<ExpressionNode> false_val)
        : condition(std::move(cond)), true_expr(std::move(true_val)), false_expr(std::move(false_val)) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct FunctionCall : ExpressionNode {
    std::string name;
    std::vector<std::unique_ptr<ExpressionNode>> arguments;
    bool is_goroutine = false;
    bool is_awaited = false;
    FunctionCall(const std::string& n) : name(n) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct MethodCall : ExpressionNode {
    std::string object_name;
    std::string method_name;
    std::vector<std::unique_ptr<ExpressionNode>> arguments;
    bool is_goroutine = false;
    bool is_awaited = false;
    MethodCall(const std::string& obj, const std::string& method) 
        : object_name(obj), method_name(method) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct ArrayLiteral : ExpressionNode {
    std::vector<std::unique_ptr<ExpressionNode>> elements;
    ArrayLiteral() {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct ObjectLiteral : ExpressionNode {
    std::vector<std::pair<std::string, std::unique_ptr<ExpressionNode>>> properties;
    ObjectLiteral() {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct TypedArrayLiteral : ExpressionNode {
    std::vector<std::unique_ptr<ExpressionNode>> elements;
    DataType array_type;
    TypedArrayLiteral(DataType type) : array_type(type) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct Assignment : ExpressionNode {
    std::string variable_name;
    std::unique_ptr<ExpressionNode> value;
    DataType declared_type = DataType::UNKNOWN;
    Assignment(const std::string& name, std::unique_ptr<ExpressionNode> val)
        : variable_name(name), value(std::move(val)) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct PropertyAssignment : ExpressionNode {
    std::string object_name;
    std::string property_name;
    std::unique_ptr<ExpressionNode> value;
    PropertyAssignment(const std::string& obj, const std::string& prop, std::unique_ptr<ExpressionNode> val)
        : object_name(obj), property_name(prop), value(std::move(val)) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct PostfixIncrement : ExpressionNode {
    std::string variable_name;
    PostfixIncrement(const std::string& name) : variable_name(name) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct PostfixDecrement : ExpressionNode {
    std::string variable_name;
    PostfixDecrement(const std::string& name) : variable_name(name) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct FunctionDecl : ASTNode {
    std::string name;
    std::vector<Variable> parameters;
    DataType return_type = DataType::UNKNOWN;
    std::vector<std::unique_ptr<ASTNode>> body;
    FunctionDecl(const std::string& n) : name(n) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct IfStatement : ASTNode {
    std::unique_ptr<ExpressionNode> condition;
    std::vector<std::unique_ptr<ASTNode>> then_body;
    std::vector<std::unique_ptr<ASTNode>> else_body;
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct ForLoop : ASTNode {
    std::unique_ptr<ASTNode> init;
    std::unique_ptr<ExpressionNode> condition;
    std::unique_ptr<ASTNode> update;
    std::vector<std::unique_ptr<ASTNode>> body;
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct ForEachLoop : ASTNode {
    std::string index_var_name;   // index for arrays, key for objects
    std::string value_var_name;   // value variable name
    std::unique_ptr<ExpressionNode> iterable;  // the array/object to iterate over
    std::vector<std::unique_ptr<ASTNode>> body;
    ForEachLoop(const std::string& index_name, const std::string& value_name)
        : index_var_name(index_name), value_var_name(value_name) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct ReturnStatement : ASTNode {
    std::unique_ptr<ExpressionNode> value;
    ReturnStatement(std::unique_ptr<ExpressionNode> val) : value(std::move(val)) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct BreakStatement : ASTNode {
    BreakStatement() {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct CaseClause : ASTNode {
    std::unique_ptr<ExpressionNode> value;  // nullptr for default case
    std::vector<std::unique_ptr<ASTNode>> body;
    bool is_default = false;
    
    CaseClause(std::unique_ptr<ExpressionNode> val) : value(std::move(val)) {}
    CaseClause() : is_default(true) {}  // Default constructor for default case
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct SwitchStatement : ASTNode {
    std::unique_ptr<ExpressionNode> discriminant;
    std::vector<std::unique_ptr<CaseClause>> cases;
    
    SwitchStatement(std::unique_ptr<ExpressionNode> disc) : discriminant(std::move(disc)) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

// Import/Export AST Nodes
struct ImportSpecifier {
    std::string imported_name;  // Name in source module
    std::string local_name;     // Name in current module (for "as" renaming)
    bool is_default = false;
    
    ImportSpecifier(const std::string& name) : imported_name(name), local_name(name) {}
    ImportSpecifier(const std::string& imported, const std::string& local) 
        : imported_name(imported), local_name(local) {}
};

struct ImportStatement : ASTNode {
    std::vector<ImportSpecifier> specifiers;
    std::string module_path;
    bool is_namespace_import = false;  // import * as name from "module"
    std::string namespace_name;        // For namespace imports
    
    ImportStatement(const std::string& path) : module_path(path) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct ExportSpecifier {
    std::string local_name;     // Name in current module
    std::string exported_name;  // Name in export (for "as" renaming)
    
    ExportSpecifier(const std::string& name) : local_name(name), exported_name(name) {}
    ExportSpecifier(const std::string& local, const std::string& exported)
        : local_name(local), exported_name(exported) {}
};

struct ExportStatement : ASTNode {
    std::vector<ExportSpecifier> specifiers;
    std::unique_ptr<ASTNode> declaration;  // For export declarations
    bool is_default = false;
    
    ExportStatement() {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct PropertyAccess : ExpressionNode {
    std::string object_name;
    std::string property_name;
    PropertyAccess(const std::string& obj, const std::string& prop) 
        : object_name(obj), property_name(prop) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct ThisExpression : ExpressionNode {
    ThisExpression() {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct NewExpression : ExpressionNode {
    std::string class_name;
    std::vector<std::unique_ptr<ExpressionNode>> arguments;
    bool is_dart_style = false; // For new Person{name: "bob"} syntax
    std::vector<std::pair<std::string, std::unique_ptr<ExpressionNode>>> dart_args;
    NewExpression(const std::string& name) : class_name(name) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct ConstructorDecl : ASTNode {
    std::string class_name;
    std::vector<Variable> parameters;
    std::vector<std::unique_ptr<ASTNode>> body;
    ConstructorDecl(const std::string& cn) : class_name(cn) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
    
    // Helper to get class info during code generation
    static GoTSCompiler* current_compiler_context;
    static void set_compiler_context(GoTSCompiler* compiler) { current_compiler_context = compiler; }
};

struct MethodDecl : ASTNode {
    std::string name;
    std::vector<Variable> parameters;
    DataType return_type = DataType::UNKNOWN;
    std::vector<std::unique_ptr<ASTNode>> body;
    bool is_static = false;
    bool is_private = false;
    bool is_protected = false;
    MethodDecl(const std::string& n) : name(n) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct SuperCall : ExpressionNode {
    std::vector<std::unique_ptr<ExpressionNode>> arguments;
    SuperCall() {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct SuperMethodCall : ExpressionNode {
    std::string method_name;
    std::vector<std::unique_ptr<ExpressionNode>> arguments;
    SuperMethodCall(const std::string& name) : method_name(name) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

struct ClassDecl : ASTNode {
    std::string name;
    std::string parent_class; // For inheritance
    std::vector<Variable> fields;
    std::unique_ptr<ConstructorDecl> constructor;
    std::vector<std::unique_ptr<MethodDecl>> methods;
    ClassDecl(const std::string& n) : name(n) {}
    void generate_code(CodeGenerator& gen, TypeInference& types) override;
};

class Lexer {
private:
    std::string source;
    size_t pos = 0;
    int line = 1, column = 1;
    
    char current_char();
    char peek_char(int offset = 1);
    void advance();
    void skip_whitespace();
    void skip_comment();
    Token make_number();
    Token make_string();
    Token make_identifier();
    
public:
    Lexer(const std::string& src) : source(src) {}
    std::vector<Token> tokenize();
};

class Parser {
private:
    std::vector<Token> tokens;
    size_t pos = 0;
    
    Token& current_token();
    Token& peek_token(int offset = 1);
    void advance();
    bool match(TokenType type);
    bool check(TokenType type);
    
    std::unique_ptr<ExpressionNode> parse_expression();
    std::unique_ptr<ExpressionNode> parse_assignment_expression();
    std::unique_ptr<ExpressionNode> parse_ternary();
    std::unique_ptr<ExpressionNode> parse_logical_or();
    std::unique_ptr<ExpressionNode> parse_logical_and();
    std::unique_ptr<ExpressionNode> parse_equality();
    std::unique_ptr<ExpressionNode> parse_comparison();
    std::unique_ptr<ExpressionNode> parse_addition();
    std::unique_ptr<ExpressionNode> parse_multiplication();
    std::unique_ptr<ExpressionNode> parse_exponentiation();
    std::unique_ptr<ExpressionNode> parse_unary();
    std::unique_ptr<ExpressionNode> parse_primary();
    std::unique_ptr<ExpressionNode> parse_call();
    
    std::unique_ptr<ASTNode> parse_statement();
    std::unique_ptr<ASTNode> parse_import_statement();
    std::unique_ptr<ASTNode> parse_export_statement();
    std::unique_ptr<ASTNode> parse_function_declaration();
    std::unique_ptr<ASTNode> parse_variable_declaration();
    std::unique_ptr<ASTNode> parse_if_statement();
    std::unique_ptr<ASTNode> parse_for_statement();
    std::unique_ptr<ASTNode> parse_for_each_statement();
    std::unique_ptr<ASTNode> parse_switch_statement();
    std::unique_ptr<CaseClause> parse_case_clause();
    std::unique_ptr<ASTNode> parse_return_statement();
    std::unique_ptr<ASTNode> parse_break_statement();
    std::unique_ptr<ASTNode> parse_expression_statement();
    std::unique_ptr<ASTNode> parse_class_declaration();
    std::unique_ptr<MethodDecl> parse_method_declaration();
    std::unique_ptr<ConstructorDecl> parse_constructor_declaration(const std::string& class_name);
    
    DataType parse_type();
    
public:
    Parser(std::vector<Token> toks) : tokens(std::move(toks)) {}
    std::vector<std::unique_ptr<ASTNode>> parse();
};

// Module system structures
enum class ModuleState {
    NOT_LOADED,      // Module not yet loaded
    LOADING,         // Currently being loaded (for circular import detection)
    LOADED,          // Fully loaded and ready
    ERROR,           // Failed to load
    PARTIAL_LOADED   // Partially loaded due to circular import
};

struct ModuleLoadInfo {
    std::string error_message;
    std::vector<std::string> import_stack;  // Stack trace for circular imports
    std::chrono::steady_clock::time_point load_start_time;
    
    ModuleLoadInfo() : load_start_time(std::chrono::steady_clock::now()) {}
};

struct Module {
    std::string path;
    std::unordered_map<std::string, Variable> exports;
    std::unordered_map<std::string, Function> exported_functions;
    bool has_default_export = false;
    std::string default_export_name;
    bool loaded = false;  // Keep for backward compatibility
    std::vector<std::unique_ptr<ASTNode>> ast;
    
    // New lazy loading system
    ModuleState state = ModuleState::NOT_LOADED;
    ModuleLoadInfo load_info;
    bool exports_partial = false;  // True if exports are incomplete due to circular import
    std::vector<std::string> pending_imports;  // List of imports this module depends on
    
    // Lazy loading flag - only execute module code when exports are accessed
    bool code_executed = false;
    
    Module() = default;
    Module(const std::string& module_path) : path(module_path) {}
    
    bool is_ready() const { return state == ModuleState::LOADED; }
    bool is_loading() const { return state == ModuleState::LOADING; }
    bool has_error() const { return state == ModuleState::ERROR; }
    bool is_partial() const { return state == ModuleState::PARTIAL_LOADED; }
};

class GoTSCompiler {
private:
    std::unique_ptr<CodeGenerator> codegen;
    TypeInference type_system;
    std::unordered_map<std::string, Function> functions;
    std::unordered_map<std::string, Variable> global_variables;
    std::unordered_map<std::string, ClassInfo> classes;  // Class registry
    std::unordered_map<std::string, Module> modules;     // Module cache
    Backend target_backend;
    std::string current_file_path;  // Track current file being compiled
    
public:
    GoTSCompiler(Backend backend = Backend::X86_64);
    void compile(const std::string& source);
    void compile_file(const std::string& file_path);
    std::vector<uint8_t> get_machine_code();
    void execute();
    void set_backend(Backend backend);
    
    // Module system
    std::string resolve_module_path(const std::string& module_path, const std::string& current_file = "");
    Module* load_module(const std::string& module_path);
    void create_synthetic_default_export(Module& module);
    void set_current_file(const std::string& file_path) { current_file_path = file_path; }
    const std::string& get_current_file() const { return current_file_path; }
    
    // Enhanced lazy loading system
    Module* load_module_lazy(const std::string& module_path);
    bool is_circular_import(const std::string& module_path);
    void handle_circular_import(const std::string& module_path);
    Module* handle_circular_import_and_return(const std::string& module_path);
    std::string get_import_stack_trace() const;
    void execute_module_code(Module& module);
    void prepare_partial_exports(Module& module);
    
private:
    std::vector<std::string> current_loading_stack;  // Track circular imports
    
public:
    
    // Class management
    void register_class(const ClassInfo& class_info);
    ClassInfo* get_class(const std::string& class_name);
    bool is_class_defined(const std::string& class_name);
};

}