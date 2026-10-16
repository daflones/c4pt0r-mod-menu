Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap 256, 256
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = 'AntiAlias'
$g.TextRenderingHint = 'AntiAlias'
$g.Clear([System.Drawing.Color]::FromArgb(0, 0, 0, 0))
$path = New-Object System.Drawing.Drawing2D.GraphicsPath
$r = 48
$path.AddArc(8, 8, $r * 2, $r * 2, 180, 90)
$path.AddArc(192, 8, $r * 2, $r * 2, 270, 90)
$path.AddArc(192, 192, $r * 2, $r * 2, 0, 90)
$path.AddArc(8, 192, $r * 2, $r * 2, 90, 90)
$path.CloseFigure()
$rect = New-Object System.Drawing.Rectangle 0, 0, 256, 256
$bg = New-Object System.Drawing.Drawing2D.LinearGradientBrush $rect, ([System.Drawing.Color]::FromArgb(20, 22, 48)), ([System.Drawing.Color]::FromArgb(7, 8, 18)), 90
$g.FillPath($bg, $path)
$pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(0, 212, 255)), 6
$g.DrawPath($pen, $path)
$f = New-Object System.Drawing.Font('Segoe UI', 96, [System.Drawing.FontStyle]::Bold)
$sf = New-Object System.Drawing.StringFormat
$sf.Alignment = 'Center'
$sf.LineAlignment = 'Center'
$tb = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(0, 212, 255))
$g.DrawString('C4', $f, $tb, (New-Object System.Drawing.RectangleF 0, 0, 256, 256), $sf)
$bmp.Save('icon.png', [System.Drawing.Imaging.ImageFormat]::Png)
$png = [System.IO.File]::ReadAllBytes('icon.png')
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]1)
$bw.Write([byte]0); $bw.Write([byte]0); $bw.Write([byte]0); $bw.Write([byte]0)
$bw.Write([uint16]1); $bw.Write([uint16]32)
$bw.Write([uint32]$png.Length); $bw.Write([uint32]22)
$bw.Write($png); $bw.Flush()
[System.IO.File]::WriteAllBytes('icon.ico', $ms.ToArray())
Remove-Item icon.png
$g.Dispose(); $bmp.Dispose()
Write-Output "icon.ico criado"
