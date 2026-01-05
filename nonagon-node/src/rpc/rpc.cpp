#include "nonagon/rpc.hpp"
#include "nonagon/storage.hpp"
#include "nonagon/consensus.hpp"
#include "nonagon/execution.hpp"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <regex>
#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#endif

namespace nonagon {
namespace rpc {

// ============================================================================
// Simple HTTP Socket Helpers
// ============================================================================

#ifdef _WIN32
using socket_t = SOCKET;
#define INVALID_SOCK INVALID_SOCKET
#define close_socket closesocket
#else
using socket_t = int;
#define INVALID_SOCK -1
#define close_socket close
#endif

static bool init_sockets() {
#ifdef _WIN32
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

static void cleanup_sockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

// ============================================================================
// Request Implementation
// ============================================================================

std::optional<Request> Request::parse(const std::string& json) {
    Request req;
    
    // Simple JSON parsing (in production, use nlohmann/json or rapidjson)
    auto extract_string = [&json](const std::string& key) -> std::optional<std::string> {
        std::string pattern = "\"" + key + "\"\\s*:\\s*\"([^\"]+)\"";
        std::regex re(pattern);
        std::smatch match;
        if (std::regex_search(json, match, re) && match.size() > 1) {
            return match[1].str();
        }
        return std::nullopt;
    };
    
    auto extract_number = [&json](const std::string& key) -> std::optional<uint64_t> {
        std::string pattern = "\"" + key + "\"\\s*:\\s*(\\d+)";
        std::regex re(pattern);
        std::smatch match;
        if (std::regex_search(json, match, re) && match.size() > 1) {
            return std::stoull(match[1].str());
        }
        return std::nullopt;
    };
    
    // Extract method
    auto method = extract_string("method");
    if (!method) {
        return std::nullopt;
    }
    req.method = *method;
    
    // Extract id
    req.id = extract_number("id");
    
    // Extract params (as raw JSON string)
    size_t params_start = json.find("\"params\"");
    if (params_start != std::string::npos) {
        size_t bracket_start = json.find('[', params_start);
        size_t brace_start = json.find('{', params_start);
        
        size_t start = std::min(bracket_start, brace_start);
        if (start != std::string::npos) {
            char open = json[start];
            char close = (open == '[') ? ']' : '}';
            int depth = 1;
            size_t end = start + 1;
            
            while (end < json.length() && depth > 0) {
                if (json[end] == open) depth++;
                else if (json[end] == close) depth--;
                end++;
            }
            
            if (depth == 0) {
                req.params = json.substr(start, end - start);
            }
        }
    }
    
    return req;
}

// ============================================================================
// Response Implementation
// ============================================================================

std::string Response::to_json() const {
    std::ostringstream ss;
    ss << "{\"jsonrpc\":\"2.0\"";
    
    if (id.has_value()) {
        ss << ",\"id\":" << *id;
    } else {
        ss << ",\"id\":null";
    }
    
    if (result.has_value()) {
        ss << ",\"result\":" << *result;
    }
    
    if (error_info.has_value()) {
        ss << ",\"error\":{\"code\":" << static_cast<int>(error_info->first)
           << ",\"message\":\"" << error_info->second << "\"}";
    }
    
    ss << "}";
    return ss.str();
}

Response Response::success(uint64_t id, const std::string& result) {
    Response resp;
    resp.id = id;
    resp.result = result;
    return resp;
}

Response Response::make_error(uint64_t id, ErrorCode code, const std::string& message) {
    Response resp;
    resp.id = id;
    resp.error_info = {code, message};
    return resp;
}

// ============================================================================
// Server Implementation with Real HTTP
// ============================================================================

Server::Server(const ServerConfig& config) : config_(config) {}

Server::~Server() {
    stop();
}

bool Server::start() {
    if (running_) {
        return true;
    }
    
    if (!init_sockets()) {
        std::cerr << "[RPC] Failed to initialize sockets" << std::endl;
        return false;
    }
    
    running_ = true;
    
    if (config_.enable_http) {
        http_thread_ = std::thread([this]() {
            http_loop();
        });
    }
    
    if (config_.enable_websocket) {
        ws_thread_ = std::thread([this]() {
            ws_loop();
        });
    }
    
    std::cout << "[RPC] HTTP Server started on http://" << config_.host 
              << ":" << config_.http_port << std::endl;
    return true;
}

void Server::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // Close server sockets to unblock accept()
    cleanup_sockets();
    
    if (http_thread_.joinable()) {
        http_thread_.join();
    }
    if (ws_thread_.joinable()) {
        ws_thread_.join();
    }
    
    std::cout << "[RPC] Server stopped" << std::endl;
}

void Server::register_method(const std::string& name, MethodHandler handler) {
    std::unique_lock lock(methods_mutex_);
    methods_[name] = handler;
}

void Server::unregister_method(const std::string& name) {
    std::unique_lock lock(methods_mutex_);
    methods_.erase(name);
}

void Server::broadcast_subscription(const std::string& type, const std::string& data) {
    std::shared_lock lock(subs_mutex_);
    for (const auto& sub : subscriptions_) {
        if (sub.type == type) {
            // Would send to WebSocket connection
        }
    }
}

Server::Stats Server::get_stats() const {
    return Stats{
        total_requests_.load(),
        failed_requests_.load(),
        0,
        subscriptions_.size()
    };
}

Response Server::handle_request(const std::string& body) {
    total_requests_++;
    
    auto req_opt = Request::parse(body);
    if (!req_opt) {
        failed_requests_++;
        return Response::make_error(0, ErrorCode::ParseError, "Invalid JSON");
    }
    
    auto& req = *req_opt;
    uint64_t id = req.id.value_or(0);
    
    std::shared_lock lock(methods_mutex_);
    auto it = methods_.find(req.method);
    if (it == methods_.end()) {
        failed_requests_++;
        return Response::make_error(id, ErrorCode::MethodNotFound, 
                                     "Method not found: " + req.method);
    }
    
    try {
        return it->second(req);
    } catch (const std::exception& e) {
        failed_requests_++;
        return Response::make_error(id, ErrorCode::InternalError, e.what());
    }
}

void Server::http_loop() {
    // Create server socket
    socket_t server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == INVALID_SOCK) {
        std::cerr << "[RPC] Failed to create socket" << std::endl;
        return;
    }
    
    // Allow address reuse
    int opt = 1;
#ifdef _WIN32
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    
    // Bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.http_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[RPC] Failed to bind to port " << config_.http_port << std::endl;
        close_socket(server_sock);
        return;
    }
    
    // Listen
    if (listen(server_sock, 10) < 0) {
        std::cerr << "[RPC] Failed to listen" << std::endl;
        close_socket(server_sock);
        return;
    }
    
    std::cout << "[RPC] HTTP listening on port " << config_.http_port << std::endl;
    
    // Set non-blocking timeout for accept
#ifdef _WIN32
    DWORD timeout = 1000;
    setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(server_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        socket_t client_sock = accept(server_sock, (sockaddr*)&client_addr, &client_len);
        if (client_sock == INVALID_SOCK) {
            continue;  // Timeout or error, check running_ flag
        }
        
        // Read request - need to read headers first, then body
        std::string request;
        char buffer[4096];
        int total_read = 0;
        size_t content_length = 0;
        size_t headers_end = std::string::npos;
        
        // Keep reading until we have the complete request
        while (true) {
            int bytes_read = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
            if (bytes_read <= 0) break;
            
            buffer[bytes_read] = '\0';
            request.append(buffer, bytes_read);
            total_read += bytes_read;
            
            // Find headers end if we haven't yet
            if (headers_end == std::string::npos) {
                headers_end = request.find("\r\n\r\n");
                if (headers_end != std::string::npos) {
                    // Look for Content-Length header
                    std::string lower_request = request;
                    std::transform(lower_request.begin(), lower_request.end(), 
                                   lower_request.begin(), ::tolower);
                    size_t cl_pos = lower_request.find("content-length:");
                    if (cl_pos != std::string::npos) {
                        size_t cl_end = lower_request.find("\r\n", cl_pos);
                        std::string cl_value = lower_request.substr(cl_pos + 15, cl_end - cl_pos - 15);
                        content_length = std::stoull(cl_value);
                    }
                }
            }
            
            // Check if we have complete body
            if (headers_end != std::string::npos) {
                size_t body_length = request.length() - (headers_end + 4);
                if (body_length >= content_length) {
                    break;  // We have the complete request
                }
            }
            
            // Safety limit
            if (total_read > 65536) break;
        }
        
        if (request.empty()) {
            close_socket(client_sock);
            continue;
        }
        
        // Find JSON body (after HTTP headers)
        size_t body_start = request.find("\r\n\r\n");
        std::string json_body;
        if (body_start != std::string::npos) {
            json_body = request.substr(body_start + 4);
        } else {
            json_body = request;  // Assume raw JSON
        }
        
        // Debug: print the JSON body
        std::cout << "[RPC] Request body: " << json_body << std::endl;
        
        // Process request
        Response resp = handle_request(json_body);
        std::string json_response = resp.to_json();
        
        // Build HTTP response
        std::ostringstream http_resp;
        http_resp << "HTTP/1.1 200 OK\r\n";
        http_resp << "Content-Type: application/json\r\n";
        http_resp << "Access-Control-Allow-Origin: *\r\n";
        http_resp << "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n";
        http_resp << "Access-Control-Allow-Headers: Content-Type\r\n";
        http_resp << "Content-Length: " << json_response.length() << "\r\n";
        http_resp << "Connection: close\r\n";
        http_resp << "\r\n";
        http_resp << json_response;
        
        std::string response_str = http_resp.str();
        send(client_sock, response_str.c_str(), (int)response_str.length(), 0);
        
        close_socket(client_sock);
    }
    
    close_socket(server_sock);
}

void Server::ws_loop() {
    // WebSocket server (simplified - would need full WS handshake)
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ============================================================================
// EthNamespace Implementation
// ============================================================================

EthNamespace::EthNamespace(std::shared_ptr<storage::StateManager> state,
                           std::shared_ptr<storage::BlockStore> blocks,
                           std::shared_ptr<consensus::Mempool> mempool,
                           std::shared_ptr<execution::TransactionProcessor> tx_processor)
    : state_(state), blocks_(blocks), mempool_(mempool), tx_processor_(tx_processor) {}

Response EthNamespace::chain_id(const Request& req) {
    return Response::success(req.id.value_or(0), "\"0x1\"");
}

Response EthNamespace::block_number(const Request& req) {
    uint64_t head = blocks_->get_head();
    std::ostringstream ss;
    ss << "\"0x" << std::hex << head << "\"";
    return Response::success(req.id.value_or(0), ss.str());
}

Response EthNamespace::gas_price(const Request& req) {
    return Response::success(req.id.value_or(0), "\"0x3b9aca00\"");
}

Response EthNamespace::max_priority_fee_per_gas(const Request& req) {
    return Response::success(req.id.value_or(0), "\"0x3b9aca00\"");
}

Response EthNamespace::fee_history(const Request& req) {
    return Response::success(req.id.value_or(0), 
        "{\"oldestBlock\":\"0x0\",\"baseFeePerGas\":[\"0x3b9aca00\"],\"gasUsedRatio\":[0.5]}");
}

Response EthNamespace::get_balance(const Request& req) {
    if (!state_ || !req.params) return Response::success(req.id.value_or(0), "\"0x0\"");
    
    // Simple regex to extract address from params JSON like ["0x...", "latest"]
    std::regex addr_regex(R"(\"(0x[a-fA-F0-9]{40,})\")");
    std::smatch match;
    std::string params = *req.params;
    
    if (std::regex_search(params, match, addr_regex)) {
        auto addr_opt = Address::from_hex(match[1].str());
        if (addr_opt) {
            uint64_t bal = state_->get_balance(*addr_opt);
            std::ostringstream ss;
            ss << "\"0x" << std::hex << bal << "\"";
            return Response::success(req.id.value_or(0), ss.str());
        }
    }
    
    return Response::success(req.id.value_or(0), "\"0x0\"");
}

Response EthNamespace::get_transaction_count(const Request& req) {
    if (!state_ || !req.params) return Response::success(req.id.value_or(0), "\"0x0\"");
    
    std::regex addr_regex(R"(\"(0x[a-fA-F0-9]{40,})\")");
    std::smatch match;
    std::string params = *req.params;
    
    if (std::regex_search(params, match, addr_regex)) {
        auto addr_opt = Address::from_hex(match[1].str());
        if (addr_opt) {
            uint64_t nonce = state_->get_nonce(*addr_opt);
            std::ostringstream ss;
            ss << "\"0x" << std::hex << nonce << "\"";
            return Response::success(req.id.value_or(0), ss.str());
        }
    }
    return Response::success(req.id.value_or(0), "\"0x0\"");
}

Response EthNamespace::get_code(const Request& req) {
    return Response::success(req.id.value_or(0), "\"0x\"");
}

Response EthNamespace::get_storage_at(const Request& req) {
    return Response::success(req.id.value_or(0), 
        "\"0x0000000000000000000000000000000000000000000000000000000000000000\"");
}

Response EthNamespace::get_block_by_number(const Request& req) {
    uint64_t num = blocks_->get_head();
    if (req.params) {
        std::string p = *req.params;
        if (p.find("earliest") != std::string::npos) num = 0;
        else if (p.find("pending") != std::string::npos) num = blocks_->get_head();
        else if (p.find("latest") != std::string::npos) num = blocks_->get_head();
        else {
            std::regex hex_regex(R"(\"(0x[0-9a-fA-F]+)\")");
            std::smatch match;
            if (std::regex_search(p, match, hex_regex)) {
                try {
                    num = std::stoull(match[1].str(), nullptr, 16);
                } catch(...) { }
            }
        }
    }

    auto block_opt = blocks_->get_block(num);
    if (!block_opt) {
        return Response::success(req.id.value_or(0), "null");
    }
    
    auto& block = *block_opt;
    std::ostringstream ss;
    ss << "{";
    ss << "\"number\":\"0x" << std::hex << block.header.number << "\"";
    ss << ",\"hash\":\"0x";
    auto hash = block.header.hash();
    for (auto b : hash) ss << std::setw(2) << std::setfill('0') << (int)b;
    ss << "\"";
    ss << ",\"parentHash\":\"0x";
    for (auto b : block.header.parent_hash) ss << std::setw(2) << std::setfill('0') << (int)b;
    ss << "\"";
    ss << ",\"timestamp\":\"0x" << std::hex << block.header.timestamp << "\"";
    ss << ",\"gasLimit\":\"0x" << std::hex << block.header.gas_limit << "\"";
    ss << ",\"gasUsed\":\"0x" << std::hex << block.header.gas_used << "\"";
    ss << ",\"baseFeePerGas\":\"0x" << std::hex << block.header.base_fee << "\"";
    // Check for full transaction objects request
    bool full_txs = false;
    if (req.params && req.params->find("true") != std::string::npos) {
        full_txs = true;
    }

    ss << ",\"transactions\":[";
    for (size_t i = 0; i < block.transactions.size(); ++i) {
        if (i > 0) ss << ",";
        const auto& tx = block.transactions[i];
        
        if (full_txs) {
            ss << "{";
            
            ss << "\"hash\":\"0x";
            auto h = tx.hash();
            for (auto b : h) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
            ss << "\",";
            
            ss << "\"nonce\":\"0x" << std::hex << tx.nonce << "\",";
            
            ss << "\"blockHash\":\"0x";
            auto bh = block.header.hash();
            for (auto b : bh) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
            ss << "\",";
            
            ss << "\"blockNumber\":\"0x" << std::hex << block.header.number << "\",";
            ss << "\"transactionIndex\":\"0x" << std::hex << i << "\",";
            
            ss << "\"from\":\"0x";
            for (auto b : tx.from.payment_credential) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
            ss << "\",";
            
            ss << "\"to\":\"0x";
            for (auto b : tx.to.payment_credential) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
            ss << "\",";
            
            ss << "\"value\":\"0x" << std::hex << tx.value << "\",";
            ss << "\"gas\":\"0x" << std::hex << tx.gas_limit << "\",";
            ss << "\"gasPrice\":\"0x" << std::hex << tx.max_fee_per_gas << "\",";
            
            ss << "\"input\":\"0x";
            for (auto b : tx.data) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
            ss << "\"";
            
            ss << "}";
        } else {
            ss << "\"0x";
            auto h = tx.hash();
            for (auto b : h) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
            ss << "\"";
        }
    }
    ss << "]";
    ss << "}";
    
    return Response::success(req.id.value_or(0), ss.str());
}

Response EthNamespace::get_block_by_hash(const Request& req) {
    return get_block_by_number(req);
}

Response EthNamespace::get_block_transaction_count_by_number(const Request& req) {
    auto block_opt = blocks_->get_block(blocks_->get_head());
    if (!block_opt) {
        return Response::success(req.id.value_or(0), "\"0x0\"");
    }
    
    std::ostringstream ss;
    ss << "\"0x" << std::hex << block_opt->transactions.size() << "\"";
    return Response::success(req.id.value_or(0), ss.str());
}

Response EthNamespace::get_block_transaction_count_by_hash(const Request& req) {
    return get_block_transaction_count_by_number(req);
}

Response EthNamespace::get_transaction_by_hash(const Request& req) {
    if (!req.params.has_value() || req.params->empty()) return Response::success(req.id.value_or(0), "null");

    std::string params = req.params.value();
    std::regex hex_regex("\"(0x[a-fA-F0-9]+)\"");
    std::smatch match;
    if (!std::regex_search(params, match, hex_regex)) return Response::success(req.id.value_or(0), "null");

    std::string tx_hash_str = match[1].str();
    if (tx_hash_str.starts_with("0x")) tx_hash_str = tx_hash_str.substr(2);
    
    if (tx_hash_str.length() != 64) return Response::success(req.id.value_or(0), "null");

    Hash256 tx_hash;
    for (size_t i = 0; i < 32; ++i) {
        try {
            tx_hash[i] = static_cast<uint8_t>(std::stoi(tx_hash_str.substr(i*2, 2), nullptr, 16));
        } catch (...) { return Response::success(req.id.value_or(0), "null"); }
    }
    
    auto receipt_opt = blocks_->get_receipt(tx_hash);
    if (!receipt_opt) return Response::success(req.id.value_or(0), "null");
    
    auto block = blocks_->get_block(receipt_opt->block_number);
    if (!block || receipt_opt->transaction_index >= block->transactions.size()) 
        return Response::success(req.id.value_or(0), "null");
        
    const auto& tx = block->transactions[receipt_opt->transaction_index];
    
    std::ostringstream ss;
    ss << "{";
    ss << "\"hash\":\"0x" << tx_hash_str << "\",";
    ss << "\"nonce\":\"0x" << std::hex << tx.nonce << "\",";
    
    ss << "\"blockHash\":\"0x";
    auto bh = block->header.hash();
    for (auto b : bh) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
    ss << "\",";
    
    ss << "\"blockNumber\":\"0x" << std::hex << block->header.number << "\",";
    ss << "\"transactionIndex\":\"0x" << std::hex << receipt_opt->transaction_index << "\",";
    
    ss << "\"from\":\"0x";
    for (auto b : tx.from.payment_credential) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
    ss << "\",";
    
    ss << "\"to\":\"0x";
    for (auto b : tx.to.payment_credential) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
    ss << "\",";
    
    ss << "\"value\":\"0x" << std::hex << tx.value << "\",";
    ss << "\"gas\":\"0x" << std::hex << tx.gas_limit << "\",";
    ss << "\"gasPrice\":\"0x" << std::hex << tx.max_fee_per_gas << "\",";
    
    ss << "\"input\":\"0x";
    for (auto b : tx.data) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
    ss << "\"";
    
    ss << "}";
    return Response::success(req.id.value_or(0), ss.str());
}

Response EthNamespace::get_transaction_by_block_number_and_index(const Request& req) {
    return Response::success(req.id.value_or(0), "null");
}

Response EthNamespace::get_transaction_receipt(const Request& req) {
    if (!req.params.has_value() || req.params->empty()) {
        return Response::make_error(req.id.value_or(0), 
            ErrorCode::InvalidParams, "Missing transaction hash");
    }

    // Extract tx hash
    std::string params = req.params.value();
    std::regex hex_regex("\"(0x[a-fA-F0-9]+)\"");
    std::smatch match;
    if (!std::regex_search(params, match, hex_regex) || match.size() < 2) {
        return Response::make_error(req.id.value_or(0),
            ErrorCode::InvalidParams, "Invalid format");
    }
    
    std::string tx_hash_str = match[1].str();
    if (tx_hash_str.starts_with("0x")) tx_hash_str = tx_hash_str.substr(2);
    
    Hash256 tx_hash;
    if (tx_hash_str.length() != 64) {
         return Response::make_error(req.id.value_or(0),
            ErrorCode::InvalidParams, "Invalid hash length");
    }
    
    for (size_t i = 0; i < 32; ++i) {
        try {
            tx_hash[i] = static_cast<uint8_t>(std::stoi(tx_hash_str.substr(i*2, 2), nullptr, 16));
        } catch (...) {
             return Response::make_error(req.id.value_or(0),
                ErrorCode::InvalidParams, "Invalid hash hex");
        }
    }
    
    auto receipt_opt = blocks_->get_receipt(tx_hash);
    if (!receipt_opt) {
        return Response::success(req.id.value_or(0), "null");
    }
    
    const auto& receipt = *receipt_opt;
    
    std::ostringstream ss;
    ss << "{";
    ss << "\"transactionHash\":\"0x" << tx_hash_str << "\",";
    ss << "\"transactionIndex\":\"0x" << std::hex << receipt.transaction_index << "\",";
    ss << "\"blockNumber\":\"0x" << std::hex << receipt.block_number << "\",";
    
    // Fetch block to get hash
    auto block = blocks_->get_block(receipt.block_number);
    if (block) {
        auto h = block->header.hash();
        ss << "\"blockHash\":\"0x";
        for (auto b : h) ss << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        ss << "\",";
    }
    
    // From/To
    ss << "\"from\":\"0x";
    for (auto b : receipt.from.payment_credential) ss << std::setfill('0') << std::setw(2) << static_cast<int>(b);
    ss << "\",";
    
    if (receipt.to.payment_credential == Address{}.payment_credential) {
        ss << "\"to\":null,";
    } else {
        ss << "\"to\":\"0x";
        for (auto b : receipt.to.payment_credential) ss << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        ss << "\",";
    }
    
    if (receipt.contract_address) {
        ss << "\"contractAddress\":\"0x";
        for (auto b : receipt.contract_address->payment_credential) ss << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        ss << "\",";
    } else {
        ss << "\"contractAddress\":null,";
    }
    
    ss << "\"status\":\"0x" << (receipt.success ? "1" : "0") << "\",";
    ss << "\"gasUsed\":\"0x" << std::hex << receipt.gas_used << "\",";
    ss << "\"cumulativeGasUsed\":\"0x" << std::hex << receipt.cumulative_gas_used << "\",";
    
    // Logs
    ss << "\"logs\":[";
    for (size_t i = 0; i < receipt.logs.size(); ++i) {
        if (i > 0) ss << ",";
        const auto& log = receipt.logs[i];
        ss << "{";
        ss << "\"address\":\"0x";
        for (auto b : log.address.payment_credential) ss << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        ss << "\",";
        ss << "\"topics\":[";
        for (size_t t = 0; t < log.topics.size(); ++t) {
            if (t > 0) ss << ",";
            ss << "\"0x";
            for (auto b : log.topics[t]) ss << std::setfill('0') << std::setw(2) << static_cast<int>(b);
            ss << "\"";
        }
        ss << "],";
        ss << "\"data\":\"0x";
        for (auto b : log.data) ss << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        ss << "\"";
        ss << "}";
    }
    ss << "]";
    ss << "}";
    
    return Response::success(req.id.value_or(0), ss.str());
}

Response EthNamespace::send_raw_transaction(const Request& req) {
    // Parse the raw transaction hex from params
    if (!req.params.has_value() || req.params->empty()) {
        return Response::make_error(req.id.value_or(0), 
            ErrorCode::InvalidParams, "Missing transaction data");
    }
    
    // Extract hex string (first param)
    std::string raw_tx = req.params.value();
    
    // Find the hex string in params (handle JSON array)
    std::regex hex_regex("\"(0x[a-fA-F0-9]+)\"");
    std::smatch match;
    if (!std::regex_search(raw_tx, match, hex_regex) || match.size() < 2) {
        return Response::make_error(req.id.value_or(0),
            ErrorCode::InvalidParams, "Invalid transaction hex format");
    }
    
    std::string hex_data = match[1].str();
    if (hex_data.starts_with("0x")) {
        hex_data = hex_data.substr(2);
    }
    
    // Decode hex to bytes
    Bytes tx_bytes;
    for (size_t i = 0; i < hex_data.length(); i += 2) {
        try {
            tx_bytes.push_back(static_cast<uint8_t>(
                std::stoi(hex_data.substr(i, 2), nullptr, 16)));
        } catch (...) {
            return Response::make_error(req.id.value_or(0),
                ErrorCode::InvalidParams, "Invalid hex in transaction data");
        }
    }
    
    // Decode transaction
    auto tx_opt = Transaction::decode(tx_bytes);
    if (!tx_opt) {
        return Response::make_error(req.id.value_or(0),
            ErrorCode::InvalidParams, "Failed to decode transaction");
    }
    
    Transaction tx = *tx_opt;
    
    // Verify signature
    if (!tx.verify_signature()) {
        return Response::make_error(req.id.value_or(0),
            ErrorCode::InvalidParams, "Invalid transaction signature");
    }
    
    // Compute transaction hash
    auto tx_hash = tx.hash();
    
    // Get sender balance for mempool checks
    uint64_t sender_balance = 0;
    if (state_) {
        sender_balance = state_->get_balance(tx.from);
    }
    
    // Add to mempool
    if (mempool_) {
        auto result = mempool_->add_transaction(tx, sender_balance);
        if (result == consensus::Mempool::AddResult::Added ||
            result == consensus::Mempool::AddResult::Replaced) {
            std::cout << "[RPC] Transaction added to mempool: 0x";
            for (auto b : tx_hash) {
                printf("%02x", b);
            }
            std::cout << std::endl;
        } else {
            return Response::make_error(req.id.value_or(0),
                ErrorCode::InvalidParams, "Transaction rejected by mempool");
        }
    }
    
    // Return transaction hash
    std::ostringstream ss;
    ss << "\"0x";
    for (auto b : tx_hash) {
        ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
    }
    ss << "\"";
    
    return Response::success(req.id.value_or(0), ss.str());
}

Response EthNamespace::call(const Request& req) {
    return Response::success(req.id.value_or(0), "\"0x\"");
}

Response EthNamespace::estimate_gas(const Request& req) {
    return Response::success(req.id.value_or(0), "\"0x5208\"");
}

Response EthNamespace::get_logs(const Request& req) {
    return Response::success(req.id.value_or(0), "[]");
}

Response EthNamespace::get_recent_transactions(const Request& req) {
    uint64_t count = 50;
    if (req.params) {
        std::regex num_regex(R"(\[(\d+)\])");
        std::smatch match;
        std::string p = *req.params;
        if (std::regex_search(p, match, num_regex)) {
            try {
                count = std::stoull(match[1].str());
            } catch(...) {}
        }
    }

    uint64_t head = blocks_->get_head();
    std::ostringstream ss;
    ss << "[";
    
    uint64_t gathered = 0;
    bool first = true;

    for (uint64_t i = head; i != (uint64_t)-1 && gathered < count; --i) {
        auto block_opt = blocks_->get_block(i);
        if (!block_opt) continue;

        for (auto it = block_opt->transactions.rbegin(); it != block_opt->transactions.rend() && gathered < count; ++it) {
            if (!first) ss << ",";
            first = false;

            const auto& tx = *it;
            ss << "{";
            
            ss << "\"hash\":\"0x";
            auto h = tx.hash();
            for (auto b : h) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
            ss << "\",";
            
            ss << "\"blockNumber\":\"0x" << std::hex << i << "\",";
            ss << "\"timestamp\":\"0x" << std::hex << block_opt->header.timestamp << "\",";
            
            ss << "\"from\":\"0x";
            for (auto b : tx.from.payment_credential) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
            ss << "\",";
            
            ss << "\"to\":\"0x";
            for (auto b : tx.to.payment_credential) ss << std::setw(2) << std::setfill('0') << std::hex << (int)b;
            ss << "\",";
            
            ss << "\"value\":\"0x" << std::hex << tx.value << "\",";
            ss << "\"nonce\":\"0x" << std::hex << tx.nonce << "\"";
            
            ss << "}";
            gathered++;
        }
    }
    ss << "]";

    return Response::success(req.id.value_or(0), ss.str());
}

void EthNamespace::register_methods(Server& server) {
    server.register_method("eth_chainId", [this](const Request& req) { return chain_id(req); });
    server.register_method("eth_blockNumber", [this](const Request& req) { return block_number(req); });
    server.register_method("eth_gasPrice", [this](const Request& req) { return gas_price(req); });
    server.register_method("eth_maxPriorityFeePerGas", [this](const Request& req) { return max_priority_fee_per_gas(req); });
    server.register_method("eth_feeHistory", [this](const Request& req) { return fee_history(req); });
    server.register_method("eth_getBalance", [this](const Request& req) { return get_balance(req); });
    server.register_method("eth_getTransactionCount", [this](const Request& req) { return get_transaction_count(req); });
    server.register_method("eth_getCode", [this](const Request& req) { return get_code(req); });
    server.register_method("eth_getStorageAt", [this](const Request& req) { return get_storage_at(req); });
    server.register_method("eth_getBlockByNumber", [this](const Request& req) { return get_block_by_number(req); });
    server.register_method("eth_getBlockByHash", [this](const Request& req) { return get_block_by_hash(req); });
    server.register_method("eth_getBlockTransactionCountByNumber", [this](const Request& req) { return get_block_transaction_count_by_number(req); });
    server.register_method("eth_getBlockTransactionCountByHash", [this](const Request& req) { return get_block_transaction_count_by_hash(req); });
    server.register_method("eth_getTransactionByHash", [this](const Request& req) { return get_transaction_by_hash(req); });
    server.register_method("eth_getTransactionByBlockNumberAndIndex", [this](const Request& req) { return get_transaction_by_block_number_and_index(req); });
    server.register_method("eth_getTransactionReceipt", [this](const Request& req) { return get_transaction_receipt(req); });
    server.register_method("eth_sendRawTransaction", [this](const Request& req) { return send_raw_transaction(req); });
    server.register_method("eth_call", [this](const Request& req) { return call(req); });
    server.register_method("eth_estimateGas", [this](const Request& req) { return estimate_gas(req); });
    server.register_method("eth_getLogs", [this](const Request& req) { return get_logs(req); });
    server.register_method("nonagon_getRecentTransactions", [this](const Request& req) { return get_recent_transactions(req); });
    
    server.register_method("web3_clientVersion", [](const Request& req) {
        return Response::success(req.id.value_or(0), "\"Nonagon/v0.1.0/C++20\"");
    });
    
    server.register_method("net_version", [](const Request& req) {
        return Response::success(req.id.value_or(0), "\"1\"");
    });
    
    server.register_method("net_listening", [](const Request& req) {
        return Response::success(req.id.value_or(0), "true");
    });
    
    server.register_method("net_peerCount", [](const Request& req) {
        return Response::success(req.id.value_or(0), "\"0x0\"");
    });
}

// ============================================================================
// NonagonNamespace Implementation
// ============================================================================

NonagonNamespace::NonagonNamespace(std::shared_ptr<settlement::SettlementManager> settlement,
                                   std::shared_ptr<consensus::ConsensusEngine> consensus)
    : settlement_(settlement), consensus_(consensus) {}

Response NonagonNamespace::get_batch(const Request& req) {
    return Response::success(req.id.value_or(0), "null");
}

Response NonagonNamespace::get_latest_batch(const Request& req) {
    return Response::success(req.id.value_or(0), 
        "{\"batchId\":0,\"status\":\"pending\",\"blockRange\":[0,0]}");
}

Response NonagonNamespace::get_batch_status(const Request& req) {
    return Response::success(req.id.value_or(0), "\"pending\"");
}

Response NonagonNamespace::get_l1_finalized_block(const Request& req) {
    return Response::success(req.id.value_or(0), "\"0x0\"");
}

Response NonagonNamespace::get_deposit_status(const Request& req) {
    return Response::success(req.id.value_or(0), "\"confirmed\"");
}

Response NonagonNamespace::get_withdrawal_status(const Request& req) {
    return Response::success(req.id.value_or(0), "\"pending\"");
}

Response NonagonNamespace::estimate_withdrawal_time(const Request& req) {
    return Response::success(req.id.value_or(0), "604800");
}

Response NonagonNamespace::get_sequencer_set(const Request& req) {
    if (!consensus_) {
        return Response::success(req.id.value_or(0), "[]");
    }
    
    auto sequencers = consensus_->get_active_sequencers();
    
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < sequencers.size(); ++i) {
        if (i > 0) ss << ",";
        ss << "{";
        ss << "\"address\":\"" << sequencers[i].address.to_bech32() << "\"";
        ss << ",\"stake\":" << sequencers[i].stake;
        ss << ",\"status\":\"active\"";
        ss << "}";
    }
    ss << "]";
    
    return Response::success(req.id.value_or(0), ss.str());
}

Response NonagonNamespace::get_current_sequencer(const Request& req) {
    if (consensus_) {
        // Return first active sequencer for now
        auto active = consensus_->get_active_sequencers();
        if (!active.empty()) {
            return Response::success(req.id.value_or(0), "\"" + active[0].address.to_hex() + "\"");
        }
    }
    return Response::success(req.id.value_or(0), "\"0x0000000000000000000000000000000000000000\"");
}

Response NonagonNamespace::get_next_batch_time(const Request& req) {
    return Response::success(req.id.value_or(0), "3600");
}

void NonagonNamespace::register_methods(Server& server) {
    server.register_method("nonagon_getBatch", [this](const Request& req) { return get_batch(req); });
    server.register_method("nonagon_getLatestBatch", [this](const Request& req) { return get_latest_batch(req); });
    server.register_method("nonagon_getBatchStatus", [this](const Request& req) { return get_batch_status(req); });
    server.register_method("nonagon_getL1FinalizedBlock", [this](const Request& req) { return get_l1_finalized_block(req); });
    server.register_method("nonagon_getDepositStatus", [this](const Request& req) { return get_deposit_status(req); });
    server.register_method("nonagon_getWithdrawalStatus", [this](const Request& req) { return get_withdrawal_status(req); });
    server.register_method("nonagon_estimateWithdrawalTime", [this](const Request& req) { return estimate_withdrawal_time(req); });
    server.register_method("nonagon_getSequencerSet", [this](const Request& req) { return get_sequencer_set(req); });
    server.register_method("nonagon_getCurrentSequencer", [this](const Request& req) { return get_current_sequencer(req); });
    server.register_method("nonagon_getNextBatchTime", [this](const Request& req) { return get_next_batch_time(req); });
}

} // namespace rpc
} // namespace nonagon
