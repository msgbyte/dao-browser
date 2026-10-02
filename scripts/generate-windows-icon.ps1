# Package the Windows-specific master without changing macOS artwork.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$brandingDirectory = Join-Path $PSScriptRoot '../branding/win'
$source = [System.Drawing.Image]::FromFile((Join-Path $brandingDirectory 'app.png'))
$sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
$images = [System.Collections.Generic.List[byte[]]]::new()
try {
    foreach ($size in $sizes) {
        $bitmap = [System.Drawing.Bitmap]::new($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $png = [System.IO.MemoryStream]::new()
        try {
            $graphics.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
            $graphics.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.DrawImage($source, [System.Drawing.Rectangle]::new(0, 0, $size, $size), 0, 0, $source.Width, $source.Height, [System.Drawing.GraphicsUnit]::Pixel)
            $bitmap.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
            $images.Add($png.ToArray())
        } finally {
            $png.Dispose()
            $graphics.Dispose()
            $bitmap.Dispose()
        }
    }
} finally {
    $source.Dispose()
}

$output = [System.IO.MemoryStream]::new()
$writer = [System.IO.BinaryWriter]::new($output)
try {
    $writer.Write([uint16]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($index = 0; $index -lt $sizes.Count; $index++) {
        $writer.Write([byte]($sizes[$index] % 256))
        $writer.Write([byte]($sizes[$index] % 256))
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]32)
        $writer.Write([uint32]$images[$index].Length)
        $writer.Write([uint32]$offset)
        $offset += $images[$index].Length
    }
    foreach ($png in $images) {
        $writer.Write($png)
    }
    [System.IO.File]::WriteAllBytes((Join-Path $brandingDirectory 'dao.ico'), $output.ToArray())
} finally {
    $writer.Dispose()
    $output.Dispose()
}
Write-Output "Generated branding/win/dao.ico at $($sizes -join ', ')px."
