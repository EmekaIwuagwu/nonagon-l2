#include "nonagon/network.hpp"
#include <iostream>
#include <thread>
#include <cstring>
#include <algorithm>
#include <random>

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
#include <fcntl.h>
#endif

namespace nonagon {
namespace network {

// ============================================================================
// Platform Socket Helpers
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

// ============================================================================
// PeerId Implementation
// ============================================================================

std::string PeerId::to_string() const {
    std::string hex;
    hex.reserve(64);
    static const char hex_chars[] = "0123456789abcdef";
    for (uint8_t b : id) {
        hex.push_back(hex_chars[b >> 4]);
        hex.push_back(hex_chars[b & 0x0F]);
    }
    return hex;
}

// ============================================================================
// Message Implementation
// ============================================================================

Bytes Message::encode() const {
    Bytes data;
    data.push_back(static_cast<uint8_t>(type));
    // Timestamp
    for (int i = 7; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((timestamp >> (i * 8)) & 0xFF));
    }
    // Size
    uint32_t size = static_cast<uint32_t>(payload.size());
    for (int i = 3; i >= 0; --i) {
        data.push_back(static_cast<uint8_t>((size >> (i * 8)) & 0xFF));
    }
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
}

std::optional<Message> Message::decode(const Bytes& data) {
    if (data.size() < 13) return std::nullopt;
    Message msg;
    size_t offset = 0;
    msg.type = static_cast<MessageType>(data[offset++]);
    msg.timestamp = 0;
    for (int i = 0; i < 8; ++i) {
        msg.timestamp = (msg.timestamp << 8) | data[offset++];
    }
    uint32_t size = 0;
    for (int i = 0; i < 4; ++i) {
        size = (size << 8) | data[offset++];
    }
    if (data.size() < offset + size) return std::nullopt;
    msg.payload.assign(data.begin() + offset, data.begin() + offset + size);
    return msg;
}

// ============================================================================
// PeerDiscovery Implementation
// ============================================================================

PeerDiscovery::PeerDiscovery(const PeerId& local_id)
    : local_id_(local_id) {
}

void PeerDiscovery::add_peer(const PeerInfo& peer) {
    if (peer.id == local_id_) return;
    size_t idx = bucket_index(peer.id);
    if (idx < buckets_.size()) {
        auto& bucket = buckets_[idx];
        for (auto& p : bucket) {
            if (p.id == peer.id) {
                p = peer;
                return;
            }
        }
        if (bucket.size() < K) {
            bucket.push_back(peer);
        } else {
            bucket.pop_back();
            bucket.push_back(peer);
        }
    }
}

void PeerDiscovery::remove_peer(const PeerId& id) {
    size_t idx = bucket_index(id);
    if (idx < buckets_.size()) {
        auto& bucket = buckets_[idx];
        bucket.erase(
            std::remove_if(bucket.begin(), bucket.end(), 
                [&id](const PeerInfo& p) { return p.id == id; }),
            bucket.end()
        );
    }
}

std::vector<PeerInfo> PeerDiscovery::find_node(const PeerId& target, size_t count) {
    std::vector<PeerInfo> all_peers;
    for (const auto& bucket : buckets_) {
        all_peers.insert(all_peers.end(), bucket.begin(), bucket.end());
    }
    if (all_peers.size() > count) all_peers.resize(count);
    return all_peers;
}

std::vector<PeerInfo> PeerDiscovery::get_peers(size_t count) const {
    std::vector<PeerInfo> all_peers;
    for (const auto& bucket : buckets_) {
        all_peers.insert(all_peers.end(), bucket.begin(), bucket.end());
    }
    if (all_peers.size() > count) all_peers.resize(count);
    return all_peers;
}

void PeerDiscovery::bootstrap(const std::vector<NetworkAddress>& nodes) {
    // Stub
}

void PeerDiscovery::announce() {
    // Stub
}

size_t PeerDiscovery::bucket_index(const PeerId& id) const {
    return xor_distance(local_id_, id) % 256;
}

size_t PeerDiscovery::xor_distance(const PeerId& a, const PeerId& b) const {
    size_t dist = 0;
    for (size_t i = 0; i < 32; ++i) {
        dist += (a.id[i] ^ b.id[i]);
    }
    return dist;
}

// ============================================================================
// P2PNetwork Implementation
// ============================================================================

P2PNetwork::P2PNetwork(const NetworkConfig& config)
    : config_(config) {
    // Generate random local ID
    for (int i = 0; i < 32; ++i) {
        local_id_.id[i] = static_cast<uint8_t>(rand() & 0xFF);
    }
    discovery_ = std::make_unique<PeerDiscovery>(local_id_);
    
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

P2PNetwork::~P2PNetwork() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool P2PNetwork::start() {
    if (running_) return true;
    running_ = true;
    listener_thread_ = std::thread([this]() { listener_loop(); });
    discovery_thread_ = std::thread([this]() { discovery_loop(); });
    std::cout << "[P2P] Network started on port " << config_.listen_port << std::endl;
    return true;
}

void P2PNetwork::stop() {
    running_ = false;
    if (listener_thread_.joinable()) listener_thread_.join();
    if (discovery_thread_.joinable()) discovery_thread_.join();
    if (maintenance_thread_.joinable()) maintenance_thread_.join();
    std::unique_lock lock(peers_mutex_);
    peers_.clear();
}

void P2PNetwork::broadcast(const Message& msg) {
    std::shared_lock lock(peers_mutex_);
    for (const auto& [id, peer_ctx] : peers_) {
        // Send to each peer
    }
}

void P2PNetwork::send(const PeerId& peer, const Message& msg) {
    // Stub
}

bool P2PNetwork::connect(const NetworkAddress& addr) {
    // Stub
    return true;
}

void P2PNetwork::disconnect(const PeerId& peer) {
    std::unique_lock lock(peers_mutex_);
    peers_.erase(peer.to_string());
    discovery_->remove_peer(peer);
}

std::vector<PeerInfo> P2PNetwork::get_connected_peers() const {
    std::shared_lock lock(peers_mutex_);
    std::vector<PeerInfo> result;
    for (const auto& [id, ctx] : peers_) {
        result.push_back(ctx);
    }
    return result;
}

size_t P2PNetwork::peer_count() const {
    std::shared_lock lock(peers_mutex_);
    return peers_.size();
}

void P2PNetwork::register_handler(MessageType type, MessageHandler handler) {
    uint8_t key = static_cast<uint8_t>(type);
    handlers_[key].push_back(handler);
}

void P2PNetwork::request_headers(const PeerId& peer, uint64_t from_block, uint32_t count) {
    Message msg;
    msg.type = MessageType::GetBlockHeaders;
    // encode payload
    send(peer, msg);
}

void P2PNetwork::request_bodies(const PeerId& peer, const std::vector<Hash256>& hashes) {
    Message msg;
    msg.type = MessageType::GetBlockBodies;
    send(peer, msg);
}

void P2PNetwork::adjust_reputation(const PeerId& peer, int delta) {}
void P2PNetwork::ban_peer(const PeerId& peer, uint64_t duration_seconds) {}

NetworkAddress P2PNetwork::local_address() const {
    NetworkAddress addr;
    addr.port = config_.listen_port;
    addr.host = config_.listen_address;
    return addr;
}

void P2PNetwork::listener_loop() {
    socket_t server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == INVALID_SOCK) return;
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.listen_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close_socket(server_sock);
        return;
    }
    
    if (listen(server_sock, 10) < 0) {
        close_socket(server_sock);
        return;
    }
    
    while (running_) {
        // Accept loop
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    close_socket(server_sock);
}

void P2PNetwork::discovery_loop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

void P2PNetwork::maintenance_loop() {
}

// ============================================================================
// BlockSynchronizer Implementation
// ============================================================================

BlockSynchronizer::BlockSynchronizer(std::shared_ptr<P2PNetwork> network,
                                     std::shared_ptr<storage::BlockStore> blocks,
                                     std::shared_ptr<storage::StateManager> state)
    : network_(network), blocks_(blocks), state_(state) {
}

void BlockSynchronizer::start(SyncMode mode) {
    mode_ = mode;
    syncing_ = true;
    sync_thread_ = std::thread([this]() {
        std::cout << "[SYNC] Block synchronizer started" << std::endl;
        while (syncing_) {
            // Check if we need to sync
            if (network_->peer_count() > 0) {
                // Simplified sync logic:
                // 1. Get highest block from peers
                // 2. Request missing blocks
                // 3. Process received blocks
                
                // For now, simple log every 10s
                static auto last_log = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count() > 10) {
                    std::cout << "[SYNC] Syncing... current block: " << current_block_ << std::endl;
                    last_log = now;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // tight loop for responsiveness
        }
        std::cout << "[SYNC] Block synchronizer stopped" << std::endl;
    });
}

void BlockSynchronizer::stop() {
    syncing_ = false;
    if (sync_thread_.joinable()) sync_thread_.join();
}

BlockSynchronizer::SyncStatus BlockSynchronizer::status() const {
    SyncStatus s;
    s.syncing = syncing_;
    s.current_block = current_block_;
    s.highest_block = target_block_;
    return s;
}

} // namespace network
} // namespace nonagon
