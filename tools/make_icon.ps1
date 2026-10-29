param(
  [string]$IcoPath,
  [string]$PreviewPath,
  # Emits the ten PNGs macOS wants, ready for `iconutil -c icns`. The artwork is
  # identical to the .ico; only the framing differs, for the reason explained
  # where the inset is applied.
  [string]$IconsetPath,
  # Sizes below this are drawn as the keyhole alone. Above it, the page is
  # included. Only the two largest sizes keep the page by default: everything
  # Explorer and the taskbar reach for day to day should be the bold mark.
  [int]$KeyholeBelow = 128
)

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

# Must track src/gui/theme.hpp: BG and ACCENT.
$BG   = [System.Drawing.Color]::FromArgb(255, 0x0A, 0x0F, 0x0B)
$MARK = [System.Drawing.Color]::FromArgb(255, 0x33, 0xFF, 0x6B)

function New-RoundedPath([single]$x,[single]$y,[single]$w,[single]$h,[single]$r) {
  $p = New-Object System.Drawing.Drawing2D.GraphicsPath
  if ($r -le 0.5) { $p.AddRectangle((New-Object System.Drawing.RectangleF($x,$y,$w,$h))); return $p }
  $d = $r*2
  $p.AddArc($x,$y,$d,$d,180,90); $p.AddArc($x+$w-$d,$y,$d,$d,270,90)
  $p.AddArc($x+$w-$d,$y+$h-$d,$d,$d,0,90); $p.AddArc($x,$y+$h-$d,$d,$d,90,90)
  $p.CloseFigure(); return $p
}

function Draw-Keyhole($g, [int]$s, $color, [single]$holeR, [single]$cy,
                     [single]$slotHalf, [single]$slotBot) {
  $brush = New-Object System.Drawing.SolidBrush($color)
  $hr = $s * $holeR
  $g.FillEllipse($brush, ($s*0.5-$hr), ($s*$cy-$hr), ($hr*2), ($hr*2))
  $slot = New-Object System.Drawing.Drawing2D.GraphicsPath
  $slotTop = $s*$cy + $hr*0.5
  $slot.AddPolygon(@(
    (New-Object System.Drawing.PointF(($s*0.5-$s*$slotHalf),      $slotTop)),
    (New-Object System.Drawing.PointF(($s*0.5+$s*$slotHalf),      $slotTop)),
    (New-Object System.Drawing.PointF(($s*0.5+$s*$slotHalf*1.75), ($s*$slotBot))),
    (New-Object System.Drawing.PointF(($s*0.5-$s*$slotHalf*1.75), ($s*$slotBot)))
  ))
  $g.FillPath($brush, $slot); $slot.Dispose()
}

# Draws the icon at `s` pixels.
#
# Below $KeyholeBelow the page is dropped and only the keyhole is drawn. At
# small sizes a page outline is two or three pixels of edge that read as noise,
# and it competes with the keyhole for the same space, weakening both.
#
# The colour roles stay fixed across sizes -- the phosphor green is the subject,
# the near-black is the ground -- so only the shape simplifies, never the palette.
# $decideAt lets the caller choose the page-or-keyhole variant by a different
# size than the one being drawn. macOS needs that: its artwork is drawn smaller
# than the canvas it sits in, and the variant should follow what the user sees
# the icon as, not the inset measurement.
function Draw-Icon([int]$s, [int]$decideAt = 0) {
  $bmp = New-Object System.Drawing.Bitmap($s,$s,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
  $g.Clear([System.Drawing.Color]::Transparent)

  $decide = if ($decideAt -gt 0) { $decideAt } else { $s }
  $small = $decide -lt $script:KeyholeBelow

  $tile = New-RoundedPath 0 0 $s $s ($s * $(if ($small) { 0.20 } else { 0.22 }))
  $g.FillPath((New-Object System.Drawing.SolidBrush($BG)), $tile); $tile.Dispose()

  if ($small) {
    Draw-Keyhole $g $s $MARK 0.195 0.400 0.080 0.765
    $g.Dispose(); return $bmp
  }

  # Page with a folded corner, keyhole cut back through to the ground.
  $l = 0.20; $t = 0.11; $r = 0.80; $b = 0.89; $fold = 0.20
  $page = New-Object System.Drawing.Drawing2D.GraphicsPath
  $page.AddPolygon(@(
    (New-Object System.Drawing.PointF(($s*$l),         ($s*$t))),
    (New-Object System.Drawing.PointF(($s*($r-$fold)), ($s*$t))),
    (New-Object System.Drawing.PointF(($s*$r),         ($s*($t+$fold)))),
    (New-Object System.Drawing.PointF(($s*$r),         ($s*$b))),
    (New-Object System.Drawing.PointF(($s*$l),         ($s*$b)))
  ))
  $g.FillPath((New-Object System.Drawing.SolidBrush($MARK)), $page); $page.Dispose()

  Draw-Keyhole $g $s $BG 0.115 0.455 0.048 0.70

  $g.Dispose()
  return $bmp
}

# --- ICO encoding ----------------------------------------------------------

# 32bpp DIB: BITMAPINFOHEADER, bottom-up BGRA, then an empty AND mask.
function Get-DibBytes($bmp) {
  $w = $bmp.Width; $h = $bmp.Height
  $rect = New-Object System.Drawing.Rectangle(0,0,$w,$h)
  $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $stride = $data.Stride
  $buf = New-Object byte[] ($stride*$h)
  [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
  $bmp.UnlockBits($data)

  $ms = New-Object System.IO.MemoryStream
  $bw = New-Object System.IO.BinaryWriter($ms)
  $bw.Write([int]40); $bw.Write([int]$w); $bw.Write([int]($h*2))
  $bw.Write([int16]1); $bw.Write([int16]32)
  $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0)
  $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0)
  for ($y = $h-1; $y -ge 0; $y--) { $bw.Write($buf, ($y*$stride), ($w*4)) }
  $maskRow = [int][math]::Floor(($w+31)/32) * 4
  $zero = New-Object byte[] $maskRow
  for ($y = 0; $y -lt $h; $y++) { $bw.Write($zero, 0, $maskRow) }
  $bw.Flush()
  $out = $ms.ToArray(); $bw.Dispose(); return $out
}

function Get-PngBytes($bmp) {
  $ms = New-Object System.IO.MemoryStream
  $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
  $out = $ms.ToArray(); $ms.Dispose(); return $out
}

# --- macOS .iconset --------------------------------------------------------
# Written to a directory of PNGs rather than an .icns, because building the
# .icns needs `iconutil`, which only exists on macOS.
if ($IconsetPath) {
  # Unlike Windows, a macOS icon is not drawn edge to edge. Apple's grid puts
  # the rounded tile in about 80% of the canvas and leaves the rest clear, which
  # is what keeps Dock icons from touching. A full-bleed square would render
  # noticeably larger than every neighbouring app.
  $inset = 0.805

  # Every size macOS asks for. The @2x files are the same pixels as the plain
  # file of twice the dimension -- macOS picks by name, not by content.
  $macSizes = @(
    @{ Name = 'icon_16x16';       Px = 16   },
    @{ Name = 'icon_16x16@2x';    Px = 32   },
    @{ Name = 'icon_32x32';       Px = 32   },
    @{ Name = 'icon_32x32@2x';    Px = 64   },
    @{ Name = 'icon_128x128';     Px = 128  },
    @{ Name = 'icon_128x128@2x';  Px = 256  },
    @{ Name = 'icon_256x256';     Px = 256  },
    @{ Name = 'icon_256x256@2x';  Px = 512  },
    @{ Name = 'icon_512x512';     Px = 512  },
    @{ Name = 'icon_512x512@2x';  Px = 1024 }
  )

  New-Item -ItemType Directory -Force -Path $IconsetPath | Out-Null
  foreach ($m in $macSizes) {
    $canvas = [int]$m.Px
    $inner  = [int][math]::Round($canvas * $inset)

    # The variant is chosen by the canvas size so a 128px macOS icon shows the
    # same thing a 128px Windows icon does.
    $art = Draw-Icon $inner $canvas

    $bmp = New-Object System.Drawing.Bitmap($canvas,$canvas,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $off = [int][math]::Round(($canvas - $inner) / 2)
    $g.DrawImage($art, $off, $off, $inner, $inner)
    $g.Dispose(); $art.Dispose()

    $out = Join-Path $IconsetPath "$($m.Name).png"
    $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
  }
  "wrote $IconsetPath ($($macSizes.Count) PNGs)"
  if (-not $IcoPath) { return }
}

# 72 and 96 exist because Explorer at 150% DPI asks for exactly those (48 and
# 64 logical). Without them the shell resamples and the mark goes soft.
$sizes = @(16, 20, 24, 32, 48, 64, 72, 96, 128, 256)
$images = @()
foreach ($s in $sizes) {
  $bmp = Draw-Icon $s
  # 256 goes in PNG-compressed; the rest as DIB, which every shell surface
  # handles without question.
  $bytes = if ($s -ge 256) { Get-PngBytes $bmp } else { Get-DibBytes $bmp }
  $images += @{ Size = $s; Bytes = $bytes; Bitmap = $bmp }
}

$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter($ms)
$bw.Write([int16]0); $bw.Write([int16]1); $bw.Write([int16]$images.Count)
$offset = 6 + 16*$images.Count
foreach ($e in $images) {
  $dim = if ($e.Size -ge 256) { 0 } else { $e.Size }
  $bw.Write([byte]$dim); $bw.Write([byte]$dim)
  $bw.Write([byte]0); $bw.Write([byte]0)
  $bw.Write([int16]1); $bw.Write([int16]32)
  $bw.Write([int]$e.Bytes.Length); $bw.Write([int]$offset)
  $offset += $e.Bytes.Length
}
foreach ($e in $images) { $bw.Write($e.Bytes, 0, $e.Bytes.Length) }
$bw.Flush()
[System.IO.File]::WriteAllBytes($IcoPath, $ms.ToArray())
$bw.Dispose()
"wrote $IcoPath ($($images.Count) sizes, $((Get-Item $IcoPath).Length) bytes)"

# --- preview: actual size, plus an 8x nearest-neighbour blow-up ------------
if (-not $PreviewPath) {
  foreach ($e in $images) { $e.Bitmap.Dispose() }
  return
}
$cell = 280
$sheetW = $sizes.Count * $cell
$sheetH = 760
$sheet = New-Object System.Drawing.Bitmap($sheetW, $sheetH)
$sg = [System.Drawing.Graphics]::FromImage($sheet)
$sg.Clear([System.Drawing.Color]::FromArgb(255,0x9A,0x9A,0x9A))
$font = New-Object System.Drawing.Font('Consolas',16,[System.Drawing.FontStyle]::Bold)
$black = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::Black)
$sl = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255,0xF0,0xF0,0xF0))
$sd = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255,0x2B,0x2B,0x2B))
$sg.FillRectangle($sl, 0, 40, $sheetW, 150)
$sg.FillRectangle($sd, 0, 190, $sheetW, 150)

for ($i = 0; $i -lt $images.Count; $i++) {
  $e = $images[$i]; $s = $e.Size
  $sg.DrawString("${s}px", $font, $black, ($i*$cell+10), 10)
  $sg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $x = $i*$cell + [int](($cell-$s)/2)
  $sg.DrawImage($e.Bitmap, $x, (40+[int]((150-$s)/2)), $s, $s)
  $sg.DrawImage($e.Bitmap, $x, (190+[int]((150-$s)/2)), $s, $s)

  # Blow-up so individual pixels are inspectable.
  $sg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
  $sg.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
  $zoom = [math]::Min(256, $s*8)
  $sg.DrawImage($e.Bitmap, ($i*$cell + [int](($cell-$zoom)/2)), (380+[int]((256-$zoom)/2)), $zoom, $zoom)
}
$sheet.Save($PreviewPath, [System.Drawing.Imaging.ImageFormat]::Png)
$sg.Dispose(); $sheet.Dispose()
foreach ($e in $images) { $e.Bitmap.Dispose() }
"wrote $PreviewPath"
