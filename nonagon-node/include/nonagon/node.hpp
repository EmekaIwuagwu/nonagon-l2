#pragma once

#include <memory>
#include <string>
#include <fstream>
#include "nonagon/types.hpp"
#include "nonagon/storage.hpp"
#include "nonagon/execution.hpp"
#include "nonagon/consensus.hpp"
#include "nonagon/settlement.hpp"
#include "nonagon/network.hpp"
#include "nonagon/rpc.hpp"

namespace nonagon {

/**
 * @brief Node configuration
 */
struct NodeConfig {
    // Identity
    std::string node_name{"nonagon-node"};
    std::string data_dir{"./data"};
    uint64_t chain_id{1};  // 1 = mainnet, 2 = testnet
    
    // Genesis
    std::string genesis_file{"genesis.json"};
    
    // Network
    network::NetworkConfig network;
    
    // RPC
    rpc::ServerConfig rpc;
    
    // Settlement
    settlement::CardanoConfig cardano;
    
    // Consensus
    consensus::ConsensusConfig consensus;
    
    // Sequencer mode
    bool is_sequencer{false};
    std::string sequencer_key_file;
    Address sequencer_address;
    
    // Logging
    enum class LogLevel {
        Trace,
        Debug,
        Info,
        Warn,
        Error
    };
    LogLevel log_level{LogLevel::Info};
    std::string log_file;
    
    // Load from file
    static NodeConfig from_file(const std::string& path);
    void save_to_file(const std::string& path) const;
};

/**
 * @brief Genesis block configuration
 */
struct GenesisConfig {
    uint64_t chain_id{1};
    uint64_t timestamp{0};
    uint64_t gas_limit{30000000};
    uint64_t base_fee{1000000000};
    
    // Pre-funded accounts
    struct Allocation {
        Address address;
        uint64_t balance;
    };
    std::vector<Allocation> alloc;
    
    // Initial sequencer set
    std::vector<consensus::Sequencer> sequencers;
    
    // Cardano L1 anchor
    std::string cardano_genesis_hash;
    uint64_t cardano_genesis_slot{0};
    
    // Load from file
    static GenesisConfig from_file(const std::string& path);
    Block to_genesis_block() const;
};

/**
 * @brief Metrics and monitoring
 */
class Metrics {
public:
    // Counters
    void increment(const std::string& name, uint64_t value = 1);
    uint64_t get_counter(const std::string& name) const;
    
    // Gauges
    void set_gauge(const std::string& name, double value);
    double get_gauge(const std::string& name) const;
    
    // Histograms
    void observe(const std::string& name, double value);
    
    // Prometheus format export
    std::string prometheus_export() const;
    
    // Common metrics
    static Metrics& instance();
    
    // Pre-defined metric names
    static constexpr const char* BLOCKS_PROCESSED = "nonagon_blocks_processed_total";
    static constexpr const char* TXS_PROCESSED = "nonagon_transactions_processed_total";
    static constexpr const char* PENDING_TXS = "nonagon_pending_transactions";
    static constexpr const char* PEER_COUNT = "nonagon_peer_count";
    static constexpr const char* CHAIN_HEAD = "nonagon_chain_head";
    static constexpr const char* STATE_ROOT = "nonagon_state_root";
    static constexpr const char* BATCH_SUBMITTED = "nonagon_batches_submitted_total";
    static constexpr const char* BLOCK_TIME_MS = "nonagon_block_time_ms";
    static constexpr const char* GAS_USED = "nonagon_gas_used_total";

private:
    Metrics() = default;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, uint64_t> counters_;
    std::unordered_map<std::string, double> gauges_;
    std::unordered_map<std::string, std::vector<double>> histograms_;
};

/**
 * @brief Main Nonagon Node
 * 
 * Orchestrates all components:
 * - Storage (RocksDB + State Trie)
 * - Execution (EVM)
 * - Consensus (Rotating Sequencer Set)
 * - Settlement (Cardano Bridge)
 * - Network (P2P)
 * - RPC (JSON-RPC API)
 */
class Node {
public:
    explicit Node(const NodeConfig& config);
    ~Node();
    
    // Lifecycle
    bool initialize();
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    // Chain state
    uint64_t chain_head() const;
    Hash256 state_root() const;
    Block latest_block() const;
    
    // Transaction submission
    Hash256 submit_transaction(const Transaction& tx);
    
    // Sequencer operations (if enabled)
    bool is_sequencer() const { return config_.is_sequencer; }
    void produce_block();
    void submit_batch();
    
    // Component access
    std::shared_ptr<storage::StateManager> state_manager() { return state_manager_; }
    std::shared_ptr<storage::BlockStore> block_store() { return block_store_; }
    std::shared_ptr<consensus::Mempool> mempool() { return mempool_; }
    std::shared_ptr<network::P2PNetwork> network() { return network_; }
    std::shared_ptr<rpc::Server> rpc_server() { return rpc_server_; }
    std::shared_ptr<settlement::SettlementManager> settlement_manager() { return settlement_manager_; }
    std::shared_ptr<consensus::ConsensusEngine> consensus() { return consensus_; }
    std::shared_ptr<execution::TransactionProcessor> transaction_processor() { return tx_processor_; }
    
    // Health check
    struct HealthStatus {
        bool healthy;
        bool synced;
        uint64_t chain_head;
        uint64_t l1_finalized;
        size_t peer_count;
        std::string version;
    };
    HealthStatus health() const;

private:
    NodeConfig config_;
    std::atomic<bool> running_{false};
    
    // Storage layer
    std::shared_ptr<storage::Database> db_;
    std::shared_ptr<storage::StateManager> state_manager_;
    std::shared_ptr<storage::BlockStore> block_store_;
    
    // Execution layer
    std::shared_ptr<execution::EVM> evm_;
    std::shared_ptr<execution::TransactionProcessor> tx_processor_;
    std::shared_ptr<execution::BlockProcessor> block_processor_;
    
    // Consensus layer
    std::shared_ptr<consensus::ConsensusEngine> consensus_;
    std::shared_ptr<consensus::Mempool> mempool_;
    
    // Settlement layer
    std::shared_ptr<settlement::CardanoClient> cardano_client_;
    std::shared_ptr<settlement::BatchBuilder> batch_builder_;
    std::shared_ptr<settlement::SettlementManager> settlement_manager_;
    
    // Network layer
    std::shared_ptr<network::P2PNetwork> network_;
    std::shared_ptr<network::BlockSynchronizer> synchronizer_;
    
    // RPC layer
    std::shared_ptr<rpc::Server> rpc_server_;
    std::shared_ptr<rpc::EthNamespace> eth_rpc_;
    std::shared_ptr<rpc::NonagonNamespace> nonagon_rpc_;
    
    // Sequencer key (if sequencer mode)
    std::optional<crypto::Ed25519::KeyPair> sequencer_keypair_;
    
    // Background threads
    std::thread block_production_thread_;
    std::thread batch_submission_thread_;
    
    void init_genesis();
    void setup_rpc_handlers();
    void block_production_loop();
    void batch_submission_loop();
    
    void on_new_block(const Block& block);
    void on_new_transaction(const Transaction& tx);
};

} // namespace nonagon
