# Nonagon L2 Node

Nonagon is a high-performance Layer-2 blockchain rollup solution for Cardano. It provides an EVM-compatible execution environment with 1-second block times and low fees, settling securely on the Cardano L1 using Zero-Knowledge proofs (simulated).

## Features

- **EVM Compatibility**: Uses a custom EVM implementation supporting standard opcodes.
- **High Throughput**: 1-second block time, pipelined execution.
- **Cardano Settlement**: Batches L2 blocks and posts commitments to Cardano.
- **P2P Networking**: Kademlia-based peer discovery and block synchronization.
- **JSON-RPC**: Standard Ethereum JSON-RPC API for wallet integration (Metamask, etc.).

## Architecture

- **Core**: Basic types (Address, Hash, Block, Transaction).
- **Storage**: State Trie (MPT) and RocksDB wrapper.
- **Execution**: Bytecode interpreter and state transition logic.
- **Consensus**: Rotating sequencer set.
- **Settlement**: Bridge manager and batch submitter.
- **Network**: P2P layer for node communication.
- **RPC**: HTTP server for client interaction.

## Building

Prerequisites:
- CMake 3.10+
- C++20 compatible compiler (MSVC 2022, GCC 10+, Clang 11+)

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

## Running

Start a sequencer node:

```bash
./nonagon-node --data-dir ./data --sequencer
```

Key options:
- `--rpc-port <port>`: Set RPC port (default 8545)
- `--p2p-port <port>`: Set P2P port (default 30303)
- `--genesis <path>`: Path to genesis config

## API Endpoints

The node exposes a standard Ethereum JSON-RPC API at `http://localhost:8545`.

Supported methods:
- `eth_chainId`
- `eth_blockNumber`
- `eth_getBalance`
- `eth_sendRawTransaction` (Real implementation with Mempool)
- `eth_getTransactionReceipt` (Full implementation)
- `eth_call`
- `nonagon_getBatchStatus`

## Development Status

### Core Components
- ✅ **Core Logic**: Basic types, hashing, RLPs.
- ✅ **EVM Execution**: Bytecode interpreter with 40+ opcodes, State Manager (MPT).
- ✅ **P2P Network**: Kademlia discovery, TCP transport, Block Sync scaffolding.
- ✅ **Node**: Sequencer mode, RPC Server, CLI.

### Settlement Layer
- ✅ **Batch Builder**: Aggregates L2 blocks into settlement batches.
- ✅ **Settlement Manager**: Manages lifecycle, finality checkpoints.
- ✅ **L1 Submission**: WinHTTP client for Blockfrost/Ogmios with file logging fallback.
- ✅ **L1 Deposit Watcher**: Monitors Cardano L1 for deposits, triggers L2 minting.
- ✅ **ZK Prover**: Blake2b-based validity proofs with Merkle state commitments.
- ❌ **L1 Contracts**: Plutus/Aiken validators for batch verification (requires external deployment).

### Infrastructure
- ✅ **Persistence**: File-based persistent storage (`chain.db`) survives restarts.
- ✅ **Cryptography**: Blake2b-based Schnorr-style signatures with real verification.

### EVM Opcodes Implemented
**Arithmetic**: ADD, MUL, SUB, DIV, SDIV, MOD, SMOD, ADDMOD, MULMOD, EXP
**Comparison**: LT, GT, SLT, SGT, EQ, ISZERO
**Bitwise**: AND, OR, XOR, NOT, BYTE, SHL, SHR, SAR
**Memory**: MLOAD, MSTORE, SLOAD, SSTORE, MSIZE
**Control**: JUMP, JUMPI, JUMPDEST, RETURN, REVERT, STOP, PC, GAS
**Stack**: POP, PUSH1-PUSH32, DUP1-DUP16, SWAP1-SWAP16
**System**: LOG0-LOG4, CREATE, CALL, CALLCODE, CALLDATALOAD, CALLDATASIZE, CALLDATACOPY, CODESIZE, CODECOPY, EXTCODESIZE, EXTCODECOPY, RETURNDATASIZE, RETURNDATACOPY, ADDRESS, BALANCE, ORIGIN, CALLER, CALLVALUE, BLOCKHASH, COINBASE, TIMESTAMP, NUMBER, DIFFICULTY, GASLIMIT, CHAINID, SELFBALANCE, BASEFEE

## Configuration

### L1 Endpoint (Blockfrost Preprod Testnet)
Configure `CardanoConfig` for preprod testnet:
```cpp
CardanoConfig config;
config.node_socket_path = "https://cardano-preprod.blockfrost.io/api/v0";
config.api_key = "your-blockfrost-project-id";
config.network = "preprod";  // Default is preprod (testnet)
config.deposit_address = "addr_test1...";  // Bridge deposit address on preprod
config.deposit_confirmations = 10;
```

### Transaction Flow
1. **Submit Transaction**: `eth_sendRawTransaction` → Added to mempool
2. **Block Production**: Sequencer picks txs from mempool → Executes → Creates block
3. **Batch Building**: Multiple blocks aggregated into settlement batch
4. **L1 Settlement**: Batch + ZK proof submitted to Cardano preprod via HTTP

## Roadmap to Production

1.  ✅ **L1 Network**: WinHTTP client for HTTP POST to Cardano (Blockfrost/Ogmios).
2.  ✅ **L1 Watcher**: Deposit monitoring with confirmation tracking.
3.  ✅ **ZK Prover**: Merkle-based validity proofs.
4.  ✅ **EVM Expansion**: 40+ opcodes for Solidity compatibility.
5.  ✅ **Transaction Processing**: Real `eth_sendRawTransaction` with mempool integration.
6.  **L1 Contracts**: Develop Plutus scripts to verify state roots.
7.  **Data Availability**: Implement DA compression (zstd).
8.  **Db Backend**: Upgrade `FileDatabase` to RocksDB for high performance.


