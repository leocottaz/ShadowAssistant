# Affiche le mode de compositing de WebView2 (GPU ou logiciel) dans le dock lancé avec le port de débogage.
param([int]$Port = 9333)
$t = Invoke-RestMethod "http://127.0.0.1:$Port/json/version" -TimeoutSec 5
$ws = [System.Net.WebSockets.ClientWebSocket]::new()
$ws.ConnectAsync([Uri]$t.webSocketDebuggerUrl, [Threading.CancellationToken]::None).Wait()
$b = [Text.Encoding]::UTF8.GetBytes('{"id":1,"method":"SystemInfo.getInfo"}')
$ws.SendAsync([ArraySegment[byte]]::new($b), 'Text', $true, [Threading.CancellationToken]::None).Wait()
$buf = New-Object byte[] 4194304; $sb = New-Object Text.StringBuilder
do {
    $r = $ws.ReceiveAsync([ArraySegment[byte]]::new($buf), [Threading.CancellationToken]::None).Result
    [void]$sb.Append([Text.Encoding]::UTF8.GetString($buf, 0, $r.Count))
} while (-not $r.EndOfMessage)
'gpu_compositing: ' + ($sb.ToString() | ConvertFrom-Json).result.gpu.featureStatus.gpu_compositing
