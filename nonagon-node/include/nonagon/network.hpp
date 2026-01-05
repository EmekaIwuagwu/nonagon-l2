#pragma once

#include <memory>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <chrono>
#include "nonagon/types.hpp"

// Forward declarations
namespace nonagon {
namespace storage { class BlockStore; class StateManager; }
}

namespace nonagon {
namespace network {

/**
 * @brief Peer identifier
 */
struct PeerId {
    std::array<uint8_t, 32> id;
    
    std::string to_string() const;
    static PeerId from_public_key(const crypto::Ed25519::PublicKey& pk);
    bool operator==(const PeerId& other) const = default;
};

/**
 * @brief Network address
 */
struct NetworkAddress {
    std::string host;
    uint16_t port;
    
    std::string to_string() const;
    static std::optional<NetworkAddress> parse(const std::string& str);
};

/**
 * @brief Peer connection info
 */
struct PeerInfo {
    PeerId id;
    NetworkAddress address;
    crypto::Ed25519::PublicKey public_key;
    
    enum class Status {
        Connecting,
        Connected,
        Disconnected,
        Banned
    };
    Status status{Status::Disconnected};
    
    // Metrics
    uint64_t connected_since{0};
    uint64_t bytes_sent{0};
    uint64_t bytes_received{0};
    uint32_t latency_ms{0};
    
    // Reputation (0-100)
    int reputation{50};
};

/**
 * @brief P2P message types
 */
enum class MessageType : uint8_t {
    // Handshake
    Hello = 0x00,
    HelloAck = 0x01,
    Disconnect = 0x02,
    Ping = 0x03,
    Pong = 0x04,
    
    // Sync
    GetBlockHeaders = 0x10,
    BlockHeaders = 0x11,
    GetBlockBodies = 0x12,
    BlockBodies = 0x13,
    GetState = 0x14,
    StateData = 0x15,
    
    // Propagation
    NewBlock = 0x20,
    NewBlockHashes = 0x21,
    NewTransactions = 0x22,
    
    // Consensus
    BlockProposal = 0x30,
    BlockVote = 0x31,
    
    // Settlement
    BatchAnnounce = 0x40,
    FraudProofAlert = 0x41
};

/**
 * @brief P2P message structure
 */
struct Message {
    MessageType type;
    Bytes payload;
    PeerId from;
    uint64_t timestamp;
    
    Bytes encode() const;
    static std::optional<Message> decode(const Bytes& data);
};

/**
 * @brief Network configuration
 */
struct NetworkConfig {
    uint16_t listen_port{30303};
    std::string listen_address{"0.0.0.0"};
    
    uint32_t max_peers{50};
    uint32_t target_peers{25};
    
    std::vector<NetworkAddress> bootstrap_nodes;
    
    // Timeouts (ms)
    uint32_t connection_timeout{5000};
    uint32_t handshake_timeout{10000};
    uint32_t request_timeout{30000};
    
    // Rate limiting
    uint32_t max_messages_per_second{100};
    uint32_t max_bytes_per_second{10485760};  // 10 MB/s
    
    // Peer scoring
    int ban_threshold{-50};
    uint64_t ban_duration_seconds{86400};  // 24 hours
};

/**
 * @brief Peer discovery using Kademlia DHT
 */
class PeerDiscovery {
public:
    explicit PeerDiscovery(const PeerId& local_id);
    
    // Bootstrap from known nodes
    void bootstrap(const std::vector<NetworkAddress>& nodes);
    
    // Find peers close to target ID
    std::vector<PeerInfo> find_node(const PeerId& target, size_t count);
    
    // Announce presence
    void announce();
    
    // Get known peers
    std::vector<PeerInfo> get_peers(size_t count) const;
    
    // Add discovered peer
    void add_peer(const PeerInfo& peer);
    void remove_peer(const PeerId& id);

private:
    PeerId local_id_;
    
    // Kademlia k-buckets (256 buckets, k=16)
    static constexpr size_t K = 16;
    std::array<std::vector<PeerInfo>, 256> buckets_;
    
    size_t bucket_index(const PeerId& id) const;
    size_t xor_distance(const PeerId& a, const PeerId& b) const;
};

/**
 * @brief P2P network manager
 */
class P2PNetwork {
public:
    explicit P2PNetwork(const NetworkConfig& config);
    ~P2PNetwork();
    
    // Lifecycle
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    // Peer management
    bool connect(const NetworkAddress& addr);
    void disconnect(const PeerId& peer);
    std::vector<PeerInfo> get_connected_peers() const;
    size_t peer_count() const;
    
    // Messaging
    void broadcast(const Message& msg);
    void send(const PeerId& peer, const Message& msg);
    
    // Message handlers
    using MessageHandler = std::function<void(const Message&)>;
    void register_handler(MessageType type, MessageHandler handler);
    
    // Sync protocol
    void request_headers(const PeerId& peer, uint64_t from_block, uint32_t count);
    void request_bodies(const PeerId& peer, const std::vector<Hash256>& hashes);
    
    // Peer scoring
    void adjust_reputation(const PeerId& peer, int delta);
    void ban_peer(const PeerId& peer, uint64_t duration_seconds);
    
    // Local node info
    PeerId local_peer_id() const { return local_id_; }
    NetworkAddress local_address() const;

private:
    NetworkConfig config_;
    PeerId local_id_;
    crypto::Ed25519::KeyPair keypair_;
    
    std::atomic<bool> running_{false};
    std::unique_ptr<PeerDiscovery> discovery_;
    
    // Connected peers
    mutable std::shared_mutex peers_mutex_;
    std::unordered_map<std::string, PeerInfo> peers_;
    
    // Message handlers
    std::unordered_map<uint8_t, std::vector<MessageHandler>> handlers_;
    
    // Background threads
    std::thread listener_thread_;
    std::thread discovery_thread_;
    std::thread maintenance_thread_;
    
    void listener_loop();
    void discovery_loop();
    void maintenance_loop();
    
    void handle_connection(int socket);
    void process_message(const Message& msg);
};

/**
 * @brief Block synchronizer
 * 
 * Syncs chain state from peers:
 * - Fast sync: Download state snapshot
 * - Full sync: Download and verify all blocks
 * - Snap sync: Hybrid approach
 */
class BlockSynchronizer {
public:
    enum class SyncMode {
        Full,    // Download and execute all blocks
        Fast,    // Download state at checkpoint, then full
        Snap     // Download state in chunks
    };
    
    BlockSynchronizer(std::shared_ptr<P2PNetwork> network,
                      std::shared_ptr<storage::BlockStore> blocks,
                      std::shared_ptr<storage::StateManager> state);
    
    // Start/stop sync
    void start(SyncMode mode = SyncMode::Fast);
    void stop();
    
    // Sync status
    struct SyncStatus {
        bool syncing;
        uint64_t current_block;
        uint64_t highest_block;
        float progress_percent;
        uint32_t peers_syncing;
    };
    SyncStatus status() const;
    
    // Callbacks
    using ProgressCallback = std::function<void(uint64_t current, uint64_t target)>;
    using CompleteCallback = std::function<void()>;
    void on_progress(ProgressCallback cb) { progress_cb_ = cb; }
    void on_complete(CompleteCallback cb) { complete_cb_ = cb; }

private:
    std::shared_ptr<P2PNetwork> network_;
    std::shared_ptr<storage::BlockStore> blocks_;
    std::shared_ptr<storage::StateManager> state_;
    
    SyncMode mode_{SyncMode::Fast};
    std::atomic<bool> syncing_{false};
    std::atomic<uint64_t> current_block_{0};
    std::atomic<uint64_t> target_block_{0};
    
    ProgressCallback progress_cb_;
    CompleteCallback complete_cb_;
    
    std::thread sync_thread_;
    
    void sync_loop();
    void download_headers(uint64_t from, uint64_t to);
    void download_bodies(const std::vector<Hash256>& hashes);
    void download_state(uint64_t block_number);
};

} // namespace network
} // namespace nonagon
