import React, { useState, useEffect } from 'react';
import {
    Wallet, Send, ArrowDownToLine, History, Settings,
    Copy, ExternalLink, ChevronRight, Shield, Eye, EyeOff,
    QrCode, RefreshCw, ArrowUpRight, ArrowDownLeft, Clock
} from 'lucide-react';
import { formatDistanceToNow } from 'date-fns';
import { JsonRpcProvider, formatEther } from 'ethers';

// Configuration
const RPC_URL = "http://localhost:8545";
// Using the address we seeded in genesis/testing (Alice)
const DEMO_ADDRESS = "0x0000000000000000000000000000000000000001";

// Mock data (kept for UI structure until full indexer integration)
const MOCK_USD_VALUE = "$51,274.50"; // Fixed for now

const MOCK_TOKENS = [
    { symbol: "NATX", name: "Nonagon", balance: "Loading...", usd: "...", change: "+2.4%" },
    { symbol: "nADA", name: "Wrapped ADA", balance: "5,000.00", usd: "$2,750.00", change: "-0.8%" },
    { symbol: "nUSDC", name: "Wrapped USDC", balance: "1,250.00", usd: "$1,250.00", change: "0.0%" },
];

const MOCK_TRANSACTIONS = [
    { type: "send", to: "0x71C7656EC7ab88b098defB751B7401B5f6d8976F", amount: "100.00", symbol: "NATX", status: "confirmed", time: Date.now() - 3600000 },
    { type: "receive", from: "0xf39Fd6e51aad88F6F4ce6aB8827279cffFb92266", amount: "500.00", symbol: "NATX", status: "confirmed", time: Date.now() - 86400000 },
    { type: "bridge", to: "Cardano L1", amount: "200.00", symbol: "nADA", status: "pending", time: Date.now() - 1800000 },
    { type: "send", to: "0x3C44CdDdB6a900fa2b585dd299e03d12FA4293BC", amount: "50.00", symbol: "nUSDC", status: "confirmed", time: Date.now() - 172800000 },
];

const Dashboard: React.FC = () => {
    const [view, setView] = useState<'dashboard' | 'send' | 'receive' | 'history'>('dashboard');
    const [showBalance, setShowBalance] = useState(true);
    const [copied, setCopied] = useState(false);

    // Real chain state
    const [balance, setBalance] = useState<string>("0.00");
    const [blockNumber, setBlockNumber] = useState<number>(0);
    const [isConnected, setIsConnected] = useState<boolean>(false);
    const [error, setError] = useState<string | null>(null);

    useEffect(() => {
        const fetchChainData = async () => {
            try {
                // Connect to local Nonagon node
                const provider = new JsonRpcProvider(RPC_URL);

                // Get basic chain info
                const block = await provider.getBlockNumber();
                setBlockNumber(block);
                setIsConnected(true);
                setError(null);

                // Get balance
                const bal = await provider.getBalance(DEMO_ADDRESS);
                const formattedBal = formatEther(bal);
                // Format to 2 decimals
                setBalance(parseFloat(formattedBal).toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 }));

            } catch (e) {
                console.error("Connection error:", e);
                setIsConnected(false);
                setError("Node disconnected");
            }
        };

        fetchChainData();
        const interval = setInterval(fetchChainData, 5000);
        return () => clearInterval(interval);
    }, []);

    const copyAddress = () => {
        navigator.clipboard.writeText(DEMO_ADDRESS);
        setCopied(true);
        setTimeout(() => setCopied(false), 2000);
    };

    return (
        <div className="min-h-screen bg-gray-50 font-sans">
            {/* Header */}
            <header className="bg-white border-b border-gray-100 sticky top-0 z-50">
                <div className="max-w-lg mx-auto px-4 py-4 flex items-center justify-between">
                    <div className="flex items-center gap-3">
                        <div className="w-10 h-10 bg-black rounded-xl flex items-center justify-center text-white font-bold rotate-12">
                            N
                        </div>
                        <div>
                            <h1 className="text-lg font-bold text-gray-900">Nonagon Wallet</h1>
                            <div className="flex items-center gap-1 text-xs text-gray-500">
                                <div className={`w-2 h-2 rounded-full ${isConnected ? 'bg-green-500' : 'bg-red-500'}`}></div>
                                <span>{isConnected ? `Connected (Block ${blockNumber})` : 'Disconnected'}</span>
                            </div>
                        </div>
                    </div>
                    <button className="p-2 hover:bg-gray-100 rounded-xl transition-colors">
                        <Settings size={20} className="text-gray-600" />
                    </button>
                </div>
                {error && (
                    <div className="bg-red-50 text-red-600 px-4 py-2 text-xs text-center border-b border-red-100">
                        {error} - Make sure Nonagon Node is running on port 8545
                    </div>
                )}
            </header>

            <main className="max-w-lg mx-auto px-4 py-6 space-y-6">
                {/* Balance Card */}
                <div className="bg-gradient-to-br from-gray-900 to-gray-800 rounded-3xl p-6 text-white shadow-xl">
                    <div className="flex items-center justify-between mb-6">
                        <span className="text-sm text-gray-400 uppercase tracking-wider">Total Balance</span>
                        <button onClick={() => setShowBalance(!showBalance)} className="p-2 hover:bg-white/10 rounded-lg transition-colors">
                            {showBalance ? <Eye size={18} /> : <EyeOff size={18} />}
                        </button>
                    </div>

                    <div className="mb-2">
                        <span className="text-4xl font-bold">
                            {showBalance ? balance : "••••••"} <span className="text-2xl text-gray-400">NATX</span>
                        </span>
                    </div>
                    <div className="text-gray-400 mb-6">
                        {showBalance ? MOCK_USD_VALUE : "••••••"}
                        <span className="text-green-400 ml-2 text-sm">+2.4%</span>
                    </div>

                    {/* Address */}
                    <div className="flex items-center gap-2 bg-white/10 rounded-xl px-4 py-3">
                        <span className="text-sm font-mono text-gray-300 flex-1 truncate">{DEMO_ADDRESS}</span>
                        <button onClick={copyAddress} className="p-2 hover:bg-white/10 rounded-lg transition-colors">
                            <Copy size={16} className={copied ? "text-green-400" : ""} />
                        </button>
                        <button className="p-2 hover:bg-white/10 rounded-lg transition-colors">
                            <QrCode size={16} />
                        </button>
                    </div>
                </div>

                {/* Action Buttons */}
                <div className="grid grid-cols-3 gap-4">
                    <button
                        onClick={() => setView('send')}
                        className="flex flex-col items-center gap-2 bg-white p-4 rounded-2xl border border-gray-100 hover:border-blue-200 hover:bg-blue-50/50 transition-all shadow-sm"
                    >
                        <div className="w-12 h-12 bg-blue-100 rounded-xl flex items-center justify-center text-blue-600">
                            <Send size={22} />
                        </div>
                        <span className="text-sm font-medium text-gray-700">Send</span>
                    </button>

                    <button
                        onClick={() => setView('receive')}
                        className="flex flex-col items-center gap-2 bg-white p-4 rounded-2xl border border-gray-100 hover:border-green-200 hover:bg-green-50/50 transition-all shadow-sm"
                    >
                        <div className="w-12 h-12 bg-green-100 rounded-xl flex items-center justify-center text-green-600">
                            <ArrowDownToLine size={22} />
                        </div>
                        <span className="text-sm font-medium text-gray-700">Receive</span>
                    </button>

                    <button
                        onClick={() => setView('history')}
                        className="flex flex-col items-center gap-2 bg-white p-4 rounded-2xl border border-gray-100 hover:border-purple-200 hover:bg-purple-50/50 transition-all shadow-sm"
                    >
                        <div className="w-12 h-12 bg-purple-100 rounded-xl flex items-center justify-center text-purple-600">
                            <History size={22} />
                        </div>
                        <span className="text-sm font-medium text-gray-700">History</span>
                    </button>
                </div>

                {/* Tokens */}
                <div className="bg-white rounded-3xl border border-gray-100 shadow-sm overflow-hidden">
                    <div className="p-4 border-b border-gray-50 flex items-center justify-between">
                        <h2 className="text-lg font-bold text-gray-900">Assets</h2>
                        <button className="p-2 hover:bg-gray-100 rounded-lg transition-colors">
                            <RefreshCw size={18} className="text-gray-500" />
                        </button>
                    </div>

                    <div className="divide-y divide-gray-50">
                        {MOCK_TOKENS.map((token, i) => (
                            <div key={i} className="flex items-center justify-between p-4 hover:bg-gray-50 transition-colors cursor-pointer">
                                <div className="flex items-center gap-3">
                                    <div className={`w-10 h-10 rounded-full flex items-center justify-center text-white font-bold text-sm
                    ${token.symbol === 'NATX' ? 'bg-black' : token.symbol === 'nADA' ? 'bg-blue-500' : 'bg-green-500'}`}>
                                        {token.symbol.charAt(0)}
                                    </div>
                                    <div>
                                        <div className="font-semibold text-gray-900">{token.symbol}</div>
                                        <div className="text-sm text-gray-500">{token.name}</div>
                                    </div>
                                </div>
                                <div className="text-right">
                                    <div className="font-semibold text-gray-900">
                                        {token.symbol === 'NATX' ? balance : token.balance}
                                    </div>
                                    <div className="text-sm text-gray-500">
                                        {token.usd}
                                        <span className={`ml-1 ${parseFloat(token.change) >= 0 ? 'text-green-500' : 'text-red-500'}`}>
                                            {token.change}
                                        </span>
                                    </div>
                                </div>
                            </div>
                        ))}
                    </div>
                </div>

                {/* Recent Activity */}
                <div className="bg-white rounded-3xl border border-gray-100 shadow-sm overflow-hidden">
                    <div className="p-4 border-b border-gray-50 flex items-center justify-between">
                        <h2 className="text-lg font-bold text-gray-900">Recent Activity</h2>
                        <button className="text-sm font-medium text-blue-600 hover:text-blue-700 flex items-center gap-1">
                            View All <ChevronRight size={16} />
                        </button>
                    </div>

                    <div className="divide-y divide-gray-50">
                        {MOCK_TRANSACTIONS.slice(0, 3).map((tx, i) => (
                            <div key={i} className="flex items-center justify-between p-4 hover:bg-gray-50 transition-colors">
                                <div className="flex items-center gap-3">
                                    <div className={`w-10 h-10 rounded-full flex items-center justify-center
                    ${tx.type === 'send' ? 'bg-red-100 text-red-600' :
                                            tx.type === 'receive' ? 'bg-green-100 text-green-600' :
                                                'bg-purple-100 text-purple-600'}`}>
                                        {tx.type === 'send' ? <ArrowUpRight size={20} /> :
                                            tx.type === 'receive' ? <ArrowDownLeft size={20} /> :
                                                <ExternalLink size={20} />}
                                    </div>
                                    <div>
                                        <div className="font-medium text-gray-900 capitalize">{tx.type}</div>
                                        <div className="text-sm text-gray-500 flex items-center gap-1">
                                            <Clock size={12} />
                                            {formatDistanceToNow(tx.time)} ago
                                        </div>
                                    </div>
                                </div>
                                <div className="text-right">
                                    <div className={`font-semibold ${tx.type === 'receive' ? 'text-green-600' : 'text-gray-900'}`}>
                                        {tx.type === 'receive' ? '+' : '-'}{tx.amount} {tx.symbol}
                                    </div>
                                    <div className={`text-xs font-medium px-2 py-0.5 rounded-full inline-block
                    ${tx.status === 'confirmed' ? 'bg-green-100 text-green-700' : 'bg-yellow-100 text-yellow-700'}`}>
                                        {tx.status}
                                    </div>
                                </div>
                            </div>
                        ))}
                    </div>
                </div>

                {/* Security Banner */}
                <div className="bg-gradient-to-r from-blue-500 to-purple-600 rounded-2xl p-4 flex items-center gap-4">
                    <div className="w-12 h-12 bg-white/20 rounded-xl flex items-center justify-center">
                        <Shield size={24} className="text-white" />
                    </div>
                    <div className="flex-1">
                        <div className="font-semibold text-white">Enable Extra Security</div>
                        <div className="text-sm text-white/80">Set up biometric authentication</div>
                    </div>
                    <ChevronRight size={20} className="text-white/60" />
                </div>
            </main>

            {/* Bottom Navigation */}
            <nav className="fixed bottom-0 left-0 right-0 bg-white border-t border-gray-100 py-2">
                <div className="max-w-lg mx-auto flex justify-around">
                    {[
                        { icon: Wallet, label: 'Wallet', active: true },
                        { icon: History, label: 'Activity', active: false },
                        { icon: ExternalLink, label: 'Bridge', active: false },
                        { icon: Settings, label: 'Settings', active: false },
                    ].map((item, i) => (
                        <button key={i} className={`flex flex-col items-center gap-1 px-4 py-2 rounded-xl transition-colors
              ${item.active ? 'text-blue-600' : 'text-gray-400 hover:text-gray-600'}`}>
                            <item.icon size={22} />
                            <span className="text-xs font-medium">{item.label}</span>
                        </button>
                    ))}
                </div>
            </nav>
        </div>
    );
};

export default Dashboard;
