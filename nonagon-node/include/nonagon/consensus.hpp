#pragma once

#include <memory>
#include <vector>
#include <queue>
#include <map>
#include <set>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <functional>
#include "nonagon/types.hpp"

namespace nonagon {
namespace consensus {

/**
 * @brief Sequencer status in the rotating set
 */
enum class SequencerStatus {
    Active,      // Currently producing blocks
    Standby,     // In rotation, waiting
    Slashed,     // Removed due to misbehavior
    Exiting      // Voluntary exit, unbonding period
};

/**
 * @brief Sequencer information
 */
struct Sequencer {
    Address address;
    crypto::Ed25519::PublicKey public_key;
    uint64_t stake;                // NATX bonded
    uint64_t last_block_produced;
    SequencerStatus status{SequencerStatus::Standby};
    
    // Performance metrics
    uint64_t blocks_produced{0};
    uint64_t missed_slots{0};
    double uptime_percentage{100.0};
};

/**
 * @brief Slashing conditions and evidence
 */
struct SlashingEvidence {
    enum class Type {
        DoubleSign,       // Signed two blocks at same height
        InvalidBlock,     // Produced invalid state transition
        Unavailability,   // Missed too many slots
        DataWithholding   // Failed to publish DA
    };
    
    Type type;
    Address sequencer;
    uint64_t block_number;
    Bytes evidence_data;
    uint64_t slash_amount;  // NATX to slash
};

/**
 * @brief Consensus configuration
 */
struct ConsensusConfig {
    // Block production
    uint64_t block_time_ms{1000};         // 1 second blocks
    uint64_t blocks_per_epoch{86400};     // ~24 hours
    
    // Sequencer set
    uint32_t max_sequencers{21};
    uint64_t min_stake{100000};           // 100k NATX minimum
    uint64_t unbonding_period{604800};    // 7 days in seconds
    
    // Slashing
    double double_sign_slash_percent{5.0};
    double unavailability_slash_percent{0.1};
    uint32_t max_missed_blocks{1000};     // Before slash
    
    // Finality
    uint32_t soft_finality_blocks{5};     // L2 soft finality
    uint64_t challenge_period_seconds{604800}; // 7 days for L1
};

/**
 * @brief Block proposal for consensus
 */
struct BlockProposal {
    Block block;
    crypto::Ed25519::Signature signature;
    
    bool verify(const crypto::Ed25519::PublicKey& pk) const;
    Bytes encode() const;
};

/**
 * @brief Consensus engine - Rotating Sequencer Set (RSS)
 * 
 * Implements stake-weighted round-robin leader election:
 * - Top N stakers become active sequencers
 * - Each sequencer gets proportional slots per epoch
 * - Automatic failover if leader misses slot
 */
class ConsensusEngine {
public:
    explicit ConsensusEngine(const ConsensusConfig& config);
    
    // Sequencer management
    bool register_sequencer(const Sequencer& seq);
    bool unregister_sequencer(const Address& addr);
    void update_stake(const Address& addr, uint64_t new_stake);
    std::vector<Sequencer> get_active_sequencers() const;
    
    // Leader election
    Address get_leader_for_slot(uint64_t slot) const;
    bool is_my_slot(uint64_t slot, const Address& my_addr) const;
    uint64_t next_slot_for(const Address& addr, uint64_t current_slot) const;
    
    // Block production
    std::optional<Block> produce_block(const Address& sequencer,
                                        const Hash256& parent_hash,
                                        const std::vector<Transaction>& txs,
                                        const Hash256& state_root);
    
    // Block validation
    struct ValidationResult {
        bool valid;
        std::string error;
    };
    ValidationResult validate_block(const Block& block) const;
    
    // Fork choice - longest chain rule with L1 checkpoints
    Hash256 get_canonical_head() const;
    bool process_block(const Block& block);
    void set_l1_checkpoint(uint64_t block_number, const Hash256& block_hash);
    
    // Slashing
    void report_misbehavior(const SlashingEvidence& evidence);
    std::vector<SlashingEvidence> get_pending_slashings() const;
    
    // Epoch management
    uint64_t current_epoch() const;
    void on_epoch_end(uint64_t epoch);
    
    // Callbacks
    using BlockCallback = std::function<void(const Block&)>;
    void on_new_block(BlockCallback cb) { block_callbacks_.push_back(cb); }

private:
    ConsensusConfig config_;
    mutable std::shared_mutex mutex_;
    
    std::vector<Sequencer> sequencers_;
    std::vector<Sequencer> active_set_;  // Top N by stake
    
    // Chain state
    uint64_t chain_head_{0};
    Hash256 head_hash_;
    std::vector<Hash256> l1_checkpoints_;
    
    // Slashing queue
    std::vector<SlashingEvidence> pending_slashings_;
    
    // Callbacks
    std::vector<BlockCallback> block_callbacks_;
    
    void update_active_set();
    uint64_t total_active_stake() const;
};

/**
 * @brief Mempool for pending transactions
 * 
 * Implements EIP-1559 fee market:
 * - Priority by effective gas price
 * - Replace-by-fee (RBF) support
 * - Nonce tracking per sender
 */
class Mempool {
public:
    explicit Mempool(size_t max_size = 10000);
    
    // Transaction management
    enum class AddResult {
        Added,
        Replaced,
        AlreadyKnown,
        Underpriced,
        NonceTooLow,
        NonceTooHigh,
        InsufficientFunds,
        PoolFull,
        Invalid
    };
    AddResult add_transaction(Transaction tx, uint64_t sender_balance);
    
    bool remove_transaction(const Hash256& hash);
    void remove_confirmed(const std::vector<Hash256>& hashes);
    
    // Query
    std::optional<Transaction> get_transaction(const Hash256& hash) const;
    std::vector<Transaction> get_pending_for(const Address& addr) const;
    size_t size() const;
    
    // Block production - get best transactions
    std::vector<Transaction> get_block_transactions(uint64_t gas_limit, 
                                                     uint64_t base_fee);
    
    // Nonce management
    uint64_t get_pending_nonce(const Address& addr) const;
    
    // Stats
    struct Stats {
        size_t pending_count;
        size_t queued_count;
        uint64_t min_gas_price;
        uint64_t max_gas_price;
    };
    Stats get_stats() const;

private:
    size_t max_size_;
    mutable std::shared_mutex mutex_;
    
    // Transaction pool organized by sender
    struct SenderTxs {
        std::map<uint64_t, Transaction> by_nonce;  // nonce -> tx
        uint64_t pending_nonce;  // Next expected nonce
    };
    std::unordered_map<std::string, SenderTxs> by_sender_;
    
    // Global index
    std::unordered_map<std::string, Transaction> by_hash_;
    
    // Priority queue by effective gas price
    struct TxPriority {
        Hash256 hash;
        uint64_t effective_price;
        bool operator<(const TxPriority& other) const {
            return effective_price < other.effective_price;
        }
    };
    std::priority_queue<TxPriority> priority_queue_;
    
    void rebuild_priority_queue(uint64_t base_fee);
};

} // namespace consensus
} // namespace nonagon
