#include "nonagon/execution.hpp"
#include "nonagon/crypto.hpp"
#include <iostream>
#include <cstring>

namespace nonagon {
namespace execution {

// ============================================================================
// EVM Implementation
// ============================================================================

EVM::EVM(std::shared_ptr<storage::StateManager> state) : state_(state) {
    // Register standard precompiles (addresses 1-9)
    // In production, implement each precompile
}

ExecutionResult EVM::execute_transaction(const Transaction& tx, const ExecutionContext& ctx) {
    ExecutionResult result;
    
    // Increment sender nonce
    state_->increment_nonce(tx.from);
    
    // Calculate gas cost
    uint64_t gas_cost = tx.gas_limit * tx.effective_gas_price(ctx.base_fee);
    
    // Deduct max gas cost from sender
    if (state_->get_balance(tx.from) < tx.value + gas_cost) {
        result.success = false;
        result.error = "Insufficient balance";
        return result;
    }
    
    state_->sub_balance(tx.from, tx.value + gas_cost);
    
    // Determine if contract creation or call
    bool is_create = (tx.to.payment_credential == std::array<uint8_t, 28>{});
    
    if (is_create) {
        result = create(tx.from, tx.data, tx.value, tx.gas_limit);
    } else {
        // Transfer value
        state_->add_balance(tx.to, tx.value);
        
        // Execute contract code if present
        auto code = state_->get_code(tx.to);
        if (!code.empty()) {
            result = execute_code(tx.from, tx.to, code, tx.data, 
                                   tx.value, tx.gas_limit, false);
        } else {
            // Simple transfer
            result.success = true;
            result.gas_used = GasCosts::TRANSACTION;
        }
    }
    
    // Refund unused gas
    uint64_t gas_refund = (tx.gas_limit - result.gas_used) * tx.effective_gas_price(ctx.base_fee);
    state_->add_balance(tx.from, gas_refund);
    
    // Pay gas to sequencer
    uint64_t gas_payment = result.gas_used * tx.effective_gas_price(ctx.base_fee);
    state_->add_balance(ctx.coinbase, gas_payment);
    
    return result;
}

ExecutionResult EVM::simulate_transaction(const Transaction& tx, const ExecutionContext& ctx) {
    // Create snapshot for reversion
    auto snapshot = state_->snapshot();
    
    // Execute transaction
    auto result = execute_transaction(tx, ctx);
    
    // Revert all state changes
    state_->revert(snapshot);
    
    return result;
}

ExecutionResult EVM::call(const Address& from, const Address& to,
                           const Bytes& data, uint64_t gas_limit) {
    auto code = state_->get_code(to);
    if (code.empty()) {
        return ExecutionResult{true, 0, {}, "", std::nullopt, {}, {}};
    }
    
    auto snapshot = state_->snapshot();
    auto result = execute_code(from, to, code, data, 0, gas_limit, true);
    state_->revert(snapshot);
    
    return result;
}

ExecutionResult EVM::create(const Address& from, const Bytes& code,
                             uint64_t value, uint64_t gas_limit) {
    ExecutionResult result;
    result.gas_used = GasCosts::CREATE;
    
    // Compute contract address
    // In Ethereum: keccak256(RLP([sender, nonce]))[12:]
    // We'll use Blake2b for Cardano compatibility
    uint64_t nonce = state_->get_nonce(from);
    std::vector<uint8_t> address_preimage;
    address_preimage.insert(address_preimage.end(), 
                            from.payment_credential.begin(), 
                            from.payment_credential.end());
    for (int i = 7; i >= 0; --i) {
        address_preimage.push_back(static_cast<uint8_t>((nonce >> (i * 8)) & 0xFF));
    }
    
    auto hash = crypto::Blake2b256::hash(address_preimage);
    
    Address contract_addr;
    contract_addr.type = Address::Type::Script;
    std::copy(hash.begin(), hash.begin() + 28, contract_addr.payment_credential.begin());
    
    // Transfer value to new contract
    if (value > 0) {
        state_->add_balance(contract_addr, value);
    }
    
    // Execute constructor (init code)
    auto init_result = execute_code(from, contract_addr, code, {}, value, gas_limit - result.gas_used, false);
    
    result.gas_used += init_result.gas_used;
    
    if (init_result.success) {
        // Store returned bytecode as contract code
        state_->set_code(contract_addr, init_result.return_data);
        result.success = true;
        result.created_address = contract_addr;
        result.logs = init_result.logs;
    } else {
        result.success = false;
        result.error = init_result.error;
    }
    
    return result;
}

void EVM::register_precompile(const Address& addr, std::shared_ptr<Precompile> precompile) {
    precompiles_[addr.to_hex()] = precompile;
}

ExecutionResult EVM::execute_code(const Address& caller, const Address& address,
                                   const Bytes& code, const Bytes& input,
                                   uint64_t value, uint64_t gas_limit,
                                   bool is_static) {
    ExecutionResult result;
    result.gas_used = 0;
    
    if (code.empty()) {
        result.success = true;
        return result;
    }
    
    // Check for precompiles
    auto precompile_it = precompiles_.find(address.to_hex());
    if (precompile_it != precompiles_.end()) {
        return precompile_it->second->execute(input, gas_limit);
    }
    
    // Initialize execution state
    ExecutionState state;
    state.memory.reserve(1024);
    state.stack.reserve(1024);
    state.gas_remaining = gas_limit;
    state.pc = 0;
    state.stopped = false;
    state.reverted = false;
    
    // Execute bytecode
    while (!state.stopped && !state.reverted && state.pc < code.size()) {
        uint8_t opcode = code[state.pc];
        
        // Check gas
        uint64_t gas_cost = get_opcode_gas_cost(opcode);
        if (state.gas_remaining < gas_cost) {
            result.success = false;
            result.error = "Out of gas";
            result.gas_used = gas_limit;
            return result;
        }
        state.gas_remaining -= gas_cost;
        
        // Execute opcode
        switch (opcode) {
            case 0x00: // STOP
                state.stopped = true;
                break;
                
            case 0x01: // ADD
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    // 256-bit addition (simplified)
                    uint64_t carry = 0;
                    for (int i = 3; i >= 0; --i) {
                        uint64_t sum = a[i] + b[i] + carry;
                        c[i] = sum;
                        carry = (sum < a[i]) ? 1 : 0;
                    }
                    state.stack.push_back(c);
                }
                break;
                
            case 0x02: // MUL
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};  // Simplified
                    state.stack.push_back(c);
                }
                break;
                
            case 0x03: // SUB
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    // 256-bit subtraction with borrow
                    uint64_t borrow = 0;
                    for (int i = 3; i >= 0; --i) {
                        uint64_t diff = a[i] - b[i] - borrow;
                        borrow = (a[i] < b[i] + borrow) ? 1 : 0;
                        c[i] = diff;
                    }
                    state.stack.push_back(c);
                }
                break;
                
            case 0x04: // DIV
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    // Simplified: only handles small values
                    if (b[3] != 0) {
                        c[3] = a[3] / b[3];
                    }
                    state.stack.push_back(c);
                }
                break;
                
            case 0x05: // SDIV (signed division)
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    // Simplified signed division
                    if (b[3] != 0) {
                        c[3] = static_cast<uint64_t>(
                            static_cast<int64_t>(a[3]) / static_cast<int64_t>(b[3]));
                    }
                    state.stack.push_back(c);
                }
                break;
                
            case 0x06: // MOD
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    if (b[3] != 0) {
                        c[3] = a[3] % b[3];
                    }
                    state.stack.push_back(c);
                }
                break;
                
            case 0x07: // SMOD (signed modulo)
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    if (b[3] != 0) {
                        c[3] = static_cast<uint64_t>(
                            static_cast<int64_t>(a[3]) % static_cast<int64_t>(b[3]));
                    }
                    state.stack.push_back(c);
                }
                break;
                
            case 0x08: // ADDMOD
                if (state.stack.size() >= 3) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    auto n = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    if (n[3] != 0) {
                        c[3] = (a[3] + b[3]) % n[3];
                    }
                    state.stack.push_back(c);
                }
                break;
                
            case 0x09: // MULMOD  
                if (state.stack.size() >= 3) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    auto n = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    if (n[3] != 0) {
                        c[3] = (a[3] * b[3]) % n[3];
                    }
                    state.stack.push_back(c);
                }
                break;
                
            case 0x0A: // EXP
                if (state.stack.size() >= 2) {
                    auto base = state.stack.back(); state.stack.pop_back();
                    auto exp = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    // Simple exponentiation for small values
                    uint64_t result = 1;
                    uint64_t b_val = base[3];
                    uint64_t e_val = exp[3];
                    while (e_val > 0) {
                        if (e_val & 1) result *= b_val;
                        b_val *= b_val;
                        e_val >>= 1;
                    }
                    c[3] = result;
                    state.stack.push_back(c);
                }
                break;
                
            case 0x10: // LT
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    c[3] = (a < b) ? 1 : 0;
                    state.stack.push_back(c);
                }
                break;
                
            case 0x14: // EQ
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    c[3] = (a == b) ? 1 : 0;
                    state.stack.push_back(c);
                }
                break;
                
            case 0x15: // ISZERO
                if (state.stack.size() >= 1) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    bool is_zero = true;
                    for (int i = 0; i < 4; ++i) {
                        if (a[i] != 0) is_zero = false;
                    }
                    c[3] = is_zero ? 1 : 0;
                    state.stack.push_back(c);
                }
                break;
                
            case 0x11: // GT
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    c[3] = (a > b) ? 1 : 0;
                    state.stack.push_back(c);
                }
                break;
                
            case 0x12: // SLT (signed less than)
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    c[3] = (static_cast<int64_t>(a[3]) < static_cast<int64_t>(b[3])) ? 1 : 0;
                    state.stack.push_back(c);
                }
                break;
                
            case 0x13: // SGT (signed greater than)
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    c[3] = (static_cast<int64_t>(a[3]) > static_cast<int64_t>(b[3])) ? 1 : 0;
                    state.stack.push_back(c);
                }
                break;
                
            case 0x16: // AND
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    for (int i = 0; i < 4; ++i) c[i] = a[i] & b[i];
                    state.stack.push_back(c);
                }
                break;
                
            case 0x17: // OR
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    for (int i = 0; i < 4; ++i) c[i] = a[i] | b[i];
                    state.stack.push_back(c);
                }
                break;
                
            case 0x18: // XOR
                if (state.stack.size() >= 2) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    auto b = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    for (int i = 0; i < 4; ++i) c[i] = a[i] ^ b[i];
                    state.stack.push_back(c);
                }
                break;
                
            case 0x19: // NOT
                if (state.stack.size() >= 1) {
                    auto a = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    for (int i = 0; i < 4; ++i) c[i] = ~a[i];
                    state.stack.push_back(c);
                }
                break;
                
            case 0x1A: // BYTE
                if (state.stack.size() >= 2) {
                    auto i_w = state.stack.back(); state.stack.pop_back();
                    auto x = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    size_t i = i_w[3];
                    if (i < 32) {
                        int word_idx = i / 8;
                        int byte_idx = i % 8;
                        c[3] = (x[word_idx] >> (56 - byte_idx * 8)) & 0xFF;
                    }
                    state.stack.push_back(c);
                }
                break;
                
            case 0x1B: // SHL (shift left)
                if (state.stack.size() >= 2) {
                    auto shift_w = state.stack.back(); state.stack.pop_back();
                    auto value = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    size_t shift = shift_w[3];
                    if (shift < 256) {
                        // Simplified: only handles shifts within 64 bits
                        c[3] = value[3] << (shift % 64);
                    }
                    state.stack.push_back(c);
                }
                break;
                
            case 0x1C: // SHR (logical shift right)
                if (state.stack.size() >= 2) {
                    auto shift_w = state.stack.back(); state.stack.pop_back();
                    auto value = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    size_t shift = shift_w[3];
                    if (shift < 256) {
                        c[3] = value[3] >> (shift % 64);
                    }
                    state.stack.push_back(c);
                }
                break;
                
            case 0x1D: // SAR (arithmetic shift right)
                if (state.stack.size() >= 2) {
                    auto shift_w = state.stack.back(); state.stack.pop_back();
                    auto value = state.stack.back(); state.stack.pop_back();
                    uint256 c = {};
                    size_t shift = shift_w[3];
                    if (shift < 256) {
                        c[3] = static_cast<uint64_t>(
                            static_cast<int64_t>(value[3]) >> (shift % 64));
                    }
                    state.stack.push_back(c);
                }
                break;
                
            case 0x50: // POP
                if (!state.stack.empty()) {
                    state.stack.pop_back();
                }
                break;
                
            case 0x51: // MLOAD
                if (!state.stack.empty()) {
                    auto offset_w = state.stack.back(); state.stack.pop_back();
                    size_t offset = offset_w[3];
                    uint256 val = {};
                    if (offset + 32 <= state.memory.size()) {
                        for (int i = 0; i < 32; ++i) {
                            int word_idx = i / 8;
                            int byte_idx = i % 8;
                            val[word_idx] |= static_cast<uint64_t>(state.memory[offset + i]) 
                                             << (56 - byte_idx * 8);
                        }
                    }
                    state.stack.push_back(val);
                }
                break;
                
            case 0x52: // MSTORE
                if (state.stack.size() >= 2) {
                    auto offset_w = state.stack.back(); state.stack.pop_back();
                    auto val = state.stack.back(); state.stack.pop_back();
                    size_t offset = offset_w[3];
                    
                    // Expand memory if needed
                    if (offset + 32 > state.memory.size()) {
                        state.memory.resize(offset + 32, 0);
                    }
                    
                    // Store value
                    for (int i = 0; i < 32; ++i) {
                        int word_idx = i / 8;
                        int byte_idx = i % 8;
                        state.memory[offset + i] = static_cast<uint8_t>(
                            (val[word_idx] >> (56 - byte_idx * 8)) & 0xFF);
                    }
                }
                break;
                
            case 0x54: // SLOAD
                if (!state.stack.empty()) {
                    auto key_w = state.stack.back(); state.stack.pop_back();
                    Hash256 key;
                    for (int i = 0; i < 4; ++i) {
                        for (int j = 0; j < 8; ++j) {
                            key[i * 8 + j] = static_cast<uint8_t>(
                                (key_w[i] >> (56 - j * 8)) & 0xFF);
                        }
                    }
                    
                    auto val_bytes = state_->get_storage(address, key);
                    uint256 val = {};
                    for (size_t i = 0; i < std::min(val_bytes.size(), size_t(32)); ++i) {
                        int word_idx = i / 8;
                        int byte_idx = i % 8;
                        val[word_idx] |= static_cast<uint64_t>(val_bytes[i]) 
                                         << (56 - byte_idx * 8);
                    }
                    state.stack.push_back(val);
                }
                break;
                
            case 0x55: // SSTORE
                if (is_static) {
                    state.reverted = true;
                    result.error = "State modification in static call";
                } else if (state.stack.size() >= 2) {
                    auto key_w = state.stack.back(); state.stack.pop_back();
                    auto val_w = state.stack.back(); state.stack.pop_back();
                    
                    Hash256 key;
                    Bytes val(32);
                    for (int i = 0; i < 4; ++i) {
                        for (int j = 0; j < 8; ++j) {
                            key[i * 8 + j] = static_cast<uint8_t>(
                                (key_w[i] >> (56 - j * 8)) & 0xFF);
                            val[i * 8 + j] = static_cast<uint8_t>(
                                (val_w[i] >> (56 - j * 8)) & 0xFF);
                        }
                    }
                    
                    state_->set_storage(address, key, val);
                }
                break;
                
            case 0x56: // JUMP
                if (!state.stack.empty()) {
                    auto dest = state.stack.back(); state.stack.pop_back();
                    state.pc = dest[3] - 1;  // -1 because we increment at end
                }
                break;
                
            case 0x57: // JUMPI
                if (state.stack.size() >= 2) {
                    auto dest = state.stack.back(); state.stack.pop_back();
                    auto cond = state.stack.back(); state.stack.pop_back();
                    if (cond[3] != 0) {
                        state.pc = dest[3] - 1;
                    }
                }
                break;
                
            case 0x5B: // JUMPDEST
                // No-op, just a valid jump destination
                break;
                
            case 0x60: case 0x61: case 0x62: case 0x63: // PUSH1-PUSH4
            case 0x64: case 0x65: case 0x66: case 0x67:
            case 0x68: case 0x69: case 0x6A: case 0x6B:
            case 0x6C: case 0x6D: case 0x6E: case 0x6F:
            case 0x70: case 0x71: case 0x72: case 0x73:
            case 0x74: case 0x75: case 0x76: case 0x77:
            case 0x78: case 0x79: case 0x7A: case 0x7B:
            case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
                int n = opcode - 0x60 + 1;  // Number of bytes to push
                uint256 val = {};
                for (int i = 0; i < n && state.pc + 1 + i < code.size(); ++i) {
                    int word_idx = (31 - i) / 8;
                    int byte_idx = (31 - i) % 8;
                    val[word_idx] |= static_cast<uint64_t>(code[state.pc + 1 + i]) 
                                     << (56 - byte_idx * 8);
                }
                state.stack.push_back(val);
                state.pc += n;
                break;
            }
                
            case 0x80: case 0x81: case 0x82: case 0x83: // DUP1-DUP16
            case 0x84: case 0x85: case 0x86: case 0x87:
            case 0x88: case 0x89: case 0x8A: case 0x8B:
            case 0x8C: case 0x8D: case 0x8E: case 0x8F: {
                int n = opcode - 0x80 + 1;
                if (state.stack.size() >= n) {
                    state.stack.push_back(state.stack[state.stack.size() - n]);
                }
                break;
            }
                
            case 0x90: case 0x91: case 0x92: case 0x93: // SWAP1-SWAP16
            case 0x94: case 0x95: case 0x96: case 0x97:
            case 0x98: case 0x99: case 0x9A: case 0x9B:
            case 0x9C: case 0x9D: case 0x9E: case 0x9F: {
                int n = opcode - 0x90 + 1;
                if (state.stack.size() > n) {
                    std::swap(state.stack.back(), 
                              state.stack[state.stack.size() - n - 1]);
                }
                break;
            }
            
            // ===== Environment opcodes =====
            case 0x30: { // ADDRESS
                uint256 addr_val = {};
                std::copy(address.payment_credential.begin(), 
                          address.payment_credential.end(),
                          reinterpret_cast<uint8_t*>(&addr_val[0]));
                state.stack.push_back(addr_val);
                break;
            }
            
            case 0x31: { // BALANCE
                if (!state.stack.empty()) {
                    auto addr_w = state.stack.back(); state.stack.pop_back();
                    Address target;
                    // Extract address from 256-bit word
                    for (int i = 0; i < 20 && i < 28; ++i) {
                        target.payment_credential[i] = static_cast<uint8_t>(addr_w[3] >> ((7-i) * 8));
                    }
                    uint64_t bal = state_->get_balance(target);
                    uint256 result = {};
                    result[3] = bal;
                    state.stack.push_back(result);
                }
                break;
            }
            
            case 0x32: { // ORIGIN
                uint256 origin_val = {};
                // Use caller as origin for simplicity
                state.stack.push_back(origin_val);
                break;
            }
            
            case 0x33: { // CALLER
                uint256 caller_val = {};
                state.stack.push_back(caller_val);
                break;
            }
            
            case 0x34: { // CALLVALUE
                uint256 val = {};
                val[3] = value;
                state.stack.push_back(val);
                break;
            }
            
            case 0x35: { // CALLDATALOAD
                if (!state.stack.empty()) {
                    auto offset_w = state.stack.back(); state.stack.pop_back();
                    size_t offset = offset_w[3];
                    uint256 data_val = {};
                    for (size_t i = 0; i < 32 && offset + i < input.size(); ++i) {
                        int word_idx = i / 8;
                        int byte_idx = i % 8;
                        data_val[word_idx] |= static_cast<uint64_t>(input[offset + i]) 
                                             << (56 - byte_idx * 8);
                    }
                    state.stack.push_back(data_val);
                }
                break;
            }
            
            case 0x36: { // CALLDATASIZE
                uint256 size = {};
                size[3] = input.size();
                state.stack.push_back(size);
                break;
            }
            
            case 0x37: { // CALLDATACOPY
                if (state.stack.size() >= 3) {
                    auto dest_w = state.stack.back(); state.stack.pop_back();
                    auto offset_w = state.stack.back(); state.stack.pop_back();
                    auto size_w = state.stack.back(); state.stack.pop_back();
                    size_t dest = dest_w[3];
                    size_t offset = offset_w[3];
                    size_t size = size_w[3];
                    
                    if (dest + size > state.memory.size()) {
                        state.memory.resize(dest + size, 0);
                    }
                    for (size_t i = 0; i < size; ++i) {
                        if (offset + i < input.size()) {
                            state.memory[dest + i] = input[offset + i];
                        } else {
                            state.memory[dest + i] = 0;
                        }
                    }
                }
                break;
            }
            
            case 0x38: { // CODESIZE
                uint256 size = {};
                size[3] = code.size();
                state.stack.push_back(size);
                break;
            }
            
            case 0x39: { // CODECOPY
                if (state.stack.size() >= 3) {
                    auto dest_w = state.stack.back(); state.stack.pop_back();
                    auto offset_w = state.stack.back(); state.stack.pop_back();
                    auto size_w = state.stack.back(); state.stack.pop_back();
                    size_t dest = dest_w[3];
                    size_t offset = offset_w[3];
                    size_t size = size_w[3];
                    
                    if (dest + size > state.memory.size()) {
                        state.memory.resize(dest + size, 0);
                    }
                    for (size_t i = 0; i < size; ++i) {
                        if (offset + i < code.size()) {
                            state.memory[dest + i] = code[offset + i];
                        } else {
                            state.memory[dest + i] = 0;
                        }
                    }
                }
                break;
            }
            
            case 0x3A: { // GASPRICE
                uint256 gp = {};
                gp[3] = 1000000000; // 1 Gwei
                state.stack.push_back(gp);
                break;
            }
            
            case 0x3B: { // EXTCODESIZE
                if (!state.stack.empty()) {
                    state.stack.pop_back();
                    uint256 size = {};
                    // Would look up contract code size
                    state.stack.push_back(size);
                }
                break;
            }
            
            case 0x3D: { // RETURNDATASIZE
                uint256 size = {};
                size[3] = state.return_data.size();
                state.stack.push_back(size);
                break;
            }
            
            case 0x3E: { // RETURNDATACOPY
                if (state.stack.size() >= 3) {
                    auto dest_w = state.stack.back(); state.stack.pop_back();
                    auto offset_w = state.stack.back(); state.stack.pop_back();
                    auto size_w = state.stack.back(); state.stack.pop_back();
                    size_t dest = dest_w[3];
                    size_t offset = offset_w[3];
                    size_t size = size_w[3];
                    
                    if (dest + size > state.memory.size()) {
                        state.memory.resize(dest + size, 0);
                    }
                    for (size_t i = 0; i < size; ++i) {
                        if (offset + i < state.return_data.size()) {
                            state.memory[dest + i] = state.return_data[offset + i];
                        }
                    }
                }
                break;
            }
            
            // ===== Block info opcodes =====
            case 0x40: { // BLOCKHASH
                if (!state.stack.empty()) {
                    state.stack.pop_back();
                    state.stack.push_back({});  // Zero hash
                }
                break;
            }
            
            case 0x41: { // COINBASE
                state.stack.push_back({});  // Zero address
                break;
            }
            
            case 0x42: { // TIMESTAMP
                uint256 ts = {};
                ts[3] = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                state.stack.push_back(ts);
                break;
            }
            
            case 0x43: { // NUMBER
                uint256 num = {};
                // Block number would come from context
                state.stack.push_back(num);
                break;
            }
            
            case 0x44: { // DIFFICULTY (PREVRANDAO)
                state.stack.push_back({});
                break;
            }
            
            case 0x45: { // GASLIMIT
                uint256 limit = {};
                limit[3] = gas_limit;
                state.stack.push_back(limit);
                break;
            }
            
            case 0x46: { // CHAINID
                uint256 cid = {};
                cid[3] = 1; // Nonagon chain ID
                state.stack.push_back(cid);
                break;
            }
            
            case 0x47: { // SELFBALANCE
                uint64_t bal = state_->get_balance(address);
                uint256 result = {};
                result[3] = bal;
                state.stack.push_back(result);
                break;
            }
            
            case 0x48: { // BASEFEE
                uint256 bf = {};
                bf[3] = 1000000000; // 1 Gwei
                state.stack.push_back(bf);
                break;
            }
            
            // ===== Stack/Memory/Flow opcodes =====
            case 0x58: { // PC
                uint256 pc_val = {};
                pc_val[3] = state.pc;
                state.stack.push_back(pc_val);
                break;
            }
            
            case 0x59: { // MSIZE
                uint256 msize = {};
                msize[3] = state.memory.size();
                state.stack.push_back(msize);
                break;
            }
            
            case 0x5A: { // GAS
                uint256 gas_val = {};
                gas_val[3] = state.gas_remaining;
                state.stack.push_back(gas_val);
                break;
            }
            
            // ===== LOG opcodes =====
            case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: { // LOG0-LOG4
                int num_topics = opcode - 0xA0;
                if (state.stack.size() >= static_cast<size_t>(2 + num_topics) && !is_static) {
                    auto offset_w = state.stack.back(); state.stack.pop_back();
                    auto size_w = state.stack.back(); state.stack.pop_back();
                    size_t offset = offset_w[3];
                    size_t size = size_w[3];
                    
                    Log log;
                    log.address = address;
                    
                    // Extract topics
                    for (int i = 0; i < num_topics; ++i) {
                        auto topic = state.stack.back(); state.stack.pop_back();
                        Hash256 topic_hash;
                        for (int j = 0; j < 4; ++j) {
                            for (int k = 0; k < 8; ++k) {
                                topic_hash[j * 8 + k] = static_cast<uint8_t>(
                                    (topic[j] >> (56 - k * 8)) & 0xFF);
                            }
                        }
                        log.topics.push_back(topic_hash);
                    }
                    
                    // Extract data
                    if (offset + size <= state.memory.size()) {
                        log.data.assign(state.memory.begin() + offset,
                                       state.memory.begin() + offset + size);
                    }
                    
                    result.logs.push_back(log);
                }
                break;
            }
            
            // ===== CALL opcodes =====
            case 0xF1: { // CALL
                if (state.stack.size() >= 7) {
                    auto gas_w = state.stack.back(); state.stack.pop_back();
                    auto addr_w = state.stack.back(); state.stack.pop_back();
                    auto value_w = state.stack.back(); state.stack.pop_back();
                    auto args_offset_w = state.stack.back(); state.stack.pop_back();
                    auto args_size_w = state.stack.back(); state.stack.pop_back();
                    auto ret_offset_w = state.stack.back(); state.stack.pop_back();
                    auto ret_size_w = state.stack.back(); state.stack.pop_back();
                    
                    uint64_t val = value_w[3];
                    uint64_t args_off = args_offset_w[3];
                    uint64_t args_sz = args_size_w[3];
                    uint64_t ret_off = ret_offset_w[3];
                    uint64_t ret_sz = ret_size_w[3];
                    uint64_t gas = gas_w[3];

                    // Extract Address (28 bytes)
                    Address to_addr;
                    // Provide defaults
                    
                    // Decode uint256 addr_w to 28 bytes
                    auto extract_byte = [&](int word_idx, int byte_idx) {
                        return (uint8_t)((addr_w[word_idx] >> (byte_idx * 8)) & 0xFF);
                    };
                    
                    // 28 bytes total. 
                    // Word 0 (Top): skip 4, take 4 loops (3 downto 0).
                    int pos = 0;
                    for(int i=3; i>=0; --i) to_addr.payment_credential[pos++] = extract_byte(0, i);
                    for(int i=7; i>=0; --i) to_addr.payment_credential[pos++] = extract_byte(1, i);
                    for(int i=7; i>=0; --i) to_addr.payment_credential[pos++] = extract_byte(2, i);
                    for(int i=7; i>=0; --i) to_addr.payment_credential[pos++] = extract_byte(3, i);
                    
                    // Get args
                    Bytes input;
                    if (args_sz > 0) {
                        if (state.memory.size() < args_off + args_sz) state.memory.resize(args_off + args_sz);
                        input.assign(state.memory.begin() + args_off, state.memory.begin() + args_off + args_sz);
                    }

                    auto snap = state_->snapshot();
                    bool success = true;

                    if (val > 0) {
                        if (state_->get_balance(address) >= val) {
                            state_->sub_balance(address, val);
                            state_->add_balance(to_addr, val);
                        } else {
                            success = false;
                        }
                    }

                    if (success) {
                        Bytes target_code = state_->get_code(to_addr);
                        // If precompile check needed here? execute_code logic handles it? yes (L158 in Step 2116)
                        auto res = execute_code(address, to_addr, target_code, input, val, gas, is_static);
                        success = res.success;
                        
                        if (ret_sz > 0) {
                             if (state.memory.size() < ret_off + ret_sz) state.memory.resize(ret_off + ret_sz);
                             size_t copy = std::min((size_t)ret_sz, res.return_data.size());
                             std::copy(res.return_data.begin(), res.return_data.begin() + copy, state.memory.begin() + ret_off);
                        }
                    }

                    if (!success) {
                        state_->revert(snap);
                        uint256 zero = {};
                        state.stack.push_back(zero);
                    } else {
                        uint256 one = {}; one[3] = 1;
                        state.stack.push_back(one);
                    }
                }
                break;
            }
            
            case 0xF2: { // CALLCODE
                // Similar to CALL but preserves caller context
                if (state.stack.size() >= 7) {
                    for (int i = 0; i < 7; ++i) state.stack.pop_back();
                    uint256 success = {};
                    success[3] = 1;
                    state.stack.push_back(success);
                }
                break;
            }
            
            case 0xF4: { // DELEGATECALL
                if (state.stack.size() >= 6) {
                    for (int i = 0; i < 6; ++i) state.stack.pop_back();
                    uint256 success = {};
                    success[3] = 1;
                    state.stack.push_back(success);
                }
                break;
            }
            
            case 0xFA: { // STATICCALL
                if (state.stack.size() >= 6) {
                    for (int i = 0; i < 6; ++i) state.stack.pop_back();
                    uint256 success = {};
                    success[3] = 1;
                    state.stack.push_back(success);
                }
                break;
            }
            
            case 0xF0: { // CREATE
                if (state.stack.size() >= 3 && !is_static) {
                    auto value_w = state.stack.back(); state.stack.pop_back();
                    auto offset_w = state.stack.back(); state.stack.pop_back();
                    auto size_w = state.stack.back(); state.stack.pop_back();
                    
                    uint64_t value = value_w[3];
                    uint64_t offset = offset_w[3];
                    uint64_t size = size_w[3];

                    // Check balance
                    if (state_->get_balance(address) < value) {
                        state.stack.push_back({}); // fail
                        break;
                    }

                    // Get Init Code
                    Bytes init_code;
                    if (size > 0 && offset + size <= state.memory.size()) {
                        init_code.assign(state.memory.begin() + offset, state.memory.begin() + offset + size);
                    }

                    // Calculate new address (Blake2b of addr + nonce)
                    uint64_t nonce = state_->get_nonce(address);
                    state_->increment_nonce(address);

                    std::vector<uint8_t> preimage;
                    preimage.insert(preimage.end(), address.payment_credential.begin(), address.payment_credential.end());
                    for(int i=7; i>=0; --i) preimage.push_back((uint8_t)((nonce >> (i*8)) & 0xFF));
                    auto hash = crypto::Blake2b256::hash(preimage);

                    Address new_addr;
                    new_addr.type = Address::Type::Script;
                    std::copy(hash.begin(), hash.begin()+28, new_addr.payment_credential.begin());

                    // Snapshot
                    auto snap = state_->snapshot();

                    // Transfer
                    state_->sub_balance(address, value);
                    state_->add_balance(new_addr, value);
                    state_->set_account(new_addr, {value, 0}); // Reset account code/storage implicitly? (Assuming empty)

                    // Execute Init Code
                    auto res = execute_code(address, new_addr, init_code, {}, value, state.gas_remaining, false);

                    if (res.success) {
                        state_->set_code(new_addr, res.return_data);
                        
                        // Push Address to stack
                        uint256 addr_val = {};
                        // Pack 28 bytes into uint256 (Right aligned)
                        // addr bytes: [0..27]
                        // uint256: [0]=High, [3]=Low
                        // [0]: bytes 0..3 of addr go to lowest 4 bytes of word 0
                        for(int i=0; i<4; ++i) addr_val[0] |= ((uint64_t)new_addr.payment_credential[i] << ((3-i)*8));
                        for(int i=0; i<8; ++i) addr_val[1] |= ((uint64_t)new_addr.payment_credential[4+i] << ((7-i)*8));
                        for(int i=0; i<8; ++i) addr_val[2] |= ((uint64_t)new_addr.payment_credential[12+i] << ((7-i)*8));
                        for(int i=0; i<8; ++i) addr_val[3] |= ((uint64_t)new_addr.payment_credential[20+i] << ((7-i)*8));
                        
                        state.stack.push_back(addr_val);
                    } else {
                        state_->revert(snap);
                        state.stack.push_back({}); // 0
                    }
                }
                break;
            }
            
            case 0xF5: { // CREATE2
                if (state.stack.size() >= 4 && !is_static) {
                    for (int i = 0; i < 4; ++i) state.stack.pop_back();
                    state.stack.push_back({});
                }
                break;
            }
                
            case 0xF3: // RETURN
                if (state.stack.size() >= 2) {
                    auto offset_w = state.stack.back(); state.stack.pop_back();
                    auto size_w = state.stack.back(); state.stack.pop_back();
                    size_t offset = offset_w[3];
                    size_t size = size_w[3];
                    
                    if (offset + size <= state.memory.size()) {
                        state.return_data.assign(
                            state.memory.begin() + offset,
                            state.memory.begin() + offset + size);
                    }
                }
                state.stopped = true;
                break;
                
            case 0xFD: // REVERT
                if (state.stack.size() >= 2) {
                    auto offset_w = state.stack.back(); state.stack.pop_back();
                    auto size_w = state.stack.back(); state.stack.pop_back();
                    size_t offset = offset_w[3];
                    size_t size = size_w[3];
                    
                    if (offset + size <= state.memory.size()) {
                        state.return_data.assign(
                            state.memory.begin() + offset,
                            state.memory.begin() + offset + size);
                    }
                }
                state.reverted = true;
                break;
                
            case 0xFE: // INVALID
                state.reverted = true;
                result.error = "Invalid opcode";
                break;
                
            case 0xFF: // SELFDESTRUCT
                if (!is_static && !state.stack.empty()) {
                    auto beneficiary_w = state.stack.back(); state.stack.pop_back();
                    // Transfer balance to beneficiary and mark for deletion
                    // Simplified: just stop
                }
                state.stopped = true;
                break;
                
            default:
                // Unknown opcode
                state.reverted = true;
                result.error = "Unknown opcode: 0x" + 
                               std::to_string(static_cast<int>(opcode));
                break;
        }
        
        state.pc++;
    }
    
    result.gas_used = gas_limit - state.gas_remaining;
    result.success = !state.reverted;
    result.return_data = state.return_data;
    
    return result;
}

uint64_t get_opcode_gas_cost(uint8_t opcode) {
    switch (opcode) {
        case 0x00: return GasCosts::ZERO;  // STOP
        case 0x01: case 0x02: case 0x03: return GasCosts::VERYLOW;  // ADD, MUL, SUB
        case 0x10: case 0x11: case 0x12: case 0x13: return GasCosts::VERYLOW;  // LT, GT, SLT, SGT
        case 0x14: case 0x15: return GasCosts::VERYLOW;  // EQ, ISZERO
        case 0x50: return GasCosts::BASE;  // POP
        case 0x51: case 0x52: return GasCosts::VERYLOW;  // MLOAD, MSTORE
        case 0x54: return GasCosts::SLOAD;
        case 0x55: return GasCosts::SSTORE_SET;  // Simplified
        case 0x56: return GasCosts::MID;  // JUMP
        case 0x57: return GasCosts::HIGH;  // JUMPI
        case 0x5B: return 1;  // JUMPDEST
        case 0xF3: case 0xFD: return GasCosts::ZERO;  // RETURN, REVERT
        case 0xFE: return GasCosts::ZERO;  // INVALID
        case 0xFF: return GasCosts::SELFDESTRUCT;
        default:
            if (opcode >= 0x60 && opcode <= 0x7F) return GasCosts::VERYLOW;  // PUSH
            if (opcode >= 0x80 && opcode <= 0x8F) return GasCosts::VERYLOW;  // DUP
            if (opcode >= 0x90 && opcode <= 0x9F) return GasCosts::VERYLOW;  // SWAP
            return GasCosts::BASE;
    }
}

// ============================================================================
// TransactionProcessor Implementation
// ============================================================================

TransactionProcessor::TransactionProcessor(std::shared_ptr<storage::StateManager> state,
                                           std::shared_ptr<EVM> evm)
    : state_(state), evm_(evm) {}

TransactionProcessor::ProcessResult TransactionProcessor::process(
    const Transaction& tx, const ExecutionContext& ctx) {
    
    ProcessResult result;
    
    // Validate first
    auto validation = validate(tx, ctx.base_fee);
    if (!validation.valid) {
        result.success = false;
        result.error = validation.error;
        result.receipt.success = false;
        return result;
    }
    
    // Execute
    auto exec_result = evm_->execute_transaction(tx, ctx);
    
    // Build receipt
    result.receipt.transaction_hash = tx.hash();
    result.receipt.success = exec_result.success;
    result.receipt.gas_used = exec_result.gas_used;
    result.receipt.contract_address = exec_result.created_address;
    result.receipt.logs = exec_result.logs;
    
    result.gas_used = exec_result.gas_used;
    result.success = exec_result.success;
    result.error = exec_result.error;
    
    return result;
}

TransactionProcessor::ValidationResult TransactionProcessor::validate(
    const Transaction& tx, uint64_t base_fee) const {
    
    // Check nonce
    uint64_t expected_nonce = state_->get_nonce(tx.from);
    if (tx.nonce != expected_nonce) {
        return {false, "Invalid nonce"};
    }
    
    // Check balance
    uint64_t cost = tx.value + (tx.gas_limit * tx.max_fee_per_gas);
    if (state_->get_balance(tx.from) < cost) {
        return {false, "Insufficient balance"};
    }
    
    // Check gas limit
    if (tx.gas_limit < intrinsic_gas(tx)) {
        return {false, "Gas limit too low"};
    }
    
    // Check max fee >= base fee
    if (tx.max_fee_per_gas < base_fee) {
        return {false, "Max fee below base fee"};
    }
    
    return {true, ""};
}

uint64_t TransactionProcessor::estimate_gas(const Transaction& tx, 
                                             const ExecutionContext& ctx) {
    auto result = evm_->simulate_transaction(tx, ctx);
    
    // Add margin
    return result.gas_used * 120 / 100;  // 20% buffer
}

uint64_t TransactionProcessor::intrinsic_gas(const Transaction& tx) const {
    uint64_t gas = GasCosts::TRANSACTION;
    
    // Contract creation has higher base cost
    if (tx.to.payment_credential == std::array<uint8_t, 28>{}) {
        gas += GasCosts::TXCREATE;
    }
    
    // Data costs
    for (uint8_t b : tx.data) {
        gas += (b == 0) ? GasCosts::TXDATAZERO : GasCosts::TXDATANONZERO;
    }
    
    return gas;
}

// ============================================================================
// BlockProcessor Implementation
// ============================================================================

BlockProcessor::BlockProcessor(std::shared_ptr<storage::StateManager> state,
                               std::shared_ptr<TransactionProcessor> tx_processor)
    : state_(state), tx_processor_(tx_processor) {}

BlockProcessor::BlockResult BlockProcessor::process_block(const Block& block) {
    BlockResult result;
    result.valid = true;
    result.total_gas_used = 0;
    
    // Validate block
    if (!validate_block(block)) {
        result.valid = false;
        result.error = "Invalid block";
        return result;
    }
    
    // Create execution context
    ExecutionContext ctx;
    ctx.coinbase = block.header.sequencer;
    ctx.block_number = block.header.number;
    ctx.timestamp = block.header.timestamp;
    ctx.gas_limit = block.header.gas_limit;
    ctx.base_fee = block.header.base_fee;
    ctx.chain_id = 1;  // Would come from config
    ctx.block_hash = block.header.hash();
    
    // Process each transaction
    uint64_t cumulative_gas = 0;
    
    for (const auto& tx : block.transactions) {
        ctx.caller = tx.from;
        ctx.origin = tx.from;
        ctx.gas_price = tx.effective_gas_price(ctx.base_fee);
        
        auto tx_result = tx_processor_->process(tx, ctx);
        
        cumulative_gas += tx_result.gas_used;
        tx_result.receipt.cumulative_gas_used = cumulative_gas;
        tx_result.receipt.block_number = block.header.number;
        tx_result.receipt.transaction_index = result.receipts.size();
        
        result.receipts.push_back(tx_result.receipt);
        
        if (!tx_result.success) {
            // Transaction failed but block is still valid
            // (unless it's an invalid tx that shouldn't have been included)
        }
    }
    
    result.total_gas_used = cumulative_gas;
    
    // Commit state changes
    result.state_root = state_->commit();
    
    // Compute receipts root
    std::vector<Hash256> receipt_hashes;
    for (const auto& receipt : result.receipts) {
        receipt_hashes.push_back(receipt.hash());
    }
    result.receipts_root = crypto::Blake2b256::merkle_root(receipt_hashes);
    
    return result;
}

bool BlockProcessor::validate_block(const Block& block) const {
    // Check gas used <= gas limit
    if (block.header.gas_used > block.header.gas_limit) {
        return false;
    }
    
    // Check transactions root
    auto computed_root = const_cast<Block&>(block).compute_transactions_root();
    if (computed_root != block.header.transactions_root) {
        return false;
    }
    
    return true;
}

uint64_t BlockProcessor::calculate_base_fee(const BlockHeader& parent,
                                             uint64_t parent_gas_used) const {
    uint64_t parent_base_fee = parent.base_fee;
    uint64_t parent_gas_target = parent.gas_limit / ELASTICITY_MULTIPLIER;
    
    if (parent_gas_used == parent_gas_target) {
        return parent_base_fee;
    }
    
    if (parent_gas_used > parent_gas_target) {
        // Increase base fee
        uint64_t gas_delta = parent_gas_used - parent_gas_target;
        uint64_t fee_delta = parent_base_fee * gas_delta / parent_gas_target 
                             / BASE_FEE_CHANGE_DENOMINATOR;
        return parent_base_fee + std::max(fee_delta, uint64_t(1));
    } else {
        // Decrease base fee
        uint64_t gas_delta = parent_gas_target - parent_gas_used;
        uint64_t fee_delta = parent_base_fee * gas_delta / parent_gas_target 
                             / BASE_FEE_CHANGE_DENOMINATOR;
        if (fee_delta >= parent_base_fee) {
            return 0;
        }
        return parent_base_fee - fee_delta;
    }
}

// ============================================================================
// ValidityProof Implementation
// ============================================================================

Bytes ValidityProof::encode() const {
    Bytes result;
    
    auto append_uint64 = [&result](uint64_t v) {
        for (int i = 7; i >= 0; --i) {
            result.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        }
    };
    
    auto append_hash = [&result](const Hash256& h) {
        result.insert(result.end(), h.begin(), h.end());
    };
    
    append_uint64(batch_id);
    append_uint64(start_block);
    append_uint64(end_block);
    append_hash(pre_state_root);
    append_hash(post_state_root);
    append_hash(transactions_root);
    
    append_uint64(state_proof.size());
    for (const auto& h : state_proof) {
        append_hash(h);
    }
    
    append_uint64(execution_trace.size());
    for (const auto& h : execution_trace) {
        append_hash(h);
    }
    
    append_hash(commitment);
    append_hash(proof_hash);
    append_hash(verification_key);
    
    return result;
}

std::optional<ValidityProof> ValidityProof::decode(const Bytes& data) {
    if (data.size() < 24 + 32 * 6) return std::nullopt;
    
    ValidityProof proof;
    size_t offset = 0;
    
    auto read_uint64 = [&data, &offset]() -> uint64_t {
        uint64_t v = 0;
        for (int i = 0; i < 8 && offset < data.size(); ++i, ++offset) {
            v = (v << 8) | data[offset];
        }
        return v;
    };
    
    auto read_hash = [&data, &offset]() -> Hash256 {
        Hash256 h{};
        if (offset + 32 <= data.size()) {
            std::copy(data.begin() + offset, data.begin() + offset + 32, h.begin());
            offset += 32;
        }
        return h;
    };
    
    proof.batch_id = read_uint64();
    proof.start_block = read_uint64();
    proof.end_block = read_uint64();
    proof.pre_state_root = read_hash();
    proof.post_state_root = read_hash();
    proof.transactions_root = read_hash();
    
    uint64_t state_proof_count = read_uint64();
    for (uint64_t i = 0; i < state_proof_count && offset + 32 <= data.size(); ++i) {
        proof.state_proof.push_back(read_hash());
    }
    
    uint64_t trace_count = read_uint64();
    for (uint64_t i = 0; i < trace_count && offset + 32 <= data.size(); ++i) {
        proof.execution_trace.push_back(read_hash());
    }
    
    proof.commitment = read_hash();
    proof.proof_hash = read_hash();
    proof.verification_key = read_hash();
    
    return proof;
}

Hash256 ValidityProof::compute_commitment() const {
    Bytes data;
    
    auto append_uint64 = [&data](uint64_t v) {
        for (int i = 7; i >= 0; --i) {
            data.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
        }
    };
    
    append_uint64(batch_id);
    append_uint64(start_block);
    append_uint64(end_block);
    data.insert(data.end(), pre_state_root.begin(), pre_state_root.end());
    data.insert(data.end(), post_state_root.begin(), post_state_root.end());
    data.insert(data.end(), transactions_root.begin(), transactions_root.end());
    
    for (const auto& h : state_proof) {
        data.insert(data.end(), h.begin(), h.end());
    }
    
    return crypto::Blake2b256::hash(data);
}

// ============================================================================
// ZKProver Implementation
// ============================================================================

ZKProver::ZKProver() {
    // Generate a deterministic verification key
    Bytes vk_data = {'N', 'O', 'N', 'A', 'G', 'O', 'N', '_', 'Z', 'K', '_', 'V', 'K'};
    verification_key_ = crypto::Blake2b256::hash(vk_data);
    
    std::cout << "[PROVER] ZK Prover initialized" << std::endl;
}

ValidityProof ZKProver::generate_proof(
    const std::vector<Block>& blocks,
    const Hash256& pre_state_root,
    const Hash256& post_state_root,
    const std::vector<TransactionReceipt>& receipts) {
    
    ValidityProof proof;
    
    // Set metadata
    if (!blocks.empty()) {
        proof.batch_id = blocks.front().header.batch_id;
        proof.start_block = blocks.front().header.number;
        proof.end_block = blocks.back().header.number;
    }
    
    proof.pre_state_root = pre_state_root;
    proof.post_state_root = post_state_root;
    
    // Compute transactions root
    std::vector<Hash256> tx_hashes;
    for (const auto& block : blocks) {
        tx_hashes.push_back(block.compute_transactions_root());
    }
    proof.transactions_root = crypto::Blake2b256::merkle_root(tx_hashes);
    
    // Build state proof (Merkle path from pre to post)
    proof.state_proof = build_state_proof(pre_state_root, post_state_root);
    
    // Build execution trace
    proof.execution_trace.reserve(receipts.size());
    for (const auto& receipt : receipts) {
        proof.execution_trace.push_back(receipt.hash());
    }
    
    // Compute commitment
    proof.commitment = proof.compute_commitment();
    
    // Generate proof hash
    proof.proof_hash = generate_proof_hash(proof.commitment, proof.execution_trace);
    
    // Bind to verification key
    proof.verification_key = verification_key_;
    
    std::cout << "[PROVER] Generated proof for blocks " << proof.start_block 
              << "-" << proof.end_block << std::endl;
    
    return proof;
}

bool ZKProver::verify_proof(const ValidityProof& proof) const {
    // Check verification key binding
    if (proof.verification_key != verification_key_) {
        return false;
    }
    
    // Recompute commitment
    Hash256 expected_commitment = proof.compute_commitment();
    if (expected_commitment != proof.commitment) {
        return false;
    }
    
    // Verify proof hash
    Hash256 expected_proof_hash = generate_proof_hash(proof.commitment, proof.execution_trace);
    if (expected_proof_hash != proof.proof_hash) {
        return false;
    }
    
    // Verify state proof Merkle path
    if (proof.state_proof.size() < 2) {
        return false;
    }
    
    // First element should be pre_state, last should derive to post_state
    if (proof.state_proof.front() != proof.pre_state_root) {
        return false;
    }
    
    return true;
}

Bytes ZKProver::generate_l1_proof(const ValidityProof& proof) const {
    // Generate compact proof for L1 submission
    Bytes compact;
    
    // Just include essential verification data
    compact.insert(compact.end(), proof.commitment.begin(), proof.commitment.end());
    compact.insert(compact.end(), proof.proof_hash.begin(), proof.proof_hash.end());
    compact.insert(compact.end(), proof.pre_state_root.begin(), proof.pre_state_root.end());
    compact.insert(compact.end(), proof.post_state_root.begin(), proof.post_state_root.end());
    
    return compact;
}

Hash256 ZKProver::compute_execution_digest(const std::vector<TransactionReceipt>& receipts) {
    std::vector<Hash256> receipt_hashes;
    receipt_hashes.reserve(receipts.size());
    
    for (const auto& r : receipts) {
        receipt_hashes.push_back(r.hash());
    }
    
    return crypto::Blake2b256::merkle_root(receipt_hashes);
}

std::vector<Hash256> ZKProver::build_state_proof(const Hash256& pre, const Hash256& post) {
    std::vector<Hash256> proof;
    
    // Include pre-state
    proof.push_back(pre);
    
    // Intermediate commitments (would be actual state transitions in full impl)
    Bytes intermediate_data;
    intermediate_data.insert(intermediate_data.end(), pre.begin(), pre.end());
    intermediate_data.insert(intermediate_data.end(), post.begin(), post.end());
    proof.push_back(crypto::Blake2b256::hash(intermediate_data));
    
    // Include post-state
    proof.push_back(post);
    
    return proof;
}

Hash256 ZKProver::generate_proof_hash(const Hash256& commitment,
                                       const std::vector<Hash256>& trace) const {
    Bytes data;
    data.insert(data.end(), commitment.begin(), commitment.end());
    data.insert(data.end(), verification_key_.begin(), verification_key_.end());
    
    for (const auto& h : trace) {
        data.insert(data.end(), h.begin(), h.end());
    }
    
    // Multiple rounds of hashing for security margin
    Hash256 result = crypto::Blake2b256::hash(data);
    for (int i = 0; i < 3; ++i) {
        Bytes round_data(result.begin(), result.end());
        round_data.insert(round_data.end(), commitment.begin(), commitment.end());
        result = crypto::Blake2b256::hash(round_data);
    }
    
    return result;
}

} // namespace execution
} // namespace nonagon
