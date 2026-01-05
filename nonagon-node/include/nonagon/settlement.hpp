#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <atomic>
#include "nonagon/types.hpp"

namespace nonagon {

// Forward declarations
namespace execution {
    struct ValidityProof;
}

namespace settlement {

/**
 * @brief Cardano network configuration
 */
struct CardanoConfig {
    // API Endpoint (Blockfrost or Ogmios)
    std::string node_socket_path;      // e.g., "https://cardano-preprod.blockfrost.io/api/v0"
    std::string api_key;               // Blockfrost project ID
    std::string network{"preprod"};    // "mainnet", "preprod", "preview" (default: preprod testnet)
    
    // Contract addresses (Plutus script hashes)
    std::string state_contract;        // State commitment contract
    std::string bridge_contract;       // Token bridge contract  
    std::string challenge_contract;    // Fraud proof contract
    
    // Deposit monitoring
    std::string deposit_address;       // L1 address to watch for deposits
    uint64_t deposit_confirmations{10}; // Required confirmations
    uint64_t poll_interval_ms{5000};   // How often to poll for deposits
    
    // Timing
    uint64_t slot_duration_ms{1000};
    uint64_t epoch_length{432000};     // ~5 days
    uint64_t challenge_period_slots{604800}; // 7 days in slots
};

/**
 * @brief Plutus datum for state commitment
 */
struct StateCommitmentDatum {
    uint64_t batch_id;
    uint64_t start_block;
    uint64_t end_block;
    Hash256 pre_state_root;
    Hash256 post_state_root;
    Hash256 transactions_root;
    uint64_t timestamp;
    Address sequencer;
    
    Bytes to_cbor() const;
    static StateCommitmentDatum from_cbor(const Bytes& data);
};

/**
 * @brief Fraud proof for Optimistic Rollup
 * 
 * Interactive bisection protocol:
 * 1. Challenger claims invalid state transition
 * 2. Sequencer and challenger bisect to single step
 * 3. Final step verified on-chain
 */
struct FraudProof {
    uint64_t batch_id;
    Address challenger;
    Address sequencer;
    
    // Bisection state
    uint64_t step_index;  // Which execution step is disputed
    Hash256 pre_state;
    Hash256 post_state;
    
    // Execution trace for disputed step
    Bytes execution_trace;
    std::vector<Bytes> state_proofs;
    
    enum class Status {
        Initiated,
        Bisecting,
        Resolved,
        ChallengerWon,
        SequencerWon
    };
    Status status{Status::Initiated};
    
    Bytes to_cbor() const;
};

/**
 * @brief Bridge deposit from Cardano L1
 */
struct BridgeDeposit {
    std::string cardano_tx_hash;
    uint64_t cardano_slot;
    Address l2_recipient;
    uint64_t amount;         // In lovelace (ADA) or native token units
    std::string asset_id;    // "lovelace" for ADA, policy_id.asset_name for tokens
    
    Hash256 hash() const;
};

/**
 * @brief Bridge withdrawal from L2 to Cardano
 */
struct BridgeWithdrawal {
    Hash256 l2_tx_hash;
    uint64_t l2_block_number;
    Address l2_sender;
    std::string cardano_recipient;  // Cardano address (bech32)
    uint64_t amount;
    std::string asset_id;
    
    // Proof for L1 claim
    std::vector<Hash256> merkle_proof;
    uint64_t batch_id;
    
    enum class Status {
        Pending,      // Awaiting batch inclusion
        Submitted,    // In challenge period
        Claimable,    // Ready to claim on L1
        Claimed,      // Completed
        Reverted      // Batch was challenged
    };
    Status status{Status::Pending};
};

/**
 * @brief Cardano RPC client
 */
class CardanoClient {
public:
    explicit CardanoClient(const CardanoConfig& config);
    
    // Connection
    bool connect();
    bool is_connected() const;
    
    // Chain queries
    uint64_t get_current_slot() const;
    uint64_t get_current_epoch() const;
    std::optional<Bytes> get_utxo(const std::string& tx_hash, uint32_t index) const;
    
    // Transaction submission
    std::future<std::string> submit_transaction(const Bytes& signed_tx);
    std::optional<std::string> get_tx_status(const std::string& tx_hash) const;
    
    // Contract interactions
    std::optional<StateCommitmentDatum> get_latest_state_commitment() const;
    std::vector<FraudProof> get_active_challenges() const;
    std::vector<BridgeDeposit> get_pending_deposits(uint64_t from_slot) const;

private:
    CardanoConfig config_;
    bool connected_{false};
};

/**
 * @brief Batch builder for L1 settlement
 * 
 * Aggregates L2 blocks into batches for Cardano submission:
 * - Time-based: Every 1 hour
 * - Size-based: When batch reaches 50k txs
 * - Whichever comes first
 */
class BatchBuilder {
public:
    struct Config {
        uint64_t max_batch_size{50000};      // Max transactions
        uint64_t max_batch_age_seconds{3600}; // 1 hour
        uint64_t min_batch_size{100};        // Don't submit tiny batches
    };
    
    explicit BatchBuilder(const Config& config);
    
    // Add blocks to pending batch
    void add_block(const Block& block);
    
    // Check if batch is ready
    bool is_ready() const;
    
    // Build batch for submission
    std::optional<SettlementBatch> build_batch(const Hash256& pre_state_root);
    
    // Clear after successful submission
    void clear();
    
    // Stats
    size_t pending_blocks() const;
    size_t pending_transactions() const;
    
    // Accessors
    uint64_t current_batch_id() const { return next_batch_id_; }
    const std::vector<Block>& get_pending_blocks() const { return pending_blocks_; }

private:
    Config config_;
    mutable std::mutex mutex_;
    
    std::vector<Block> pending_blocks_;
    uint64_t batch_start_time_{0};
    uint64_t next_batch_id_{1};
};

/**
 * @brief Settlement manager
 * 
 * Coordinates:
 * - Batch submission to Cardano
 * - Challenge monitoring
 * - Fraud proof generation/response
 * - Deposit/withdrawal processing
 */
class SettlementManager {
public:
    SettlementManager(std::shared_ptr<CardanoClient> client,
                      std::shared_ptr<BatchBuilder> builder);
    
    // Batch settlement
    std::future<bool> submit_batch(const SettlementBatch& batch);
    std::vector<SettlementBatch> get_pending_batches() const;
    std::optional<SettlementBatch> get_batch(uint64_t batch_id) const;
    
    // Challenge handling
    void handle_challenge(const FraudProof& challenge);
    std::optional<FraudProof> generate_fraud_proof(uint64_t batch_id, 
                                                    uint64_t invalid_step);
    void respond_to_bisection(const FraudProof& challenge, 
                              const Hash256& claimed_state);
    
    // Bridge operations
    void process_deposits();
    std::vector<BridgeDeposit> get_new_deposits() const;
    void queue_withdrawal(const BridgeWithdrawal& withdrawal);
    std::vector<BridgeWithdrawal> get_claimable_withdrawals() const;
    
    // Finality tracking
    bool is_batch_finalized(uint64_t batch_id) const;
    uint64_t get_finalized_block() const;
    
    // Callbacks
    using DepositCallback = std::function<void(const BridgeDeposit&)>;
    using FinalityCallback = std::function<void(uint64_t batch_id)>;
    void on_deposit(DepositCallback cb) { deposit_callbacks_.push_back(cb); }
    void on_finality(FinalityCallback cb) { finality_callbacks_.push_back(cb); }
    
    // Block batching for settlement
    uint64_t get_current_batch_id() const;
    void add_block_to_batch(const Block& block);
    std::vector<Block> get_batch_blocks(uint64_t batch_id) const;
    bool submit_batch_to_l1(uint64_t batch_id, const execution::ValidityProof& proof);
    
    // Background processing
    void start();
    void stop();

private:
    std::shared_ptr<CardanoClient> client_;
    std::shared_ptr<BatchBuilder> builder_;
    
    std::vector<SettlementBatch> pending_batches_;
    std::vector<SettlementBatch> finalized_batches_;
    std::vector<BridgeWithdrawal> pending_withdrawals_;
    
    std::vector<DepositCallback> deposit_callbacks_;
    std::vector<FinalityCallback> finality_callbacks_;
    
    std::atomic<bool> running_{false};
    std::thread background_thread_;
    
    void background_loop();
    void check_batch_finality();
    void process_pending_withdrawals();
};

/**
 * @brief L1 Deposit Watcher
 * 
 * Monitors Cardano L1 for deposit transactions to the bridge contract.
 * When a deposit is confirmed, triggers L2 minting via callback.
 */
class L1DepositWatcher {
public:
    L1DepositWatcher(const CardanoConfig& config, 
                     std::shared_ptr<CardanoClient> client);
    
    // Lifecycle
    void start();
    void stop();
    bool is_running() const { return running_; }
    
    // Deposit info
    struct DepositInfo {
        std::string tx_hash;
        uint32_t output_index;
        std::string l1_address;     // Sender on L1
        Address l2_address;         // Recipient on L2
        uint64_t amount;            // Amount in lovelace
        uint64_t confirmations;     // Current confirmations
        uint64_t slot;              // L1 slot
    };
    
    // Query deposits
    std::vector<DepositInfo> get_pending_deposits() const;
    std::vector<DepositInfo> get_confirmed_deposits() const;
    
    // Callbacks
    using DepositConfirmedCallback = std::function<void(const DepositInfo&)>;
    void on_deposit_confirmed(DepositConfirmedCallback cb);
    
    // Manual poll (for testing)
    void poll_deposits();

private:
    CardanoConfig config_;
    std::shared_ptr<CardanoClient> client_;
    
    std::vector<DepositInfo> pending_deposits_;
    std::vector<DepositInfo> confirmed_deposits_;
    std::vector<DepositConfirmedCallback> callbacks_;
    
    std::atomic<bool> running_{false};
    std::thread watcher_thread_;
    mutable std::mutex mutex_;
    
    uint64_t last_checked_slot_{0};
    
    void watcher_loop();
    void fetch_new_deposits();
    void update_confirmations();
    void process_confirmed();
};

} // namespace settlement
} // namespace nonagon
