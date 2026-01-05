import React, { useState, useEffect } from 'react';
import { Activity, Box, Database, Zap, ArrowRight, Search, Clock, Cpu, AlertTriangle } from 'lucide-react';
import { formatDistanceToNow } from 'date-fns';

const RPC_URL = "http://localhost:8545";

const SummaryCard = ({ title, value, icon: Icon, change }: { title: string, value: string, icon: any, change?: string }) => (
    <div className="bg-white p-6 rounded-2xl border border-gray-100 shadow-sm hover:shadow-md transition-shadow">
        <div className="flex items-center justify-between mb-4">
            <div className="p-3 bg-blue-50 rounded-xl text-blue-600">
                <Icon size={24} />
            </div>
            {change && <span className="text-sm font-medium text-green-600">{change}</span>}
        </div>
        <h3 className="text-sm font-medium text-gray-500 uppercase tracking-wider">{title}</h3>
        <p className="text-2xl font-bold text-gray-900 mt-1">{value}</p>
    </div>
);

const TransactionRow = ({ tx }: { tx: any }) => (
    <div className="flex items-center justify-between p-4 hover:bg-gray-50 transition-colors border-b border-gray-50 last:border-0">
        <div className="flex items-center gap-4">
            <div className="p-2 bg-gray-100 rounded-lg text-gray-600">
                <Zap size={18} />
            </div>
            <div>
                <div className="font-mono text-sm text-blue-600 font-medium truncate w-32 md:w-64">
                    {tx.hash}
                </div>
                <div className="text-xs text-gray-500 mt-1">
                    {tx.timestamp ? formatDistanceToNow(tx.timestamp) : 'Just now'} ago
                </div>
            </div>
        </div>
        <div className="text-right">
            <div className="text-sm font-semibold text-gray-900">
                {tx.value || "0.0"} NATX
            </div>
            <div className="text-xs text-gray-400 mt-1 uppercase tracking-tighter">
                L2 Confirmed
            </div>
        </div>
    </div>
);

const BlockRow = ({ block }: { block: any }) => (
    <div className="flex items-center justify-between p-4 hover:bg-gray-50 transition-colors border-b border-gray-50 last:border-0">
        <div className="flex items-center gap-4">
            <div className="p-2 bg-black rounded-lg text-white">
                <Box size={18} />
            </div>
            <div>
                <div className="font-bold text-gray-900">
                    #{parseInt(block.number, 16)}
                </div>
                <div className="text-xs text-gray-500 mt-1">
                    {block.transactions ? block.transactions.length : 0} Transactions
                </div>
            </div>
        </div>
        <div className="text-right">
            <div className="text-xs font-mono text-gray-400">
                Hash: {block.hash ? block.hash.substring(0, 10) : '...'}...
            </div>
            <div className="text-xs text-gray-950 mt-1 font-medium">
                {block.miner ? block.miner.substring(0, 16) : 'Unknown'}
            </div>
        </div>
    </div>
);

export default function Dashboard() {
    const [blockHeight, setBlockHeight] = useState<number>(0);
    const [blocks, setBlocks] = useState<any[]>([]);
    const [isConnected, setIsConnected] = useState(false);

    // Default mock stats
    const stats = [
        { title: "Net TPS", value: "1,248", icon: Zap, change: "+12.4%" },
        { title: "L2 Blocks", value: blockHeight.toLocaleString(), icon: Box },
        { title: "NATX Price", value: "$4.12", icon: Activity, change: "+2.1%" },
        { title: "Batcher TVL", value: "$128.4M", icon: Database },
    ];

    // Mock txs for now as fetching all takes time
    const [recentTxs, setRecentTxs] = useState<any[]>([]);

    useEffect(() => {
        const fetchData = async () => {
            try {
                // Fetch Block Number
                const bnRes = await fetch(RPC_URL, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ jsonrpc: "2.0", method: "eth_blockNumber", params: [], id: 1 })
                });
                const bnJson = await bnRes.json();
                const height = parseInt(bnJson.result, 16);
                setBlockHeight(height);
                setIsConnected(true);

                // Fetch Latest Block
                const blkRes = await fetch(RPC_URL, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ jsonrpc: "2.0", method: "eth_getBlockByNumber", params: ["latest", true], id: 2 })
                });
                const blkJson = await blkRes.json();
                if (blkJson.result) {
                    setBlocks([blkJson.result]);

                    // Process transactions from the block
                    if (blkJson.result.transactions && blkJson.result.transactions.length > 0) {
                        const newTxs = blkJson.result.transactions.map((tx: any) => ({
                            hash: tx.hash,
                            value: (parseInt(tx.value, 16) / 1e18).toLocaleString('en-US', { maximumFractionDigits: 4 }),
                            timestamp: new Date(parseInt(blkJson.result.timestamp, 16) * 1000)
                        }));
                        setRecentTxs(newTxs.slice(0, 10)); // Show up to 10 latest
                    } else {
                        // If no txs in latest block, keep previous or clear?
                        // For a live ticker, we might want to accumulate, but let's just show latest block's txs for clarity
                        // Or keep existing if new block is empty? 
                        // Let's keep existing to avoid flickering to empty
                    }
                }

            } catch (e) {
                console.error("Explorer sync error:", e);
                setIsConnected(false);
            }
        };

        fetchData();
        const interval = setInterval(fetchData, 3000);
        return () => clearInterval(interval);
    }, []);

    return (
        <div className="min-h-screen bg-gray-50 text-gray-900 font-sans p-4 md:p-8">
            {/* Header */}
            <div className="max-w-7xl mx-auto flex flex-col md:flex-row justify-between items-center mb-12 gap-6">
                <div>
                    <h1 className="text-4xl font-extrabold tracking-tight text-black flex items-center gap-3">
                        <div className="w-12 h-12 bg-black rounded-lg flex items-center justify-center text-white rotate-12">N</div>
                        NONAGON<span className="text-blue-600">SCAN</span>
                    </h1>
                    <p className="text-gray-500 mt-2 font-medium">The Ultra-High Performance Cardano L2 Explorer</p>
                </div>

                <div className="relative w-full md:w-96 group">
                    <Search className="absolute left-4 top-1/2 -translate-y-1/2 text-gray-400 group-focus-within:text-blue-600 transition-colors" size={20} />
                    <input
                        type="text"
                        placeholder="Search Tx, Block, Address..."
                        className="w-full bg-white border border-gray-200 py-4 pl-12 pr-4 rounded-2xl shadow-sm focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500 transition-all font-medium"
                    />
                </div>
            </div>

            {!isConnected && (
                <div className="max-w-7xl mx-auto mb-8 bg-yellow-50 border border-yellow-200 text-yellow-800 px-6 py-4 rounded-2xl flex items-center gap-3">
                    <AlertTriangle size={24} />
                    <div>
                        <span className="font-bold">Connecting to Node...</span>
                        <p className="text-sm mt-1">Ensure nonagon-node is running on port 8545.</p>
                    </div>
                </div>
            )}

            {/* Stats Grid */}
            <div className="max-w-7xl mx-auto grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-6 mb-12">
                {stats.map((s, i) => <SummaryCard key={i} {...s} />)}
            </div>

            {/* Main Content */}
            <div className="max-w-7xl mx-auto grid grid-cols-1 lg:grid-cols-2 gap-8">
                {/* Latest Blocks */}
                <div className="bg-white rounded-3xl border border-gray-100 shadow-sm overflow-hidden">
                    <div className="p-6 border-b border-gray-50 flex justify-between items-center">
                        <h2 className="text-lg font-bold flex items-center gap-2">
                            <Box size={20} className="text-blue-600" />
                            Latest Blocks
                        </h2>
                        <button className="text-sm font-semibold text-blue-600 hover:text-blue-700 flex items-center gap-1 transition-colors">
                            View All <ArrowRight size={14} />
                        </button>
                    </div>
                    <div className="divide-y divide-gray-50">
                        {blocks.length > 0 ? (
                            blocks.map((b, i) => <BlockRow key={i} block={b} />)
                        ) : (
                            <div className="p-8 text-center text-gray-400">Waiting for blocks...</div>
                        )}
                    </div>
                </div>

                {/* Latest Transactions */}
                <div className="bg-white rounded-3xl border border-gray-100 shadow-sm overflow-hidden">
                    <div className="p-6 border-b border-gray-50 flex justify-between items-center">
                        <h2 className="text-lg font-bold flex items-center gap-2">
                            <Zap size={20} className="text-yellow-500" />
                            Latest Transactions
                        </h2>
                        <button className="text-sm font-semibold text-blue-600 hover:text-blue-700 flex items-center gap-1 transition-colors">
                            View All <ArrowRight size={14} />
                        </button>
                    </div>
                    <div className="divide-y divide-gray-50">
                        {recentTxs.map((tx, i) => <TransactionRow key={i} tx={tx} />)}
                    </div>
                </div>
            </div>

            {/* Footer Meta */}
            <div className="max-w-7xl mx-auto mt-12 pt-8 border-t border-gray-100 flex flex-col md:flex-row justify-between items-center gap-4 text-gray-500 text-sm">
                <div className="flex items-center gap-6">
                    <div className="flex items-center gap-2">
                        <div className={`w-2 h-2 rounded-full ${isConnected ? 'bg-green-500 animate-pulse' : 'bg-red-500'}`}></div>
                        L2 Node Status: {isConnected ? 'Operational' : 'Offline'}
                    </div>
                    <div className="flex items-center gap-2">
                        <Cpu size={16} />
                        Sequencer: Nonagon-Main-A
                    </div>
                </div>
                <div>
                    © 2026 Nonagon Labs. Build on Cardano.
                </div>
            </div>
        </div>
    );
}
