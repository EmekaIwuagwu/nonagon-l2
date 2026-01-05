import { useState, useEffect } from 'react';
import { useParams } from 'react-router-dom';
import { Clock, FileText, Hash, DollarSign, Activity } from 'lucide-react';
import Footer from './Footer';

const RPC_URL = "http://localhost:8545";

export default function TxDetail() {
    const { hash } = useParams();
    const [tx, setTx] = useState<any>(null);
    const [loading, setLoading] = useState(true);

    useEffect(() => {
        const fetchTx = async () => {
            if (!hash) return;
            try {
                // Fetch Tx
                const res = await fetch(RPC_URL, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ jsonrpc: "2.0", method: "eth_getTransactionByHash", params: [hash], id: 1 })
                });
                const json = await res.json();
                if (json.result) {
                    const t = json.result;
                    // Also fetch receipt for status/gasUsed
                    const rRes = await fetch(RPC_URL, {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({ jsonrpc: "2.0", method: "eth_getTransactionReceipt", params: [hash], id: 2 })
                    });
                    const rJson = await rRes.json();

                    setTx({ ...t, receipt: rJson.result });
                }
            } catch (e) {
                console.error(e);
            } finally {
                setLoading(false);
            }
        };
        fetchTx();
    }, [hash]);

    if (loading) return <div className="p-8 text-center text-gray-500">Loading transaction details...</div>;
    if (!tx) return <div className="p-8 text-center text-red-500">Transaction not found</div>;

    return (
        <div className="min-h-screen bg-gray-50 p-4 md:p-8 font-sans">
            <div className="max-w-4xl mx-auto">
                <div className="bg-white rounded-xl shadow-sm border border-gray-100 overflow-hidden">
                    <div className="p-6 border-b border-gray-100 bg-gray-50 flex items-center justify-between">
                        <h2 className="text-xl font-bold flex items-center text-gray-900">
                            <FileText className="w-5 h-5 mr-3 text-gray-500" />
                            Transaction Details
                        </h2>
                        {tx.receipt && tx.receipt.status === '0x1' ? (
                            <span className="bg-green-100 text-green-700 px-4 py-1.5 rounded-full text-sm font-bold shadow-sm">Success</span>
                        ) : (
                            <span className="bg-red-100 text-red-700 px-4 py-1.5 rounded-full text-sm font-bold shadow-sm">Failed</span>
                        )}
                    </div>

                    <div className="p-6 space-y-1">
                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 border-b border-gray-50 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium flex items-center"><Hash className="w-4 h-4 mr-2" /> Transaction Hash</div>
                            <div className="md:col-span-2 font-mono text-sm break-all text-gray-900">{hash}</div>
                        </div>

                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 border-b border-gray-50 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium flex items-center"><Clock className="w-4 h-4 mr-2" /> Block</div>
                            <div className="md:col-span-2 text-blue-600 font-mono">{parseInt(tx.blockNumber, 16)}</div>
                        </div>

                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 border-b border-gray-50 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium">From</div>
                            <div className="md:col-span-2 font-mono text-sm text-blue-600 break-all">{tx.from}</div>
                        </div>

                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 border-b border-gray-50 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium">To</div>
                            <div className="md:col-span-2 font-mono text-sm text-blue-600 break-all">{tx.to}</div>
                        </div>

                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 border-b border-gray-50 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium flex items-center"><DollarSign className="w-4 h-4 mr-2" /> Value</div>
                            <div className="md:col-span-2 font-medium text-lg text-gray-900">{(parseInt(tx.value, 16) / 1e18).toFixed(6)} NATX</div>
                        </div>

                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 border-b border-gray-50 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium flex items-center"><Activity className="w-4 h-4 mr-2" /> Gas Used</div>
                            <div className="md:col-span-2 text-gray-700 font-mono">{tx.receipt ? parseInt(tx.receipt.gasUsed, 16).toLocaleString() : '-'}</div>
                        </div>

                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium">Nonce</div>
                            <div className="md:col-span-2 text-gray-700 font-mono">{parseInt(tx.nonce, 16)}</div>
                        </div>
                    </div>
                </div>
            </div>
            <Footer isConnected={true} />
        </div>
    );
}
