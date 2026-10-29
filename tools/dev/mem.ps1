# Mémoire du dock par type de processus (ShadowAssistant.exe + processus WebView2 de son dossier de données).
# Usage : tools/dev/mem.ps1 [-Label "caché"]
param([string]$Label = '')
$rows = @()
$dock = Get-Process ShadowAssistant -ErrorAction SilentlyContinue
if ($dock) { $rows += [pscustomobject]@{ Type = 'ShadowAssistant.exe'; Mo = [math]::Round($dock.PrivateMemorySize64 / 1MB, 1) } }
Get-CimInstance Win32_Process -Filter "Name='msedgewebview2.exe'" | Where-Object { $_.CommandLine -match 'ShadowAssistant' } | ForEach-Object {
    $type = if ($_.CommandLine -match '--type=([\w-]+)') { $Matches[1] } else { 'browser' }
    if ($_.CommandLine -match '--utility-sub-type=([\w.]+)') { $type += ' ' + ($Matches[1] -replace '^.*\.', '') }
    $p = Get-Process -Id $_.ProcessId -ErrorAction SilentlyContinue
    if ($p) { $rows += [pscustomobject]@{ Type = $type; Mo = [math]::Round($p.PrivateMemorySize64 / 1MB, 1) } }
}
$total = [math]::Round(($rows | Measure-Object Mo -Sum).Sum, 0)
$detail = ($rows | Sort-Object Mo -Descending | ForEach-Object { "$($_.Type)=$($_.Mo)" }) -join ', '
"[$Label] total prive $total Mo  ($detail)"
