# ============================================================================
# Nonagon L2 End-to-End Demo
# ============================================================================

Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "           NONAGON L2 - End-to-End Demo                     " -ForegroundColor Cyan
Write-Host "           Cardano Layer-2 with EVM Compatibility           " -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

$uri = "http://localhost:8545"

# Helper function for RPC calls
function Invoke-RPC {
    param($method, $params, $id)
    $body = @{
        jsonrpc = "2.0"
        method = $method
        params = $params
        id = $id
    } | ConvertTo-Json -Compress -Depth 10
    
    try {
        $response = Invoke-RestMethod -Uri $uri -Method Post -Body $body -ContentType "application/json" -TimeoutSec 10
        return $response
    } catch {
        return @{ error = $_.Exception.Message }
    }
}

# Test account addresses (pre-funded in genesis)
$aliceAddr = "0x00000000000000000000000000000000000000000000000000000001"
$bobAddr = "0x00000000000000000000000000000000000000000000000000000002"
$charlieAddr = "0x00000000000000000000000000000000000000000000000000000003"

Write-Host "============================================================" -ForegroundColor Yellow
Write-Host "  STEP 1: Check Node Status" -ForegroundColor Yellow
Write-Host "============================================================" -ForegroundColor Yellow
Write-Host ""

$version = Invoke-RPC "web3_clientVersion" @() 1
Write-Host "  Node Version:    $($version.result)" -ForegroundColor White

$chainId = Invoke-RPC "eth_chainId" @() 2
Write-Host "  Chain ID:        $($chainId.result)" -ForegroundColor White

$blockNum = Invoke-RPC "eth_blockNumber" @() 3
Write-Host "  Block Number:    $($blockNum.result)" -ForegroundColor White

$gasPrice = Invoke-RPC "eth_gasPrice" @() 4
Write-Host "  Gas Price:       $($gasPrice.result) wei" -ForegroundColor White
Write-Host ""

Write-Host "============================================================" -ForegroundColor Yellow
Write-Host "  STEP 2: Check Pre-Funded Wallets (Genesis Accounts)" -ForegroundColor Yellow
Write-Host "============================================================" -ForegroundColor Yellow
Write-Host ""

Write-Host "  [ALICE]" -ForegroundColor Green
Write-Host "  Address: $aliceAddr"
$aliceBal = Invoke-RPC "eth_getBalance" @($aliceAddr, "latest") 5
$aliceNonce = Invoke-RPC "eth_getTransactionCount" @($aliceAddr, "latest") 6
Write-Host "  Balance: $($aliceBal.result) wei"
Write-Host "  Nonce:   $($aliceNonce.result)"
Write-Host ""

Write-Host "  [BOB]" -ForegroundColor Magenta
Write-Host "  Address: $bobAddr"
$bobBal = Invoke-RPC "eth_getBalance" @($bobAddr, "latest") 7
$bobNonce = Invoke-RPC "eth_getTransactionCount" @($bobAddr, "latest") 8
Write-Host "  Balance: $($bobBal.result) wei"
Write-Host "  Nonce:   $($bobNonce.result)"
Write-Host ""

Write-Host "  [CHARLIE]" -ForegroundColor Blue
Write-Host "  Address: $charlieAddr"
$charlieBal = Invoke-RPC "eth_getBalance" @($charlieAddr, "latest") 9
Write-Host "  Balance: $($charlieBal.result) wei"
Write-Host ""

Write-Host "============================================================" -ForegroundColor Yellow
Write-Host "  STEP 3: View Genesis Block" -ForegroundColor Yellow
Write-Host "============================================================" -ForegroundColor Yellow
Write-Host ""

$block = Invoke-RPC "eth_getBlockByNumber" @("0x0", $true) 10
if ($block.result) {
    Write-Host "  Genesis Block:" -ForegroundColor Cyan
    Write-Host "    Number:     $($block.result.number)"
    Write-Host "    Hash:       $($block.result.hash)"
    Write-Host "    Gas Limit:  $($block.result.gasLimit)"
    Write-Host "    Base Fee:   $($block.result.baseFeePerGas)"
}
Write-Host ""

Write-Host "============================================================" -ForegroundColor Yellow
Write-Host "  STEP 4: Check L1 Settlement Status" -ForegroundColor Yellow
Write-Host "============================================================" -ForegroundColor Yellow
Write-Host ""

$sequencer = Invoke-RPC "nonagon_getCurrentSequencer" @() 11
Write-Host "  Current Sequencer: $($sequencer.result)"

$latestBatch = Invoke-RPC "nonagon_getLatestBatch" @() 12
Write-Host "  Latest Batch:      $($latestBatch.result)"
Write-Host ""

Write-Host "============================================================" -ForegroundColor Yellow
Write-Host "  STEP 5: Network Configuration" -ForegroundColor Yellow
Write-Host "============================================================" -ForegroundColor Yellow
Write-Host ""
Write-Host "  L1 Network:      Cardano Preprod (Testnet)" -ForegroundColor White
Write-Host "  L2 Native Token: NATX" -ForegroundColor White
Write-Host "  RPC Endpoint:    http://localhost:8545" -ForegroundColor White
Write-Host "  P2P Port:        30303" -ForegroundColor White
Write-Host ""

Write-Host "============================================================" -ForegroundColor Green
Write-Host "  DEMO COMPLETE!" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host ""
Write-Host "  The Nonagon L2 node is running with:" -ForegroundColor White
Write-Host "  - Pre-funded test accounts (Alice, Bob, Charlie)" -ForegroundColor White
Write-Host "  - EVM-compatible RPC for MetaMask integration" -ForegroundColor White
Write-Host "  - Settlement layer ready for Cardano Preprod" -ForegroundColor White
Write-Host ""
