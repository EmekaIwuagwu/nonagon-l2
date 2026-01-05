#include <iostream>
#include <csignal>
#include <thread>
#include "nonagon/node.hpp"
#include "nonagon/rpc.hpp"
#include "nonagon/storage.hpp"
#include "nonagon/consensus.hpp"
#include "nonagon/execution.hpp"

using namespace nonagon;

// Global instances for signal handling
std::unique_ptr<Node> g_node;
std::unique_ptr<rpc::Server> g_rpc_server;

void signal_handler(int signal) {
    std::cout << "\n[NONAGON] Received signal " << signal << ", shutting down..." << std::endl;
    if (g_rpc_server) {
        g_rpc_server->stop();
    }
    if (g_node) {
        g_node->stop();
    }
}

void print_banner() {
    std::cout << R"(
    ╔═══════════════════════════════════════════════════════════════╗
    ║                                                               ║
    ║     ███╗   ██╗ ██████╗ ███╗   ██╗ █████╗  ██████╗  ██████╗ ███╗   ██╗    ║
    ║     ████╗  ██║██╔═══██╗████╗  ██║██╔══██╗██╔════╝ ██╔═══██╗████╗  ██║    ║
    ║     ██╔██╗ ██║██║   ██║██╔██╗ ██║███████║██║  ███╗██║   ██║██╔██╗ ██║    ║
    ║     ██║╚██╗██║██║   ██║██║╚██╗██║██╔══██║██║   ██║██║   ██║██║╚██╗██║    ║
    ║     ██║ ╚████║╚██████╔╝██║ ╚████║██║  ██║╚██████╔╝╚██████╔╝██║ ╚████║    ║
    ║     ╚═╝  ╚═══╝ ╚═════╝ ╚═╝  ╚═══╝╚═╝  ╚═╝ ╚═════╝  ╚═════╝ ╚═╝  ╚═══╝    ║
    ║                                                               ║
    ║               Layer-2 Blockchain for Cardano                  ║
    ║                    Native Asset: NATX                         ║
    ║                                                               ║
    ╚═══════════════════════════════════════════════════════════════╝
    )" << std::endl;
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "\nOptions:\n"
              << "  --config <path>      Path to config file (default: config.toml)\n"
              << "  --data-dir <path>    Data directory (default: ./data)\n"
              << "  --genesis <path>     Genesis file (default: genesis.json)\n"
              << "  --sequencer          Enable sequencer mode\n"
              << "  --rpc-port <port>    RPC HTTP port (default: 8545)\n"
              << "  --p2p-port <port>    P2P port (default: 30303)\n"
              << "  --log-level <level>  Log level: trace, debug, info, warn, error\n"
              << "  --help               Show this help message\n"
              << std::endl;
}

NodeConfig parse_args(int argc, char* argv[]) {
    NodeConfig config;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "--config" && i + 1 < argc) {
            config = NodeConfig::from_file(argv[++i]);
        } else if (arg == "--data-dir" && i + 1 < argc) {
            config.data_dir = argv[++i];
        } else if (arg == "--genesis" && i + 1 < argc) {
            config.genesis_file = argv[++i];
        } else if (arg == "--sequencer") {
            config.is_sequencer = true;
        } else if (arg == "--rpc-port" && i + 1 < argc) {
            config.rpc.http_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--p2p-port" && i + 1 < argc) {
            config.network.listen_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--log-level" && i + 1 < argc) {
            std::string level = argv[++i];
            if (level == "trace") config.log_level = NodeConfig::LogLevel::Trace;
            else if (level == "debug") config.log_level = NodeConfig::LogLevel::Debug;
            else if (level == "info") config.log_level = NodeConfig::LogLevel::Info;
            else if (level == "warn") config.log_level = NodeConfig::LogLevel::Warn;
            else if (level == "error") config.log_level = NodeConfig::LogLevel::Error;
        }
    }
    
    return config;
}

int main(int argc, char* argv[]) {
    print_banner();
    
    std::cout << "[NONAGON] Version: 0.1.0-dev" << std::endl;
    std::cout << "[NONAGON] Chain: Cardano L2" << std::endl;
    std::cout << "[NONAGON] Starting node..." << std::endl;
    
    // Parse command line arguments
    NodeConfig config = parse_args(argc, argv);
    
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        // Create and initialize node
        g_node = std::make_unique<Node>(config);
        
        std::cout << "[NONAGON] Initializing components..." << std::endl;
        if (!g_node->initialize()) {
            std::cerr << "[NONAGON] Failed to initialize node" << std::endl;
            return 1;
        }
        
        std::cout << "[NONAGON] Starting services..." << std::endl;
        if (!g_node->start()) {
            std::cerr << "[NONAGON] Failed to start node" << std::endl;
            return 1;
        }
        
        // Start RPC server
        std::cout << "[NONAGON] Starting RPC server..." << std::endl;
        rpc::ServerConfig rpc_config;
        rpc_config.http_port = config.rpc.http_port;
        rpc_config.host = "0.0.0.0";
        rpc_config.enable_http = true;
        
        g_rpc_server = std::make_unique<rpc::Server>(rpc_config);
        
        // Create RPC namespace handlers
        auto state_mgr = g_node->state_manager();
        auto block_store = g_node->block_store();
        auto mpool = g_node->mempool();
        auto tx_proc = g_node->transaction_processor();
        auto settlement_mgr = g_node->settlement_manager();
        auto consensus_eng = g_node->consensus();
        
        auto eth_ns = std::make_shared<rpc::EthNamespace>(state_mgr, block_store, mpool, tx_proc);
        auto nonagon_ns = std::make_shared<rpc::NonagonNamespace>(settlement_mgr, consensus_eng);
        
        eth_ns->register_methods(*g_rpc_server);
        nonagon_ns->register_methods(*g_rpc_server);
        
        if (!g_rpc_server->start()) {
            std::cerr << "[NONAGON] Failed to start RPC server" << std::endl;
            return 1;
        }
        
        // Print status
        std::cout << "\n[NONAGON] Node is running!" << std::endl;
        std::cout << "[NONAGON] RPC: http://localhost:" << config.rpc.http_port << std::endl;
        std::cout << "[NONAGON] P2P: " << config.network.listen_address << ":" 
                  << config.network.listen_port << std::endl;
        
        if (config.is_sequencer) {
            std::cout << "[NONAGON] Mode: SEQUENCER" << std::endl;
        } else {
            std::cout << "[NONAGON] Mode: FULL NODE" << std::endl;
        }
        
        std::cout << "\n[NONAGON] Press Ctrl+C to stop\n" << std::endl;
        
        // Main loop - wait for shutdown signal
        while (g_node->is_running()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Periodic status update
            auto health = g_node->health();
            // Could log metrics here
        }
        
        std::cout << "[NONAGON] Shutdown complete" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "[NONAGON] Fatal error: " << e.what() << std::endl;
        return 1;
    }
}

