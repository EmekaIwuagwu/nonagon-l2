#include "nonagon/settlement.hpp"
#include "nonagon/crypto.hpp"
#include "nonagon/storage.hpp" // For StateManager
#include "nonagon/execution.hpp" // For ValidityProof
#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

// Platform-specific HTTP client
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace nonagon {
namespace settlement {

// ============================================================================
// StateCommitmentDatum Implementation
// ============================================================================

Bytes StateCommitmentDatum::to_cbor() const {
    // Simplified CBOR encoding
    Bytes result;
    
    // Batch ID (8 bytes)
    for (int i = 7; i >= 0; --i) {
        result.push_back(static_cast<uint8_t>((batch_id >> (i * 8)) & 0xFF));
    }
    
    // Block range
    for (int i = 7; i >= 0; --i) {
        result.push_back(static_cast<uint8_t>((start_block >> (i * 8)) & 0xFF));
    }
    for (int i = 7; i >= 0; --i) {
        result.push_back(static_cast<uint8_t>((end_block >> (i * 8)) & 0xFF));
    }
    
    // State roots
    result.insert(result.end(), pre_state_root.begin(), pre_state_root.end());
    result.insert(result.end(), post_state_root.begin(), post_state_root.end());
    result.insert(result.end(), transactions_root.begin(), transactions_root.end());
    
    return result;
}

StateCommitmentDatum StateCommitmentDatum::from_cbor(const Bytes& data) {
    StateCommitmentDatum datum;
    if (data.size() < 24 + 32 * 3) return datum;
    
    size_t offset = 0;
    
    // Batch ID
    datum.batch_id = 0;
    for (int i = 0; i < 8; ++i) {
        datum.batch_id = (datum.batch_id << 8) | data[offset++];
    }
    
    // Start block
    datum.start_block = 0;
    for (int i = 0; i < 8; ++i) {
        datum.start_block = (datum.start_block << 8) | data[offset++];
    }
    
    // End block
    datum.end_block = 0;
    for (int i = 0; i < 8; ++i) {
        datum.end_block = (datum.end_block << 8) | data[offset++];
    }
    
    // State roots
    std::copy(data.begin() + offset, data.begin() + offset + 32, datum.pre_state_root.begin());
    offset += 32;
    std::copy(data.begin() + offset, data.begin() + offset + 32, datum.post_state_root.begin());
    offset += 32;
    std::copy(data.begin() + offset, data.begin() + offset + 32, datum.transactions_root.begin());
    
    return datum;
}

// ============================================================================
// FraudProof Implementation
// ============================================================================

Bytes FraudProof::to_cbor() const {
    Bytes result;
    
    // Batch ID
    for (int i = 7; i >= 0; --i) {
        result.push_back(static_cast<uint8_t>((batch_id >> (i * 8)) & 0xFF));
    }
    
    // Step index
    for (int i = 7; i >= 0; --i) {
        result.push_back(static_cast<uint8_t>((step_index >> (i * 8)) & 0xFF));
    }
    
    // States
    result.insert(result.end(), pre_state.begin(), pre_state.end());
    result.insert(result.end(), post_state.begin(), post_state.end());
    
    // Execution trace
    result.insert(result.end(), execution_trace.begin(), execution_trace.end());
    
    return result;
}

// ============================================================================
// BridgeDeposit Implementation
// ============================================================================

Hash256 BridgeDeposit::hash() const {
    Bytes data;
    
    // TX hash
    data.insert(data.end(), cardano_tx_hash.begin(), cardano_tx_hash.end());
    
    // Slot
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((cardano_slot >> (i * 8)) & 0xFF));
    }
    
    // Amount
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((amount >> (i * 8)) & 0xFF));
    }
    
    return crypto::Blake2b256::hash(data);
}

// ============================================================================
// HTTP Client Helper (Platform-specific)
// ============================================================================

#ifdef _WIN32
// Windows HTTP POST using WinHTTP
static bool http_post(const std::string& url, const std::string& data, 
                      const std::string& content_type, std::string& response) {
    // Parse URL
    URL_COMPONENTSW urlComp = {0};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwSchemeLength = -1;
    urlComp.dwHostNameLength = -1;
    urlComp.dwUrlPathLength = -1;
    
    std::wstring wurl(url.begin(), url.end());
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &urlComp)) {
        return false;
    }
    
    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    
    HINTERNET hSession = WinHttpOpen(L"Nonagon/1.0", 
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, 
                                      WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), 
                                         urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
                                             NULL, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    std::wstring wct(content_type.begin(), content_type.end());
    std::wstring headers = L"Content-Type: " + wct;
    
    bool success = WinHttpSendRequest(hRequest, headers.c_str(), -1,
                                       (LPVOID)data.c_str(), (DWORD)data.size(),
                                       (DWORD)data.size(), 0);
    
    if (success) {
        success = WinHttpReceiveResponse(hRequest, NULL);
    }
    
    if (success) {
        DWORD size = 0;
        WinHttpQueryDataAvailable(hRequest, &size);
        if (size > 0) {
            std::vector<char> buffer(size + 1);
            DWORD downloaded = 0;
            WinHttpReadData(hRequest, buffer.data(), size, &downloaded);
            response = std::string(buffer.data(), downloaded);
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return success;
}
#else
// Stub for non-Windows platforms (would use libcurl)
static bool http_post(const std::string& url, const std::string& data,
                      const std::string& content_type, std::string& response) {
    (void)url; (void)data; (void)content_type; (void)response;
    return false;
}
#endif

// ============================================================================
// CardanoClient Implementation
// ============================================================================

CardanoClient::CardanoClient(const CardanoConfig& config) : config_(config) {}

bool CardanoClient::connect() {
    std::cout << "[CARDANO] Connecting to L1 Gateway at " << config_.node_socket_path << std::endl;
    connected_ = true;
    return true;
}

bool CardanoClient::is_connected() const {
    return connected_;
}

uint64_t CardanoClient::get_current_slot() const {
    if (!connected_) return 0;
    
    // Return simulated current slot based on time
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(epoch).count();
}

uint64_t CardanoClient::get_current_epoch() const {
    return get_current_slot() / config_.epoch_length;
}

std::optional<Bytes> CardanoClient::get_utxo(const std::string& tx_hash, uint32_t index) const {
    if (!connected_) return std::nullopt;
    // Would query Cardano node for UTxO
    return std::nullopt;
}

std::future<std::string> CardanoClient::submit_transaction(const Bytes& signed_tx) {
    return std::async(std::launch::async, [this, signed_tx]() -> std::string {
        if (!connected_) return "";
        
        // Calculate hash
        auto hash = crypto::Blake2b256::hash(signed_tx);
        std::string tx_hash;
        for (auto b : hash) {
            char buf[3];
            snprintf(buf, sizeof(buf), "%02x", b);
            tx_hash += buf;
        }
        
        std::cout << "[CARDANO] Submitting Batch Tx " << tx_hash << " to L1..." << std::endl;
        
        // Convert transaction to hex for API
        std::string tx_hex;
        for (auto b : signed_tx) {
            char buf[3];
            snprintf(buf, sizeof(buf), "%02x", b);
            tx_hex += buf;
        }
        
        bool http_success = false;
        std::string http_response;
        
        // Try HTTP submission if endpoint is configured
        if (!config_.node_socket_path.empty() && 
            config_.node_socket_path.find("http") == 0) {
            
            // Blockfrost-style API
            std::string endpoint = config_.node_socket_path;
            if (endpoint.back() != '/') endpoint += '/';
            endpoint += "tx/submit";
            
            // Try to submit via HTTP POST
            http_success = http_post(endpoint, tx_hex, "application/cbor", http_response);
            
            if (http_success) {
                std::cout << "[CARDANO] HTTP submission successful. Response: " 
                          << http_response << std::endl;
            } else {
                std::cout << "[CARDANO] HTTP submission failed, using local log fallback." 
                          << std::endl;
            }
        }
        
        // Always write to persistent L1 log (audit trail)
        std::ofstream log_file("l1_submissions.log", std::ios::app);
        if (log_file.is_open()) {
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            std::string ts = std::ctime(&t);
            if (!ts.empty()) ts.pop_back();
            
            log_file << "[" << ts << "] SUBMIT TX_HASH=" << tx_hash 
                     << " SIZE=" << signed_tx.size() << " BYTES"
                     << " HTTP=" << (http_success ? "OK" : "LOCAL")
                     << std::endl;
        }

        return tx_hash;
    });
}

std::optional<std::string> CardanoClient::get_tx_status(const std::string& tx_hash) const {
    if (!connected_) return std::nullopt;
    return "confirmed";  // Simulated
}

std::optional<StateCommitmentDatum> CardanoClient::get_latest_state_commitment() const {
    if (!connected_) return std::nullopt;
    
    StateCommitmentDatum datum;
    datum.batch_id = 0;
    datum.start_block = 0;
    datum.end_block = 0;
    return datum;
}

std::vector<FraudProof> CardanoClient::get_active_challenges() const {
    return {};  // No active challenges
}

std::vector<BridgeDeposit> CardanoClient::get_pending_deposits(uint64_t from_slot) const {
    return {};  // Would scan Cardano blocks
}

// ============================================================================
// BatchBuilder Implementation
// ============================================================================

BatchBuilder::BatchBuilder(const Config& config) : config_(config) {
    batch_start_time_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

void BatchBuilder::add_block(const Block& block) {
    std::unique_lock lock(mutex_);
    pending_blocks_.push_back(block);
}

bool BatchBuilder::is_ready() const {
    std::unique_lock lock(mutex_);
    
    if (pending_blocks_.empty()) return false;
    
    // Check transaction count
    size_t total_txs = 0;
    for (const auto& block : pending_blocks_) {
        total_txs += block.transactions.size();
    }
    
    if (total_txs >= config_.max_batch_size) return true;
    
    // Check age
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    if (now - batch_start_time_ >= static_cast<int64_t>(config_.max_batch_age_seconds) &&
        total_txs >= config_.min_batch_size) {
        return true;
    }
    
    return false;
}

std::optional<SettlementBatch> BatchBuilder::build_batch(const Hash256& pre_state_root) {
    std::unique_lock lock(mutex_);
    
    if (pending_blocks_.empty()) return std::nullopt;
    
    SettlementBatch batch;
    batch.batch_id = next_batch_id_++;
    batch.start_block = pending_blocks_.front().header.number;
    batch.end_block = pending_blocks_.back().header.number;
    batch.pre_state_root = pre_state_root;
    batch.post_state_root = pending_blocks_.back().header.state_root;
    
    // Compute transactions root
    std::vector<Hash256> block_hashes;
    for (const auto& block : pending_blocks_) {
        block_hashes.push_back(block.header.hash());
    }
    batch.transactions_root = crypto::Blake2b256::merkle_root(block_hashes);
    
    // Compress block data
    Bytes compressed;
    for (const auto& block : pending_blocks_) {
        auto encoded = block.encode();
        // Length prefix
        uint32_t len = static_cast<uint32_t>(encoded.size());
        for (int i = 3; i >= 0; --i) {
            compressed.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
        compressed.insert(compressed.end(), encoded.begin(), encoded.end());
    }
    batch.compressed_data = compressed;
    
    batch.status = SettlementBatch::Status::Pending;
    
    std::cout << "[SETTLEMENT] Built batch #" << batch.batch_id 
              << " (blocks " << batch.start_block << "-" << batch.end_block << ")"
              << std::endl;
    
    return batch;
}

void BatchBuilder::clear() {
    std::unique_lock lock(mutex_);
    pending_blocks_.clear();
    batch_start_time_ = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

size_t BatchBuilder::pending_blocks() const {
    std::unique_lock lock(mutex_);
    return pending_blocks_.size();
}

size_t BatchBuilder::pending_transactions() const {
    std::unique_lock lock(mutex_);
    size_t count = 0;
    for (const auto& block : pending_blocks_) {
        count += block.transactions.size();
    }
    return count;
}

// ============================================================================
// SettlementManager Implementation
// ============================================================================

SettlementManager::SettlementManager(std::shared_ptr<CardanoClient> client,
                                     std::shared_ptr<BatchBuilder> builder)
    : client_(client), builder_(builder) {}

std::future<bool> SettlementManager::submit_batch(const SettlementBatch& batch) {
    return std::async(std::launch::async, [this, batch]() -> bool {
        // Build state commitment datum
        StateCommitmentDatum datum;
        datum.batch_id = batch.batch_id;
        datum.start_block = batch.start_block;
        datum.end_block = batch.end_block;
        datum.pre_state_root = batch.pre_state_root;
        datum.post_state_root = batch.post_state_root;
        datum.transactions_root = batch.transactions_root;
        datum.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        // Build and submit Cardano transaction
        Bytes tx_data = datum.to_cbor();
        auto tx_hash_future = client_->submit_transaction(tx_data);
        auto tx_hash = tx_hash_future.get();
        
        if (tx_hash.empty()) {
            std::cerr << "[SETTLEMENT] Failed to submit batch" << std::endl;
            return false;
        }
        
        std::cout << "[SETTLEMENT] Batch #" << batch.batch_id 
                  << " submitted, tx: " << tx_hash << std::endl;
        
        // Track pending batch
        SettlementBatch submitted = batch;
        submitted.status = SettlementBatch::Status::Submitted;
        submitted.cardano_tx_hash = tx_hash;
        submitted.cardano_slot = client_->get_current_slot();
        
        pending_batches_.push_back(submitted);
        
        return true;
    });
}

std::vector<SettlementBatch> SettlementManager::get_pending_batches() const {
    return pending_batches_;
}

std::optional<SettlementBatch> SettlementManager::get_batch(uint64_t batch_id) const {
    for (const auto& batch : pending_batches_) {
        if (batch.batch_id == batch_id) return batch;
    }
    for (const auto& batch : finalized_batches_) {
        if (batch.batch_id == batch_id) return batch;
    }
    return std::nullopt;
}

void SettlementManager::handle_challenge(const FraudProof& challenge) {
    std::cout << "[SETTLEMENT] Challenge received for batch #" << challenge.batch_id 
              << std::endl;
    // Would initiate bisection protocol
}

std::optional<FraudProof> SettlementManager::generate_fraud_proof(uint64_t batch_id, 
                                                                   uint64_t invalid_step) {
    FraudProof proof;
    proof.batch_id = batch_id;
    proof.step_index = invalid_step;
    proof.status = FraudProof::Status::Initiated;
    return proof;
}

void SettlementManager::respond_to_bisection(const FraudProof& challenge, 
                                              const Hash256& claimed_state) {
    std::cout << "[SETTLEMENT] Responding to bisection for batch #" 
              << challenge.batch_id << std::endl;
}

void SettlementManager::process_deposits() {
    auto deposits = client_->get_pending_deposits(0);
    
    for (const auto& deposit : deposits) {
        std::cout << "[BRIDGE] Processing deposit: " << deposit.amount 
                  << " " << deposit.asset_id << std::endl;
        
        for (auto& cb : deposit_callbacks_) {
            cb(deposit);
        }
    }
}

std::vector<BridgeDeposit> SettlementManager::get_new_deposits() const {
    return client_->get_pending_deposits(0);
}

void SettlementManager::queue_withdrawal(const BridgeWithdrawal& withdrawal) {
    pending_withdrawals_.push_back(withdrawal);
    std::cout << "[BRIDGE] Withdrawal queued: " << withdrawal.amount 
              << " to " << withdrawal.cardano_recipient << std::endl;
}

std::vector<BridgeWithdrawal> SettlementManager::get_claimable_withdrawals() const {
    std::vector<BridgeWithdrawal> claimable;
    for (const auto& w : pending_withdrawals_) {
        if (w.status == BridgeWithdrawal::Status::Claimable) {
            claimable.push_back(w);
        }
    }
    return claimable;
}

bool SettlementManager::is_batch_finalized(uint64_t batch_id) const {
    for (const auto& batch : finalized_batches_) {
        if (batch.batch_id == batch_id) return true;
    }
    return false;
}

uint64_t SettlementManager::get_finalized_block() const {
    if (finalized_batches_.empty()) return 0;
    return finalized_batches_.back().end_block;
}

uint64_t SettlementManager::get_current_batch_id() const {
    if (!builder_) return 0;
    return builder_->current_batch_id();
}

void SettlementManager::add_block_to_batch(const Block& block) {
    if (builder_) builder_->add_block(block);
}

std::vector<Block> SettlementManager::get_batch_blocks(uint64_t batch_id) const {
    if (builder_ && builder_->current_batch_id() == batch_id) {
        return builder_->get_pending_blocks();
    }
    return {};
}

bool SettlementManager::submit_batch_to_l1(uint64_t batch_id, const execution::ValidityProof& proof) {
    if (!builder_ || !client_) return false;
    
    // If it's the current batch being built
    if (builder_->current_batch_id() == batch_id) {
        // Get pre-state
        auto latest_state = client_->get_latest_state_commitment();
        Hash256 pre_state = latest_state ? latest_state->post_state_root : Hash256{};
        
        // Build batch
        auto batch_opt = builder_->build_batch(pre_state);
        if (!batch_opt) return false;
        
        // TODO: Store proof in batch if we update SettlementBatch struct
        // batch_opt->proof = proof;
        
        // Submit
        auto future = submit_batch(*batch_opt);
        
        // Clear builder
        builder_->clear();
        
        // Wait for result (since this is called from a dedicated thread)
        return future.get();
    }
    return false;
}

void SettlementManager::start() {
    if (running_) return;
    
    if (!client_->connect()) {
        std::cerr << "[SETTLEMENT] Failed to connect to Cardano" << std::endl;
        return;
    }
    
    running_ = true;
    background_thread_ = std::thread([this]() {
        background_loop();
    });
    
    std::cout << "[SETTLEMENT] Settlement manager started" << std::endl;
}

void SettlementManager::stop() {
    running_ = false;
    if (background_thread_.joinable()) {
        background_thread_.join();
    }
    std::cout << "[SETTLEMENT] Settlement manager stopped" << std::endl;
}

void SettlementManager::background_loop() {
    while (running_) {
        // Check if batch is ready
        if (builder_->is_ready()) {
            auto latest_state = client_->get_latest_state_commitment();
            Hash256 pre_state = latest_state ? latest_state->post_state_root : Hash256{};
            
            auto batch = builder_->build_batch(pre_state);
            if (batch) {
                submit_batch(*batch);
                builder_->clear();
            }
        }
        
        // Check batch finality
        check_batch_finality();
        
        // Process pending withdrawals
        process_pending_withdrawals();
        
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

void SettlementManager::check_batch_finality() {
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    for (auto it = pending_batches_.begin(); it != pending_batches_.end(); ) {
        if (it->status == SettlementBatch::Status::Submitted) {
            // Check if challenge period passed (simulated)
            if (now - it->cardano_slot > static_cast<int64_t>(604800)) {  // 7 days
                it->status = SettlementBatch::Status::Finalized;
                finalized_batches_.push_back(*it);
                
                std::cout << "[SETTLEMENT] Batch #" << it->batch_id 
                          << " FINALIZED!" << std::endl;
                
                for (auto& cb : finality_callbacks_) {
                    cb(it->batch_id);
                }
                
                it = pending_batches_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

void SettlementManager::process_pending_withdrawals() {
    for (auto& w : pending_withdrawals_) {
        if (w.status == BridgeWithdrawal::Status::Pending) {
            // Check if batch is finalized
            if (is_batch_finalized(w.batch_id)) {
                w.status = BridgeWithdrawal::Status::Claimable;
                std::cout << "[BRIDGE] Withdrawal ready to claim: " 
                          << w.amount << std::endl;
            }
        }
    }
}

// ============================================================================
// L1DepositWatcher Implementation
// ============================================================================

L1DepositWatcher::L1DepositWatcher(const CardanoConfig& config,
                                   std::shared_ptr<CardanoClient> client)
    : config_(config), client_(client) {}

void L1DepositWatcher::start() {
    if (running_) return;
    running_ = true;
    
    std::cout << "[WATCHER] Starting L1 deposit watcher..." << std::endl;
    std::cout << "[WATCHER] Monitoring address: " << config_.deposit_address << std::endl;
    std::cout << "[WATCHER] Required confirmations: " << config_.deposit_confirmations << std::endl;
    
    watcher_thread_ = std::thread([this]() { watcher_loop(); });
}

void L1DepositWatcher::stop() {
    if (!running_) return;
    running_ = false;
    
    if (watcher_thread_.joinable()) {
        watcher_thread_.join();
    }
    std::cout << "[WATCHER] L1 deposit watcher stopped" << std::endl;
}

std::vector<L1DepositWatcher::DepositInfo> L1DepositWatcher::get_pending_deposits() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_deposits_;
}

std::vector<L1DepositWatcher::DepositInfo> L1DepositWatcher::get_confirmed_deposits() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return confirmed_deposits_;
}

void L1DepositWatcher::on_deposit_confirmed(DepositConfirmedCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.push_back(cb);
}

void L1DepositWatcher::poll_deposits() {
    fetch_new_deposits();
    update_confirmations();
    process_confirmed();
}

void L1DepositWatcher::watcher_loop() {
    while (running_) {
        poll_deposits();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.poll_interval_ms));
    }
}

void L1DepositWatcher::fetch_new_deposits() {
    if (!client_ || !client_->is_connected()) return;
    
    // Check if we have an HTTP endpoint configured
    if (config_.node_socket_path.empty() || 
        config_.node_socket_path.find("http") != 0) {
        return;
    }
    
    // Build Blockfrost API URL for address UTxOs
    std::string endpoint = config_.node_socket_path;
    if (endpoint.back() != '/') endpoint += '/';
    endpoint += "addresses/" + config_.deposit_address + "/utxos";
    
    std::string response;
    bool success = http_post(endpoint, "", "application/json", response);
    
    if (!success || response.empty()) return;
    
    // Parse JSON response (simplified - would use proper JSON parser)
    // Looking for new UTxOs with deposit metadata
    uint64_t current_slot = client_->get_current_slot();
    
    // For demo: simulate finding a deposit every ~60 seconds  
    static uint64_t last_simulated = 0;
    if (current_slot - last_simulated > 60) {
        last_simulated = current_slot;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check if this "deposit" already exists
        std::string sim_hash = "sim_" + std::to_string(current_slot);
        bool exists = false;
        for (const auto& d : pending_deposits_) {
            if (d.tx_hash == sim_hash) {
                exists = true;
                break;
            }
        }
        
        if (!exists && config_.deposit_address.length() > 0) {
            DepositInfo deposit;
            deposit.tx_hash = sim_hash;
            deposit.output_index = 0;
            deposit.l1_address = config_.deposit_address;
            deposit.l2_address = Address{}; // Would parse from UTxO datum
            deposit.amount = 1000000000; // 1000 ADA in lovelace
            deposit.confirmations = 0;
            deposit.slot = current_slot;
            
            pending_deposits_.push_back(deposit);
            std::cout << "[WATCHER] New deposit detected: " << deposit.tx_hash 
                      << " (" << deposit.amount << " lovelace)" << std::endl;
        }
    }
}

void L1DepositWatcher::update_confirmations() {
    if (!client_) return;
    
    uint64_t current_slot = client_->get_current_slot();
    
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& deposit : pending_deposits_) {
        // Each slot is roughly 1 second on Cardano
        deposit.confirmations = current_slot - deposit.slot;
    }
}

void L1DepositWatcher::process_confirmed() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = pending_deposits_.begin();
    while (it != pending_deposits_.end()) {
        if (it->confirmations >= config_.deposit_confirmations) {
            std::cout << "[WATCHER] Deposit CONFIRMED after " << it->confirmations 
                      << " confirmations: " << it->tx_hash << std::endl;
            
            // Move to confirmed list
            confirmed_deposits_.push_back(*it);
            
            // Trigger callbacks (for L2 minting)
            for (auto& cb : callbacks_) {
                cb(*it);
            }
            
            it = pending_deposits_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace settlement
} // namespace nonagon
