import { useState } from 'react';
import { Search, Activity } from 'lucide-react';
import { Link, useNavigate } from 'react-router-dom';

export default function Navbar() {
    const [searchQuery, setSearchQuery] = useState("");
    const navigate = useNavigate();

    const handleSearch = (e: React.FormEvent) => {
        e.preventDefault();
        const query = searchQuery.trim();
        if (!query) return;

        if (query.startsWith("0x")) {
            if (query.length > 42) {
                navigate(`/tx/${query}`);
            } else {
                // Address detail not yet implemented
            }
        } else if (!isNaN(Number(query))) {
            navigate(`/block/${query}`);
        } else if (query.length === 64) {
            navigate(`/tx/0x${query}`);
        }
        setSearchQuery("");
    };

    return (
        <nav className="bg-white border-b border-gray-100 sticky top-0 z-50 shadow-sm">
            <div className="max-w-7xl mx-auto px-4 h-20 flex items-center justify-between gap-8">
                {/* Logo */}
                <Link to="/" className="flex items-center gap-3 shrink-0">
                    <div className="w-10 h-10 bg-black rounded-xl flex items-center justify-center text-white font-bold text-xl rotate-12 shadow-lg shadow-blue-500/10">
                        N
                    </div>
                    <div className="hidden sm:block">
                        <span className="text-xl font-black tracking-tighter block leading-none text-gray-900">NONAGON<span className="text-blue-600">SCAN</span></span>
                        <span className="text-[10px] font-bold text-gray-400 uppercase tracking-widest mt-0.5 block">Cardano L2 Explorer</span>
                    </div>
                </Link>

                {/* Search Bar - Center */}
                <div className="flex-1 max-w-2xl relative group hidden md:block">
                    <form onSubmit={handleSearch}>
                        <Search className="absolute left-4 top-1/2 -translate-y-1/2 text-gray-400 group-focus-within:text-blue-600 transition-colors" size={18} />
                        <input
                            type="text"
                            value={searchQuery}
                            onChange={(e) => setSearchQuery(e.target.value)}
                            placeholder="Search by Txn Hash / Block / Address"
                            className="w-full bg-gray-50 border border-gray-200 py-3 pl-12 pr-4 rounded-xl focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500 focus:bg-white transition-all font-medium text-sm"
                        />
                    </form>
                </div>

                {/* Right Links */}
                <div className="flex items-center gap-4">
                    <div className="hidden lg:flex items-center gap-2 px-3 py-1.5 bg-green-50 rounded-full border border-green-100">
                        <div className="w-2 h-2 rounded-full bg-green-500 animate-pulse"></div>
                        <span className="text-xs font-bold text-green-700 uppercase tracking-wider">Mainnet Alpha</span>
                    </div>

                    <div className="h-8 w-px bg-gray-100 mx-2 hidden sm:block"></div>

                    <div className="flex items-center gap-1 text-blue-600 bg-blue-50 px-4 py-2 rounded-xl text-sm font-bold border border-blue-100 cursor-pointer hover:bg-blue-100 transition-all">
                        <Activity size={16} />
                        <span className="hidden sm:inline">L2 Nodes</span>
                    </div>
                </div>
            </div>

            {/* Mobile Search - Only visible on small screens */}
            <div className="md:hidden px-4 pb-4 bg-white">
                <form onSubmit={handleSearch} className="relative group">
                    <Search className="absolute left-4 top-1/2 -translate-y-1/2 text-gray-400" size={16} />
                    <input
                        type="text"
                        value={searchQuery}
                        onChange={(e) => setSearchQuery(e.target.value)}
                        placeholder="Search..."
                        className="w-full bg-gray-50 border border-gray-200 py-2.5 pl-10 pr-4 rounded-lg focus:outline-none focus:ring-2 focus:ring-blue-500/20 focus:border-blue-500 text-sm"
                    />
                </form>
            </div>
        </nav>
    );
}
