use aiken/collection/list
use aiken/crypto.{VerificationKeyHash}
use cardano/transaction.{
  InlineDatum, Input, Output, OutputReference, Transaction,
}
use nonagon/types.{PublishBatch, StateCommitmentDatum, StateCommitmentRedeemer}

pub const authorized_sequencer: VerificationKeyHash =
  #"a41af35019f8be5c24f2e3abdd9c677de21cb383c67f170f5faabeb1"


validator state_commitment {
  spend(
    datum: Option<StateCommitmentDatum>,
    redeemer: StateCommitmentRedeemer,
    _ref: OutputReference,
    transaction: Transaction,
  ) {
    expect Some(current_datum) = datum
    when redeemer is {
      PublishBatch -> {
        let must_be_signed =
          list.has(transaction.extra_signatories, current_datum.sequencer)
        expect Some(own_input) =
          list.find(
            transaction.inputs,
            fn(_input) {
              True
            },
          )
        // Find the single output that goes back to this script
        expect Some(next_output) =
          list.find(
            transaction.outputs,
            fn(output) { output.address == own_input.output.address },
          )

        // Parse the next datum
        expect InlineDatum(next_datum_data) = next_output.datum
        expect next_datum: StateCommitmentDatum = next_datum_data

        // Validate State Transitions
        let valid_batch_sequence =
          next_datum.batch_id == current_datum.batch_id + 1
        let valid_state_link =
          next_datum.pre_state_root == current_datum.post_state_root
        // Enforce Sequencer Continuity (unless rotation logic)
        let sequencer_unchanged =
          next_datum.sequencer == current_datum.sequencer

        and {
          must_be_signed?,
          valid_batch_sequence?,
          valid_state_link?,
          sequencer_unchanged?,
        }
      }
      _ -> False
    }
    // UpdateSequencer not implemented yet
  }

  else(_) {
    fail
  }
}
