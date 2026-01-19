import { Lucid } from "lucid-cardano";

async function main() {
    try {
        const lucid = await Lucid.new(undefined, "Preprod");

        const privateKey = lucid.utils.generatePrivateKey();
        const address = await lucid.selectWalletFromPrivateKey(privateKey).wallet.address();
        const pubKeyHash = lucid.utils.getAddressDetails(address).paymentCredential.hash;

        const fs = await import('fs');
        const credentials = {
            privateKey,
            address,
            pubKeyHash
        };
        fs.writeFileSync('credentials.json', JSON.stringify(credentials, null, 2));

        console.log("Written to credentials.json");
    } catch (e) {
        console.error("Error generating keys:", e);
        const fs = await import('fs');
        fs.writeFileSync('error.log', e.toString());
    }
}

main();
