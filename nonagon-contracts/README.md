# Nonagon L2 Smart Contracts (Aiken)

This directory contains the Aiken smart contracts for the **Nonagon Optimistic Rollup** on Cardano. These contracts serve as the L1 settlement and verification layer.

## 📁 Structure

- `lib/nonagon/`: Shared types (Datum, Redeemer) mirroring C++ node structures.
- `validators/`: Main contract logic.
  - `state_commitment.ak`: Anchors L2 state roots to L1 with dynamic sequencer support.
- `scripts/`: Utility scripts for credential management and automation.
  - `gen_key.js`: Generates new sequencer credentials.
  - `sequencer_cli.js`: Integration bridge for the C++ node.

---

## 🏛️ State Commitment Contract (`state_commitment.ak`)

This is the primary contract that establishes the "Source of Truth" for the L2 state on the Cardano mainnet.

### 1. Purpose

The contract maintains an immutable, cryptographically-linked sequence of L2 state roots. By posting these roots to Cardano, the L2 inherits the security and finality of the L1.

### 2. Key Logic

The validator enforces strict rules for every state update:

- **Dynamic Authorization**: Instead of hardcoded keys, the contract reads the authorized **Sequencer** directly from the current on-chain state (Datum).
- **Update Path**: Supports `UpdateSequencer` redeemer, allowing the current sequencer to safely rotate control to a new set of keys while preserving all L2 state data.
- **Continuity**:
  - The `batch_id` must increment exactly by 1.
  - The `pre_state_root` of the new batch must exactly match the `post_state_root` of the previous batch.
- **State Folding**: The script ensures that the state is updated by spending the previous UTxO and creating a new one at the same script address.

### 3. Data Structures

#### `StateCommitmentDatum`

| Field | Type | Description |
| :--- | :--- | :--- |
| `batch_id` | `Int` | Sequential identifier for the batch. |
| `start_block` | `Int` | Starting L2 block height in this batch. |
| `end_block` | `Int` | Ending L2 block height in this batch. |
| `pre_state_root` | `ByteArray` | The state root before applying this batch. |
| `post_state_root` | `ByteArray` | The state root after applying this batch. |
| `transactions_root` | `ByteArray` | Merkle root of all L2 transactions (Data Availability). |
| `timestamp` | `Int` | Unix timestamp of batch creation. |
| `sequencer` | `Hash` | The public key hash of the authorized sequencer. |

---

## ⚙️ Automation & Integration

The C++ **nonagon-node** integrates with these contracts via the `sequencer_cli.js` bridge.

### 1. Discovery

The node uses `node sequencer_cli.js get-latest` to:

- Dynamically find the L1 script address from `plutus.json`.
- Identify the most recent batch and state root confirmed on Cardano.

### 2. Submission

The node uses `node sequencer_cli.js publish '<batch_json>'` to:

- Construct a valid Plutus transaction.
- Attach the required signature from `credentials.json`.
- Submit the new state commitment to the L1 network.

---

## 🛠️ Build & Development

Requires [Aiken](https://aiken-lang.org/installation-instructions) v1.1.19+.

```bash
# Formatter
aiken fmt

# Build (compiles to plutus.json)
aiken build

# Check (Unit tests)
aiken check
```

## 🔑 Credential Management

1. Navigate to `scripts/`.
2. Install dependencies: `npm install`.
3. Generate initial keys: `node gen_key.js`.
4. Credentials will be saved to `scripts/credentials.json`.

> [!TIP]
> Use the rotation feature (`UpdateSequencer`) to change keys without re-deploying the contract. This is critical for long-term security and key management.
