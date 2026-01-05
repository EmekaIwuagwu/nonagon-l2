#pragma once

#include <vector>
#include <string>
#include <array>
#include <cstdint>
#include <optional>
#include <chrono>
#include "nonagon/crypto.hpp"

namespace nonagon {

using Bytes = std::vector<uint8_t>;
using Hash256 = crypto::Blake2b256::HashBytes;

/**
 * @brief Nonagon Address - Cardano-compatible Bech32 format
 * 
 * Format: addr1<payload> (mainnet) or addr_test1<payload> (testnet)
 * Payload includes:
 *   - Address type (1 byte): 0x00 = base, 0x01 = enterprise, 0x02 = script
 *   - Payment credential (28 bytes): Blake2b-224 of public key
 *   - Optional stake credential (28 bytes)
 */
struct Address {
    enum class Type : uint8_t {
        Base = 0x00,       // Payment + Staking credential
        Enterprise = 0x01, // Payment only (no staking)
        Script = 0x02      // Script hash (smart contracts)
    };

    Type type{Type::Enterprise};
    std::array<uint8_t, 28> payment_credential{};
    std::optional<std::array<uint8_t, 28>> stake_credential;
    bool is_mainnet{true};

    std::string to_bech32() const;
    static std::optional<Address> from_bech32(const std::string& str);
    static std::optional<Address> from_hex(const std::string& str);
    static Address from_public_key(const crypto::Ed25519::PublicKey& pk, bool mainnet = true);
    
    bool operator==(const Address& other) const = default;
    
    // For use as map key
    std::string to_hex() const;
};

/**
 * @brief Transaction structure for Nonagon L2
 * 
 * Follows EIP-1559 fee model for gas pricing.
 * Uses Ed25519 signatures for Cardano compatibility.
 */
struct Transaction {
    // Core fields
    Address from;
    Address to;
    uint64_t value{0};           // NATX in base units (1 NATX = 10^18 units)
    uint64_t nonce{0};
    Bytes data;                  // Contract call data or deployment bytecode

    // Gas fields (EIP-1559 style)
    uint64_t gas_limit{21000};
    uint64_t max_fee_per_gas{0};
    uint64_t max_priority_fee_per_gas{0};

    // Signature
    crypto::Ed25519::PublicKey sender_pubkey{};
    crypto::Ed25519::Signature signature{};

    // Computed fields
    Hash256 hash() const;
    uint64_t effective_gas_price(uint64_t base_fee) const;
    bool verify_signature() const;
    
    // Serialization
    Bytes encode() const;
    static std::optional<Transaction> decode(const Bytes& data);
};

/**
 * @brief Block header for Nonagon L2
 */
struct BlockHeader {
    uint64_t number{0};
    Hash256 parent_hash{};
    Hash256 state_root{};
    Hash256 transactions_root{};
    Hash256 receipts_root{};
    
    Address sequencer;           // Block producer
    uint64_t gas_limit{30000000};
    uint64_t gas_used{0};
    uint64_t base_fee{1000000000}; // 1 Gwei default
    
    uint64_t timestamp{0};
    uint64_t l1_block_number{0}; // Cardano block reference
    
    // Settlement tracking
    uint64_t batch_id{0};        // Which L1 batch includes this block
    
    Hash256 hash() const;
    Bytes encode() const;
};

/**
 * @brief Full block with transactions
 */
struct Block {
    BlockHeader header;
    std::vector<Transaction> transactions;
    
    Hash256 compute_transactions_root() const;
    Bytes encode() const;
    static std::optional<Block> decode(const Bytes& data);
};

/**
 * @brief Log entry from contract execution
 */
struct Log {
    Address address;
    std::vector<Hash256> topics;
    Bytes data;
};

/**
 * @brief Transaction receipt after execution
 */
struct TransactionReceipt {
    Hash256 transaction_hash{}; // Align with node.cpp usage
    uint64_t block_number{0};
    uint64_t transaction_index{0};
    Address from{};
    Address to{};
    
    bool success{false};
    uint64_t status{0}; // 1 for success, 0 for failure (EVM standard)
    uint64_t gas_used{0};
    uint64_t cumulative_gas_used{0};
    
    // Contract creation
    std::optional<Address> contract_address;
    
    // Logs
    std::vector<Log> logs;

    Hash256 hash() const;
};

/**
 * @brief Account state in the state trie
 */
struct AccountState {
    uint64_t nonce{0};
    uint64_t balance{0};         // NATX balance
    Hash256 storage_root{};      // Merkle root of contract storage
    Hash256 code_hash{};         // Hash of contract bytecode
    
    bool is_contract() const;
    Bytes encode() const;
    static AccountState decode(const Bytes& data);
};

/**
 * @brief Settlement batch for Cardano L1
 */
struct SettlementBatch {
    uint64_t batch_id{0};
    uint64_t start_block{0};
    uint64_t end_block{0};
    
    Hash256 pre_state_root{};
    Hash256 post_state_root{};
    Hash256 transactions_root{};
    
    // Compressed transaction data for DA
    Bytes compressed_data;
    
    // Merkle proof for state transition
    std::vector<Hash256> state_proof;
    
    // L1 settlement info
    std::string cardano_tx_hash;
    uint64_t cardano_slot{0};
    
    enum class Status {
        Pending,       // Awaiting submission
        Submitted,     // On Cardano, in challenge period
        Finalized,     // Challenge period passed
        Challenged,    // Under dispute
        Reverted       // Fraud proven
    };
    Status status{Status::Pending};
    
    Bytes encode() const;
};

} // namespace nonagon
