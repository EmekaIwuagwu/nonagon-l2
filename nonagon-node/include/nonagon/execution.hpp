#pragma once

#include <memory>
#include <optional>
#include <functional>
#include <set>
#include <unordered_map>
#include "nonagon/types.hpp"
#include "nonagon/storage.hpp"

// 256-bit unsigned integer for EVM stack (4 x 64-bit words)
using uint256 = std::array<uint64_t, 4>;

namespace nonagon {
namespace execution {

/**
 * @brief EVM execution context
 */
struct ExecutionContext {
    Address caller;
    Address origin;        // Original tx sender
    Address coinbase;      // Block producer (sequencer)
    uint64_t block_number;
    uint64_t timestamp;
    uint64_t gas_limit;
    uint64_t gas_price;
    uint64_t base_fee;
    uint64_t chain_id;
    Hash256 block_hash;
};

/**
 * @brief Result of contract execution
 */
struct ExecutionResult {
    bool success{false};
    uint64_t gas_used{0};
    Bytes return_data;
    std::string error;
    
    // Contract creation result
    std::optional<Address> created_address;
    
    // Logs emitted during execution
    std::vector<Log> logs;
    
    // State changes (for simulation)
    std::vector<std::pair<Address, AccountState>> state_changes;
};

/**
 * @brief EVM opcode costs (based on Ethereum Yellow Paper)
 */
struct GasCosts {
    static constexpr uint64_t ZERO = 0;
    static constexpr uint64_t BASE = 2;
    static constexpr uint64_t VERYLOW = 3;
    static constexpr uint64_t LOW = 5;
    static constexpr uint64_t MID = 8;
    static constexpr uint64_t HIGH = 10;
    static constexpr uint64_t EXTCODE = 700;
    static constexpr uint64_t BALANCE = 700;
    static constexpr uint64_t SLOAD = 800;
    static constexpr uint64_t SSTORE_SET = 20000;
    static constexpr uint64_t SSTORE_RESET = 5000;
    static constexpr uint64_t SSTORE_REFUND = 15000;
    static constexpr uint64_t CALL = 700;
    static constexpr uint64_t CALLVALUE = 9000;
    static constexpr uint64_t CALLNEWACCOUNT = 25000;
    static constexpr uint64_t CREATE = 32000;
    static constexpr uint64_t SELFDESTRUCT = 5000;
    static constexpr uint64_t MEMORY = 3;
    static constexpr uint64_t LOG = 375;
    static constexpr uint64_t LOGDATA = 8;
    static constexpr uint64_t LOGTOPIC = 375;
    static constexpr uint64_t SHA3 = 30;
    static constexpr uint64_t SHA3WORD = 6;
    static constexpr uint64_t COPY = 3;
    static constexpr uint64_t EXP = 10;
    static constexpr uint64_t EXPBYTE = 50;
    static constexpr uint64_t TRANSACTION = 21000;
    static constexpr uint64_t TXCREATE = 32000;
    static constexpr uint64_t TXDATAZERO = 4;
    static constexpr uint64_t TXDATANONZERO = 16;
};

/**
 * @brief Precompiled contract interface
 */
class Precompile {
public:
    virtual ~Precompile() = default;
    virtual ExecutionResult execute(const Bytes& input, uint64_t gas_limit) = 0;
    virtual uint64_t gas_cost(const Bytes& input) const = 0;
};

/**
 * @brief EVM Virtual Machine
 * 
 * Implements Ethereum Virtual Machine for smart contract execution.
 * Supports Solidity contracts and standard EVM opcodes.
 */
class EVM {
public:
    explicit EVM(std::shared_ptr<storage::StateManager> state);
    
    // Execute a transaction
    ExecutionResult execute_transaction(const Transaction& tx, const ExecutionContext& ctx);
    
    // Simulate transaction (dry run, no state changes)
    ExecutionResult simulate_transaction(const Transaction& tx, const ExecutionContext& ctx);
    
    // Call contract (view function, no state changes)
    ExecutionResult call(const Address& from, const Address& to, 
                         const Bytes& data, uint64_t gas_limit);
    
    // Create contract
    ExecutionResult create(const Address& from, const Bytes& code, 
                           uint64_t value, uint64_t gas_limit);
    
    // Register precompiled contract
    void register_precompile(const Address& addr, std::shared_ptr<Precompile> precompile);
    
    // Chain configuration
    void set_chain_id(uint64_t chain_id) { chain_id_ = chain_id; }
    uint64_t chain_id() const { return chain_id_; }

private:
    std::shared_ptr<storage::StateManager> state_;
    std::unordered_map<std::string, std::shared_ptr<Precompile>> precompiles_;
    uint64_t chain_id_{1};  // Default mainnet
    
    // Internal execution
    ExecutionResult execute_code(const Address& caller, const Address& address,
                                  const Bytes& code, const Bytes& input,
                                  uint64_t value, uint64_t gas_limit,
                                  bool is_static);
    
    // Memory and stack management
    struct ExecutionState {
        std::vector<uint8_t> memory;
        std::vector<uint256> stack;  // 256-bit words (4 x uint64)
        uint64_t gas_remaining;
        size_t pc;  // Program counter
        bool stopped;
        bool reverted;
        Bytes return_data;
    };
    
    // Access list for EIP-2929
    struct AccessList {
        std::set<std::string> addresses;
        std::set<std::pair<std::string, Hash256>> storage_keys;
    };
};

/**
 * @brief Transaction processor
 * 
 * Handles complete transaction lifecycle:
 * 1. Validate transaction
 * 2. Execute via EVM
 * 3. Generate receipt
 * 4. Update state
 */
class TransactionProcessor {
public:
    TransactionProcessor(std::shared_ptr<storage::StateManager> state,
                         std::shared_ptr<EVM> evm);
    
    // Process a single transaction
    struct ProcessResult {
        TransactionReceipt receipt;
        uint64_t gas_used;
        bool success;
        std::string error;
    };
    ProcessResult process(const Transaction& tx, const ExecutionContext& ctx);
    
    // Validate transaction before execution
    struct ValidationResult {
        bool valid;
        std::string error;
    };
    ValidationResult validate(const Transaction& tx, uint64_t base_fee) const;
    
    // Gas estimation
    uint64_t estimate_gas(const Transaction& tx, const ExecutionContext& ctx);
    
private:
    std::shared_ptr<storage::StateManager> state_;
    std::shared_ptr<EVM> evm_;
    
    uint64_t intrinsic_gas(const Transaction& tx) const;
};

/**
 * @brief Block processor
 * 
 * Executes all transactions in a block and produces state root.
 */
class BlockProcessor {
public:
    BlockProcessor(std::shared_ptr<storage::StateManager> state,
                   std::shared_ptr<TransactionProcessor> tx_processor);
    
    // Process entire block
    struct BlockResult {
        Hash256 state_root;
        Hash256 receipts_root;
        std::vector<TransactionReceipt> receipts;
        uint64_t total_gas_used;
        bool valid;
        std::string error;
    };
    BlockResult process_block(const Block& block);
    
    // Validate block before processing
    bool validate_block(const Block& block) const;
    
    // Calculate next base fee (EIP-1559)
    uint64_t calculate_base_fee(const BlockHeader& parent, 
                                 uint64_t parent_gas_used) const;

private:
    std::shared_ptr<storage::StateManager> state_;
    std::shared_ptr<TransactionProcessor> tx_processor_;
    
    static constexpr uint64_t TARGET_GAS_USED = 15000000;  // 50% of 30M limit
    static constexpr uint64_t BASE_FEE_CHANGE_DENOMINATOR = 8;
    static constexpr uint64_t ELASTICITY_MULTIPLIER = 2;
};

// Helper function
uint64_t get_opcode_gas_cost(uint8_t opcode);

/**
 * @brief Validity Proof for ZK Rollup
 * 
 * Cryptographic proof that a state transition is valid.
 * Uses a Merkle-based commitment scheme for L1 verification.
 */
struct ValidityProof {
    // Proof metadata
    uint64_t batch_id;
    uint64_t start_block;
    uint64_t end_block;
    
    // State commitments
    Hash256 pre_state_root;
    Hash256 post_state_root;
    Hash256 transactions_root;
    
    // Merkle proofs for state transition
    std::vector<Hash256> state_proof;      // Merkle path for pre -> post
    std::vector<Hash256> execution_trace;  // Hash of each step
    
    // Commitment (for verification)
    Hash256 commitment;                     // H(all above)
    Hash256 proof_hash;                     // The actual proof
    
    // Verification key binding
    Hash256 verification_key;
    
    // Serialization
    Bytes encode() const;
    static std::optional<ValidityProof> decode(const Bytes& data);
    
    // Compute commitment from components
    Hash256 compute_commitment() const;
};

/**
 * @brief ZK Prover for Validity Proofs
 * 
 * Generates cryptographic proofs that state transitions are valid.
 * Uses a simplified Merkle-based scheme (upgradeable to Halo2/StarkWare).
 */
class ZKProver {
public:
    ZKProver();
    
    // Generate proof for a batch of blocks
    ValidityProof generate_proof(
        const std::vector<Block>& blocks,
        const Hash256& pre_state_root,
        const Hash256& post_state_root,
        const std::vector<TransactionReceipt>& receipts);
    
    // Verify a proof is valid
    bool verify_proof(const ValidityProof& proof) const;
    
    // Get verification key (for L1 contract)
    Hash256 get_verification_key() const { return verification_key_; }
    
    // Generate compact proof for L1 (CBOR encoded)
    Bytes generate_l1_proof(const ValidityProof& proof) const;

private:
    Hash256 verification_key_;
    
    // Internal proof generation
    Hash256 compute_execution_digest(const std::vector<TransactionReceipt>& receipts);
    std::vector<Hash256> build_state_proof(const Hash256& pre, const Hash256& post);
    Hash256 generate_proof_hash(const Hash256& commitment, 
                                 const std::vector<Hash256>& trace) const;
};

} // namespace execution
} // namespace nonagon
