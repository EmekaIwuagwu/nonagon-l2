#pragma once

#include <memory>
#include <string>
#include <functional>
#include <future>
#include <optional>
#include <vector>
#include <thread>
#include <atomic>
#include <shared_mutex>
#include <unordered_map>
#include "nonagon/types.hpp"

// Forward declarations
namespace nonagon {
namespace storage { class StateManager; class BlockStore; }
namespace consensus { class Mempool; class ConsensusEngine; }
namespace execution { class TransactionProcessor; }
namespace settlement { class SettlementManager; }
}

namespace nonagon {
namespace rpc {

/**
 * @brief JSON-RPC error codes (Ethereum compatible)
 */
enum class ErrorCode : int {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    
    // Ethereum specific
    ResourceNotFound = -32001,
    ResourceUnavailable = -32002,
    TransactionRejected = -32003,
    MethodNotSupported = -32004,
    LimitExceeded = -32005,
    
    // Nonagon specific
    BatchNotFound = -32100,
    SettlementPending = -32101,
    BridgePaused = -32102
};

/**
 * @brief JSON-RPC request
 */
struct Request {
    std::string jsonrpc{"2.0"};
    std::string method;
    std::optional<std::string> params;  // JSON string
    std::optional<uint64_t> id;
    
    static std::optional<Request> parse(const std::string& json);
};

/**
 * @brief JSON-RPC response
 */
struct Response {
    std::string jsonrpc{"2.0"};
    std::optional<std::string> result;  // JSON string
    std::optional<std::pair<ErrorCode, std::string>> error_info;
    std::optional<uint64_t> id;
    
    std::string to_json() const;
    
    static Response success(uint64_t id, const std::string& result);
    static Response make_error(uint64_t id, ErrorCode code, const std::string& message);
};

/**
 * @brief RPC method handler
 */
using MethodHandler = std::function<Response(const Request&)>;

/**
 * @brief RPC server configuration
 */
struct ServerConfig {
    std::string host{"127.0.0.1"};
    uint16_t http_port{8545};
    uint16_t ws_port{8546};
    
    bool enable_http{true};
    bool enable_websocket{true};
    bool enable_admin{false};  // Admin APIs (dangerous)
    
    // Rate limiting
    uint32_t max_requests_per_second{100};
    uint32_t max_connections{500};
    
    // CORS
    std::vector<std::string> allowed_origins{"*"};
    
    // Authentication (for admin APIs)
    std::string admin_token;
};

/**
 * @brief WebSocket subscription
 */
struct Subscription {
    std::string id;
    std::string type;  // "newHeads", "logs", "newPendingTransactions"
    std::optional<std::string> filter;  // JSON filter for logs
    
    // Connection info
    void* connection;  // Opaque connection pointer
};

/**
 * @brief JSON-RPC Server
 * 
 * Implements Ethereum-compatible JSON-RPC API:
 * - eth_* namespace for chain interaction
 * - net_* namespace for network status
 * - web3_* namespace for utilities
 * - nonagon_* namespace for L2 specific
 */
class Server {
public:
    explicit Server(const ServerConfig& config);
    ~Server();
    
    // Lifecycle
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    // Method registration
    void register_method(const std::string& name, MethodHandler handler);
    void unregister_method(const std::string& name);
    
    // WebSocket subscriptions
    void broadcast_subscription(const std::string& type, const std::string& data);
    
    // Stats
    struct Stats {
        uint64_t total_requests;
        uint64_t failed_requests;
        uint64_t active_connections;
        uint64_t active_subscriptions;
    };
    Stats get_stats() const;

private:
    ServerConfig config_;
    std::atomic<bool> running_{false};
    
    // Method registry
    std::unordered_map<std::string, MethodHandler> methods_;
    mutable std::shared_mutex methods_mutex_;
    
    // Subscriptions
    std::vector<Subscription> subscriptions_;
    mutable std::shared_mutex subs_mutex_;
    
    // Stats
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> failed_requests_{0};
    
    // Threads
    std::thread http_thread_;
    std::thread ws_thread_;
    
    void http_loop();
    void ws_loop();
    Response handle_request(const std::string& body);
};

/**
 * @brief Standard Ethereum RPC methods implementation
 */
class EthNamespace {
public:
    EthNamespace(std::shared_ptr<storage::StateManager> state,
                 std::shared_ptr<storage::BlockStore> blocks,
                 std::shared_ptr<consensus::Mempool> mempool,
                 std::shared_ptr<execution::TransactionProcessor> tx_processor);
    
    // Chain state
    Response chain_id(const Request& req);
    Response block_number(const Request& req);
    Response gas_price(const Request& req);
    Response max_priority_fee_per_gas(const Request& req);
    Response fee_history(const Request& req);
    
    // Account state
    Response get_balance(const Request& req);
    Response get_transaction_count(const Request& req);
    Response get_code(const Request& req);
    Response get_storage_at(const Request& req);
    
    // Block queries
    Response get_block_by_number(const Request& req);
    Response get_block_by_hash(const Request& req);
    Response get_block_transaction_count_by_number(const Request& req);
    Response get_block_transaction_count_by_hash(const Request& req);
    
    // Transaction queries
    Response get_transaction_by_hash(const Request& req);
    Response get_transaction_by_block_number_and_index(const Request& req);
    Response get_transaction_receipt(const Request& req);
    
    // Transaction submission
    Response send_raw_transaction(const Request& req);
    Response call(const Request& req);
    Response estimate_gas(const Request& req);
    
    // Logs
    Response get_logs(const Request& req);
    
    // Register all methods with server
    void register_methods(Server& server);

private:
    std::shared_ptr<storage::StateManager> state_;
    std::shared_ptr<storage::BlockStore> blocks_;
    std::shared_ptr<consensus::Mempool> mempool_;
    std::shared_ptr<execution::TransactionProcessor> tx_processor_;
};

/**
 * @brief Nonagon-specific RPC methods
 */
class NonagonNamespace {
public:
    NonagonNamespace(std::shared_ptr<settlement::SettlementManager> settlement,
                     std::shared_ptr<consensus::ConsensusEngine> consensus);
    
    // Settlement queries
    Response get_batch(const Request& req);
    Response get_latest_batch(const Request& req);
    Response get_batch_status(const Request& req);
    Response get_l1_finalized_block(const Request& req);
    
    // Bridge queries
    Response get_deposit_status(const Request& req);
    Response get_withdrawal_status(const Request& req);
    Response estimate_withdrawal_time(const Request& req);
    
    // Sequencer queries
    Response get_sequencer_set(const Request& req);
    Response get_current_sequencer(const Request& req);
    Response get_next_batch_time(const Request& req);
    
    // Register all methods with server
    void register_methods(Server& server);

private:
    std::shared_ptr<settlement::SettlementManager> settlement_;
    std::shared_ptr<consensus::ConsensusEngine> consensus_;
};

} // namespace rpc
} // namespace nonagon
