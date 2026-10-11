# Generates res\app.ico, the one icon of the app (exe icon and tray icon): a key cap with a Shift
# arrow and an "a" ("hold a key, type letters"), white on a blue tile, so it shows on light and dark
# taskbars and Explorer backgrounds alike. Drawn as geometry, not text, so no font is involved.
# Frames are 32-bit BGRA DIBs (256 px: PNG), 16-28 px covering the tray at 100-175% scaling; the
# key-cap outline snaps to whole pixels at every size.
# Usage (from the repo root): pwsh -File tools\make-icons.ps1 [-Preview out.png]
param([string]$Preview)

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$appSizes = 16, 20, 24, 28, 32, 40, 48, 64, 128, 256
$tile = [System.Drawing.Color]::FromArgb(255, 43, 89, 195)  # #2B59C3

# Draws the key-cap glyph at the graphics origin, `s` px square.
function Add-KeyCap([System.Drawing.Graphics]$g, [int]$s, [System.Drawing.Color]$color) {
    $brush = [System.Drawing.SolidBrush]::new($color)
    # Key cap: rounded rectangle; whole-pixel stroke and margin, centered on half pixels when odd.
    $w = [Math]::Max(1, [Math]::Round($s * 0.065))
    $m = [Math]::Floor($s / 20)
    $x0 = $m + $w / 2.0; $side = $s - 2 * $m - $w
    $d = 2 * [Math]::Max(2.0, [Math]::Round($s * 0.19))
    $cap = [System.Drawing.Drawing2D.GraphicsPath]::new()
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
    $g.FillRectangle($brush, [single]($cx + $rb - $t / 2), [single](16.9 * $k), [single]$t, [single](8.7 * $k))

    $pen.Dispose(); $penA.Dispose(); $brush.Dispose(); $cap.Dispose()
}

function New-Canvas([int]$s) {
    $bmp = [System.Drawing.Bitmap]::new($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([System.Drawing.Color]::Transparent)
    return @($bmp, $g)
}

# A rounded blue tile with the glyph, in white, centered at about 3/4 of the tile.
function New-AppFrame([int]$s) {
    $bmp, $g = New-Canvas $s
    $r = [Math]::Max(3.0, [Math]::Round($s * 0.2)); $d = 2 * $r
    $bg = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $bg.AddArc(0, 0, $d, $d, 180, 90); $bg.AddArc($s - $d, 0, $d, $d, 270, 90)
    $bg.AddArc($s - $d, $s - $d, $d, $d, 0, 90); $bg.AddArc(0, $s - $d, $d, $d, 90, 90)
    $bg.CloseFigure()
    $fill = [System.Drawing.SolidBrush]::new($tile)
    $g.FillPath($fill, $bg)
    $gs = [int][Math]::Round($s * 0.74)
    $off = [int][Math]::Floor(($s - $gs) / 2)
    $g.TranslateTransform($off, $off)
    Add-KeyCap $g $gs ([System.Drawing.Color]::White)
    $g.Dispose(); $fill.Dispose(); $bg.Dispose()
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

function Get-PngBytes([System.Drawing.Bitmap]$bmp) {
    $ms = [System.IO.MemoryStream]::new()
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    return , $ms.ToArray()
}

# Writes an .ico from `$sizes`, calling `$makeFrame` for each size. 256 px is stored as PNG.
function Write-Ico([string]$path, [int[]]$sizes, [scriptblock]$makeFrame) {
    $images = [System.Collections.Generic.List[byte[]]]::new()
    foreach ($s in $sizes) {
        $b = & $makeFrame $s
        $images.Add($(if ($s -ge 256) { Get-PngBytes $b } else { Get-DibBytes $b }))
        $b.Dispose()
    }
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
Write-Ico (Join-Path $res 'app.ico') $appSizes { param($s) New-AppFrame $s }

# Optional preview: on a light and a dark strip, the tray sizes (16-64 px) at 1x and 8x, then 256 px.
if ($Preview) {
    $scale = 8
    $small = 16, 20, 24, 28, 32, 40, 48, 64
    $width = [int](($small | Measure-Object -Sum).Sum * ($scale + 1) + 40 * $small.Count + 256 + 40)
    $rowH = 64 * $scale + 120
    $sheet = [System.Drawing.Bitmap]::new($width, 2 * $rowH)
    $g = [System.Drawing.Graphics]::FromImage($sheet)
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $backgrounds = [System.Drawing.Color]::FromArgb(238, 240, 243), [System.Drawing.Color]::FromArgb(28, 28, 28)
    for ($ri = 0; $ri -lt 2; $ri++) {
        $top = $ri * $rowH
        $g.FillRectangle([System.Drawing.SolidBrush]::new($backgrounds[$ri]), 0, $top, $width, $rowH)
        $x = 20
        foreach ($s in $small) {
            $b = New-AppFrame $s
            $g.DrawImage($b, $x, $top + 20, $s, $s)
            $g.DrawImage($b, [System.Drawing.Rectangle]::new($x, $top + 100, $s * $scale, $s * $scale))
            $b.Dispose()
            $x += $s * ($scale + 1) + 40
        }
        $b = New-AppFrame 256
        $g.DrawImage($b, $x, $top + 100, 256, 256)
        $b.Dispose()
    }
    $g.Dispose()
    $sheet.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png)
    $sheet.Dispose()
    Write-Host "wrote $Preview"
}
