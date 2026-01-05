#include "nonagon/consensus.hpp"
#include "nonagon/crypto.hpp"
#include <algorithm>
#include <chrono>

namespace nonagon {
namespace consensus {

// ============================================================================
// BlockProposal Implementation
// ============================================================================

bool BlockProposal::verify(const crypto::Ed25519::PublicKey& pk) const {
    auto block_hash = block.header.hash();
    return crypto::Ed25519::verify(block_hash.data(), block_hash.size(), signature, pk);
}

Bytes BlockProposal::encode() const {
    Bytes result = block.encode();
    result.insert(result.end(), signature.begin(), signature.end());
    return result;
}

// ============================================================================
// ConsensusEngine Implementation
// ============================================================================

ConsensusEngine::ConsensusEngine(const ConsensusConfig& config) 
    : config_(config) {}

bool ConsensusEngine::register_sequencer(const Sequencer& seq) {
    std::unique_lock lock(mutex_);
    
    // Check minimum stake
    if (seq.stake < config_.min_stake) {
        return false;
    }
    
    // Check if already registered
    auto it = std::find_if(sequencers_.begin(), sequencers_.end(),
        [&seq](const Sequencer& s) { return s.address == seq.address; });
    
    if (it != sequencers_.end()) {
        // Update existing
        *it = seq;
    } else {
        sequencers_.push_back(seq);
    }
    
    update_active_set();
    return true;
}

bool ConsensusEngine::unregister_sequencer(const Address& addr) {
    std::unique_lock lock(mutex_);
    
    auto it = std::find_if(sequencers_.begin(), sequencers_.end(),
        [&addr](const Sequencer& s) { return s.address == addr; });
    
    if (it != sequencers_.end()) {
        it->status = SequencerStatus::Exiting;
        update_active_set();
        return true;
    }
    return false;
}

void ConsensusEngine::update_stake(const Address& addr, uint64_t new_stake) {
    std::unique_lock lock(mutex_);
    
    auto it = std::find_if(sequencers_.begin(), sequencers_.end(),
        [&addr](const Sequencer& s) { return s.address == addr; });
    
    if (it != sequencers_.end()) {
        it->stake = new_stake;
        update_active_set();
    }
}

std::vector<Sequencer> ConsensusEngine::get_active_sequencers() const {
    std::shared_lock lock(mutex_);
    return active_set_;
}

void ConsensusEngine::update_active_set() {
    // Filter eligible sequencers
    std::vector<Sequencer> eligible;
    for (const auto& s : sequencers_) {
        if (s.status == SequencerStatus::Active || s.status == SequencerStatus::Standby) {
            if (s.stake >= config_.min_stake) {
                eligible.push_back(s);
            }
        }
    }
    
    // Sort by stake (descending)
    std::sort(eligible.begin(), eligible.end(),
        [](const Sequencer& a, const Sequencer& b) { return a.stake > b.stake; });
    
    // Take top N
    size_t count = std::min(eligible.size(), static_cast<size_t>(config_.max_sequencers));
    active_set_.assign(eligible.begin(), eligible.begin() + count);
    
    // Update status
    for (auto& s : active_set_) {
        s.status = SequencerStatus::Active;
    }
}

Address ConsensusEngine::get_leader_for_slot(uint64_t slot) const {
    std::shared_lock lock(mutex_);
    
    if (active_set_.empty()) {
        return Address{};
    }
    
    // Stake-weighted round-robin
    uint64_t total_stake = total_active_stake();
    uint64_t slot_stake = slot % total_stake;
    
    uint64_t cumulative = 0;
    for (const auto& seq : active_set_) {
        cumulative += seq.stake;
        if (slot_stake < cumulative) {
            return seq.address;
        }
    }
    
    // Fallback to first
    return active_set_[0].address;
}

bool ConsensusEngine::is_my_slot(uint64_t slot, const Address& my_addr) const {
    return get_leader_for_slot(slot) == my_addr;
}

uint64_t ConsensusEngine::next_slot_for(const Address& addr, uint64_t current_slot) const {
    std::shared_lock lock(mutex_);
    
    // Find next slot where addr is leader
    for (uint64_t s = current_slot + 1; s < current_slot + 10000; ++s) {
        if (get_leader_for_slot(s) == addr) {
            return s;
        }
    }
    
    return UINT64_MAX;  // Not found
}

uint64_t ConsensusEngine::total_active_stake() const {
    uint64_t total = 0;
    for (const auto& s : active_set_) {
        total += s.stake;
    }
    return total > 0 ? total : 1;  // Avoid division by zero
}

std::optional<Block> ConsensusEngine::produce_block(const Address& sequencer,
                                                     const Hash256& parent_hash,
                                                     const std::vector<Transaction>& txs,
                                                     const Hash256& state_root) {
    std::shared_lock lock(mutex_);
    
    Block block;
    block.header.number = chain_head_ + 1;
    block.header.parent_hash = parent_hash;
    block.header.state_root = state_root;
    block.header.sequencer = sequencer;
    block.header.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    block.transactions = txs;
    block.header.transactions_root = block.compute_transactions_root();
    
    // Calculate gas used
    uint64_t total_gas = 0;
    for (const auto& tx : txs) {
        total_gas += tx.gas_limit;  // Simplified; should be actual gas used
    }
    block.header.gas_used = total_gas;
    
    return block;
}

ConsensusEngine::ValidationResult ConsensusEngine::validate_block(const Block& block) const {
    std::shared_lock lock(mutex_);
    
    // Check block number
    if (block.header.number != chain_head_ + 1) {
        return {false, "Invalid block number"};
    }
    
    // Check parent hash
    if (block.header.parent_hash != head_hash_) {
        return {false, "Parent hash mismatch"};
    }
    
    // Check sequencer is valid for this slot
    uint64_t slot = block.header.number;  // Simplified: block number = slot
    Address expected_leader = get_leader_for_slot(slot);
    
    if (block.header.sequencer != expected_leader) {
        return {false, "Invalid sequencer for slot"};
    }
    
    // Check transactions root
    Hash256 computed_tx_root = const_cast<Block&>(block).compute_transactions_root();
    if (computed_tx_root != block.header.transactions_root) {
        return {false, "Transactions root mismatch"};
    }
    
    // Check gas limit
    if (block.header.gas_used > block.header.gas_limit) {
        return {false, "Gas used exceeds limit"};
    }
    
    return {true, ""};
}

Hash256 ConsensusEngine::get_canonical_head() const {
    std::shared_lock lock(mutex_);
    return head_hash_;
}

bool ConsensusEngine::process_block(const Block& block) {
    auto validation = validate_block(block);
    if (!validation.valid) {
        return false;
    }
    
    std::unique_lock lock(mutex_);
    
    chain_head_ = block.header.number;
    head_hash_ = block.header.hash();
    
    // Notify callbacks
    for (auto& cb : block_callbacks_) {
        cb(block);
    }
    
    return true;
}

void ConsensusEngine::set_l1_checkpoint(uint64_t block_number, const Hash256& block_hash) {
    std::unique_lock lock(mutex_);
    
    // Store checkpoint for finality reference
    if (l1_checkpoints_.size() > 100) {
        l1_checkpoints_.erase(l1_checkpoints_.begin());
    }
    l1_checkpoints_.push_back(block_hash);
}

void ConsensusEngine::report_misbehavior(const SlashingEvidence& evidence) {
    std::unique_lock lock(mutex_);
    pending_slashings_.push_back(evidence);
    
    // Find and update sequencer status
    auto it = std::find_if(sequencers_.begin(), sequencers_.end(),
        [&evidence](const Sequencer& s) { return s.address == evidence.sequencer; });
    
    if (it != sequencers_.end()) {
        it->status = SequencerStatus::Slashed;
        update_active_set();
    }
}

std::vector<SlashingEvidence> ConsensusEngine::get_pending_slashings() const {
    std::shared_lock lock(mutex_);
    return pending_slashings_;
}

uint64_t ConsensusEngine::current_epoch() const {
    std::shared_lock lock(mutex_);
    return chain_head_ / config_.blocks_per_epoch;
}

void ConsensusEngine::on_epoch_end(uint64_t epoch) {
    std::unique_lock lock(mutex_);
    
    // Process pending slashings
    for (const auto& slash : pending_slashings_) {
        auto it = std::find_if(sequencers_.begin(), sequencers_.end(),
            [&slash](const Sequencer& s) { return s.address == slash.sequencer; });
        
        if (it != sequencers_.end()) {
            // Apply slash
            uint64_t slash_amount = slash.slash_amount;
            it->stake = (it->stake > slash_amount) ? (it->stake - slash_amount) : 0;
        }
    }
    pending_slashings_.clear();
    
    // Remove exiting sequencers after unbonding period
    sequencers_.erase(
        std::remove_if(sequencers_.begin(), sequencers_.end(),
            [](const Sequencer& s) { return s.status == SequencerStatus::Exiting; }),
        sequencers_.end());
    
    update_active_set();
}

// ============================================================================
// Mempool Implementation
// ============================================================================

Mempool::Mempool(size_t max_size) : max_size_(max_size) {}

Mempool::AddResult Mempool::add_transaction(Transaction tx, uint64_t sender_balance) {
    std::unique_lock lock(mutex_);
    
    auto tx_hash = tx.hash();
    std::string hash_str(tx_hash.begin(), tx_hash.end());
    
    // Check if already known
    if (by_hash_.find(hash_str) != by_hash_.end()) {
        return AddResult::AlreadyKnown;
    }
    
    // Validate balance
    uint64_t required = tx.value + (tx.gas_limit * tx.max_fee_per_gas);
    if (sender_balance < required) {
        return AddResult::InsufficientFunds;
    }
    
    // Check pool capacity
    if (by_hash_.size() >= max_size_) {
        return AddResult::PoolFull;
    }
    
    std::string sender_key = tx.from.to_hex();
    
    // Check nonce
    auto& sender_txs = by_sender_[sender_key];
    
    // Check for replacement
    auto existing = sender_txs.by_nonce.find(tx.nonce);
    if (existing != sender_txs.by_nonce.end()) {
        // Allow replacement if higher gas price (10% bump required)
        if (tx.max_fee_per_gas <= existing->second.max_fee_per_gas * 110 / 100) {
            return AddResult::Underpriced;
        }
        
        // Remove old transaction
        auto old_hash = existing->second.hash();
        std::string old_hash_str(old_hash.begin(), old_hash.end());
        by_hash_.erase(old_hash_str);
        
        sender_txs.by_nonce[tx.nonce] = tx;
        by_hash_[hash_str] = tx;
        
        return AddResult::Replaced;
    }
    
    // Add new transaction
    sender_txs.by_nonce[tx.nonce] = tx;
    by_hash_[hash_str] = tx;
    
    // Update pending nonce
    if (sender_txs.by_nonce.begin()->first == sender_txs.pending_nonce) {
        // Continuous nonce, update pending
        while (sender_txs.by_nonce.find(sender_txs.pending_nonce) != sender_txs.by_nonce.end()) {
            sender_txs.pending_nonce++;
        }
    }
    
    // Add to priority queue
    priority_queue_.push({tx_hash, tx.max_fee_per_gas});
    
    return AddResult::Added;
}

bool Mempool::remove_transaction(const Hash256& hash) {
    std::unique_lock lock(mutex_);
    
    std::string hash_str(hash.begin(), hash.end());
    auto it = by_hash_.find(hash_str);
    if (it == by_hash_.end()) {
        return false;
    }
    
    auto& tx = it->second;
    std::string sender_key = tx.from.to_hex();
    
    auto sender_it = by_sender_.find(sender_key);
    if (sender_it != by_sender_.end()) {
        sender_it->second.by_nonce.erase(tx.nonce);
        if (sender_it->second.by_nonce.empty()) {
            by_sender_.erase(sender_it);
        }
    }
    
    by_hash_.erase(it);
    return true;
}

void Mempool::remove_confirmed(const std::vector<Hash256>& hashes) {
    for (const auto& hash : hashes) {
        remove_transaction(hash);
    }
}

std::optional<Transaction> Mempool::get_transaction(const Hash256& hash) const {
    std::shared_lock lock(mutex_);
    
    std::string hash_str(hash.begin(), hash.end());
    auto it = by_hash_.find(hash_str);
    if (it != by_hash_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Transaction> Mempool::get_pending_for(const Address& addr) const {
    std::shared_lock lock(mutex_);
    
    std::string addr_key = addr.to_hex();
    auto it = by_sender_.find(addr_key);
    if (it == by_sender_.end()) {
        return {};
    }
    
    std::vector<Transaction> result;
    for (const auto& [nonce, tx] : it->second.by_nonce) {
        result.push_back(tx);
    }
    return result;
}

size_t Mempool::size() const {
    std::shared_lock lock(mutex_);
    return by_hash_.size();
}

std::vector<Transaction> Mempool::get_block_transactions(uint64_t gas_limit, 
                                                          uint64_t base_fee) {
    std::unique_lock lock(mutex_);
    
    std::vector<Transaction> result;
    uint64_t gas_used = 0;
    
    // Rebuild priority queue with current base fee
    rebuild_priority_queue(base_fee);
    
    std::set<std::string> selected_hashes;
    
    while (!priority_queue_.empty() && gas_used < gas_limit) {
        auto top = priority_queue_.top();
        priority_queue_.pop();
        
        std::string hash_str(top.hash.begin(), top.hash.end());
        
        // Skip if already selected or removed
        if (selected_hashes.count(hash_str) || by_hash_.find(hash_str) == by_hash_.end()) {
            continue;
        }
        
        auto& tx = by_hash_[hash_str];
        
        // Check if fits in block
        if (gas_used + tx.gas_limit > gas_limit) {
            continue;  // Try next
        }
        
        // Check effective gas price meets base fee
        if (tx.effective_gas_price(base_fee) < base_fee) {
            continue;
        }
        
        result.push_back(tx);
        selected_hashes.insert(hash_str);
        gas_used += tx.gas_limit;
    }
    
    return result;
}

uint64_t Mempool::get_pending_nonce(const Address& addr) const {
    std::shared_lock lock(mutex_);
    
    std::string addr_key = addr.to_hex();
    auto it = by_sender_.find(addr_key);
    if (it != by_sender_.end()) {
        return it->second.pending_nonce;
    }
    return 0;
}

Mempool::Stats Mempool::get_stats() const {
    std::shared_lock lock(mutex_);
    
    Stats stats;
    stats.pending_count = by_hash_.size();
    stats.queued_count = 0;  // Simplified
    stats.min_gas_price = UINT64_MAX;
    stats.max_gas_price = 0;
    
    for (const auto& [hash, tx] : by_hash_) {
        if (tx.max_fee_per_gas < stats.min_gas_price) {
            stats.min_gas_price = tx.max_fee_per_gas;
        }
        if (tx.max_fee_per_gas > stats.max_gas_price) {
            stats.max_gas_price = tx.max_fee_per_gas;
        }
    }
    
    if (stats.min_gas_price == UINT64_MAX) {
        stats.min_gas_price = 0;
    }
    
    return stats;
}

void Mempool::rebuild_priority_queue(uint64_t base_fee) {
    // Clear and rebuild
    while (!priority_queue_.empty()) {
        priority_queue_.pop();
    }
    
    for (const auto& [hash_str, tx] : by_hash_) {
        Hash256 hash;
        std::copy(hash_str.begin(), hash_str.begin() + 32, hash.begin());
        priority_queue_.push({hash, tx.effective_gas_price(base_fee)});
    }
}

} // namespace consensus
} // namespace nonagon
