import { useEffect, useState } from 'react';
import { Link, useSearchParams } from 'react-router-dom';
import { ChevronLeft, ChevronRight, Box } from 'lucide-react';
import { formatDistanceToNow } from 'date-fns';
import Footer from './Footer';

const RPC_URL = "http://localhost:8545";
const PAGE_SIZE = 20;

export default function Blocks() {
    const [searchParams, setSearchParams] = useSearchParams();
    const page = parseInt(searchParams.get('page') || '1');
    const [blocks, setBlocks] = useState<any[]>([]);
    const [loading, setLoading] = useState(true);

    useEffect(() => {
        const fetchBlocks = async () => {
            setLoading(true);
            try {
                // Get Head
                const hRes = await fetch(RPC_URL, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ jsonrpc: "2.0", method: "eth_blockNumber", params: [], id: 1 })
                });
                const hJson = await hRes.json();
                const hNum = parseInt(hJson.result, 16);

                const start = hNum - ((page - 1) * PAGE_SIZE);
                const end = start - PAGE_SIZE; // exclusive loop

                const promises = [];
                for (let i = start; i > end && i >= 0; i--) {
                    promises.push(
                        fetch(RPC_URL, {
                            method: 'POST',
                            headers: { 'Content-Type': 'application/json' },
                            body: JSON.stringify({ jsonrpc: "2.0", method: "eth_getBlockByNumber", params: ["0x" + i.toString(16), true], id: i })
                        }).then(r => r.json())
                    );
                }
                const results = await Promise.all(promises);
                const fetched = results.map(r => r.result).filter(b => b);
                setBlocks(fetched);
            } catch (e) { console.error(e); }
            finally { setLoading(false); }
        };
        fetchBlocks();
    }, [page]);

    return (
        <div className="min-h-screen bg-gray-50 p-4 md:p-8 font-sans">
            <div className="max-w-6xl mx-auto">
                <div className="flex justify-between items-center mb-6">
                    <h1 className="text-2xl font-bold text-gray-900 flex items-center gap-2">
                        <Box className="w-6 h-6 text-blue-600" /> Blocks
                    </h1>
                </div>

                <div className="bg-white rounded-xl shadow-sm border border-gray-100 overflow-hidden">
                    <div className="overflow-x-auto">
                        <table className="w-full text-left text-sm">
                            <thead className="bg-gray-50 border-b border-gray-100">
                                <tr>
                                    <th className="p-4 font-semibold text-gray-600">Block</th>
                                    <th className="p-4 font-semibold text-gray-600">Age</th>
                                    <th className="p-4 font-semibold text-gray-600">Txn</th>
                                    <th className="p-4 font-semibold text-gray-600">Gas Used</th>
                                    <th className="p-4 font-semibold text-gray-600">Hash</th>
                                </tr>
                            </thead>
                            <tbody className="divide-y divide-gray-50">
                                {blocks.map(b => (
                                    <tr key={b.number} className="hover:bg-gray-50 transition-colors">
                                        <td className="p-4 font-mono font-medium text-blue-600">
                                            <Link to={`/block/${parseInt(b.number, 16)}`} className="hover:underline">
                                                {parseInt(b.number, 16)}
                                            </Link>
                                        </td>
                                        <td className="p-4 text-gray-500">
                                            {formatDistanceToNow(new Date(parseInt(b.timestamp, 16) * 1000))} ago
                                        </td>
                                        <td className="p-4 text-gray-900 font-medium">{b.transactions.length}</td>
                                        <td className="p-4 text-gray-500 font-mono text-xs">
                                            {parseInt(b.gasUsed, 16).toLocaleString()}
                                        </td>
                                        <td className="p-4 text-gray-400 font-mono text-xs truncate max-w-[150px]">
                                            {b.hash}
                                        </td>
                                    </tr>
                                ))}
                                {blocks.length === 0 && !loading && (
                                    <tr><td colSpan={5} className="p-8 text-center text-gray-400">No blocks found</td></tr>
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
                    <span className="font-medium text-gray-600">Page {page}</span>
                    <button
                        disabled={blocks.length < PAGE_SIZE}
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
