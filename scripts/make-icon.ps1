# Draws ZOID's XMB art and logo (needs the Bahnschrift font, part of Windows 10 and 11):
#   ICON0.PNG  144x80   the icon in the XMB's Game menu: a dark tile, lime equalizer bars, "ZOID."
#   PIC1.PNG   480x272  the XMB background while ZOID is highlighted: dark, a lime glow, faint bars
#   logo.png   864x480  the same icon at 6x, for the README and posts (-Logo <path>)
# Written to assets\. build.ps1 packs ICON0.PNG and PIC1.PNG into EBOOT.PBP. The PNGs are committed, so a build doesn't need the font.
param([string]$OutDir=(Join-Path $PSScriptRoot '..\assets'),[string]$Logo)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$LIME='c8f000';$FG='f2f2f4'
function Col($hex,$a=255){[Drawing.Color]::FromArgb($a,[Convert]::ToInt32($hex.Substring(0,2),16),[Convert]::ToInt32($hex.Substring(2,2),16),[Convert]::ToInt32($hex.Substring(4,2),16))}
function RRect([float]$x,[float]$y,[float]$w,[float]$h,[float]$r){$p=[Drawing.Drawing2D.GraphicsPath]::new();$d=2*$r;$p.AddArc($x,$y,$d,$d,180,90);$p.AddArc($x+$w-$d,$y,$d,$d,270,90);$p.AddArc($x+$w-$d,$y+$h-$d,$d,$d,0,90);$p.AddArc($x,$y+$h-$d,$d,$d,90,90);$p.CloseFigure();$p}
$fam=[Drawing.FontFamily]::new('Bahnschrift')
if($fam.Name -ne 'Bahnschrift'){throw 'The Bahnschrift font is missing'}
# "ZOID" centered on (cx,cy); Winding keeps Bahnschrift's overlapping outlines solid
function WordPath([float]$size,[float]$cx,[float]$cy){
  $p=[Drawing.Drawing2D.GraphicsPath]::new([Drawing.Drawing2D.FillMode]::Winding)
  $p.AddString('ZOID',$fam,[int][Drawing.FontStyle]::Bold,$size,[Drawing.PointF]::new(0,0),[Drawing.StringFormat]::GenericTypographic)
  $b=$p.GetBounds();$m=[Drawing.Drawing2D.Matrix]::new();$m.Translate($cx-$b.Width/2-$b.X,$cy-$b.Height/2-$b.Y);$p.Transform($m);$p}
function Canvas($w,$h){$bmp=[Drawing.Bitmap]::new($w,$h,[Drawing.Imaging.PixelFormat]::Format32bppArgb);$g=[Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode='AntiAlias';$g.PixelOffsetMode='HighQuality';$g.CompositingQuality='HighQuality';$g.Clear([Drawing.Color]::Transparent);@($bmp,$g)}
# the icon, drawn in 144x80 units scaled by $s
function Icon([float]$s){
  $c=Canvas ([int](144*$s)) ([int](80*$s));$g=$c[1];$g.ScaleTransform($s,$s)
  $tile=RRect 1 1 141 77 12
  $g.FillPath([Drawing.Drawing2D.LinearGradientBrush]::new([Drawing.PointF]::new(0,0),[Drawing.PointF]::new(0,80),(Col '2e2e36'),(Col '0d0d0f')),$tile)
  $g.DrawPath([Drawing.Pen]::new((Col 'ffffff' 40),1),$tile)
  $lime=[Drawing.SolidBrush]::new((Col $LIME))
  $hs=14,26,38,22,32;for($i=0;$i -lt 5;$i++){$g.FillPath($lime,(RRect (14+$i*7) (58-$hs[$i]) 5 $hs[$i] 2))}
  $w=WordPath 32 95 40;$b=$w.GetBounds()
  $g.FillPath([Drawing.SolidBrush]::new((Col $FG)),$w)
  $r=2.7;$g.FillEllipse($lime,$b.Right+2.2,$b.Bottom-2*$r,2*$r,2*$r)
  $g.Dispose();$c[0]}
function Save($bmp,$path){$bmp=@($bmp | Where-Object { $_ -is [Drawing.Bitmap] })[-1];$bmp.Save($path,[Drawing.Imaging.ImageFormat]::Png);$bmp.Dispose();Get-Item $path}

Save (Icon 1) (Join-Path $OutDir 'ICON0.PNG')

# PIC1: the XMB draws the icon column and title on the left, so the art sits low and right
# Background pixel by pixel: a top-to-bottom gradient plus a Gaussian lime glow, dithered so it
# doesn't band on the PSP's 16-bit screen (GDI+ gradient brushes band and ring here)
$c=Canvas 480 272;$g=$c[1]
$px=[byte[]]::new(480*272*4);$rnd=[Random]::new(7)
for($y=0;$y -lt 272;$y++){$t=$y/271.0;$br=0x1d+(0x08-0x1d)*$t;$bg=0x1d+(0x08-0x1d)*$t;$bb=0x22+(0x0a-0x22)*$t
  for($x=0;$x -lt 480;$x++){$dx=($x-380)/260.0;$dy=($y-300)/170.0;$a=0.30*[math]::Exp(-2.2*($dx*$dx+$dy*$dy));$n=$rnd.NextDouble()*2-1
    $i=($y*480+$x)*4
    $px[$i]=[byte][math]::Min(255,[math]::Max(0,[math]::Round($bb*(1-$a)+0x00*$a+$n)))
    $px[$i+1]=[byte][math]::Min(255,[math]::Max(0,[math]::Round($bg*(1-$a)+0xf0*$a+$n)))
    $px[$i+2]=[byte][math]::Min(255,[math]::Max(0,[math]::Round($br*(1-$a)+0xc8*$a+$n)))
    $px[$i+3]=255}}
$bd=$c[0].LockBits([Drawing.Rectangle]::new(0,0,480,272),'WriteOnly',[Drawing.Imaging.PixelFormat]::Format32bppArgb)
[Runtime.InteropServices.Marshal]::Copy($px,0,$bd.Scan0,$px.Length);$c[0].UnlockBits($bd)
$bar=[Drawing.SolidBrush]::new((Col $LIME 38));$hs=30,52,78,64,96,120,88,70,104,58,82,46,66,38,24
for($i=0;$i -lt $hs.Count;$i++){$g.FillPath($bar,(RRect (232+$i*16) (262-$hs[$i]) 10 $hs[$i] 3))}
$g.FillRectangle([Drawing.SolidBrush]::new((Col $LIME 70)),0,268,480,1)
$g.Dispose();Save $c[0] (Join-Path $OutDir 'PIC1.PNG')

if($Logo){Save (Icon 6) $Logo}
