param([string]$Dir, [int]$Seconds = 240)
# Session multimédia Windows silencieuse pour tester le lecteur du dock.
$wav = Join-Path $Dir 'silence.wav'
# PCM 16 bits : des zéros = silence complet.
$rate = 8000; $len = $rate * $Seconds * 2
$fs = [IO.File]::Create($wav); $bw = New-Object IO.BinaryWriter $fs
$bw.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $bw.Write([int](36 + $len)); $bw.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
$bw.Write([int]16); $bw.Write([int16]1); $bw.Write([int16]1); $bw.Write([int]$rate); $bw.Write([int]($rate * 2)); $bw.Write([int16]2); $bw.Write([int16]16)
$bw.Write([Text.Encoding]::ASCII.GetBytes('data')); $bw.Write([int]$len); $bw.Write((New-Object byte[] $len)); $bw.Close()

$null = [Windows.Media.Playback.MediaPlayer, Windows.Media, ContentType = WindowsRuntime]
$null = [Windows.Media.Core.MediaSource, Windows.Media.Core, ContentType = WindowsRuntime]
$null = [Windows.Media.Playback.MediaPlaybackItem, Windows.Media.Playback, ContentType = WindowsRuntime]
$player = [Windows.Media.Playback.MediaPlayer]::new()
$player.Volume = 0
$item = [Windows.Media.Playback.MediaPlaybackItem]::new([Windows.Media.Core.MediaSource]::CreateFromUri([Uri]$wav))
$props = $item.GetDisplayProperties()
$props.Type = [Windows.Media.MediaPlaybackType]::Music
$props.MusicProperties.Title = 'Morceau de test du dock'
$props.MusicProperties.Artist = 'Artiste de test'
$props.MusicProperties.AlbumTitle = 'Album de test'
$item.ApplyDisplayProperties($props)
$player.Source = $item
$player.Play()
"session prête"
$end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) { Start-Sleep -Milliseconds 500 }
$player.Dispose()
