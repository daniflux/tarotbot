$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $repoRoot "decks\rider-waite\images"
$outDir = Join-Path $repoRoot "decks\emoji-matrix\images"

$outputWidth = 520
$outputHeight = 880
$green = [System.Drawing.Color]::FromArgb(255, 0, 255, 65)
$black = [System.Drawing.Color]::FromArgb(255, 0, 0, 0)

$files = @(
  "major_arcana_fool.png",
  "major_arcana_magician.png",
  "major_arcana_priestess.png",
  "major_arcana_empress.png",
  "major_arcana_emperor.png",
  "major_arcana_hierophant.png",
  "major_arcana_lovers.png",
  "major_arcana_chariot.png",
  "major_arcana_strength.png",
  "major_arcana_hermit.png",
  "major_arcana_fortune.png",
  "major_arcana_justice.png",
  "major_arcana_hanged.png",
  "major_arcana_death.png",
  "major_arcana_temperance.png",
  "major_arcana_devil.png",
  "major_arcana_tower.png",
  "major_arcana_star.png",
  "major_arcana_moon.png",
  "major_arcana_sun.png",
  "major_arcana_judgement.png",
  "major_arcana_world.png",
  "minor_arcana_wands_ace.png",
  "minor_arcana_wands_2.png",
  "minor_arcana_wands_3.png",
  "minor_arcana_wands_4.png",
  "minor_arcana_wands_5.png",
  "minor_arcana_wands_6.png",
  "minor_arcana_wands_7.png",
  "minor_arcana_wands_8.png",
  "minor_arcana_wands_9.png",
  "minor_arcana_wands_10.png",
  "minor_arcana_wands_page.png",
  "minor_arcana_wands_knight.png",
  "minor_arcana_wands_queen.png",
  "minor_arcana_wands_king.png",
  "minor_arcana_cups_ace.png",
  "minor_arcana_cups_2.png",
  "minor_arcana_cups_3.png",
  "minor_arcana_cups_4.png",
  "minor_arcana_cups_5.png",
  "minor_arcana_cups_6.png",
  "minor_arcana_cups_7.png",
  "minor_arcana_cups_8.png",
  "minor_arcana_cups_9.png",
  "minor_arcana_cups_10.png",
  "minor_arcana_cups_page.png",
  "minor_arcana_cups_knight.png",
  "minor_arcana_cups_queen.png",
  "minor_arcana_cups_king.png",
  "minor_arcana_swords_ace.png",
  "minor_arcana_swords_2.png",
  "minor_arcana_swords_3.png",
  "minor_arcana_swords_4.png",
  "minor_arcana_swords_5.png",
  "minor_arcana_swords_6.png",
  "minor_arcana_swords_7.png",
  "minor_arcana_swords_8.png",
  "minor_arcana_swords_9.png",
  "minor_arcana_swords_10.png",
  "minor_arcana_swords_page.png",
  "minor_arcana_swords_knight.png",
  "minor_arcana_swords_queen.png",
  "minor_arcana_swords_king.png",
  "minor_arcana_pentacles_ace.png",
  "minor_arcana_pentacles_2.png",
  "minor_arcana_pentacles_3.png",
  "minor_arcana_pentacles_4.png",
  "minor_arcana_pentacles_5.png",
  "minor_arcana_pentacles_6.png",
  "minor_arcana_pentacles_7.png",
  "minor_arcana_pentacles_8.png",
  "minor_arcana_pentacles_9.png",
  "minor_arcana_pentacles_10.png",
  "minor_arcana_pentacles_page.png",
  "minor_arcana_pentacles_knight.png",
  "minor_arcana_pentacles_queen.png",
  "minor_arcana_pentacles_king.png"
)

if (!(Test-Path $sourceDir)) {
  throw "Missing Rider-Waite image folder: $sourceDir"
}
if (!(Test-Path $outDir)) {
  New-Item -ItemType Directory -Path $outDir | Out-Null
}

function Test-InkPixel([System.Drawing.Color]$p) {
  $max = [Math]::Max($p.R, [Math]::Max($p.G, $p.B))
  $min = [Math]::Min($p.R, [Math]::Min($p.G, $p.B))
  $luma = (0.299 * $p.R) + (0.587 * $p.G) + (0.114 * $p.B)
  $deepInk = $max -lt 58
  $neutralInk = $luma -lt 92 -and ($max - $min) -lt 46
  return $p.A -gt 32 -and ($deepInk -or $neutralInk)
}

function Convert-ToMatrixImage([System.Drawing.Bitmap]$src) {
  $sample = New-Object System.Drawing.Bitmap $outputWidth, $outputHeight, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $graphics = [System.Drawing.Graphics]::FromImage($sample)
  $graphics.Clear([System.Drawing.Color]::White)
  $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
  $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

  $scale = [Math]::Min($outputWidth / $src.Width, $outputHeight / $src.Height)
  $drawW = [Math]::Round($src.Width * $scale)
  $drawH = [Math]::Round($src.Height * $scale)
  $drawX = [Math]::Floor(($outputWidth - $drawW) / 2)
  $drawY = [Math]::Floor(($outputHeight - $drawH) / 2)
  $graphics.DrawImage($src, $drawX, $drawY, $drawW, $drawH)
  $graphics.Dispose()

  $rect = New-Object System.Drawing.Rectangle 0, 0, $outputWidth, $outputHeight
  $sampleData = $sample.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $bytes = [Math]::Abs($sampleData.Stride) * $sampleData.Height
  $sourceBytes = New-Object byte[] $bytes
  [System.Runtime.InteropServices.Marshal]::Copy($sampleData.Scan0, $sourceBytes, 0, $bytes)
  $sample.UnlockBits($sampleData)

  $matrix = New-Object System.Drawing.Bitmap $outputWidth, $outputHeight, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $matrixData = $matrix.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
  $targetBytes = New-Object byte[] $bytes

  for ($y = 0; $y -lt $outputHeight; $y++) {
    $row = $y * $sampleData.Stride
    for ($x = 0; $x -lt $outputWidth; $x++) {
      $offset = $row + ($x * 4)
      $b = $sourceBytes[$offset]
      $g = $sourceBytes[$offset + 1]
      $r = $sourceBytes[$offset + 2]
      $a = $sourceBytes[$offset + 3]
      $max = [Math]::Max($r, [Math]::Max($g, $b))
      $min = [Math]::Min($r, [Math]::Min($g, $b))
      $luma = (0.299 * $r) + (0.587 * $g) + (0.114 * $b)
      $deepInk = $max -lt 58
      $neutralInk = $luma -lt 92 -and ($max - $min) -lt 46
      $isInk = $a -gt 32 -and ($deepInk -or $neutralInk)
      $targetBytes[$offset] = 0
      $targetBytes[$offset + 1] = $(if ($isInk) { 255 } else { 0 })
      $targetBytes[$offset + 2] = 0
      $targetBytes[$offset + 3] = 255
    }
  }
  [System.Runtime.InteropServices.Marshal]::Copy($targetBytes, 0, $matrixData.Scan0, $bytes)
  $matrix.UnlockBits($matrixData)
  $sample.Dispose()
  return $matrix
}

for ($i = 0; $i -lt $files.Count; $i++) {
  $name = $files[$i]
  $source = Join-Path $sourceDir $name
  $target = Join-Path $outDir $name
  if (!(Test-Path $source)) {
    throw "Missing source image: $source"
  }
  Write-Host ("[{0:00}/{1}] {2}" -f ($i + 1), $files.Count, $name)
  $src = [System.Drawing.Bitmap]::FromFile($source)
  $matrix = Convert-ToMatrixImage $src
  $matrix.Save($target, [System.Drawing.Imaging.ImageFormat]::Png)
  $matrix.Dispose()
  $src.Dispose()
}

Write-Host "Wrote $outDir"
