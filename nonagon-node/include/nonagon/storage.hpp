#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include "nonagon/types.hpp"

namespace nonagon {
namespace storage {

/**
 * @brief Abstract database interface
 * 
 * Allows swapping between RocksDB (production) and in-memory (testing)
 */
class Database {
public:
    virtual ~Database() = default;
    
    virtual bool put(const Bytes& key, const Bytes& value) = 0;
    virtual std::optional<Bytes> get(const Bytes& key) = 0;
    virtual bool del(const Bytes& key) = 0;
    virtual bool exists(const Bytes& key) = 0;
    
    // Batch operations for efficiency
    struct WriteBatch {
        std::vector<std::pair<Bytes, Bytes>> puts;
        std::vector<Bytes> deletes;
    };
    virtual bool write_batch(const WriteBatch& batch) = 0;
    
    // Iterator for range queries
    class Iterator {
    public:
        virtual ~Iterator() = default;
        virtual bool valid() = 0;
        virtual void next() = 0;
        virtual Bytes key() = 0;
        virtual Bytes value() = 0;
    };
    virtual std::unique_ptr<Iterator> new_iterator(const Bytes& prefix) = 0;
};

/**
 * @brief In-memory database for testing
 */
class MemoryDatabase : public Database {
public:
    bool put(const Bytes& key, const Bytes& value) override;
    std::optional<Bytes> get(const Bytes& key) override;
    bool del(const Bytes& key) override;
    bool exists(const Bytes& key) override;
    bool write_batch(const WriteBatch& batch) override;
    std::unique_ptr<Iterator> new_iterator(const Bytes& prefix) override;

private:
    mutable std::shared_mutex mutex_;
    std::map<Bytes, Bytes> data_;
};

/**
 * @brief Persistent database using append-only log file.
 * Ensures data survives node restarts.
 */
class PersistentDatabase : public Database {
public:
    explicit PersistentDatabase(const std::string& path);
    ~PersistentDatabase();
    
    bool put(const Bytes& key, const Bytes& value) override;
    std::optional<Bytes> get(const Bytes& key) override;
    bool del(const Bytes& key) override;
    bool exists(const Bytes& key) override;
    bool write_batch(const WriteBatch& batch) override;
    std::unique_ptr<Iterator> new_iterator(const Bytes& prefix) override;

private:
    void load();
    void append_log(uint8_t op, const Bytes& key, const Bytes& value);
    
    std::string path_;
    mutable std::shared_mutex mutex_;
    std::map<Bytes, Bytes> data_;
    std::ofstream file_;
};

/**
 * @brief Merkle Patricia Trie for state storage
 * 
 * Implements Ethereum-style MPT for:
 * - Account state storage
 * - Contract storage
 * - Efficient state root computation
 * - State proofs for fraud proofs
 */
class StateTrie {
public:
    explicit StateTrie(std::shared_ptr<Database> db);
    StateTrie(std::shared_ptr<Database> db, const Hash256& root);
    
    // State operations
    void put(const Bytes& key, const Bytes& value);
    std::optional<Bytes> get(const Bytes& key) const;
    void del(const Bytes& key);
    
    // Commit changes and get new root
    Hash256 commit();
    Hash256 root() const { return root_; }
    
    // Generate proof for a key
    std::vector<Bytes> get_proof(const Bytes& key) const;
    static bool verify_proof(const Hash256& root, const Bytes& key, 
                             const Bytes& value, const std::vector<Bytes>& proof);

private:
    std::shared_ptr<Database> db_;
    Hash256 root_;
    std::unordered_map<std::string, Bytes> dirty_nodes_;
};

/**
 * @brief Block storage with chain management
 */
class BlockStore {
public:
    explicit BlockStore(std::shared_ptr<Database> db);
    
    // Block operations
    bool store_block(const Block& block);
    std::optional<Block> get_block(uint64_t number) const;
    std::optional<Block> get_block_by_hash(const Hash256& hash) const;
    
    // Chain head management
    void set_head(uint64_t number);
    uint64_t get_head() const;
    
    // Transaction indexing
    void index_transaction(const Hash256& tx_hash, uint64_t block_number, uint32_t tx_index);
    std::optional<std::pair<uint64_t, uint32_t>> get_tx_location(const Hash256& tx_hash) const;
    
    // Receipt storage
    void store_receipt(const TransactionReceipt& receipt);
    std::optional<TransactionReceipt> get_receipt(const Hash256& tx_hash) const;

private:
    std::shared_ptr<Database> db_;
    mutable std::shared_mutex mutex_;
    uint64_t head_{0};
};

/**
 * @brief State manager for account states
 */
class StateManager {
public:
    explicit StateManager(std::shared_ptr<Database> db);
    StateManager(std::shared_ptr<Database> db, const Hash256& state_root);
    
    // Account state operations
    AccountState get_account(const Address& addr) const;
    void set_account(const Address& addr, const AccountState& state);
    
    // Convenience methods
    uint64_t get_balance(const Address& addr) const;
    void add_balance(const Address& addr, uint64_t amount);
    void sub_balance(const Address& addr, uint64_t amount);
    
    uint64_t get_nonce(const Address& addr) const;
    void increment_nonce(const Address& addr);
    
    // Contract storage
    Bytes get_storage(const Address& addr, const Hash256& key) const;
    void set_storage(const Address& addr, const Hash256& key, const Bytes& value);
    
    // Contract code
    Bytes get_code(const Address& addr) const;
    void set_code(const Address& addr, const Bytes& code);
    
    // State root
    Hash256 commit();
    Hash256 state_root() const;
    
    // Snapshot for revert on failed tx
    struct Snapshot {
        Hash256 state_root;
        size_t journal_size;
    };
    Snapshot snapshot() const;
    void revert(const Snapshot& snap);

private:
    std::shared_ptr<Database> db_;
    std::unique_ptr<StateTrie> account_trie_;
    
    // Journal for reverts
    struct JournalEntry {
        Address addr;
        std::optional<AccountState> prev_state;
    };
    std::vector<JournalEntry> journal_;
};

} // namespace storage
} // namespace nonagon
