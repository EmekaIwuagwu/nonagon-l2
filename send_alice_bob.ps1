# Send Alice -> Bob with Nonce 2
Write-Host "Sending 2.5 ETH from Alice to Bob (Nonce 2)..." -ForegroundColor Cyan
Write-Host ""
$uri = "http://localhost:8545"

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

$txBytes = New-Object System.Collections.ArrayList
function Add-Uint64 {
    param([uint64]$v, [System.Collections.ArrayList]$arr)
    for ($i = 7; $i -ge 0; $i--) { [void]$arr.Add([byte](($v -shr ($i * 8)) -band 0xFF)) }
}
function Add-LengthPrefixedBytes {
    param([byte[]]$b, [System.Collections.ArrayList]$arr)
    Add-Uint64 ([uint64]$b.Length) $arr
    foreach ($byte in $b) { [void]$arr.Add($byte) }
}

# From: Alice (0x...01)
$fromAddr = New-Object byte[] 28
$fromAddr[27] = 1
Add-LengthPrefixedBytes $fromAddr $txBytes

# To: Bob (0x...02)
$toAddr = New-Object byte[] 28
$toAddr[27] = 2
Add-LengthPrefixedBytes $toAddr $txBytes

# Value: 2.5 ETH
Add-Uint64 2500000000000000000 $txBytes

# Nonce: 2
Add-Uint64 2 $txBytes

# Gas Limit: 21000
Add-Uint64 21000 $txBytes
# Max Fee: 2 Gwei
Add-Uint64 2000000000 $txBytes
# Max Priority: 1 Gwei
Add-Uint64 1000000000 $txBytes
# Data: empty
Add-LengthPrefixedBytes @() $txBytes
# PubKey: dummy
1..32 | % { [void]$txBytes.Add([byte]0) }
# Signature: 0xFF bypass
1..64 | % { [void]$txBytes.Add([byte]0xFF) }

$hexString = "0x"
foreach ($b in $txBytes) { $hexString += $b.ToString("x2") }

Write-Host "Submitting..."
$result = Test-RPC "eth_sendRawTransaction" @($hexString) 4

if ($result.result) {
    Write-Host "SUCCESS! Tx Hash: $($result.result)" -ForegroundColor Green
    Write-Host "Waiting for confirmation..."
    for ($i=0; $i -lt 10; $i++) {
        Start-Sleep -Seconds 1
        $receipt = Test-RPC "eth_getTransactionReceipt" @($result.result) ($i+100)
        if ($receipt.result) {
            Write-Host "Confirmed in Block $($receipt.result.blockNumber)" -ForegroundColor Green
            break
        }
    }
} else {
    Write-Host "Error: $($result.error.message)" -ForegroundColor Red
}
