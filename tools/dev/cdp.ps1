param([string]$Expr, [int]$Port = 9333)
$t = (Invoke-RestMethod "http://127.0.0.1:$Port/json" -TimeoutSec 5) | Where-Object { $_.type -eq 'page' } | Select-Object -First 1
if (-not $t) { throw 'pas de page DevTools' }
$ws = [System.Net.WebSockets.ClientWebSocket]::new()
$ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait()
$msg = @{ id = 1; method = 'Runtime.evaluate'; params = @{ expression = $Expr; returnByValue = $true; awaitPromise = $true } } | ConvertTo-Json -Depth 5 -Compress
$bytes = [Text.Encoding]::UTF8.GetBytes($msg)
$ws.SendAsync([ArraySegment[byte]]::new($bytes), 'Text', $true, [Threading.CancellationToken]::None).Wait()
$buf = New-Object byte[] 1048576
$sb = New-Object Text.StringBuilder
do {
    $r = $ws.ReceiveAsync([ArraySegment[byte]]::new($buf), [Threading.CancellationToken]::None).Result
    [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $r.Count))
} while (-not $r.EndOfMessage)
$sb.ToString()
