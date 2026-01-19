use aiken/crypto.{Blake2b_224, Hash, VerificationKey}

// Re-export specific common types for convenience
pub type VerificationKeyHash =
  Hash<Blake2b_224, VerificationKey>

// ----------------------------------------------------------------------------
// State Commitment Types
// ----------------------------------------------------------------------------

/// Represents the committed state of the L2 on L1.
/// Mirrors `StateCommitmentDatum` in C++ node.
pub type StateCommitmentDatum {
  batch_id: Int,
  // Slot/Block range this batch covers
  start_block: Int,
  end_block: Int,
  // State Roots
  pre_state_root: ByteArray,
  post_state_root: ByteArray,
  // Merkle root of all transactions in this batch (for data availability checks)
  transactions_root: ByteArray,
  // Timestamp of the batch creation
  timestamp: Int,
  // The sequencer who produced this batch
  sequencer: VerificationKeyHash,
}

/// Actions that can be performed on the State Commitment Contract
pub type StateCommitmentRedeemer {
  /// Publish a new batch of L2 blocks.
  /// This updates the datum with new state roots.
  PublishBatch
  /// Update the sequencer (Governance action - placeholder for now)
  UpdateSequencer
}
