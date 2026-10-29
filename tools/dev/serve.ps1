param([string]$Root, [int]$Port = 8765)
$types = @{ '.html' = 'text/html; charset=utf-8'; '.css' = 'text/css; charset=utf-8'; '.js' = 'text/javascript; charset=utf-8' }
$l = [System.Net.HttpListener]::new()
$l.Prefixes.Add("http://localhost:$Port/")
$l.Start()
while ($l.IsListening) {
    $c = $l.GetContext()
    $p = $c.Request.Url.AbsolutePath.TrimStart('/')
    if (-not $p) { $p = 'index.html' }
    $f = Join-Path $Root $p
    if (Test-Path $f -PathType Leaf) {
        $b = [IO.File]::ReadAllBytes($f)
        $c.Response.ContentType = $types[[IO.Path]::GetExtension($f)]
        $c.Response.OutputStream.Write($b, 0, $b.Length)
    } else { $c.Response.StatusCode = 404 }
    $c.Response.Close()
}
