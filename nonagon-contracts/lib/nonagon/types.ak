use aiken/crypto.{VerificationKeyHash}

/// Represents the committed state of the L2 on L1.
/// Mirrors `StateCommitmentDatum` in C++ node.
pub type StateCommitmentDatum {
  batch_id: Int,
  start_block: Int,
  end_block: Int,
  pre_state_root: ByteArray,
  post_state_root: ByteArray,
  transactions_root: ByteArray,
  timestamp: Int,
  sequencer: VerificationKeyHash,
}

pub type StateCommitmentRedeemer {
  PublishBatch
  UpdateSequencer
}
