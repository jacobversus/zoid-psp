# Puts a video's MP3 soundtrack and subtitles inside its MP4, so a video is a single file.
# They go in top-level 'uuid' boxes at the end, which other players (the PSP's own, PC players) skip;
# ZOID plays the MP3 from there and shows the subtitles. Re-running replaces earlier ZOID boxes.
# Without -Mp3/-Srt it looks for "<video>.mp3" and "<video>.srt" beside the video, and deletes
# them once they are inside (-Keep leaves them).
param([Parameter(Mandatory)][string]$Video, [string]$Mp3, [string]$Srt, [switch]$Keep)
$ErrorActionPreference = 'Stop'
$Video = (Resolve-Path -LiteralPath $Video).Path
if (-not $Mp3) { $Mp3 = [IO.Path]::ChangeExtension($Video, '.mp3') }
if (-not $Srt) { $Srt = [IO.Path]::ChangeExtension($Video, '.srt') }
$uuids = @{ Sound = 'ZOID-MP3-SOUND01'; Subs = 'ZOID-SRT-SUBS-01' }

function BE32([byte[]]$b, [int]$o) { ([uint32]$b[$o] -shl 24) -bor ([uint32]$b[$o + 1] -shl 16) -bor ([uint32]$b[$o + 2] -shl 8) -bor [uint32]$b[$o + 3] }
# cut off any ZOID boxes already at the end
$fs = [IO.File]::Open($Video, 'Open', 'ReadWrite'); $h = New-Object byte[] 32; $pos = [int64]0; $cut = -1
while ($pos + 8 -le $fs.Length) {
    $fs.Position = $pos; [void]$fs.Read($h, 0, 32)
    $sz = [int64](BE32 $h 0); $hdr = 8; if ($sz -eq 1) { $sz = ([int64](BE32 $h 8) -shl 32) -bor (BE32 $h 12); $hdr = 16 } elseif ($sz -eq 0) { $sz = $fs.Length - $pos }
    if ($sz -lt 8) { break }
    $type = [Text.Encoding]::ASCII.GetString($h, 4, 4); $id = [Text.Encoding]::ASCII.GetString($h, $hdr, 16)
    if ($type -eq 'uuid' -and $id -like 'ZOID-*') { if ($cut -lt 0) { $cut = $pos } } else { $cut = -1 }
    $pos += $sz
}
if ($cut -ge 0) { $fs.SetLength($cut) }
function Append([string]$uuid, [string]$file) {
    $len = (Get-Item -LiteralPath $file).Length
    if (8 + 16 + $len -gt [uint32]::MaxValue) { throw "$file is too large to embed" }
    $size = [uint32](8 + 16 + $len)
    $fs.Position = $fs.Length
    $hb = [byte[]]@((($size -shr 24) -band 255), (($size -shr 16) -band 255), (($size -shr 8) -band 255), ($size -band 255)) + [Text.Encoding]::ASCII.GetBytes('uuid' + $uuid)
    $fs.Write($hb, 0, $hb.Length)
    $in = [IO.File]::OpenRead($file); $in.CopyTo($fs); $in.Close()
    "embedded {0} ({1:N1} MB)" -f (Split-Path $file -Leaf), ($len / 1MB)
}
$done = @()
if (Test-Path -LiteralPath $Mp3) { Append $uuids.Sound $Mp3; $done += $Mp3 }
if (Test-Path -LiteralPath $Srt) { Append $uuids.Subs $Srt; $done += $Srt }
$fs.Close()
if (-not $Keep) { foreach ($f in $done) { Remove-Item -LiteralPath $f; "removed $(Split-Path $f -Leaf)" } }
"{0}: {1:N1} MB" -f (Split-Path $Video -Leaf), ((Get-Item -LiteralPath $Video).Length / 1MB)
