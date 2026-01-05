import { useEffect, useState } from 'react';
import { useParams, Link } from 'react-router-dom';
import { Box, Clock, Hash, Database, Zap, FileText } from 'lucide-react';
import { formatDistanceToNow } from 'date-fns';
import Footer from './Footer';

const RPC_URL = "http://localhost:8545";

export default function BlockDetail() {
    const { number } = useParams();
    const [block, setBlock] = useState<any>(null);
    const [loading, setLoading] = useState(true);

    useEffect(() => {
        const fetchBlock = async () => {
            if (!number) return;
            try {
                const hexNum = "0x" + parseInt(number).toString(16);
                const res = await fetch(RPC_URL, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ jsonrpc: "2.0", method: "eth_getBlockByNumber", params: [hexNum, true], id: 1 })
                });
                const json = await res.json();
                if (json.result) {
                    setBlock(json.result);
                }
            } catch (e) {
                console.error(e);
            } finally {
                setLoading(false);
            }
        };
        fetchBlock();
    }, [number]);

    if (loading) return <div className="p-8 text-center text-gray-500">Loading block details...</div>;
    if (!block) return <div className="p-8 text-center text-red-500">Block not found</div>;

    const txCount = block.transactions ? block.transactions.length : 0;
    const timestamp = new Date(parseInt(block.timestamp, 16) * 1000);

    return (
        <div className="min-h-screen bg-gray-50 p-4 md:p-8 font-sans">
            <div className="max-w-4xl mx-auto">
                <div className="bg-white rounded-xl shadow-sm border border-gray-100 overflow-hidden mb-8">
                    <div className="p-6 border-b border-gray-100 bg-gray-50 flex items-center justify-between">
                        <h2 className="text-xl font-bold flex items-center text-gray-900">
                            <Box className="w-5 h-5 mr-3 text-gray-500" />
                            Block #{number}
                        </h2>
                    </div>

                    <div className="p-6 space-y-1">
                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 border-b border-gray-50 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium flex items-center"><Hash className="w-4 h-4 mr-2" /> Block Hash</div>
                            <div className="md:col-span-2 font-mono text-sm break-all text-gray-900">{block.hash}</div>
                        </div>

                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 border-b border-gray-50 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium flex items-center"><Clock className="w-4 h-4 mr-2" /> Timestamp</div>
                            <div className="md:col-span-2 text-gray-900">
                                {timestamp.toLocaleString()} <span className="text-gray-400 text-sm">({formatDistanceToNow(timestamp)} ago)</span>
                            </div>
                        </div>

                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 border-b border-gray-50 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium flex items-center"><FileText className="w-4 h-4 mr-2" /> Transactions</div>
                            <div className="md:col-span-2 text-blue-600 font-medium">{txCount} transactions</div>
                        </div>

                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 border-b border-gray-50 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium flex items-center"><Database className="w-4 h-4 mr-2" /> Parent Hash</div>
                            <div className="md:col-span-2 font-mono text-sm break-all text-blue-600">{block.parentHash}</div>
                        </div>

                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 border-b border-gray-50 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium flex items-center"><Zap className="w-4 h-4 mr-2" /> Gas Used</div>
                            <div className="md:col-span-2 text-gray-900 font-mono">
                                {parseInt(block.gasUsed, 16).toLocaleString()}
                                <span className="text-gray-400 mx-1">/</span>
                                {parseInt(block.gasLimit, 16).toLocaleString()}
                            </div>
                        </div>

                        <div className="grid grid-cols-1 md:grid-cols-3 gap-4 py-4 hover:bg-gray-50 transition-colors px-2">
                            <div className="text-gray-500 font-medium">Extra Data</div>
                            <div className="md:col-span-2 font-mono text-xs text-gray-400 break-all">{block.extraData || '0x'}</div>
                        </div>
                    </div>
                </div>

                {txCount > 0 && (
                    <div className="bg-white rounded-xl shadow-sm border border-gray-100 overflow-hidden">
                        <div className="p-6 border-b border-gray-100 bg-gray-50">
                            <h3 className="text-lg font-bold text-gray-900">Transactions</h3>
                        </div>
                        <div className="divide-y divide-gray-50">
                            {block.transactions.map((tx: any, i: number) => (
                                <div key={i} className="p-4 hover:bg-gray-50 flex items-center justify-between">
                                    <div className="font-mono text-sm text-blue-600 truncate w-1/3">
                                        <Link to={`/tx/${tx.hash}`} className="hover:underline">{tx.hash}</Link>
                                    </div>
                                    <div className="text-sm text-gray-600">
                                        From: <span className="font-mono text-blue-600">{tx.from.substring(0, 10)}...</span>
                                    </div>
                                    <div className="text-sm font-medium">
                                        {(parseInt(tx.value, 16) / 1e18).toFixed(4)} NATX
                                    </div>
                                </div>
                            ))}
                        </div>
                    </div>
                )}
            </div>
            <Footer isConnected={true} />
        </div>
    );
}
