use aiken/collection/list
use cardano/transaction.{
  InlineDatum, Input, Output, OutputReference, Transaction,
}
use nonagon/types.{
  PublishBatch, StateCommitmentDatum, StateCommitmentRedeemer, UpdateSequencer,
}

validator state_commitment {
  spend(
    datum: Option<StateCommitmentDatum>,
    redeemer: StateCommitmentRedeemer,
    ref: OutputReference,
    transaction: Transaction,
  ) {
    expect Some(current_datum) = datum
    when redeemer is {
      PublishBatch -> {
        let must_be_signed =
          list.has(transaction.extra_signatories, current_datum.sequencer)

        expect Some(own_input) =
          list.find(transaction.inputs, fn(input) { input.output_reference == ref })

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
        let valid_batch_sequence = next_datum.batch_id == current_datum.batch_id + 1
        let valid_state_link =
          next_datum.pre_state_root == current_datum.post_state_root

        // Enforce Sequencer Continuity
        let sequencer_unchanged = next_datum.sequencer == current_datum.sequencer

        and {
          must_be_signed?,
          valid_batch_sequence?,
          valid_state_link?,
          sequencer_unchanged?,
        }
      }
      UpdateSequencer -> {
        let must_be_signed =
          list.has(transaction.extra_signatories, current_datum.sequencer)

        expect Some(own_input) =
          list.find(transaction.inputs, fn(input) { input.output_reference == ref })

        expect Some(next_output) =
          list.find(
            transaction.outputs,
            fn(output) { output.address == own_input.output.address },
          )

        expect InlineDatum(next_datum_data) = next_output.datum
        expect next_datum: StateCommitmentDatum = next_datum_data

        // Ensure ONLY the sequencer is changing, and other state is preserved
        let state_preserved =
          and {
            next_datum.batch_id == current_datum.batch_id,
            next_datum.pre_state_root == current_datum.pre_state_root,
            next_datum.post_state_root == current_datum.post_state_root,
            next_datum.start_block == current_datum.start_block,
            next_datum.end_block == current_datum.end_block,
            next_datum.transactions_root == current_datum.transactions_root,
          }

        and {
          must_be_signed?,
          state_preserved?,
        }
      }
    }
  }

  else(_) {
    fail
  }
}
