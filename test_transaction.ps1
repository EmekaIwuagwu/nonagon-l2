# Test Transaction Script for Nonagon L2
Write-Host "Testing Nonagon L2 Transaction Submission..." -ForegroundColor Cyan
Write-Host ""

$uri = "http://localhost:8545"

# Helper function for RPC calls
function Test-RPC {
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

# Build a raw transaction hex manually
# Transaction format:
# - from address (8 bytes length prefix + 28 bytes data)
# - to address (8 bytes length prefix + 28 bytes data)
# - value (8 bytes)
# - nonce (8 bytes)
# - gas_limit (8 bytes)
# - max_fee_per_gas (8 bytes)
# - max_priority_fee_per_gas (8 bytes)
# - data (8 bytes length prefix + data bytes)
# - signature (64 bytes)

$txBytes = New-Object System.Collections.ArrayList

# Helper to add uint64 as big-endian
function Add-Uint64 {
    param([uint64]$v, [System.Collections.ArrayList]$arr)
    for ($i = 7; $i -ge 0; $i--) {
        [void]$arr.Add([byte](($v -shr ($i * 8)) -band 0xFF))
    }
}

# Helper to add length-prefixed bytes
function Add-LengthPrefixedBytes {
    param([byte[]]$b, [System.Collections.ArrayList]$arr)
    Add-Uint64 ([uint64]$b.Length) $arr
    foreach ($byte in $b) {
        [void]$arr.Add($byte)
    }
}

# From address: Alice (0x00...01)
$fromAddr = New-Object byte[] 28
$fromAddr[27] = 1
Add-LengthPrefixedBytes $fromAddr $txBytes

# To address: Bob (0x00...02)
$toAddr = New-Object byte[] 28
$toAddr[27] = 2
Add-LengthPrefixedBytes $toAddr $txBytes

# Value: 1000000000000000000 (1 ETH in wei)
Add-Uint64 1000000000000000000 $txBytes

# Nonce: 0
Add-Uint64 0 $txBytes

# Gas limit: 21000
Add-Uint64 21000 $txBytes

# Max fee per gas: 2000000000 (2 Gwei)
Add-Uint64 2000000000 $txBytes

# Max priority fee: 1000000000 (1 Gwei)
Add-Uint64 1000000000 $txBytes

# Data: empty
Add-LengthPrefixedBytes @() $txBytes

# Sender PubKey: 32 bytes (dummy)
for ($i = 0; $i -lt 32; $i++) {
    [void]$txBytes.Add([byte]0)
}

# Signature: 64 bytes (0xFF for dev bypass)
for ($i = 0; $i -lt 64; $i++) {
    [void]$txBytes.Add([byte]0xFF)
}

# Convert to hex string
$hexString = "0x"
foreach ($b in $txBytes) {
    $hexString += $b.ToString("x2")
}

Write-Host "=== Transaction Details ===" -ForegroundColor Yellow
Write-Host "From:      0x$(($fromAddr | ForEach-Object { $_.ToString('x2') }) -join '')"
Write-Host "To:        0x$(($toAddr | ForEach-Object { $_.ToString('x2') }) -join '')"
Write-Host "Value:     1 ETH (1000000000000000000 wei)"
Write-Host "Nonce:     0"
Write-Host "Gas Limit: 21000"
Write-Host "Max Fee:   2 Gwei"
Write-Host ""
Write-Host "Raw TX Size: $($txBytes.Count) bytes" -ForegroundColor Cyan
Write-Host "Raw TX Hex (first 100 chars):" -ForegroundColor Cyan
Write-Host $hexString.Substring(0, [Math]::Min(102, $hexString.Length)) "..."
Write-Host ""

# Get current block number before
Write-Host "=== Before Transaction ===" -ForegroundColor Yellow
$beforeBlock = Test-RPC "eth_blockNumber" @() 1
Write-Host "Block Number: $($beforeBlock.result)"

# Check Alice's balance
$aliceHex = "0x" + (($fromAddr | ForEach-Object { $_.ToString("x2") }) -join "")
$aliceBal = Test-RPC "eth_getBalance" @($aliceHex, "latest") 2
Write-Host "Alice Balance: $($aliceBal.result)"

# Check Bob's balance
$bobHex = "0x" + (($toAddr | ForEach-Object { $_.ToString("x2") }) -join "")
$bobBal = Test-RPC "eth_getBalance" @($bobHex, "latest") 3
Write-Host "Bob Balance: $($bobBal.result)"
Write-Host ""

# Send the transaction
Write-Host "=== Sending Transaction ===" -ForegroundColor Yellow
Write-Host "Submitting to eth_sendRawTransaction..."
$result = Test-RPC "eth_sendRawTransaction" @($hexString) 4

if ($result.result -and $result.result -ne "null") {
    Write-Host "SUCCESS!" -ForegroundColor Green
    Write-Host "Transaction Hash: $($result.result)"
    
    $txHash = $result.result
    
    Write-Host ""
    Write-Host "=== Waiting for Confirmation ===" -ForegroundColor Yellow
    for ($i=0; $i -lt 20; $i++) {
        Start-Sleep -Seconds 1
        Write-Host -NoNewline "."
        $receipt = Test-RPC "eth_getTransactionReceipt" @($txHash) ($i+100)
        
        if ($receipt.result -and $receipt.result -ne "null") {
            Write-Host ""
            Write-Host "Receipt FOUND!" -ForegroundColor Green
            Write-Host "Block Number: $($receipt.result.blockNumber)"
            Write-Host "Status:       $($receipt.result.status)"
            Write-Host "Gas Used:     $($receipt.result.gasUsed)"
            Write-Host "From:         $($receipt.result.from)"
            Write-Host "To:           $($receipt.result.to)"
            break
        }
    }
    Write-Host ""
    
    # Check balances again
    Write-Host "=== Final Balances ===" -ForegroundColor Yellow
    $aliceBal = Test-RPC "eth_getBalance" @($aliceHex, "latest") 200
    $bobBal = Test-RPC "eth_getBalance" @($bobHex, "latest") 201
    Write-Host "Alice Balance: $($aliceBal.result)"
    Write-Host "Bob Balance:   $($bobBal.result)"
    
} elseif ($result.error) {
    if ($result.error -is [System.Collections.Hashtable] -or $result.error.message) {
        Write-Host "RPC Error: $($result.error.message)" -ForegroundColor Red
    } else {
        Write-Host "Error: $($result.error)" -ForegroundColor Red
    }
} else {
    Write-Host "Full Response: $($result | ConvertTo-Json -Depth 5)"
}

Write-Host ""
Write-Host "=== Test Complete ===" -ForegroundColor Cyan
Write-Host ""
