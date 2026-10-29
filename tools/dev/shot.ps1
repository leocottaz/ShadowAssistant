param([string]$Out, [int]$Height = 760, [int]$Width = 1100)
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
Add-Type @"
using System.Runtime.InteropServices;
public static class Dpi { [DllImport("user32.dll")] public static extern bool SetProcessDPIAware(); }
"@
[Dpi]::SetProcessDPIAware() | Out-Null
$screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$w = [Math]::Min($Width, $screen.Width)
$x = $screen.X + [int](($screen.Width - $w) / 2)
$bmp = New-Object System.Drawing.Bitmap $w, $Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($x, $screen.Y, 0, 0, $bmp.Size)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
"screen $($screen.Width)x$($screen.Height)"
