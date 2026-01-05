# Test Nonagon RPC
Write-Host "Testing Nonagon L2 Node RPC..." -ForegroundColor Cyan
Write-Host ""

$uri = "http://localhost:8545"

function Test-RPC {
    param($method, $params, $id)
    $body = @{
        jsonrpc = "2.0"
        method = $method
        params = $params
        id = $id
    } | ConvertTo-Json -Compress
    
    try {
        $response = Invoke-RestMethod -Uri $uri -Method Post -Body $body -ContentType "application/json" -TimeoutSec 5
        return $response
    } catch {
        return @{ error = $_.Exception.Message }
    }
}

Write-Host "1. eth_chainId:" -ForegroundColor Yellow
$r = Test-RPC "eth_chainId" @() 1
Write-Host "   Result: $($r.result)"
Write-Host ""

Write-Host "2. eth_blockNumber:" -ForegroundColor Yellow
$r = Test-RPC "eth_blockNumber" @() 2
Write-Host "   Result: $($r.result)"
Write-Host ""

Write-Host "3. web3_clientVersion:" -ForegroundColor Yellow
$r = Test-RPC "web3_clientVersion" @() 3
Write-Host "   Result: $($r.result)"
Write-Host ""

Write-Host "4. eth_gasPrice:" -ForegroundColor Yellow
$r = Test-RPC "eth_gasPrice" @() 4
Write-Host "   Result: $($r.result)"
Write-Host ""

Write-Host "5. eth_getBalance (address 0x01):" -ForegroundColor Yellow
$r = Test-RPC "eth_getBalance" @("0x0000000000000000000000000000000000000001", "latest") 5
Write-Host "   Result: $($r.result)"
Write-Host ""

Write-Host "6. eth_getBlockByNumber (latest):" -ForegroundColor Yellow
$r = Test-RPC "eth_getBlockByNumber" @("latest", $true) 6
Write-Host "   Result: $($r.result | ConvertTo-Json -Compress)"
Write-Host ""

Write-Host "7. nonagon_getCurrentSequencer:" -ForegroundColor Yellow
$r = Test-RPC "nonagon_getCurrentSequencer" @() 7
Write-Host "   Result: $($r.result)"
Write-Host ""

Write-Host "All tests complete!" -ForegroundColor Green
