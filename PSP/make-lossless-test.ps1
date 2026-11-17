# Writes the lossless test songs for the emulator: a tagged FLAC (verbatim frames, no encoder
# needed), a 96 kHz FLAC (checks down-conversion) and a WAV with a LIST/INFO title.
param([string]$Folder = (Join-Path $PSScriptRoot '..\Build\PSP-QA\emulator\memstick\MUSIC\Zoid Lossless Test'))
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Folder | Out-Null

function Tone([int]$rate, [double]$seconds) {
    $n = [int]($rate * $seconds); $s = New-Object 'int16[]' ($n * 2)
    for ($i = 0; $i -lt $n; $i++) { $v = [int16](8000 * [math]::Sin(2 * [math]::PI * 440 * $i / $rate)); $s[2 * $i] = $v; $s[2 * $i + 1] = $v }
    return , $s
}
function Crc8([byte[]]$b, [int]$len) { $c = 0; for ($i = 0; $i -lt $len; $i++) { $c = $c -bxor $b[$i]; for ($k = 0; $k -lt 8; $k++) { $c = if ($c -band 0x80) { (($c -shl 1) -bxor 0x07) -band 0xFF } else { ($c -shl 1) -band 0xFF } } }; return $c }
function Crc16([System.Collections.Generic.List[byte]]$b) { $c = 0; foreach ($x in $b) { $c = $c -bxor ([int]$x -shl 8); for ($k = 0; $k -lt 8; $k++) { $c = if ($c -band 0x8000) { (($c -shl 1) -bxor 0x8005) -band 0xFFFF } else { ($c -shl 1) -band 0xFFFF } } }; return $c }

function Write-Flac([string]$path, [int]$rate, [double]$seconds, [string]$title, [string]$artist) {
    $pcm = Tone $rate $seconds; $total = $pcm.Length / 2; $bs = 4096
    $out = New-Object System.Collections.Generic.List[byte]
    $out.AddRange([Text.Encoding]::ASCII.GetBytes('fLaC'))
    # STREAMINFO (not last)
    $out.AddRange([byte[]](0x00, 0, 0, 34)); $out.AddRange([byte[]](0x10, 0x00, 0x10, 0x00, 0, 0, 0, 0, 0, 0))
    $bits = ([uint64]$rate -shl 44) -bor ([uint64]1 -shl 41) -bor ([uint64]15 -shl 36) -bor [uint64]$total
    for ($i = 7; $i -ge 0; $i--) { $out.Add([byte](($bits -shr (8 * $i)) -band 0xFF)) }
    $out.AddRange((New-Object byte[] 16))
    # VORBIS_COMMENT (last)
    $vc = New-Object System.Collections.Generic.List[byte]
    $add = { param($s) $b = [Text.Encoding]::UTF8.GetBytes($s); $vc.AddRange([BitConverter]::GetBytes([int]$b.Length)); $vc.AddRange($b) }
    & $add 'zoid-test'; $vc.AddRange([BitConverter]::GetBytes([int]2)); & $add "TITLE=$title"; & $add "ARTIST=$artist"
    $out.AddRange([byte[]](0x84, 0, (($vc.Count -shr 8) -band 0xFF), ($vc.Count -band 0xFF))); $out.AddRange($vc)
    $rateCode = if ($rate -eq 44100) { 9 } elseif ($rate -eq 96000) { 11 } else { throw 'rate' }
    $frame = 0
    for ($start = 0; $start -lt $total; $start += $bs) {
        $n = [math]::Min($bs, $total - $start)
        $f = New-Object System.Collections.Generic.List[byte]
        $f.AddRange([byte[]](0xFF, 0xF8))
        $bcode = if ($n -eq 4096) { 12 } else { 7 }
        $f.Add([byte](($bcode -shl 4) -bor $rateCode)); $f.Add([byte]((1 -shl 4) -bor (4 -shl 1)))
        if ($frame -ge 128) { throw 'frame number too large for this simple writer' }
        $f.Add([byte]$frame)
        if ($bcode -eq 7) { $f.Add([byte](($n - 1) -shr 8)); $f.Add([byte](($n - 1) -band 0xFF)) }
        $h = $f.ToArray(); $f.Add([byte](Crc8 $h $h.Length))
        for ($ch = 0; $ch -lt 2; $ch++) {
            $f.Add(0x02)
            for ($i = 0; $i -lt $n; $i++) { $v = [uint16]([int]$pcm[2 * ($start + $i) + $ch] -band 0xFFFF); $f.Add([byte]($v -shr 8)); $f.Add([byte]($v -band 0xFF)) }
        }
        $c = Crc16 $f; $f.Add([byte]($c -shr 8)); $f.Add([byte]($c -band 0xFF))
        $out.AddRange($f); $frame++
    }
    [IO.File]::WriteAllBytes($path, $out.ToArray())
}

function Write-Wav([string]$path, [int]$rate, [double]$seconds, [string]$title) {
    $pcm = Tone $rate $seconds; $data = New-Object byte[] ($pcm.Length * 2); [Buffer]::BlockCopy($pcm, 0, $data, 0, $data.Length)
    $t = [Text.Encoding]::ASCII.GetBytes($title + "`0"); if ($t.Length % 2) { $t += [byte]0 }
    $ms = New-Object IO.MemoryStream; $w = New-Object IO.BinaryWriter $ms
    $list = 4 + 8 + $t.Length
    $w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([int](4 + 24 + 8 + $list + 8 + $data.Length)); $w.Write([Text.Encoding]::ASCII.GetBytes('WAVE'))
    $w.Write([Text.Encoding]::ASCII.GetBytes('fmt ')); $w.Write([int]16); $w.Write([int16]1); $w.Write([int16]2); $w.Write([int]$rate); $w.Write([int]($rate * 4)); $w.Write([int16]4); $w.Write([int16]16)
    $w.Write([Text.Encoding]::ASCII.GetBytes('LIST')); $w.Write([int]$list); $w.Write([Text.Encoding]::ASCII.GetBytes('INFO')); $w.Write([Text.Encoding]::ASCII.GetBytes('INAM')); $w.Write([int]$t.Length); $w.Write($t)
    $w.Write([Text.Encoding]::ASCII.GetBytes('data')); $w.Write([int]$data.Length); $w.Write($data)
    $w.Flush(); [IO.File]::WriteAllBytes($path, $ms.ToArray())
}

Write-Flac (Join-Path $Folder '01 - Flac Tone.flac') 44100 4 'Lossless Tone' 'ZOID Test'
Write-Flac (Join-Path $Folder '02 - HiRes Tone.flac') 96000 3 'HiRes Tone' 'ZOID Test'
Write-Wav (Join-Path $Folder '03 - Wave Tone.wav') 44100 3 'Wave Tone'
Get-ChildItem $Folder | Select-Object Name, Length
