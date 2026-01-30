import { Lucid, Blockfrost, Data } from "lucid-cardano";
import fs from "fs";

const CREDENTIALS_PATH = "./credentials.json";
const BLUEPRINT_PATH = "../plutus.json";

const StateCommitmentDatum = Data.Object({
    batch_id: Data.Integer(),
    start_block: Data.Integer(),
    end_block: Data.Integer(),
    pre_state_root: Data.Bytes(),
    post_state_root: Data.Bytes(),
    transactions_root: Data.Bytes(),
    timestamp: Data.Integer(),
    sequencer: Data.Bytes(),
});

const StateCommitmentRedeemer = Data.Enum([
    Data.Literal("PublishBatch"),
    Data.Literal("UpdateSequencer"),
]);

async function initLucid() {
    const credentials = JSON.parse(fs.readFileSync(CREDENTIALS_PATH, "utf-8"));
    const lucid = await Lucid.new(
        new Blockfrost("https://cardano-preprod.blockfrost.io/api/v0", "preprodYourAPIKey"),
        "Preprod"
    );
    lucid.selectWalletFromPrivateKey(credentials.privateKey);
    return { lucid, credentials };
}

async function getValidator() {
    const blueprint = JSON.parse(fs.readFileSync(BLUEPRINT_PATH, "utf-8"));
    const validatorEntry = blueprint.validators.find(v => v.title === "state_commitment.state_commitment.spend");
    return {
        type: "PlutusV3",
        script: validatorEntry.compiledCode,
    };
}

async function getLatestState() {
    const { lucid } = await initLucid();
    const validator = await getValidator();
    const scriptAddress = lucid.utils.validatorToAddress(validator);

    console.log(`Querying latest state at: ${scriptAddress}`);

    const utxos = await lucid.utxosAt(scriptAddress);
    if (utxos.length === 0) {
        console.log("No state commitments found.");
        return null;
    }

    // Find the UTXO with the highest batch_id
    let latestUtxo = null;
    let maxBatchId = -1n;

    for (const utxo of utxos) {
        if (!utxo.datum) continue;
        try {
            const datum = Data.from(utxo.datum, StateCommitmentDatum);
            if (datum.batch_id > maxBatchId) {
                maxBatchId = datum.batch_id;
                latestUtxo = {
                    utxo,
                    datum
                };
            }
        } catch (e) {
            console.warn("Found UTXO with incompatible datum format.");
        }
    }

    if (latestUtxo) {
        console.log(JSON.stringify(latestUtxo.datum, (key, value) =>
            typeof value === 'bigint' ? value.toString() : value, 2
        ));
    }
    return latestUtxo;
}

async function publishBatch(batchData) {
    const { lucid, credentials } = await initLucid();
    const validator = await getValidator();
    const scriptAddress = lucid.utils.validatorToAddress(validator);

    const currentState = await getLatestState();
    if (!currentState) {
        throw new Error("Cannot publish batch: No previous state found on-chain.");
    }

    const nextDatum = Data.to({
        batch_id: currentState.datum.batch_id + 1n,
        start_block: BigInt(batchData.start_block),
        end_block: BigInt(batchData.end_block),
        pre_state_root: currentState.datum.post_state_root,
        post_state_root: batchData.post_state_root,
        transactions_root: batchData.transactions_root,
        timestamp: BigInt(Math.floor(Date.now() / 1000)),
        sequencer: credentials.pubKeyHash,
    }, StateCommitmentDatum);

    const tx = await lucid
        .newTx()
        .collectFrom([currentState.utxo], Data.to("PublishBatch", StateCommitmentRedeemer))
        .attachSpendingValidator(validator)
        .payToContract(scriptAddress, { inline: nextDatum }, { lovelace: currentState.utxo.assets.lovelace })
        .addSignerKey(credentials.pubKeyHash)
        .complete();

    const signedTx = await tx.sign().complete();
    const txHash = await signedTx.submit();

    console.log(`Success! Batch submitted in Tx: ${txHash}`);
    return txHash;
}

const args = process.argv.slice(2);
const command = args[0];

if (command === "get-latest") {
    getLatestState().catch(console.error);
} else if (command === "publish") {
    const data = JSON.parse(args[1]); // Expecting JSON string of batch results
    publishBatch(data).catch(console.error);
} else {
    console.log("Usage: node sequencer_cli.js [get-latest | publish <json_data>]");
}
