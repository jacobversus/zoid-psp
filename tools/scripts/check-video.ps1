# Says whether an MP4 will play in ZOID: H.264 size and profile, and whether it has B-frames
# (composition offsets in ctts that vary). ZOID needs Baseline or Main profile, at most 720x480,
# and B-frames only up to level 3.0 (higher hangs a real PSP).
param([Parameter(Mandatory)][string]$Path)
$fs = [IO.File]::OpenRead((Resolve-Path -LiteralPath $Path).Path); $br = New-Object IO.BinaryReader $fs
function BE32($b, $o) { ([uint32]$b[$o] -shl 24) -bor ([uint32]$b[$o + 1] -shl 16) -bor ([uint32]$b[$o + 2] -shl 8) -bor [uint32]$b[$o + 3] }
$pos = [int64]0; $m = $null
while ($pos -lt $fs.Length) { $fs.Position = $pos; $h = $br.ReadBytes(16); $sz = [int64](BE32 $h 0); if ($sz -eq 1) { $sz = ([int64](BE32 $h 8) -shl 32) -bor (BE32 $h 12) }; if ([Text.Encoding]::ASCII.GetString($h, 4, 4) -eq 'moov') { $fs.Position = $pos; $m = $br.ReadBytes([int]$sz) }; if ($sz -lt 8) { break }; $pos += $sz }
$fs.Close()
$s = [Text.Encoding]::GetEncoding(28591).GetString($m)
$i = $s.IndexOf('avcC'); $e = $s.IndexOf('stsd') + 12
$w = ([int]$m[$e + 32] -shl 8) -bor $m[$e + 33]; $h = ([int]$m[$e + 34] -shl 8) -bor $m[$e + 35]
$c = $s.IndexOf('ctts'); $varies = $false
if ($c -ge 0) { $n = BE32 $m ($c + 8); $first = BE32 $m ($c + 16); for ($k = 1; $k -lt [math]::Min($n, 4096); $k++) { if ((BE32 $m ($c + 16 + 8 * $k)) -ne $first) { $varies = $true; break } } }
[pscustomobject]@{ File = Split-Path $Path -Leaf; Size = "${w}x$h"; Profile = @{66 = 'Baseline'; 77 = 'Main'; 100 = 'High' }[[int]$m[$i + 5]]; BFrames = $varies; Level = $m[$i + 7] / 10; PlaysInZoid = (-not ($varies -and $m[$i + 7] -gt 30) -and $w -le 720 -and $h -le 480 -and $m[$i + 5] -in 66, 77) }
