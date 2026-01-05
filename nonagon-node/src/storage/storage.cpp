#include "nonagon/storage.hpp"
#include "nonagon/crypto.hpp"
#include <algorithm>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(dir) _mkdir(dir)
#else
#include <sys/stat.h>
#define MKDIR(dir) mkdir(dir, 0777)
#endif

namespace nonagon {
namespace storage {

// ============================================================================
// MemoryDatabase Implementation
// ============================================================================

bool MemoryDatabase::put(const Bytes& key, const Bytes& value) {
    std::unique_lock lock(mutex_);
    data_[key] = value;
    return true;
}

std::optional<Bytes> MemoryDatabase::get(const Bytes& key) {
    std::shared_lock lock(mutex_);
    auto it = data_.find(key);
    if (it != data_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool MemoryDatabase::del(const Bytes& key) {
    std::unique_lock lock(mutex_);
    return data_.erase(key) > 0;
}

bool MemoryDatabase::exists(const Bytes& key) {
    std::shared_lock lock(mutex_);
    return data_.find(key) != data_.end();
}

bool MemoryDatabase::write_batch(const WriteBatch& batch) {
    std::unique_lock lock(mutex_);
    for (const auto& [key, value] : batch.puts) {
        data_[key] = value;
    }
    for (const auto& key : batch.deletes) {
        data_.erase(key);
    }
    return true;
}

class MemoryIterator : public Database::Iterator {
public:
    MemoryIterator(std::map<Bytes, Bytes>::const_iterator begin,
                   std::map<Bytes, Bytes>::const_iterator end,
                   const Bytes& prefix)
        : current_(begin), end_(end), prefix_(prefix) {
        // Find first matching prefix
        while (current_ != end_ && !matches_prefix()) {
            ++current_;
        }
    }
    
    bool valid() override { 
        return current_ != end_ && matches_prefix(); 
    }
    
    void next() override { 
        if (current_ != end_) {
            ++current_;
            while (current_ != end_ && !matches_prefix()) {
                ++current_;
            }
        }
    }
    
    Bytes key() override { return current_->first; }
    Bytes value() override { return current_->second; }
    
private:
    bool matches_prefix() const {
        if (prefix_.empty()) return true;
        if (current_->first.size() < prefix_.size()) return false;
        return std::equal(prefix_.begin(), prefix_.end(), current_->first.begin());
    }
    
    std::map<Bytes, Bytes>::const_iterator current_;
    std::map<Bytes, Bytes>::const_iterator end_;
    Bytes prefix_;
};

std::unique_ptr<Database::Iterator> MemoryDatabase::new_iterator(const Bytes& prefix) {
    std::shared_lock lock(mutex_);
    return std::make_unique<MemoryIterator>(data_.cbegin(), data_.cend(), prefix);
}

// ============================================================================
// PersistentDatabase Implementation
// ============================================================================

PersistentDatabase::PersistentDatabase(const std::string& path) : path_(path) {
    // Ensure directory exists
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        std::string dir = path.substr(0, last_slash);
        MKDIR(dir.c_str());
    }
    load();
    file_.open(path_, std::ios::binary | std::ios::app);
}

PersistentDatabase::~PersistentDatabase() {
    if (file_.is_open()) file_.close();
}

void PersistentDatabase::load() {
    std::ifstream infile(path_, std::ios::binary);
    if (!infile.is_open()) return;

    while (infile.peek() != EOF) {
        uint8_t op;
        infile.read((char*)&op, 1);
        if (infile.eof()) break;

        uint32_t key_len;
        infile.read((char*)&key_len, 4);
        
        Bytes key(key_len);
        infile.read((char*)key.data(), key_len);

        if (op == 1) { // PUT
            uint32_t val_len;
            infile.read((char*)&val_len, 4);
            Bytes val(val_len);
            infile.read((char*)val.data(), val_len);
            data_[key] = val;
        } else if (op == 2) { // DEL
            data_.erase(key);
        }
    }
}

void PersistentDatabase::append_log(uint8_t op, const Bytes& key, const Bytes& value) {
    if (!file_.is_open()) return;
    
    file_.write((char*)&op, 1);
    uint32_t klen = (uint32_t)key.size();
    file_.write((char*)&klen, 4);
    file_.write((char*)key.data(), klen);
    
    if (op == 1) {
        uint32_t vlen = (uint32_t)value.size();
        file_.write((char*)&vlen, 4);
        file_.write((char*)value.data(), vlen);
    }
    file_.flush();
}

bool PersistentDatabase::put(const Bytes& key, const Bytes& value) {
    std::unique_lock lock(mutex_);
    data_[key] = value;
    append_log(1, key, value);
    return true;
}

std::optional<Bytes> PersistentDatabase::get(const Bytes& key) {
    std::shared_lock lock(mutex_);
    auto it = data_.find(key);
    if (it != data_.end()) return it->second;
    return std::nullopt;
}

bool PersistentDatabase::del(const Bytes& key) {
    std::unique_lock lock(mutex_);
    if (data_.erase(key)) {
        append_log(2, key, {});
        return true;
    }
    return false;
}

bool PersistentDatabase::exists(const Bytes& key) {
    std::shared_lock lock(mutex_);
    return data_.find(key) != data_.end();
}

bool PersistentDatabase::write_batch(const WriteBatch& batch) {
    std::unique_lock lock(mutex_);
    for (const auto& [key, value] : batch.puts) {
        data_[key] = value;
        append_log(1, key, value);
    }
    for (const auto& key : batch.deletes) {
        if (data_.erase(key)) {
            append_log(2, key, {});
        }
    }
    return true;
}

std::unique_ptr<Database::Iterator> PersistentDatabase::new_iterator(const Bytes& prefix) {
    std::shared_lock lock(mutex_);
    return std::make_unique<MemoryIterator>(data_.cbegin(), data_.cend(), prefix);
}

// ============================================================================
// StateTrie Implementation (Simplified Merkle Patricia Trie)
// ============================================================================

StateTrie::StateTrie(std::shared_ptr<Database> db) 
    : db_(db), root_({}) {}

StateTrie::StateTrie(std::shared_ptr<Database> db, const Hash256& root)
    : db_(db), root_(root) {}

void StateTrie::put(const Bytes& key, const Bytes& value) {
    // Hash the key for path
    auto key_hash = crypto::Blake2b256::hash(key);
    std::string key_str(key_hash.begin(), key_hash.end());
    
    // Store in dirty nodes (will be committed later)
    dirty_nodes_[key_str] = value;
}

std::optional<Bytes> StateTrie::get(const Bytes& key) const {
    auto key_hash = crypto::Blake2b256::hash(key);
    std::string key_str(key_hash.begin(), key_hash.end());
    
    // Check dirty nodes first
    auto it = dirty_nodes_.find(key_str);
    if (it != dirty_nodes_.end()) {
        return it->second;
    }
    
    // Check database
    Bytes db_key;
    db_key.push_back(0x01);  // State prefix
    db_key.insert(db_key.end(), key_hash.begin(), key_hash.end());
    
    return db_->get(db_key);
}

void StateTrie::del(const Bytes& key) {
    auto key_hash = crypto::Blake2b256::hash(key);
    std::string key_str(key_hash.begin(), key_hash.end());
    
    // Mark as deleted (empty value)
    dirty_nodes_[key_str] = {};
}

Hash256 StateTrie::commit() {
    Database::WriteBatch batch;
    std::vector<Hash256> leaf_hashes;
    
    // Write all dirty nodes to database
    for (const auto& [key_str, value] : dirty_nodes_) {
        Bytes db_key;
        db_key.push_back(0x01);  // State prefix
        db_key.insert(db_key.end(), key_str.begin(), key_str.end());
        
        if (value.empty()) {
            batch.deletes.push_back(db_key);
        } else {
            batch.puts.emplace_back(db_key, value);
            
            // Compute leaf hash
            Bytes leaf_data;
            leaf_data.insert(leaf_data.end(), key_str.begin(), key_str.end());
            leaf_data.insert(leaf_data.end(), value.begin(), value.end());
            leaf_hashes.push_back(crypto::Blake2b256::hash(leaf_data));
        }
    }
    
    db_->write_batch(batch);
    dirty_nodes_.clear();
    
    // Compute new root
    if (!leaf_hashes.empty()) {
        root_ = crypto::Blake2b256::merkle_root(leaf_hashes);
    }
    
    // Store root reference
    Bytes root_key = {0x00, 'R', 'O', 'O', 'T'};
    Bytes root_value(root_.begin(), root_.end());
    db_->put(root_key, root_value);
    
    return root_;
}

std::vector<Bytes> StateTrie::get_proof(const Bytes& key) const {
    // Simplified: return path hashes
    std::vector<Bytes> proof;
    
    auto key_hash = crypto::Blake2b256::hash(key);
    proof.emplace_back(key_hash.begin(), key_hash.end());
    
    auto value = get(key);
    if (value) {
        proof.push_back(*value);
    }
    
    proof.emplace_back(root_.begin(), root_.end());
    
    return proof;
}

bool StateTrie::verify_proof(const Hash256& root, const Bytes& key,
                              const Bytes& value, const std::vector<Bytes>& proof) {
    // In a real implementation, verify each step of the MPT path
    if (proof.size() < 3) return false;
    
    // Check root matches
    Hash256 proof_root;
    if (proof.back().size() == 32) {
        std::copy(proof.back().begin(), proof.back().end(), proof_root.begin());
    }
    
    return root == proof_root;
}

// ============================================================================
// BlockStore Implementation
// ============================================================================

BlockStore::BlockStore(std::shared_ptr<Database> db) : db_(db) {
    // Load head from database
    Bytes head_key = {'H', 'E', 'A', 'D'};
    auto head_data = db_->get(head_key);
    if (head_data && head_data->size() >= 8) {
        head_ = 0;
        for (int i = 0; i < 8; ++i) {
            head_ = (head_ << 8) | (*head_data)[i];
        }
    }
}

bool BlockStore::store_block(const Block& block) {
    std::unique_lock lock(mutex_);
    
    auto block_bytes = block.encode();
    auto block_hash = block.header.hash();
    
    // Store by number
    Bytes num_key = {'B', 'N'};
    for (int i = 7; i >= 0; --i) {
        num_key.push_back(static_cast<uint8_t>((block.header.number >> (i * 8)) & 0xFF));
    }
    db_->put(num_key, block_bytes);
    
    // Store hash -> number mapping
    Bytes hash_key = {'B', 'H'};
    hash_key.insert(hash_key.end(), block_hash.begin(), block_hash.end());
    
    Bytes num_bytes;
    for (int i = 7; i >= 0; --i) {
        num_bytes.push_back(static_cast<uint8_t>((block.header.number >> (i * 8)) & 0xFF));
    }
    db_->put(hash_key, num_bytes);
    
    // Update head if this is the new highest block
    if (block.header.number > head_) {
        head_ = block.header.number;
        Bytes head_key = {'H', 'E', 'A', 'D'};
        db_->put(head_key, num_bytes);
    }
    
    return true;
}

std::optional<Block> BlockStore::get_block(uint64_t number) const {
    std::shared_lock lock(mutex_);
    
    Bytes num_key = {'B', 'N'};
    for (int i = 7; i >= 0; --i) {
        num_key.push_back(static_cast<uint8_t>((number >> (i * 8)) & 0xFF));
    }
    
    auto data = db_->get(num_key);
    if (!data) return std::nullopt;
    
    return Block::decode(*data);
}

std::optional<Block> BlockStore::get_block_by_hash(const Hash256& hash) const {
    std::shared_lock lock(mutex_);
    
    // Get block number from hash
    Bytes hash_key = {'B', 'H'};
    hash_key.insert(hash_key.end(), hash.begin(), hash.end());
    
    auto num_data = db_->get(hash_key);
    if (!num_data || num_data->size() < 8) return std::nullopt;
    
    uint64_t number = 0;
    for (int i = 0; i < 8; ++i) {
        number = (number << 8) | (*num_data)[i];
    }
    
    return get_block(number);
}

void BlockStore::set_head(uint64_t number) {
    std::unique_lock lock(mutex_);
    head_ = number;
    
    Bytes head_key = {'H', 'E', 'A', 'D'};
    Bytes num_bytes;
    for (int i = 7; i >= 0; --i) {
        num_bytes.push_back(static_cast<uint8_t>((number >> (i * 8)) & 0xFF));
    }
    db_->put(head_key, num_bytes);
}

uint64_t BlockStore::get_head() const {
    std::shared_lock lock(mutex_);
    return head_;
}

void BlockStore::index_transaction(const Hash256& tx_hash, uint64_t block_number, 
                                    uint32_t tx_index) {
    Bytes key = {'T', 'X', 'I'};
    key.insert(key.end(), tx_hash.begin(), tx_hash.end());
    
    Bytes value;
    for (int i = 7; i >= 0; --i) {
        value.push_back(static_cast<uint8_t>((block_number >> (i * 8)) & 0xFF));
    }
    for (int i = 3; i >= 0; --i) {
        value.push_back(static_cast<uint8_t>((tx_index >> (i * 8)) & 0xFF));
    }
    
    db_->put(key, value);
}

std::optional<std::pair<uint64_t, uint32_t>> BlockStore::get_tx_location(
    const Hash256& tx_hash) const {
    
    Bytes key = {'T', 'X', 'I'};
    key.insert(key.end(), tx_hash.begin(), tx_hash.end());
    
    auto data = db_->get(key);
    if (!data || data->size() < 12) return std::nullopt;
    
    uint64_t block_num = 0;
    for (int i = 0; i < 8; ++i) {
        block_num = (block_num << 8) | (*data)[i];
    }
    
    uint32_t tx_idx = 0;
    for (int i = 0; i < 4; ++i) {
        tx_idx = (tx_idx << 8) | (*data)[8 + i];
    }
    
    return std::make_pair(block_num, tx_idx);
}

void BlockStore::store_receipt(const TransactionReceipt& receipt) {
    Bytes key = {'R', 'C', 'T'};
    key.insert(key.end(), receipt.transaction_hash.begin(), receipt.transaction_hash.end());
    
    // Serialize receipt
    Bytes value;
    // 1. Success (1 byte)
    value.push_back(receipt.success ? 1 : 0);
    
    // 2. Gas used (8 bytes)
    for (int i = 7; i >= 0; --i) value.push_back(static_cast<uint8_t>((receipt.gas_used >> (i * 8)) & 0xFF));
    
    // 3. Block number (8 bytes)
    for (int i = 7; i >= 0; --i) value.push_back(static_cast<uint8_t>((receipt.block_number >> (i * 8)) & 0xFF));
    
    // 4. Transaction index (8 bytes)
    for (int i = 7; i >= 0; --i) value.push_back(static_cast<uint8_t>((receipt.transaction_index >> (i * 8)) & 0xFF));
    
    // 5. Cumulative gas used (8 bytes)
    for (int i = 7; i >= 0; --i) value.push_back(static_cast<uint8_t>((receipt.cumulative_gas_used >> (i * 8)) & 0xFF));

    // 6. From address (28 bytes)
    value.insert(value.end(), receipt.from.payment_credential.begin(), receipt.from.payment_credential.end());

    // 7. To address (28 bytes)
    value.insert(value.end(), receipt.to.payment_credential.begin(), receipt.to.payment_credential.end());
    
    // 8. Contract address (1 byte flag + 28 bytes if present)
    if (receipt.contract_address) {
        value.push_back(1);
        value.insert(value.end(), receipt.contract_address->payment_credential.begin(), receipt.contract_address->payment_credential.end());
    } else {
        value.push_back(0);
    }

    // 9. Logs 
    // Count (4 bytes)
    uint32_t log_count = static_cast<uint32_t>(receipt.logs.size());
    for (int i = 3; i >= 0; --i) value.push_back(static_cast<uint8_t>((log_count >> (i * 8)) & 0xFF));
    
    for (const auto& log : receipt.logs) {
        // Address
        value.insert(value.end(), log.address.payment_credential.begin(), log.address.payment_credential.end());
        
        // Topics count (1 byte)
        value.push_back(static_cast<uint8_t>(log.topics.size()));
        for (const auto& topic : log.topics) {
            value.insert(value.end(), topic.begin(), topic.end());
        }
        
        // Data length (4 bytes)
        uint32_t data_len = static_cast<uint32_t>(log.data.size());
        for (int i = 3; i >= 0; --i) value.push_back(static_cast<uint8_t>((data_len >> (i * 8)) & 0xFF));
        // Data
        value.insert(value.end(), log.data.begin(), log.data.end());
    }

    db_->put(key, value);
}

std::optional<TransactionReceipt> BlockStore::get_receipt(const Hash256& tx_hash) const {
    Bytes key = {'R', 'C', 'T'};
    key.insert(key.end(), tx_hash.begin(), tx_hash.end());
    
    auto data = db_->get(key);
    if (!data || data->size() < 17) return std::nullopt;
    
    TransactionReceipt receipt;
    receipt.transaction_hash = tx_hash;
    size_t offset = 0;
    
    // 1. Success
    if (offset >= data->size()) return std::nullopt;
    receipt.success = ((*data)[offset++] != 0);
    receipt.status = receipt.success ? 1 : 0;
    
    // Helper to read uint64
    auto read_uint64 = [&](size_t& off) {
        uint64_t val = 0;
        for (int i = 0; i < 8; ++i) val = (val << 8) | (*data)[off++];
        return val;
    };
    
    if (offset + 32 > data->size()) return std::nullopt; // Ensure enough bytes for basic fields
    
    receipt.gas_used = read_uint64(offset);
    receipt.block_number = read_uint64(offset);
    receipt.transaction_index = read_uint64(offset);
    receipt.cumulative_gas_used = read_uint64(offset);
    
    // Addresses
    if (offset + 56 > data->size()) return std::nullopt;
    std::copy(data->begin() + offset, data->begin() + offset + 28, receipt.from.payment_credential.begin());
    offset += 28;
    std::copy(data->begin() + offset, data->begin() + offset + 28, receipt.to.payment_credential.begin());
    offset += 28;
    
    // Contract address
    if (offset >= data->size()) return std::nullopt;
    if ((*data)[offset++]) {
        if (offset + 28 > data->size()) return std::nullopt;
        Address addr;
        std::copy(data->begin() + offset, data->begin() + offset + 28, addr.payment_credential.begin());
        receipt.contract_address = addr;
        offset += 28;
    }
    
    // Logs
    if (offset + 4 > data->size()) return std::nullopt;
    uint32_t log_count = 0;
    for (int i = 0; i < 4; ++i) log_count = (log_count << 8) | (*data)[offset++];
    
    for (uint32_t k = 0; k < log_count; ++k) {
        Log log;
        if (offset + 28 >= data->size()) break;
        std::copy(data->begin() + offset, data->begin() + offset + 28, log.address.payment_credential.begin());
        offset += 28;
        
        uint8_t topic_count = (*data)[offset++];
        for (int t = 0; t < topic_count; ++t) {
            if (offset + 32 > data->size()) return std::nullopt;
            Hash256 topic;
            std::copy(data->begin() + offset, data->begin() + offset + 32, topic.begin());
            log.topics.push_back(topic);
            offset += 32;
        }
        
        if (offset + 4 > data->size()) return std::nullopt;
        uint32_t data_len = 0;
        for (int i = 0; i < 4; ++i) data_len = (data_len << 8) | (*data)[offset++];
        
        if (offset + data_len > data->size()) return std::nullopt;
        log.data.assign(data->begin() + offset, data->begin() + offset + data_len);
        offset += data_len;
        
        receipt.logs.push_back(log);
    }
    
    return receipt;
}

// ============================================================================
// StateManager Implementation
// ============================================================================

StateManager::StateManager(std::shared_ptr<Database> db)
    : db_(db), account_trie_(std::make_unique<StateTrie>(db)) {}

StateManager::StateManager(std::shared_ptr<Database> db, const Hash256& state_root)
    : db_(db), account_trie_(std::make_unique<StateTrie>(db, state_root)) {}

AccountState StateManager::get_account(const Address& addr) const {
    auto key = Bytes(addr.payment_credential.begin(), addr.payment_credential.end());
    auto data = account_trie_->get(key);
    
    if (data) {
        return AccountState::decode(*data);
    }
    return AccountState{};  // Empty account
}

void StateManager::set_account(const Address& addr, const AccountState& state) {
    auto key = Bytes(addr.payment_credential.begin(), addr.payment_credential.end());
    auto value = state.encode();
    
    // Record for journal (revert support)
    JournalEntry entry;
    entry.addr = addr;
    auto prev = account_trie_->get(key);
    if (prev) {
        entry.prev_state = AccountState::decode(*prev);
    }
    journal_.push_back(entry);
    
    account_trie_->put(key, value);
}

uint64_t StateManager::get_balance(const Address& addr) const {
    return get_account(addr).balance;
}

void StateManager::add_balance(const Address& addr, uint64_t amount) {
    auto state = get_account(addr);
    state.balance += amount;
    set_account(addr, state);
}

void StateManager::sub_balance(const Address& addr, uint64_t amount) {
    auto state = get_account(addr);
    if (state.balance >= amount) {
        state.balance -= amount;
        set_account(addr, state);
    }
}

uint64_t StateManager::get_nonce(const Address& addr) const {
    return get_account(addr).nonce;
}

void StateManager::increment_nonce(const Address& addr) {
    auto state = get_account(addr);
    state.nonce++;
    set_account(addr, state);
}

Bytes StateManager::get_storage(const Address& addr, const Hash256& key) const {
    // Storage key = account_address || slot_key
    Bytes storage_key(addr.payment_credential.begin(), addr.payment_credential.end());
    storage_key.insert(storage_key.end(), key.begin(), key.end());
    
    Bytes db_key = {'S', 'T', 'O', 'R'};
    db_key.insert(db_key.end(), storage_key.begin(), storage_key.end());
    
    auto data = db_->get(db_key);
    return data.value_or(Bytes{});
}

void StateManager::set_storage(const Address& addr, const Hash256& key, const Bytes& value) {
    Bytes storage_key(addr.payment_credential.begin(), addr.payment_credential.end());
    storage_key.insert(storage_key.end(), key.begin(), key.end());
    
    Bytes db_key = {'S', 'T', 'O', 'R'};
    db_key.insert(db_key.end(), storage_key.begin(), storage_key.end());
    
    db_->put(db_key, value);
}

Bytes StateManager::get_code(const Address& addr) const {
    auto state = get_account(addr);
    
    Bytes db_key = {'C', 'O', 'D', 'E'};
    db_key.insert(db_key.end(), state.code_hash.begin(), state.code_hash.end());
    
    auto data = db_->get(db_key);
    return data.value_or(Bytes{});
}

void StateManager::set_code(const Address& addr, const Bytes& code) {
    auto code_hash = crypto::Blake2b256::hash(code);
    
    // Store code by hash
    Bytes db_key = {'C', 'O', 'D', 'E'};
    db_key.insert(db_key.end(), code_hash.begin(), code_hash.end());
    db_->put(db_key, code);
    
    // Update account
    auto state = get_account(addr);
    state.code_hash = code_hash;
    set_account(addr, state);
}

Hash256 StateManager::commit() {
    return account_trie_->commit();
}

Hash256 StateManager::state_root() const {
    return account_trie_->root();
}

StateManager::Snapshot StateManager::snapshot() const {
    return Snapshot{account_trie_->root(), journal_.size()};
}

void StateManager::revert(const Snapshot& snap) {
    // Revert journal entries back to snapshot point
    while (journal_.size() > snap.journal_size) {
        auto& entry = journal_.back();
        
        auto key = Bytes(entry.addr.payment_credential.begin(), 
                        entry.addr.payment_credential.end());
        
        if (entry.prev_state.has_value()) {
            account_trie_->put(key, entry.prev_state->encode());
        } else {
            account_trie_->del(key);
        }
        
        journal_.pop_back();
    }
}

} // namespace storage
} // namespace nonagon
