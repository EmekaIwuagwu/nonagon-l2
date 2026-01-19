# Nonagon L2 Smart Contracts (Aiken)

This directory contains the Aiken smart contracts for the **Nonagon Optimistic Rollup** on Cardano. These contracts serve as the L1 settlement and verification layer.

## 📁 Structure

- `lib/nonagon/`: Shared types (Datum, Redeemer) mirroring C++ node structures.
- `validators/`: Main contract logic.
  - `state_commitment.ak`: Anchors L2 state roots to L1.
- `scripts/`: Utility scripts for credential management.

---

## 🏛️ State Commitment Contract (`state_commitment.ak`)

This is the primary contract that establishes the "Source of Truth" for the L2 state on the Cardano mainnet.

### 1. Purpose

The contract maintains an immutable, cryptographically-linked sequence of L2 state roots. By posting these roots to Cardano, the L2 inherits the security and finality of the L1.

### 2. Key Logic

The validator enforces strict rules for every state update (Batch Submission):

- **Authorization**: Every transaction must be signed by the authorized **Sequencer** (verified via `VerificationKeyHash`).
- **Continuity**:
  - The `batch_id` must increment exactly by 1 (no skipped or duplicate batches).
  - The `pre_state_root` of the new batch must exactly match the `post_state_root` of the previous batch. This creates a "hash chain" of states.
- **Immutability**: The script ensures that the state is updated by spending the previous UTxO and creating a new one at the same script address (State Folding).

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

Use the provided Node.js scripts to manage sequencer keys:

1. Navigate to `scripts/`.
2. Install dependencies: `npm install`.
3. Generate keys: `node gen_key.js`.
4. Credentials will be saved to `scripts/credentials.json`.

> [!CAUTION]
> Never share your `privateKey`. If lost, the State Commitment contract will be locked unless a governance rotation mechanism is implemented.

## 🔗 Integration with `nonagon-node`

The C++ node uses the compiled `plutus.json` (specifically the `state_commitment` script hash) to:

1. Discover the latest state root on Cardano.
2. Construct and submit `PublishBatch` transactions to L1.
