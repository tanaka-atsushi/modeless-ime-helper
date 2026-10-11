# Generates the tray icons res\tray-black.ico (for a light taskbar) and res\tray-white.ico (for a
# dark taskbar): a key cap with a Shift arrow and an "a" ("hold a key, type letters").
# Drawn as geometry, not text, so no font is involved. Each .ico holds 16..64 px frames as 32-bit
# BGRA DIBs; small sizes snap the key-cap outline to whole pixels.
# Usage (from the repo root): pwsh -File tools\make-tray-icons.ps1 [-Preview out.png]
param([string]$Preview)

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sizes = 16, 20, 24, 28, 32, 40, 48, 64

# Key-cap outline per size: stroke width and margin in whole pixels.
$capStroke = @{ 16 = 1; 20 = 1; 24 = 2; 28 = 2; 32 = 2; 40 = 3; 48 = 3; 64 = 4 }
$capMargin = @{ 16 = 0; 20 = 1; 24 = 1; 28 = 1; 32 = 1; 40 = 2; 48 = 2; 64 = 3 }

function New-Frame([int]$s, [System.Drawing.Color]$color) {
    $bmp = [System.Drawing.Bitmap]::new($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    $brush = [System.Drawing.SolidBrush]::new($color)

    # Key cap: rounded rectangle, outline centered on half pixels when the stroke is odd.
    $w = $capStroke[$s]; $m = $capMargin[$s]
    $x0 = $m + $w / 2.0; $side = $s - 2 * $m - $w
    $r = [Math]::Max(2.0, [Math]::Round($s * 0.19))
    $cap = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $d = 2 * $r
    $cap.AddArc($x0, $x0, $d, $d, 180, 90)
    $cap.AddArc($x0 + $side - $d, $x0, $d, $d, 270, 90)
    $cap.AddArc($x0 + $side - $d, $x0 + $side - $d, $d, $d, 0, 90)
    $cap.AddArc($x0, $x0 + $side - $d, $d, $d, 90, 90)
    $cap.CloseFigure()
    $pen = [System.Drawing.Pen]::new($color, [single]$w)
    $g.DrawPath($pen, $cap)

    # Inner marks on a 32-unit grid, scaled.
    $k = $s / 32.0
    # Shift arrow (filled), upper left.
    $arrow = [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new(6.5 * $k, 13.5 * $k), [System.Drawing.PointF]::new(10.5 * $k, 8 * $k),
        [System.Drawing.PointF]::new(14.5 * $k, 13.5 * $k), [System.Drawing.PointF]::new(12 * $k, 13.5 * $k),
        [System.Drawing.PointF]::new(12 * $k, 16.5 * $k), [System.Drawing.PointF]::new(9 * $k, 16.5 * $k),
        [System.Drawing.PointF]::new(9 * $k, 13.5 * $k))
    $g.FillPolygon($brush, $arrow)
    # Single-story "a", lower right: a round bowl plus a stem on its right.
    $t = [Math]::Max(1.15, 2.4 * $k)
    $penA = [System.Drawing.Pen]::new($color, [single]$t)
    $cx = 19.5 * $k; $cy = 21.25 * $k; $rb = 3.75 * $k
    $g.DrawEllipse($penA, [single]($cx - $rb), [single]($cy - $rb), [single](2 * $rb), [single](2 * $rb))
    $stemX = $cx + $rb
    $g.FillRectangle($brush, [single]($stemX - $t / 2), [single](16.9 * $k), [single]$t, [single](25.6 * $k - 16.9 * $k))

    $g.Dispose(); $pen.Dispose(); $penA.Dispose(); $brush.Dispose(); $cap.Dispose()
    return $bmp
}

# One ICO image: BITMAPINFOHEADER (height doubled), BGRA rows bottom-up, then the AND mask
# (1 where fully transparent, rows padded to 4 bytes).
function Get-DibBytes([System.Drawing.Bitmap]$bmp) {
    $s = $bmp.Width
    $rect = [System.Drawing.Rectangle]::new(0, 0, $s, $s)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $px = [byte[]]::new($s * $s * 4)
    for ($y = 0; $y -lt $s; $y++) {
        [System.Runtime.InteropServices.Marshal]::Copy([IntPtr]::Add($data.Scan0, $y * $data.Stride), $px, $y * $s * 4, $s * 4)
    }
    $bmp.UnlockBits($data)
    $maskRow = [int]([Math]::Floor(($s + 31) / 32) * 4)
    $ms = [System.IO.MemoryStream]::new()
    $bw = [System.IO.BinaryWriter]::new($ms)
    $bw.Write([int]40); $bw.Write([int]$s); $bw.Write([int](2 * $s)); $bw.Write([int16]1); $bw.Write([int16]32)
    $bw.Write([int]0); $bw.Write([int]($s * $s * 4 + $maskRow * $s)); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0)
    for ($y = $s - 1; $y -ge 0; $y--) { $bw.Write($px, $y * $s * 4, $s * 4) }
    for ($y = $s - 1; $y -ge 0; $y--) {
        $row = [byte[]]::new($maskRow)
        for ($x = 0; $x -lt $s; $x++) {
            if ($px[($y * $s + $x) * 4 + 3] -eq 0) { $row[[Math]::Floor($x / 8)] = $row[[Math]::Floor($x / 8)] -bor (0x80 -shr ($x % 8)) }
        }
        $bw.Write($row)
    }
    $bw.Flush()
    return , $ms.ToArray()  # the comma keeps the byte[] from being unrolled into the pipeline
}

function Write-Ico([string]$path, [System.Drawing.Color]$color) {
    $images = [System.Collections.Generic.List[byte[]]]::new()
    foreach ($s in $sizes) { $b = New-Frame $s $color; $images.Add((Get-DibBytes $b)); $b.Dispose() }
    $fs = [System.IO.File]::Create($path)
    $bw = [System.IO.BinaryWriter]::new($fs)
    $bw.Write([int16]0); $bw.Write([int16]1); $bw.Write([int16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $s = $sizes[$i]
        $bw.Write([byte]($s % 256)); $bw.Write([byte]($s % 256)); $bw.Write([byte]0); $bw.Write([byte]0)
        $bw.Write([int16]1); $bw.Write([int16]32); $bw.Write([int]$images[$i].Length); $bw.Write([int]$offset)
        $offset += $images[$i].Length
    }
    foreach ($img in $images) { $bw.Write($img) }
    $bw.Dispose()
    Write-Host "wrote $path"
}

$res = Join-Path $root 'res'
New-Item -ItemType Directory -Force $res | Out-Null
Write-Ico (Join-Path $res 'tray-black.ico') ([System.Drawing.Color]::FromArgb(255, 26, 26, 26))
Write-Ico (Join-Path $res 'tray-white.ico') ([System.Drawing.Color]::FromArgb(255, 255, 255, 255))

# Optional preview: every size at 1x and 8x on a light and a dark strip.
if ($Preview) {
    $scale = 8
    $width = ($sizes | Measure-Object -Sum).Sum * ($scale + 1) + 40 * $sizes.Count
    $sheet = [System.Drawing.Bitmap]::new([int]$width, 2 * (64 * $scale + 120))
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $rows = @(@{ bg = [System.Drawing.Color]::FromArgb(238, 240, 243); fg = [System.Drawing.Color]::FromArgb(255, 26, 26, 26) },
              @{ bg = [System.Drawing.Color]::FromArgb(28, 28, 28); fg = [System.Drawing.Color]::White })
    for ($ri = 0; $ri -lt 2; $ri++) {
        $top = $ri * (64 * $scale + 120)
        $g.FillRectangle([System.Drawing.SolidBrush]::new($rows[$ri].bg), 0, $top, $width, 64 * $scale + 120)
        $x = 20
        foreach ($s in $sizes) {
            $b = New-Frame $s $rows[$ri].fg
            $g.DrawImage($b, $x, $top + 20, $s, $s)
            $g.DrawImage($b, [System.Drawing.Rectangle]::new($x, $top + 100, $s * $scale, $s * $scale))
            $b.Dispose()
            $x += $s * ($scale + 1) + 40
        }
    }
    $g.Dispose()
    $sheet.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png)
    $sheet.Dispose()
    Write-Host "wrote $Preview"
}
