#include "nonagon/node.hpp"
#include <iostream>
#include <fstream>

namespace nonagon {

// ============================================================================
// NodeConfig Implementation
// ============================================================================

NodeConfig NodeConfig::from_file(const std::string& path) {
    NodeConfig config;
    std::ifstream file(path);
    if (!file.is_open()) return config;

    std::string line;
    std::string current_section = "node";

    auto trim = [](const std::string& str) {
        auto first = str.find_first_not_of(" \t\r\n");
        if (std::string::npos == first) return std::string();
        auto last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    };

    auto to_uint64 = [](const std::string& s) {
        try { return std::stoull(s); } catch(...) { return 0ULL; }
    };
    auto to_uint16 = [](const std::string& s) {
        try { return (uint16_t)std::stoul(s); } catch(...) { return (uint16_t)0; }
    };

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        if (line.front() == '[' && line.back() == ']') {
            current_section = line.substr(1, line.size() - 2);
            continue;
        }

        auto eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = trim(line.substr(0, eq));
            std::string val_str = trim(line.substr(eq + 1));
            
            // Remove quotes if present
            if (val_str.size() >= 2 && val_str.front() == '"' && val_str.back() == '"') {
                val_str = val_str.substr(1, val_str.size() - 2);
            }

            if (current_section == "node") {
                 if (key == "name") config.node_name = val_str;
                 else if (key == "data_dir") config.data_dir = val_str;
                 else if (key == "chain_id") config.chain_id = to_uint64(val_str);
                 else if (key == "is_sequencer") config.is_sequencer = (val_str == "true");
                 else if (key == "sequencer_key_file") config.sequencer_key_file = val_str;
                 else if (key == "sequencer_address") config.sequencer_address = Address::from_hex(val_str).value_or(Address{});
            }
            else if (current_section == "network") {
                 if (key == "listen_port") config.network.listen_port = to_uint16(val_str);
                 else if (key == "max_peers") config.network.max_peers = (uint32_t)to_uint64(val_str);
            }
            else if (current_section == "rpc") {
                 if (key == "http_port") config.rpc.http_port = to_uint16(val_str);
                 else if (key == "ws_port") config.rpc.ws_port = to_uint16(val_str);
            }
            else if (current_section == "consensus") {
                 if (key == "block_time_ms") config.consensus.block_time_ms = to_uint64(val_str);
                 else if (key == "max_sequencers") config.consensus.max_sequencers = (uint32_t)to_uint64(val_str);
                 else if (key == "min_stake") config.consensus.min_stake = to_uint64(val_str);
            }
            else if (current_section == "settlement") {
                if (key == "cardano_node") config.cardano.node_socket_path = val_str;
                else if (key == "state_contract") config.cardano.state_contract = val_str;
                else if (key == "challenge_period_seconds") config.cardano.challenge_period_slots = to_uint64(val_str); // Approximating slots=seconds for now
            }
        }
    }
    return config;
}

void NodeConfig::save_to_file(const std::string& path) const {
    std::ofstream file(path);
    if (file.is_open()) {
        file << "# Nonagon Node Configuration\n\n";
        
        file << "[node]\n";
        file << "name = \"" << node_name << "\"\n";
        file << "data_dir = \"" << data_dir << "\"\n";
        file << "chain_id = " << chain_id << "\n";
        file << "is_sequencer = " << (is_sequencer ? "true" : "false") << "\n";
        if (!sequencer_key_file.empty()) file << "sequencer_key_file = \"" << sequencer_key_file << "\"\n";
        if (is_sequencer) file << "sequencer_address = \"" << sequencer_address.to_hex() << "\"\n";
        file << "\n";

        file << "[network]\n";
        file << "listen_port = " << network.listen_port << "\n";
        file << "max_peers = " << network.max_peers << "\n\n";

        file << "[rpc]\n";
        file << "http_port = " << rpc.http_port << "\n";
        file << "ws_port = " << rpc.ws_port << "\n\n";

        file << "[consensus]\n";
        file << "block_time_ms = " << consensus.block_time_ms << "\n";
        file << "max_sequencers = " << consensus.max_sequencers << "\n";
        file << "min_stake = " << consensus.min_stake << "\n\n";

        file << "[settlement]\n";
        file << "cardano_node = \"" << cardano.node_socket_path << "\"\n";
        file << "state_contract = \"" << cardano.state_contract << "\"\n";
        file << "challenge_period_seconds = " << cardano.challenge_period_slots << "\n";
        
        file.close();
    }
}

// ============================================================================
// GenesisConfig Implementation
// ============================================================================

GenesisConfig GenesisConfig::from_file(const std::string& path) {
    GenesisConfig config;
    // TODO: Implement JSON genesis parsing
    config.chain_id = 1;
    config.timestamp = 0;  // Fixed genesis time for reproducibility
    config.gas_limit = 30000000;
    config.base_fee = 1000000000;  // 1 Gwei
    
    // Pre-fund test accounts for development/testing
    // Each account gets 100 NATX (100 * 10^18 wei = 10^20 wei)
    // Note: uint64_t max is ~1.8 * 10^19, so we use 10 NATX = 10^19
    
    // Alice: address ending in ...01
    Address alice;
    alice.payment_credential[27] = 0x01;
    config.alloc.push_back({alice, 10000000000000000000ULL});  // 10 NATX
    
    // Bob: address ending in ...02
    Address bob;
    bob.payment_credential[27] = 0x02;
    config.alloc.push_back({bob, 10000000000000000000ULL});  // 10 NATX
    
    // Charlie: address ending in ...03
    Address charlie;
    charlie.payment_credential[27] = 0x03;
    config.alloc.push_back({charlie, 10000000000000000000ULL});  // 10 NATX
    
    // Faucet: address ending in ...FF (for testing)
    Address faucet;
    faucet.payment_credential[27] = 0xFF;
    config.alloc.push_back({faucet, 10000000000000000000ULL});  // 10 NATX
    
    std::cout << "[GENESIS] Pre-funded 4 test accounts with NATX" << std::endl;
    
    return config;
}

Block GenesisConfig::to_genesis_block() const {
    Block genesis;
    genesis.header.number = 0;
    genesis.header.parent_hash = {};  // Zero hash
    genesis.header.timestamp = timestamp;
    genesis.header.gas_limit = gas_limit;
    genesis.header.base_fee = base_fee;
    
    // Compute initial state root from allocations
    genesis.header.state_root = {};  // Would be computed from alloc
    genesis.header.transactions_root = {};  // Empty
    genesis.header.receipts_root = {};  // Empty
    
    return genesis;
}

// ============================================================================
// Metrics Implementation
// ============================================================================

Metrics& Metrics::instance() {
    static Metrics instance;
    return instance;
}

void Metrics::increment(const std::string& name, uint64_t value) {
    std::unique_lock lock(mutex_);
    counters_[name] += value;
}

uint64_t Metrics::get_counter(const std::string& name) const {
    std::shared_lock lock(mutex_);
    auto it = counters_.find(name);
    return it != counters_.end() ? it->second : 0;
}

void Metrics::set_gauge(const std::string& name, double value) {
    std::unique_lock lock(mutex_);
    gauges_[name] = value;
}

double Metrics::get_gauge(const std::string& name) const {
    std::shared_lock lock(mutex_);
    auto it = gauges_.find(name);
    return it != gauges_.end() ? it->second : 0.0;
}

void Metrics::observe(const std::string& name, double value) {
    std::unique_lock lock(mutex_);
    histograms_[name].push_back(value);
}

std::string Metrics::prometheus_export() const {
    std::shared_lock lock(mutex_);
    std::stringstream ss;
    
    for (const auto& [name, value] : counters_) {
        ss << name << " " << value << "\n";
    }
    
    for (const auto& [name, value] : gauges_) {
        ss << name << " " << value << "\n";
    }
    
    return ss.str();
}

// ============================================================================
// Node Implementation
// ============================================================================

Node::Node(const NodeConfig& config) : config_(config) {
    std::cout << "[NONAGON] Creating node with config:" << std::endl;
    std::cout << "[NONAGON]   Name: " << config_.node_name << std::endl;
    std::cout << "[NONAGON]   Data dir: " << config_.data_dir << std::endl;
    std::cout << "[NONAGON]   Chain ID: " << config_.chain_id << std::endl;
}

Node::~Node() {
    if (running_) {
        stop();
    }
}

bool Node::initialize() {
    std::cout << "[NONAGON] Initializing node..." << std::endl;
    
    try {
        // Initialize storage layer
        std::cout << "[NONAGON]   Initializing storage..." << std::endl;
        // Initialize storage
        std::string db_path = config_.data_dir + "/chain.db";
        std::cout << "[NONAGON] Opening database at " << db_path << std::endl;
        db_ = std::make_shared<storage::PersistentDatabase>(db_path);
        
        state_manager_ = std::make_shared<storage::StateManager>(db_);
        block_store_ = std::make_shared<storage::BlockStore>(db_);
        
        // Initialize consensus layer
        std::cout << "[NONAGON]   Initializing consensus..." << std::endl;
        consensus_ = std::make_shared<consensus::ConsensusEngine>(config_.consensus);
        mempool_ = std::make_shared<consensus::Mempool>(10000);
        
        // Initialize network layer
        std::cout << "[NONAGON]   Initializing network..." << std::endl;
        network_ = std::make_shared<network::P2PNetwork>(config_.network);
        synchronizer_ = std::make_shared<network::BlockSynchronizer>(
            network_, block_store_, state_manager_);
            
        // Initialize settlement layer
        std::cout << "[NONAGON]   Initializing settlement..." << std::endl;
        cardano_client_ = std::make_shared<settlement::CardanoClient>(config_.cardano);
        
        settlement::BatchBuilder::Config builder_config;
        builder_config.max_batch_size = 10; // Small for testing
        builder_config.min_batch_size = 1;
        builder_config.max_batch_age_seconds = 30;
        
        batch_builder_ = std::make_shared<settlement::BatchBuilder>(builder_config);
        
        settlement_manager_ = std::make_shared<settlement::SettlementManager>(
            cardano_client_, batch_builder_);
        
        // Initialize genesis if needed
        if (block_store_->get_head() == 0) {
            std::cout << "[NONAGON]   Initializing genesis block..." << std::endl;
            init_genesis();
        }
        
        std::cout << "[NONAGON] Initialization complete!" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "[NONAGON] Initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void Node::init_genesis() {
    GenesisConfig genesis_config = GenesisConfig::from_file(config_.genesis_file);
    Block genesis = genesis_config.to_genesis_block();
    
    // Store genesis block
    block_store_->store_block(genesis);
    
    // Apply genesis allocations
    for (const auto& alloc : genesis_config.alloc) {
        state_manager_->add_balance(alloc.address, alloc.balance);
    }
    
    // Commit initial state
    auto state_root = state_manager_->commit();
    std::cout << "[NONAGON]   Genesis state root computed" << std::endl;
}

bool Node::start() {
    if (running_) {
        return true;
    }
    
    std::cout << "[NONAGON] Starting node services..." << std::endl;
    running_ = true;
    
    // Start network
    if (network_) network_->start();
    if (synchronizer_) synchronizer_->start();
    
    // Start block production if sequencer
    if (config_.is_sequencer) {
        if (settlement_manager_) settlement_manager_->start();
        
        std::cout << "[NONAGON]   Starting block production thread..." << std::endl;
        block_production_thread_ = std::thread([this]() {
            block_production_loop();
        });
    }
    
    std::cout << "[NONAGON] All services started!" << std::endl;
    return true;
}

void Node::stop() {
    if (!running_) {
        return;
    }
    
    std::cout << "[NONAGON] Stopping node..." << std::endl;
    running_ = false;
    
    // Wait for threads to finish
    if (block_production_thread_.joinable()) {
        block_production_thread_.join();
    }
    
    if (settlement_manager_) settlement_manager_->stop();
    if (synchronizer_) synchronizer_->stop();
    if (network_) network_->stop();
    if (batch_submission_thread_.joinable()) {
        batch_submission_thread_.join();
    }
    
    std::cout << "[NONAGON] Node stopped" << std::endl;
}

uint64_t Node::chain_head() const {
    return block_store_->get_head();
}

Hash256 Node::state_root() const {
    return state_manager_->state_root();
}

Block Node::latest_block() const {
    auto block = block_store_->get_block(chain_head());
    return block.value_or(Block{});
}

Hash256 Node::submit_transaction(const Transaction& tx) {
    auto tx_hash = tx.hash();
    
    // Get sender balance for validation
    uint64_t balance = state_manager_->get_balance(tx.from);
    
    auto result = mempool_->add_transaction(tx, balance);
    if (result == consensus::Mempool::AddResult::Added || 
        result == consensus::Mempool::AddResult::Replaced) {
        Metrics::instance().increment(Metrics::TXS_PROCESSED);
        return tx_hash;
    }
    
    return {};  // Empty hash indicates failure
}

void Node::produce_block() {
    if (!config_.is_sequencer) {
        return;
    }
    
    // Get current chain state
    uint64_t current_head = chain_head();
    auto latest = block_store_->get_block(current_head);
    
    uint64_t base_fee = latest ? latest->header.base_fee : 1000000000;
    uint64_t gas_limit = latest ? latest->header.gas_limit : 30000000;
    Hash256 parent_hash = latest ? latest->header.hash() : Hash256{};
    uint64_t new_block_number = current_head + 1;
    
    // Get transactions from mempool
    auto txs = mempool_->get_block_transactions(gas_limit, base_fee);
    
    // We produce blocks even if empty (heartbeat blocks)
    // But limit empty blocks to every 5 seconds
    static auto last_empty_block = std::chrono::steady_clock::now();
    if (txs.empty()) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - last_empty_block).count() < 5) {
            return;
        }
        last_empty_block = now;
    }
    
    // Execute transactions and build receipts
    std::vector<TransactionReceipt> receipts;
    std::vector<Transaction> executed_txs;
    uint64_t total_gas_used = 0;
    
    for (const auto& tx : txs) {
        // Check sender has enough balance
        uint64_t sender_balance = state_manager_->get_balance(tx.from);
        uint64_t tx_cost = tx.value + (tx.gas_limit * tx.max_fee_per_gas);
        
        if (sender_balance < tx_cost) {
            std::cout << "[BLOCK] Skipping tx - insufficient balance" << std::endl;
            continue;
        }
        
        // Create execution context
        execution::ExecutionContext ctx;
        ctx.block_number = new_block_number;
        ctx.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        ctx.gas_limit = gas_limit;
        ctx.base_fee = base_fee;
        ctx.chain_id = 88; // Nonagon Testnet
        ctx.coinbase = config_.sequencer_address;
        ctx.caller = tx.from;
        ctx.origin = tx.from;
        ctx.gas_price = tx.max_fee_per_gas;
        ctx.block_hash = parent_hash;

        TransactionReceipt receipt;
        uint64_t gas_used = 0;

        if (tx_processor_) {
            auto result = tx_processor_->process(tx, ctx);
            receipt = result.receipt;
            gas_used = result.gas_used;
            
            // TransactionProcessor handles state updates
        } else {
            // Fallback for robust start handling
            state_manager_->sub_balance(tx.from, tx.value);
            state_manager_->add_balance(tx.to, tx.value);
            state_manager_->increment_nonce(tx.from);
            gas_used = 21000;
            
            receipt.transaction_hash = tx.hash();
            receipt.success = true;
            receipt.status = 1;
            receipt.gas_used = gas_used;
            receipt.from = tx.from;
            receipt.to = tx.to;
            receipt.block_number = new_block_number;
            receipt.transaction_index = receipts.size();
        }

        receipt.cumulative_gas_used = total_gas_used + gas_used;
        total_gas_used += gas_used;
        
        receipts.push_back(receipt);
        executed_txs.push_back(tx);
        
        std::cout << "[BLOCK] Executed tx: " << tx.value << " wei from ";
        printf("%02x...%02x", tx.from.payment_credential[0], tx.from.payment_credential[27]);
        std::cout << " to ";
        printf("%02x...%02x", tx.to.payment_credential[0], tx.to.payment_credential[27]);
        std::cout << std::endl;
    }
    
    // Commit state changes
    auto new_state_root = state_manager_->commit();
    
    // Build block
    Block block;
    block.header.number = new_block_number;
    block.header.parent_hash = parent_hash;
    block.header.state_root = new_state_root;
    block.header.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    block.header.gas_limit = gas_limit;
    block.header.gas_used = total_gas_used;
    block.header.base_fee = base_fee;
    block.header.batch_id = settlement_manager_ ? settlement_manager_->get_current_batch_id() : 0;
    block.transactions = executed_txs;
    
    // Compute merkle roots
    block.header.transactions_root = block.compute_transactions_root();
    
    // Compute receipts root
    std::vector<Hash256> receipt_hashes;
    for (const auto& r : receipts) {
        receipt_hashes.push_back(r.hash());
    }
    block.header.receipts_root = crypto::Blake2b256::merkle_root(receipt_hashes);
    
    // Store block
    block_store_->store_block(block);
    
    // Store receipts and index transactions
    for (const auto& r : receipts) {
        block_store_->store_receipt(r);
        block_store_->index_transaction(r.transaction_hash, block.header.number, r.transaction_index);
    }
    
    // Remove confirmed transactions from mempool
    std::vector<Hash256> confirmed;
    for (const auto& tx : executed_txs) {
        confirmed.push_back(tx.hash());
    }
    mempool_->remove_confirmed(confirmed);
    
    // Notify settlement layer
    if (settlement_manager_) {
        settlement_manager_->add_block_to_batch(block);
    }
    
    Metrics::instance().increment(Metrics::BLOCKS_PROCESSED);
    
    // Print block info
    auto block_hash = block.header.hash();
    std::cout << "[BLOCK] #" << block.header.number << " produced | ";
    std::cout << executed_txs.size() << " txs | ";
    std::cout << "gas: " << total_gas_used << " | hash: ";
    printf("%02x%02x...%02x%02x", block_hash[0], block_hash[1], block_hash[30], block_hash[31]);
    std::cout << std::endl;
}

void Node::block_production_loop() {
    std::cout << "[SEQUENCER] Block production started (1s intervals)" << std::endl;
    
    while (running_) {
        produce_block();
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.consensus.block_time_ms));
    }
    
    std::cout << "[SEQUENCER] Block production stopped" << std::endl;
}

void Node::batch_submission_loop() {
    std::cout << "[BATCH] Starting L1 batch submission loop (60s intervals)" << std::endl;
    
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        
        if (!settlement_manager_) continue;
        
        // Check if we have a batch ready to submit
        auto current_batch = settlement_manager_->get_current_batch_id();
        if (current_batch == 0) continue;
        
        // Get blocks in current batch
        auto blocks = settlement_manager_->get_batch_blocks(current_batch);
        if (blocks.empty()) continue;
        
        // Generate ZK proof for batch
        execution::ZKProver prover;
        
        Hash256 pre_state_root = {};  // Would be first block's parent state
        auto last_block = block_store_->get_block(chain_head());
        Hash256 post_state_root = last_block ? last_block->header.state_root : Hash256{};
        
        std::vector<TransactionReceipt> receipts;  // Would collect all receipts
        
        auto proof = prover.generate_proof(blocks, pre_state_root, post_state_root, receipts);
        
        // Submit to L1
        bool submitted = settlement_manager_->submit_batch_to_l1(current_batch, proof);
        
        if (submitted) {
            std::cout << "[BATCH] Submitted batch #" << current_batch << " to Cardano L1" << std::endl;
            std::cout << "[BATCH]   Blocks: " << proof.start_block << " - " << proof.end_block << std::endl;
            std::cout << "[BATCH]   Proof hash: ";
            printf("%02x%02x...%02x%02x", proof.proof_hash[0], proof.proof_hash[1], 
                   proof.proof_hash[30], proof.proof_hash[31]);
            std::cout << std::endl;
        }
    }
    
    std::cout << "[BATCH] L1 submission loop stopped" << std::endl;
}

Node::HealthStatus Node::health() const {
    HealthStatus status;
    status.healthy = running_;
    status.synced = true;  // Simplified
    status.chain_head = chain_head();
    status.l1_finalized = 0;  // Would come from settlement manager
    status.peer_count = 0;  // Would come from network
    status.version = "0.1.0-dev";
    return status;
}

} // namespace nonagon
