param([string]$Root, [int]$Port = 8799)
# Mini serveur de fichiers HTTP local (127.0.0.1 uniquement) pour tester l'updater. Sert $Root.
$l = New-Object System.Net.HttpListener
$l.Prefixes.Add("http://127.0.0.1:$Port/")
$l.Start()
while ($l.IsListening) {
    $ctx = $l.GetContext()
    $rel = [Uri]::UnescapeDataString($ctx.Request.Url.AbsolutePath.TrimStart('/')).Replace('/', '\')
    $path = Join-Path $Root $rel
    if ($rel -and ($path -notlike "$Root*") -or -not (Test-Path $path -PathType Leaf)) {
        $ctx.Response.StatusCode = 404
    } else {
        $bytes = [IO.File]::ReadAllBytes($path)
        $ctx.Response.ContentLength64 = $bytes.Length
        $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
    }
    $ctx.Response.Close()
}
