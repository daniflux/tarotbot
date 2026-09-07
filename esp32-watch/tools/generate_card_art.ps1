$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$watchRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = Split-Path -Parent $watchRoot
$sourceDir = Join-Path $repoRoot "decks\rider-waite\images"
$outPath = Join-Path $watchRoot "main\card_art_generated.h"

$cardWidth = 210
$cardHeight = 344
$bitsPerCard = $cardWidth * $cardHeight
$bytesPerCard = [Math]::Ceiling($bitsPerCard / 8)

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

function Convert-ToMask([System.Drawing.Bitmap]$src) {
  $mask = New-Object byte[] $bytesPerCard
  $dst = New-Object System.Drawing.Bitmap $cardWidth, $cardHeight
  $graphics = [System.Drawing.Graphics]::FromImage($dst)
  $graphics.Clear([System.Drawing.Color]::White)
  $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
  $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

  $scale = [Math]::Min($cardWidth / $src.Width, $cardHeight / $src.Height)
  $drawW = [Math]::Round($src.Width * $scale)
  $drawH = [Math]::Round($src.Height * $scale)
  $drawX = [Math]::Floor(($cardWidth - $drawW) / 2)
  $drawY = [Math]::Floor(($cardHeight - $drawH) / 2)
  $graphics.DrawImage($src, $drawX, $drawY, $drawW, $drawH)
  $graphics.Dispose()

  for ($y = 0; $y -lt $cardHeight; $y++) {
    for ($x = 0; $x -lt $cardWidth; $x++) {
      $p = $dst.GetPixel($x, $y)
      $max = [Math]::Max($p.R, [Math]::Max($p.G, $p.B))
      $min = [Math]::Min($p.R, [Math]::Min($p.G, $p.B))
      $luma = (0.299 * $p.R) + (0.587 * $p.G) + (0.114 * $p.B)
      $deepInk = $max -lt 58
      $neutralInk = $luma -lt 92 -and ($max - $min) -lt 46
      if ($p.A -gt 32 -and ($deepInk -or $neutralInk)) {
        $bit = ($y * $cardWidth) + $x
        $mask[[Math]::Floor($bit / 8)] = $mask[[Math]::Floor($bit / 8)] -bor (1 -shl ($bit % 8))
      }
    }
  }
  $dst.Dispose()
  return $mask
}

if (!(Test-Path $sourceDir)) {
  throw "Missing Rider-Waite image folder: $sourceDir"
}

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add("#pragma once")
$lines.Add("")
$lines.Add("#include <stdint.h>")
$lines.Add("")
$lines.Add("// Generated from decks/rider-waite/images local raster cards.")
$lines.Add("// Dark pixels are kept as a 1-bit mask and rendered green by the watch firmware.")
$lines.Add("// Run esp32-watch/tools/generate_card_art.ps1 from the repo root to regenerate.")
$lines.Add("constexpr int CARD_ART_W = $cardWidth;")
$lines.Add("constexpr int CARD_ART_H = $cardHeight;")
$lines.Add("constexpr int CARD_ART_BYTES = $bytesPerCard;")
$lines.Add("")
$lines.Add("static const uint8_t CARD_ART[78][CARD_ART_BYTES] = {")

for ($i = 0; $i -lt $files.Count; $i++) {
  $path = Join-Path $sourceDir $files[$i]
  if (!(Test-Path $path)) {
    throw "Missing card image: $path"
  }
  Write-Host ("[{0:D2}/78] {1}" -f ($i + 1), $files[$i])
  $src = [System.Drawing.Bitmap]::FromFile($path)
  try {
    $mask = Convert-ToMask $src
  } finally {
    $src.Dispose()
  }
  $lines.Add("  { // $($files[$i])")
  for ($j = 0; $j -lt $mask.Length; $j += 16) {
    $take = [Math]::Min(16, $mask.Length - $j)
    $chunk = for ($k = 0; $k -lt $take; $k++) { "0x{0:x2}" -f $mask[$j + $k] }
    $suffix = if ($j + $take -lt $mask.Length) { "," } else { "" }
    $lines.Add("    " + ($chunk -join ", ") + $suffix)
  }
  $lines.Add("  }" + ($(if ($i -lt $files.Count - 1) { "," } else { "" })))
}

$lines.Add("};")
[System.IO.File]::WriteAllLines($outPath, $lines)
Write-Host "Wrote $outPath"
