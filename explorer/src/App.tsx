import { BrowserRouter, Routes, Route } from 'react-router-dom';
import Dashboard from './components/Dashboard';
import TxDetail from './components/TxDetail';
import BlockDetail from './components/BlockDetail';
import Blocks from './components/Blocks';
import Transactions from './components/Transactions';

import Navbar from './components/Navbar';

function App() {
  return (
    <BrowserRouter>
      <Navbar />
      <Routes>
        <Route path="/" element={<Dashboard />} />
        <Route path="/tx/:hash" element={<TxDetail />} />
        <Route path="/block/:number" element={<BlockDetail />} />
        <Route path="/blocks" element={<Blocks />} />
        <Route path="/txs" element={<Transactions />} />
      </Routes>
    </BrowserRouter>
  )
}

export default App
