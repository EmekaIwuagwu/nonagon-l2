import { useEffect, useState } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { ChevronLeft, ChevronRight, Zap } from 'lucide-react';
import { formatDistanceToNow } from 'date-fns';
import Footer from './Footer';

const RPC_URL = "http://localhost:8545";
const SCAN_DEPTH = 100; // Scan last 100 blocks
const PAGE_SIZE = 20;

export default function Transactions() {
    const [searchParams, setSearchParams] = useSearchParams();
    const page = parseInt(searchParams.get('page') || '1');
    const [txs, setTxs] = useState<any[]>([]);
    const [loading, setLoading] = useState(true);

    useEffect(() => {
        const fetchTxs = async () => {
            setLoading(true);
            try {
                const res = await fetch(RPC_URL, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        jsonrpc: "2.0",
                        method: "nonagon_getRecentTransactions",
                        params: [100],
                        id: 1
                    })
                });
                const json = await res.json();
                if (json.result) {
                    const formattedTxs = json.result.map((tx: any) => ({
                        ...tx,
                        timestamp: parseInt(tx.timestamp, 16)
                    }));
                    setTxs(formattedTxs);
                }
            } catch (e) { console.error(e); }
            finally { setLoading(false); }
        };
        fetchTxs();
    }, []);

    const totalPages = Math.max(1, Math.ceil(txs.length / PAGE_SIZE));
    const displayedTxs = txs.slice((page - 1) * PAGE_SIZE, page * PAGE_SIZE);

    return (
        <div className="min-h-screen bg-gray-50 p-4 md:p-8 font-sans">
            <div className="max-w-6xl mx-auto">
                <div className="flex justify-between items-center mb-6">
                    <h1 className="text-2xl font-bold text-gray-900 flex items-center gap-2">
                        <Zap className="w-6 h-6 text-yellow-500" /> Transactions
                    </h1>
                </div>

                <div className="bg-white rounded-xl shadow-sm border border-gray-100 overflow-hidden">
                    <div className="overflow-x-auto">
                        <table className="w-full text-left text-sm">
                            <thead className="bg-gray-50 border-b border-gray-100">
                                <tr>
                                    <th className="p-4 font-semibold text-gray-600">Tx Hash</th>
                                    <th className="p-4 font-semibold text-gray-600">Block</th>
                                    <th className="p-4 font-semibold text-gray-600">Age</th>
                                    <th className="p-4 font-semibold text-gray-600">From</th>
                                    <th className="p-4 font-semibold text-gray-600">To</th>
                                    <th className="p-4 font-semibold text-gray-600">Value</th>
                                </tr>
                            </thead>
                            <tbody className="divide-y divide-gray-50">
                                {displayedTxs.map(tx => (
                                    <tr key={tx.hash} className="hover:bg-gray-50 transition-colors">
                                        <td className="p-4 font-mono font-medium text-blue-600 truncate max-w-[150px]">
                                            <Link to={`/tx/${tx.hash}`} className="hover:underline">{tx.hash}</Link>
                                        </td>
                                        <td className="p-4 text-blue-600 font-mono">
                                            <Link to={`/block/${parseInt(tx.blockNumber, 16)}`} className="hover:underline">
                                                {parseInt(tx.blockNumber, 16)}
                                            </Link>
                                        </td>
                                        <td className="p-4 text-gray-500">
                                            {formatDistanceToNow(new Date(tx.timestamp * 1000))} ago
                                        </td>
                                        <td className="p-4 font-mono text-gray-500 truncate max-w-[100px]">{tx.from}</td>
                                        <td className="p-4 font-mono text-gray-500 truncate max-w-[100px]">{tx.to}</td>
                                        <td className="p-4 font-medium text-gray-900">
                                            {(parseInt(tx.value, 16) / 1e18).toFixed(4)} NATX
                                        </td>
                                    </tr>
                                ))}
                                {displayedTxs.length === 0 && !loading && (
                                    <tr><td colSpan={6} className="p-8 text-center text-gray-400">No transactions found in last {SCAN_DEPTH} blocks</td></tr>
                                )}
                            </tbody>
                        </table>
                    </div>
                </div>

                <div className="flex justify-center mt-8 gap-4 items-center">
                    <button
                        disabled={page === 1}
                        onClick={() => setSearchParams({ page: (page - 1).toString() })}
                        className="p-2 border border-gray-200 rounded-lg hover:bg-white bg-gray-50 disabled:opacity-50 transition-colors"
                    >
                        <ChevronLeft size={20} />
                    </button>
                    <span className="font-medium text-gray-600">Page {page} of {totalPages}</span>
                    <button
                        disabled={page >= totalPages}
                        onClick={() => setSearchParams({ page: (page + 1).toString() })}
                        className="p-2 border border-gray-200 rounded-lg hover:bg-white bg-gray-50 disabled:opacity-50 transition-colors"
                    >
                        <ChevronRight size={20} />
                    </button>
                </div>
            </div>
            <Footer isConnected={true} />
        </div>
    );
}
