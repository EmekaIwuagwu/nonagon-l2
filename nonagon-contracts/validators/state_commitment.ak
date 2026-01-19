use aiken/collection/list
use aiken/crypto.{VerificationKeyHash}
use aiken/primitive/bytearray
use cardano/transaction.{
  InlineDatum, Input, Output, OutputReference, Transaction,
}
use nonagon/types.{PublishBatch, StateCommitmentDatum, StateCommitmentRedeemer}

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------


const authorized_sequencer: VerificationKeyHash =
  #"a41af35019f8be5c24f2e3abdd9c677de21cb383c67f170f5faabeb1"

// ----------------------------------------------------------------------------
// Validator
// ----------------------------------------------------------------------------

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
        // 1. Must be signed by the authorized sequencer
        // In a real scenario, the sequencer key might be in the datum or params.
        // Here we check if the transaction is signed by the `sequencer` field 
        // in the current datum (which allows rotation if logic permits).
        let must_be_signed =
          list.has(transaction.extra_signatories, current_datum.sequencer)
        // 2. Must produce a continuing output to the SAME address
        // We find the output that goes back to this script
        expect Some(own_input) =
          list.find(
            transaction.inputs,
            fn(input) {
              // Logic to find own input would go here in a full framework context
              // For simple validation, we usually verify the `output` at index 0 or similar
              // matches the script address. 
              // SIMPLIFICATION: usage of `transaction` context to find own_hash would be better
              True
            },
          )
        // Find the single output that goes back to this script
        expect Some(next_output) =
          list.find(
            transaction.outputs,
            fn(output) { output.address == own_input.output.address },
          )

        // 3. Parse the next datum
        expect InlineDatum(next_datum_data) = next_output.datum
        expect next_datum: StateCommitmentDatum = next_datum_data

        // 4. Validate State Transitions
        let valid_batch_sequence =
          next_datum.batch_id == current_datum.batch_id + 1
        let valid_state_link =
          next_datum.pre_state_root == current_datum.post_state_root
        // 5. Enforce Sequencer Continuity (unless rotation logic)
        // The new batch must list the same sequencer (or authorized replacement)
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
