import { Link } from 'react-router-dom';
import { Cpu, Github, Twitter, ExternalLink, Shield, Zap, Globe, MessageSquare } from 'lucide-react';

interface FooterProps {
    isConnected: boolean;
}

export default function Footer({ isConnected }: FooterProps) {
    return (
        <footer className="mt-20 border-t border-gray-100 bg-white pt-16 pb-8">
            <div className="max-w-7xl mx-auto px-4">
                <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-12 mb-16">
                    {/* Brand Column */}
                    <div className="space-y-6">
                        <div className="flex items-center gap-3">
                            <div className="w-10 h-10 bg-black rounded-xl flex items-center justify-center text-white font-bold text-xl rotate-12 shadow-lg shadow-blue-500/20">
                                N
                            </div>
                            <span className="text-xl font-black tracking-tighter">NONAGON<span className="text-blue-600">SCAN</span></span>
                        </div>
                        <p className="text-gray-500 text-sm leading-relaxed max-w-xs font-medium">
                            Providing lightning-fast visibility into the Nonagon Layer 2 ecosystem on Cardano. Real-time, decentralized, and secure.
                        </p>
                        <div className="flex items-center gap-4">
                            <a href="#" className="p-2 bg-gray-50 rounded-lg text-gray-400 hover:text-blue-600 hover:bg-blue-50 transition-all">
                                <Twitter size={18} />
                            </a>
                            <a href="#" className="p-2 bg-gray-50 rounded-lg text-gray-400 hover:text-gray-900 hover:bg-gray-100 transition-all">
                                <Github size={18} />
                            </a>
                            <a href="#" className="p-2 bg-gray-50 rounded-lg text-gray-400 hover:text-blue-500 hover:bg-blue-50 transition-all">
                                <MessageSquare size={18} />
                            </a>
                        </div>
                    </div>

                    {/* Network Status Column */}
                    <div>
                        <h4 className="font-bold text-gray-900 mb-6 uppercase text-xs tracking-widest">Network Status</h4>
                        <div className="space-y-4">
                            <div className="bg-gray-50 p-4 rounded-2xl border border-gray-100">
                                <div className="flex items-center justify-between mb-2">
                                    <span className="text-xs font-semibold text-gray-500 uppercase">L2 Node</span>
                                    <div className={`flex items-center gap-1.5 px-2 py-0.5 rounded-full text-[10px] font-bold uppercase ${isConnected ? 'bg-green-100 text-green-700' : 'bg-red-100 text-red-700'}`}>
                                        <div className={`w-1 h-1 rounded-full ${isConnected ? 'bg-green-500 animate-pulse' : 'bg-red-500'}`}></div>
                                        {isConnected ? 'Healthy' : 'Offline'}
                                    </div>
                                </div>
                                <div className="flex items-center gap-2 text-sm font-bold text-gray-900">
                                    <Globe size={14} className="text-blue-500" />
                                    Mainnet-Alpha
                                </div>
                            </div>
                            <div className="bg-gray-50 p-4 rounded-2xl border border-gray-100">
                                <div className="flex items-center justify-between mb-2">
                                    <span className="text-xs font-semibold text-gray-500 uppercase">Sequencer</span>
                                    <span className="text-[10px] font-bold text-blue-600 uppercase">Active</span>
                                </div>
                                <div className="flex items-center gap-2 text-sm font-bold text-gray-900">
                                    <Cpu size={14} className="text-gray-400" />
                                    Nonagon-S-01
                                </div>
                            </div>
                        </div>
                    </div>

                    {/* Quick Access */}
                    <div>
                        <h4 className="font-bold text-gray-900 mb-6 uppercase text-xs tracking-widest">Navigation</h4>
                        <ul className="space-y-3">
                            <li><Link to="/" className="text-sm font-medium text-gray-500 hover:text-blue-600 transition-colors flex items-center gap-2 group">Dashboard</Link></li>
                            <li><Link to="/blocks" className="text-sm font-medium text-gray-500 hover:text-blue-600 transition-colors flex items-center gap-2 group">View All Blocks</Link></li>
                            <li><Link to="/txs" className="text-sm font-medium text-gray-500 hover:text-blue-600 transition-colors flex items-center gap-2 group">Recent Transactions</Link></li>
                            <li><a href="#" className="text-sm font-medium text-gray-500 hover:text-blue-600 transition-colors flex items-center gap-2 group">Node Documentation <ExternalLink size={12} className="opacity-0 group-hover:opacity-100 transition-opacity" /></a></li>
                        </ul>
                    </div>

                    {/* Ecosystem */}
                    <div>
                        <h4 className="font-bold text-gray-900 mb-6 uppercase text-xs tracking-widest">Resources</h4>
                        <ul className="space-y-3">
                            <li><a href="#" className="text-sm font-medium text-gray-500 hover:text-blue-600 transition-colors flex items-center gap-2 group">Cardano Explorer <ExternalLink size={12} className="opacity-0 group-hover:opacity-100 transition-opacity" /></a></li>
                            <li><a href="#" className="text-sm font-medium text-gray-500 hover:text-blue-600 transition-colors flex items-center gap-2 group">L1 Bridge Status</a></li>
                            <li><a href="#" className="text-sm font-medium text-gray-500 hover:text-blue-600 transition-colors flex items-center gap-2 group flex items-center gap-2">Security Audit <Shield size={14} className="text-green-500" /></a></li>
                            <li><a href="#" className="text-sm font-medium text-gray-500 hover:text-blue-600 transition-colors flex items-center gap-2 group">NATX Tokenomics</a></li>
                        </ul>
                    </div>
                </div>

                <div className="pt-8 border-t border-gray-50 flex flex-col md:flex-row justify-between items-center gap-4">
                    <p className="text-gray-400 text-xs font-medium">
                        © 2026 Nonagon Labs. Powered by Cardano. All rights reserved.
                    </p>
                    <div className="flex items-center gap-6">
                        <a href="#" className="text-xs text-gray-400 hover:text-gray-900 transition-colors font-medium">Privacy Policy</a>
                        <a href="#" className="text-xs text-gray-400 hover:text-gray-900 transition-colors font-medium">Terms of Service</a>
                        <div className="flex items-center gap-1.5 text-xs text-gray-900 font-bold bg-gray-50 px-3 py-1.5 rounded-full border border-gray-100">
                            <Zap size={12} className="text-yellow-500 fill-yellow-500" />
                            L2 Mainnet
                        </div>
                    </div>
                </div>
            </div>
        </footer>
    );
}
