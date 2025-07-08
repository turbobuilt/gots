#include "compiler.h"
#include <iostream>
#include <algorithm>
#include <stdexcept>

namespace gots {

enum WasmOpcode {
    WASM_UNREACHABLE = 0x00,
    WASM_NOP = 0x01,
    WASM_BLOCK = 0x02,
    WASM_LOOP = 0x03,
    WASM_IF = 0x04,
    WASM_ELSE = 0x05,
    WASM_END = 0x0B,
    WASM_BR = 0x0C,
    WASM_BR_IF = 0x0D,
    WASM_RETURN = 0x0F,
    WASM_CALL = 0x10,
    WASM_CALL_INDIRECT = 0x11,
    WASM_DROP = 0x1A,
    WASM_SELECT = 0x1B,
    WASM_LOCAL_GET = 0x20,
    WASM_LOCAL_SET = 0x21,
    WASM_LOCAL_TEE = 0x22,
    WASM_GLOBAL_GET = 0x23,
    WASM_GLOBAL_SET = 0x24,
    WASM_I32_LOAD = 0x28,
    WASM_I64_LOAD = 0x29,
    WASM_F32_LOAD = 0x2A,
    WASM_F64_LOAD = 0x2B,
    WASM_I32_STORE = 0x36,
    WASM_I64_STORE = 0x37,
    WASM_F32_STORE = 0x38,
    WASM_F64_STORE = 0x39,
    WASM_I32_CONST = 0x41,
    WASM_I64_CONST = 0x42,
    WASM_F32_CONST = 0x43,
    WASM_F64_CONST = 0x44,
    WASM_I32_ADD = 0x6A,
    WASM_I32_SUB = 0x6B,
    WASM_I32_MUL = 0x6C,
    WASM_I32_DIV_S = 0x6D,
    WASM_I32_DIV_U = 0x6E,
    WASM_I64_ADD = 0x7C,
    WASM_I64_SUB = 0x7D,
    WASM_I64_MUL = 0x7E,
    WASM_I64_DIV_S = 0x7F,
    WASM_I64_DIV_U = 0x80,
    WASM_I64_REM_S = 0x81,
    WASM_I64_REM_U = 0x82,
    WASM_I64_XOR = 0x85,
    WASM_F32_ADD = 0x92,
    WASM_F32_SUB = 0x93,
    WASM_F32_MUL = 0x94,
    WASM_F32_DIV = 0x95,
    WASM_F64_ADD = 0xA0,
    WASM_F64_SUB = 0xA1,
    WASM_F64_MUL = 0xA2,
    WASM_F64_DIV = 0xA3
};

void WasmCodeGen::emit_leb128(int64_t value) {
    bool more = true;
    while (more) {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        
        if ((value == 0 && (byte & 0x40) == 0) || (value == -1 && (byte & 0x40) != 0)) {
            more = false;
        } else {
            byte |= 0x80;
        }
        
        code.push_back(byte);
    }
}

void WasmCodeGen::emit_opcode(uint8_t opcode) {
    code.push_back(opcode);
}

void WasmCodeGen::emit_prologue() {
    current_local_count = 0;
}

void WasmCodeGen::emit_epilogue() {
    emit_opcode(WASM_END);
}

void WasmCodeGen::emit_mov_reg_imm(int reg, int64_t value) {
    emit_opcode(WASM_I64_CONST);
    emit_leb128(value);
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(reg);
}

void WasmCodeGen::emit_mov_reg_reg(int dst, int src) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(src);
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(dst);
}

void WasmCodeGen::emit_mov_mem_reg(int64_t offset, int reg) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(reg);
    emit_opcode(WASM_I64_STORE);
    emit_leb128(3);
    emit_leb128(offset);
}

void WasmCodeGen::emit_mov_reg_mem(int reg, int64_t offset) {
    emit_opcode(WASM_I32_CONST);
    emit_leb128(offset);
    emit_opcode(WASM_I64_LOAD);
    emit_leb128(3);
    emit_leb128(0);
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(reg);
}

void WasmCodeGen::emit_add_reg_imm(int reg, int64_t value) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(reg);
    emit_opcode(WASM_I64_CONST);
    emit_leb128(value);
    emit_opcode(WASM_I64_ADD);
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(reg);
}

void WasmCodeGen::emit_add_reg_reg(int dst, int src) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(dst);
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(src);
    emit_opcode(WASM_I64_ADD);
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(dst);
}

void WasmCodeGen::emit_sub_reg_imm(int reg, int64_t value) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(reg);
    emit_opcode(WASM_I64_CONST);
    emit_leb128(value);
    emit_opcode(WASM_I64_SUB);
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(reg);
}

void WasmCodeGen::emit_sub_reg_reg(int dst, int src) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(dst);
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(src);
    emit_opcode(WASM_I64_SUB);
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(dst);
}

void WasmCodeGen::emit_mul_reg_reg(int dst, int src) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(dst);
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(src);
    emit_opcode(WASM_I64_MUL);
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(dst);
}

void WasmCodeGen::emit_div_reg_reg(int dst, int src) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(dst);
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(src);
    emit_opcode(WASM_I64_DIV_S);
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(dst);
}

void WasmCodeGen::emit_mod_reg_reg(int dst, int src) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(dst);
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(src);
    emit_opcode(WASM_I64_REM_S);  // Remainder (modulo) operation
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(dst);
}

void WasmCodeGen::emit_call(const std::string& label) {
    emit_opcode(WASM_CALL);
    
    auto it = label_offsets.find(label);
    if (it != label_offsets.end()) {
        emit_leb128(it->second);
    } else {
        unresolved_jumps.push_back({label, code.size()});
        emit_leb128(0);
    }
}

void WasmCodeGen::emit_ret() {
    emit_opcode(WASM_RETURN);
}

void WasmCodeGen::emit_function_return() {
    // For WASM, same as ret for now
    emit_ret();
}

void WasmCodeGen::emit_jump(const std::string& label) {
    emit_opcode(WASM_BR);
    
    auto it = label_offsets.find(label);
    if (it != label_offsets.end()) {
        emit_leb128(it->second);
    } else {
        unresolved_jumps.push_back({label, code.size()});
        emit_leb128(0);
    }
}

void WasmCodeGen::emit_jump_if_zero(const std::string& label) {
    emit_opcode(WASM_I32_CONST);
    emit_leb128(0);
    emit_opcode(WASM_BR_IF);
    
    auto it = label_offsets.find(label);
    if (it != label_offsets.end()) {
        emit_leb128(it->second);
    } else {
        unresolved_jumps.push_back({label, code.size()});
        emit_leb128(0);
    }
}

void WasmCodeGen::emit_jump_if_not_zero(const std::string& label) {
    emit_opcode(WASM_I32_CONST);
    emit_leb128(0);
    emit_opcode(WASM_BR_IF);
    
    auto it = label_offsets.find(label);
    if (it != label_offsets.end()) {
        emit_leb128(it->second);
    } else {
        unresolved_jumps.push_back({label, code.size()});
        emit_leb128(0);
    }
}

void WasmCodeGen::emit_compare(int reg1, int reg2) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(reg1);
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(reg2);
}

void WasmCodeGen::emit_setl(int reg) {
    throw std::runtime_error("Not implemented for WebAssembly backend");
}

void WasmCodeGen::emit_setg(int reg) {
    throw std::runtime_error("Not implemented for WebAssembly backend");
}

void WasmCodeGen::emit_sete(int reg) {
    throw std::runtime_error("Not implemented for WebAssembly backend");
}

void WasmCodeGen::emit_setne(int reg) {
    throw std::runtime_error("Not implemented for WebAssembly backend");
}

void WasmCodeGen::emit_setle(int reg) {
    throw std::runtime_error("Not implemented for WebAssembly backend");
}

void WasmCodeGen::emit_setge(int reg) {
    throw std::runtime_error("Not implemented for WebAssembly backend");
}

void WasmCodeGen::emit_and_reg_imm(int reg, int64_t value) {
    throw std::runtime_error("Not implemented for WebAssembly backend");
}

void WasmCodeGen::emit_label(const std::string& label) {
    label_offsets[label] = code.size();
    
    for (auto& jump : unresolved_jumps) {
        if (jump.first == label) {
            size_t offset_pos = jump.second;
            int64_t function_index = code.size();
            
            size_t i = offset_pos;
            while (i < code.size() && (code[i] & 0x80)) {
                i++;
            }
            if (i < code.size()) {
                code.erase(code.begin() + offset_pos, code.begin() + i + 1);
                auto pos = code.begin() + offset_pos;
                
                bool more = true;
                while (more) {
                    uint8_t byte = function_index & 0x7F;
                    function_index >>= 7;
                    
                    if (function_index == 0) {
                        more = false;
                    } else {
                        byte |= 0x80;
                    }
                    
                    code.insert(pos++, byte);
                }
            }
        }
    }
    
    unresolved_jumps.erase(
        std::remove_if(unresolved_jumps.begin(), unresolved_jumps.end(),
                      [&label](const std::pair<std::string, int64_t>& jump) {
                          return jump.first == label;
                      }),
        unresolved_jumps.end());
}

void WasmCodeGen::emit_goroutine_spawn(const std::string& function_name) {
    emit_call("__goroutine_spawn");
}

void WasmCodeGen::emit_goroutine_spawn_with_args(const std::string& function_name, int arg_count) {
    // TODO: Implement goroutine spawn with arguments for WebAssembly
    emit_call("__goroutine_spawn_with_args");
}

void WasmCodeGen::emit_goroutine_spawn_with_func_ptr() {
    emit_call("__goroutine_spawn_func_ptr");
}

void WasmCodeGen::emit_goroutine_spawn_with_func_id() {
    emit_call("__goroutine_spawn_func_id");
}

void WasmCodeGen::emit_promise_resolve(int value_reg) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(value_reg);
    emit_call("__promise_resolve");
}

void WasmCodeGen::emit_promise_await(int promise_reg) {
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(promise_reg);
    emit_call("__promise_await");
}

void WasmCodeGen::emit_xor_reg_reg(int dst, int src) {
    // WebAssembly XOR: load both operands and XOR them
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(dst);
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(src);
    emit_opcode(WASM_I64_XOR);  // Assuming 64-bit XOR
    emit_opcode(WASM_LOCAL_SET);
    emit_leb128(dst);
}

void WasmCodeGen::emit_call_reg(int reg) {
    // WebAssembly indirect call through function table
    emit_opcode(WASM_LOCAL_GET);
    emit_leb128(reg);
    emit_opcode(WASM_CALL_INDIRECT);
    emit_leb128(0); // type index
    emit_leb128(0); // table index
}

void WasmCodeGen::emit_goroutine_spawn_with_address(void* function_address) {
    // TODO: Implement WebAssembly goroutine spawn with address
    (void)function_address;
}

size_t WasmCodeGen::get_current_offset() const {
    return code.size();
}

}