cmake_minimum_required(VERSION 3.20)
project(NonagonNode VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(NONAGON_BUILD_TESTS "Build unit tests" OFF)
option(NONAGON_USE_ROCKSDB "Use RocksDB instead of memory-only storage" OFF)

# Include directories
include_directories(include)

# Find optional dependencies
# find_package(Boost COMPONENTS system thread QUIET)
# find_package(RocksDB QUIET)

# ============================================================================
# Core Libraries
# ============================================================================

# Crypto library (Blake2b, Ed25519, Bech32)
add_library(nonagon_crypto
    src/crypto/crypto.cpp
)
target_include_directories(nonagon_crypto PUBLIC include)

# Core types library
add_library(nonagon_core
    src/core/types.cpp
)
target_include_directories(nonagon_core PUBLIC include)
target_link_libraries(nonagon_core PUBLIC nonagon_crypto)

# Storage library (Database, StateTrie, BlockStore, StateManager)
add_library(nonagon_storage
    src/storage/storage.cpp
)
target_include_directories(nonagon_storage PUBLIC include)
target_link_libraries(nonagon_storage PUBLIC nonagon_core nonagon_crypto)

# Consensus library (ConsensusEngine, Mempool)
add_library(nonagon_consensus
    src/consensus/consensus.cpp
)
target_include_directories(nonagon_consensus PUBLIC include)
target_link_libraries(nonagon_consensus PUBLIC nonagon_core nonagon_crypto)

# Node library (Main orchestrator)
add_library(nonagon_node_lib
    src/node/node.cpp
)
target_include_directories(nonagon_node_lib PUBLIC include)
target_link_libraries(nonagon_node_lib PUBLIC
    nonagon_core
    nonagon_crypto
    nonagon_storage
    nonagon_consensus
)

# Execution library (EVM, TransactionProcessor, BlockProcessor)
add_library(nonagon_execution
    src/execution/execution.cpp
)
target_include_directories(nonagon_execution PUBLIC include)
target_link_libraries(nonagon_execution PUBLIC nonagon_core nonagon_storage nonagon_crypto)

# RPC library (JSON-RPC server)
add_library(nonagon_rpc
    src/rpc/rpc.cpp
)
target_include_directories(nonagon_rpc PUBLIC include)
target_link_libraries(nonagon_rpc PUBLIC
    nonagon_core
    nonagon_storage
    nonagon_consensus
    nonagon_execution
)

# Settlement library (Cardano client, BatchBuilder)
add_library(nonagon_settlement
    src/settlement/settlement.cpp
)
target_include_directories(nonagon_settlement PUBLIC include)
target_link_libraries(nonagon_settlement PUBLIC nonagon_core nonagon_crypto nonagon_storage)

# Network library (P2P, Sync)
add_library(nonagon_network
    src/network/network.cpp
)
target_include_directories(nonagon_network PUBLIC include)
target_link_libraries(nonagon_network PUBLIC nonagon_core nonagon_crypto nonagon_storage)

# ============================================================================
# Main Executable
# ============================================================================

add_executable(nonagon-node
    src/main.cpp
)

target_link_libraries(nonagon-node
    nonagon_crypto
    nonagon_core
    nonagon_storage
    nonagon_consensus
    nonagon_execution
    nonagon_settlement
    nonagon_network
    nonagon_rpc
    nonagon_node_lib
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(nonagon-node PRIVATE _WIN32_WINNT=0x0601)
    target_link_libraries(nonagon-node ws2_32)
endif()

# ============================================================================
# Tests (optional)
# ============================================================================

if(NONAGON_BUILD_TESTS)
    enable_testing()
    
    # Crypto tests
    add_executable(test_crypto tests/test_crypto.cpp)
    target_link_libraries(test_crypto nonagon_crypto)
    add_test(NAME CryptoTests COMMAND test_crypto)
    
    # Storage tests
    add_executable(test_storage tests/test_storage.cpp)
    target_link_libraries(test_storage nonagon_storage)
    add_test(NAME StorageTests COMMAND test_storage)
    
    # Consensus tests
    add_executable(test_consensus tests/test_consensus.cpp)
    target_link_libraries(test_consensus nonagon_consensus)
    add_test(NAME ConsensusTests COMMAND test_consensus)
endif()

# ============================================================================
# Installation
# ============================================================================

install(TARGETS nonagon-node DESTINATION bin)
install(DIRECTORY include/nonagon DESTINATION include)

# ============================================================================
# Summary
# ============================================================================

message(STATUS "")
message(STATUS "Nonagon Node Build Configuration:")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Tests:    ${NONAGON_BUILD_TESTS}")
message(STATUS "  Use RocksDB:    ${NONAGON_USE_ROCKSDB}")
message(STATUS "")
