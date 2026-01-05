import { useEffect, useState } from 'react';
import { Activity, Box, Database, Zap, ArrowRight, AlertTriangle } from 'lucide-react';
import { Link } from 'react-router-dom';
import Footer from './Footer';
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
                    <Link to={`/tx/${tx.hash}`} className="hover:underline">
                        {tx.hash}
                    </Link>
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
                <Link to={`/block/${parseInt(block.number, 16)}`} className="font-bold text-gray-900 hover:text-blue-600 hover:underline">
                    #{parseInt(block.number, 16)}
                </Link>
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

                // Fetch Head Block Number
                const headRes = await fetch(RPC_URL, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ jsonrpc: "2.0", method: "eth_blockNumber", params: [], id: 1 })
                });
                const headJson = await headRes.json();
                const headNum = parseInt(headJson.result, 16);

                // Fetch last 10 blocks in parallel
                const promises = [];
                for (let i = 0; i < 10; i++) {
                    const n = headNum - i;
                    if (n < 0) break;
                    promises.push(
                        fetch(RPC_URL, {
                            method: 'POST',
                            headers: { 'Content-Type': 'application/json' },
                            body: JSON.stringify({
                                jsonrpc: "2.0",
                                method: "eth_getBlockByNumber",
                                params: ["0x" + n.toString(16), true],
                                id: n
                            })
                        }).then(r => r.json())
                    );
                }

                const results = await Promise.all(promises);
                const blocksData = results.map(r => r.result).filter(b => b);
                setBlocks(blocksData);
                setIsConnected(true);

                // Fetch recent transactions using the new optimized RPC method
                const txRes = await fetch(RPC_URL, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        jsonrpc: "2.0",
                        method: "nonagon_getRecentTransactions",
                        params: [15],
                        id: 2
                    })
                });
                const txJson = await txRes.json();
                if (txJson.result) {
                    const formattedTxs = txJson.result.map((tx: any) => ({
                        ...tx,
                        value: (parseInt(tx.value, 16) / 1e18).toLocaleString('en-US', { maximumFractionDigits: 4 }),
                        timestamp: new Date(parseInt(tx.timestamp, 16) * 1000)
                    }));
                    setRecentTxs(formattedTxs);
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
                        <Link to="/blocks" className="text-sm font-semibold text-blue-600 hover:text-blue-700 flex items-center gap-1 transition-colors">
                            View All <ArrowRight size={14} />
                        </Link>
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
                    <div className="p-6 border-b border-gray-100 flex justify-between items-center">
                        <h2 className="text-lg font-bold flex items-center gap-2">
                            <Zap size={20} className="text-yellow-500" />
                            Latest Transactions
                        </h2>
                        <Link to="/txs" className="text-sm font-semibold text-blue-600 hover:text-blue-700 flex items-center gap-1 transition-colors">
                            View All <ArrowRight size={14} />
                        </Link>
                    </div>
                    <div className="divide-y divide-gray-50">
                        {recentTxs.map((tx, i) => <TransactionRow key={i} tx={tx} />)}
                    </div>
                </div>
            </div>

            {/* Premium Footer */}
            <Footer isConnected={isConnected} />
        </div>
    );
}
