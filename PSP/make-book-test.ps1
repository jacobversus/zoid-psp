# Test books for run-test.ps1 -Kind Books: trimmed copies of real volumes, so the test covers real
# page sizes and encodings without copying a gigabyte. -Source is a folder laid out like the
# Gunsmith Cats collection it was written for (any volumes with these encodings will do):
#   <Source>\Gunsmith Cats - Burst\Gunsmith Cats - Burst 01 ...cbz   progressive JPEG, ends with a two-page spread
#   <Source>\Gunsmith Cats - Burst\Gunsmith Cats - Burst 04 ...cbz   PNG pages
#   <Source>\Gunsmith Cats - Revised Edition 03 ...cbz              baseline JPEG
#   <Source>\Gunsmith Cats - Revised Edition 04 ...cbz              pages for a folder of images
# -Dest is the emulator's BOOKS folder; it is replaced.
# -Progressive (optional): a volume of large progressive JPEGs (Do You Like Big Girls v02 was the case): its
#   largest page and one ordinary page become Library/Chapters/Chapter 02.cbz, the memory test.
param([Parameter(Mandatory)][string]$Source,[Parameter(Mandatory)][string]$Dest,[string]$Progressive)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
function Find-Volume([string]$dir,[string]$pattern) {
  $f=Get-ChildItem -LiteralPath $dir -Filter '*.cbz' | Where-Object { -not $_.Name.StartsWith('._') -and $_.Name -like $pattern } | Select-Object -First 1
  if(-not $f){throw "No volume like '$pattern' in $dir"};$f
}
function Get-Pages($zip){@($zip.Entries | Where-Object { $_.Length -gt 0 -and $_.Name -match '\.(jpe?g|png)$' -and -not $_.Name.StartsWith('._') } | Sort-Object FullName)}
# copies the chosen pages into a new .cbz, compressed (deflate) like the originals
function Copy-Trimmed([string]$from,[string]$to,[scriptblock]$pick) {
  $src=[IO.Compression.ZipFile]::OpenRead($from)
  try {
    $pages=Get-Pages $src;$chosen=& $pick $pages
    $out=[IO.Compression.ZipFile]::Open($to,'Create')
    try {
      foreach($e in $chosen){$n=$out.CreateEntry($e.FullName,[IO.Compression.CompressionLevel]::Optimal);$w=$n.Open();$r=$e.Open();$r.CopyTo($w);$r.Close();$w.Close()}
    } finally {$out.Dispose()}
  } finally {$src.Dispose()}
}
if(Test-Path -LiteralPath $Dest){Remove-Item -LiteralPath $Dest -Recurse -Force}
$series=Join-Path $Dest 'Gunsmith Cats - Burst'
New-Item -ItemType Directory -Force $series | Out-Null
$burst=Join-Path $Source 'Gunsmith Cats - Burst'
if(-not(Test-Path -LiteralPath $burst)){$burst=Join-Path (Split-Path $Source) 'Gunsmith Cats - Burst'} # beside it, as BOOKS on the stick has it
$b1=Find-Volume $burst '*Burst 01*';$b4=Find-Volume $burst '*Burst 04*'
$r3=Find-Volume $Source '*Revised Edition 03*';$r4=Find-Volume $Source '*Revised Edition 04*'
# Burst 01: 12 pages and the spread at the end
Copy-Trimmed $b1.FullName (Join-Path $series $b1.Name) {param($p) @($p[0..11])+@($p[$p.Count-1])}
Copy-Trimmed $b4.FullName (Join-Path $series $b4.Name) {param($p) @($p[0..7])}
Copy-Trimmed $r3.FullName (Join-Path $Dest $r3.Name) {param($p) @($p[0..5])}
# a folder of 8 images, with the junk a Mac leaves behind
$fb=Join-Path $Dest 'Folder Book';New-Item -ItemType Directory -Force $fb | Out-Null
$z=[IO.Compression.ZipFile]::OpenRead($r4.FullName)
try{$i=0;foreach($e in (Get-Pages $z)[10..17]){$i++;$t=Join-Path $fb ('page {0}.jpg' -f $i);$r=$e.Open();$w=[IO.File]::Create($t);$r.CopyTo($w);$w.Close();$r.Close()}}finally{$z.Dispose()}
[IO.File]::WriteAllBytes((Join-Path $fb '._page 1.jpg'),[byte[]](0,5,22,7,0,2,0,0))
[IO.File]::WriteAllBytes((Join-Path $fb '.DS_Store'),[byte[]](0,0,0,1))
[IO.File]::WriteAllBytes((Join-Path $Dest ('._'+$r3.Name)),[byte[]](0,5,22,7,0,2,0,0))
# folders inside folders, as a file explorer shows them: a .zip book two folders down, a folder of
# pictures beside it, and an empty folder
$lib=Join-Path $Dest 'Library';$ch=Join-Path $lib 'Chapters';$cv=Join-Path $lib 'Covers'
New-Item -ItemType Directory -Force $ch,$cv,(Join-Path $lib 'Empty') | Out-Null
Copy-Trimmed $r3.FullName (Join-Path $ch 'Chapter 01.zip') {param($p) @($p[0..3])}
$z=[IO.Compression.ZipFile]::OpenRead($r4.FullName)
try{$i=0;foreach($e in (Get-Pages $z)[0..1]){$i++;$t=Join-Path $cv ('cover {0}.jpg' -f $i);$r=$e.Open();$w=[IO.File]::Create($t);$r.CopyTo($w);$w.Close();$r.Close()}}finally{$z.Dispose()}
if($Progressive){Copy-Trimmed $Progressive (Join-Path $ch 'Chapter 02.cbz') {param($p) $big=$p | Sort-Object Length -Descending | Select-Object -First 1;@($p[5],$big)}}
Get-ChildItem -LiteralPath $Dest -Recurse -File | ForEach-Object { '{0,8:N0} KB  {1}' -f ($_.Length/1KB),$_.FullName.Substring($Dest.Length+1) }
