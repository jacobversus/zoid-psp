# Writes the text subtitle tracks (tx3g) of an MP4 / M4V as .srt files, which ZOID shows under a
# video with the same name. Only plain-text tracks are read; nothing encrypted is touched.
param([Parameter(Mandatory)][string]$Source, [string]$OutBase, [switch]$List)
$ErrorActionPreference = 'Stop'
$fs = [IO.File]::OpenRead((Resolve-Path -LiteralPath $Source).Path); $br = New-Object IO.BinaryReader $fs
function BE32($b, $o) { ([uint32]$b[$o] -shl 24) -bor ([uint32]$b[$o + 1] -shl 16) -bor ([uint32]$b[$o + 2] -shl 8) -bor [uint32]$b[$o + 3] }
function BE16($b, $o) { ([int]$b[$o] -shl 8) -bor [int]$b[$o + 1] }
function Boxes($b, [int]$start, [int]$end) {
    $list = @(); $p = $start
    while ($p + 8 -le $end) { $sz = [int](BE32 $b $p); if ($sz -lt 8 -or $p + $sz -gt $end) { break }; $list += [pscustomobject]@{Type = [Text.Encoding]::GetEncoding(28591).GetString($b, $p + 4, 4); Start = $p + 8; End = $p + $sz }; $p += $sz }
    return $list
}
function Child($b, $box, $type) { Boxes $b $box.Start $box.End | Where-Object Type -eq $type | Select-Object -First 1 }
# moov
$pos = [int64]0; $moov = $null
while ($pos -lt $fs.Length) { $fs.Position = $pos; $h = $br.ReadBytes(16); $sz = [int64](BE32 $h 0); if ($sz -eq 1) { $sz = ([int64](BE32 $h 8) -shl 32) -bor (BE32 $h 12) }; if ([Text.Encoding]::ASCII.GetString($h, 4, 4) -eq 'moov') { $fs.Position = $pos; $moov = $br.ReadBytes([int]$sz) }; if ($sz -lt 8) { break }; $pos += $sz }
if (-not $moov) { throw 'no moov box' }
$found = 0
foreach ($trak in (Boxes $moov 8 $moov.Length | Where-Object Type -eq 'trak')) {
    $mdia = Child $moov $trak 'mdia'; $mdhd = Child $moov $mdia 'mdhd'; $minf = Child $moov $mdia 'minf'; $stbl = Child $moov $minf 'stbl'; $stsd = Child $moov $stbl 'stsd'
    if (-not $stsd) { continue }
    $fmt = [Text.Encoding]::ASCII.GetString($moov, $stsd.Start + 12, 4)
    if ($fmt -notin 'tx3g', 'svtt', 'wvtt') { continue } # plain-text formats only; drmt (protected) is skipped
    $v = $moov[$mdhd.Start]; if ($v -eq 1) { $ts = BE32 $moov ($mdhd.Start + 20); $langAt = $mdhd.Start + 32 } else { $ts = BE32 $moov ($mdhd.Start + 12); $langAt = $mdhd.Start + 20 }
    $lc = BE16 $moov $langAt; $lang = -join ([char](0x60 + (($lc -shr 10) -band 31)), [char](0x60 + (($lc -shr 5) -band 31)), [char](0x60 + ($lc -band 31)))
    $tkhd = Child $moov $trak 'tkhd'; $enabled = ($moov[$tkhd.Start + 3] -band 1) -eq 1
    # sample table
    $stsz = Child $moov $stbl 'stsz'; $fixed = BE32 $moov ($stsz.Start + 4); $n = [int](BE32 $moov ($stsz.Start + 8))
    $sizes = New-Object int[] $n; for ($i = 0; $i -lt $n; $i++) { $sizes[$i] = if ($fixed) { $fixed } else { [int](BE32 $moov ($stsz.Start + 12 + 4 * $i)) } }
    $stco = Child $moov $stbl 'stco'; $wide = $false; if (-not $stco) { $stco = Child $moov $stbl 'co64'; $wide = $true }
    $chunks = [int](BE32 $moov ($stco.Start + 4)); $co = New-Object int64[] $chunks
    for ($i = 0; $i -lt $chunks; $i++) { $co[$i] = if ($wide) { ([int64](BE32 $moov ($stco.Start + 8 + 8 * $i)) -shl 32) -bor (BE32 $moov ($stco.Start + 12 + 8 * $i)) } else { [int64](BE32 $moov ($stco.Start + 8 + 4 * $i)) } }
    $stsc = Child $moov $stbl 'stsc'; $ents = [int](BE32 $moov ($stsc.Start + 4)); $offs = New-Object int64[] $n; $s = 0
    for ($e = 0; $e -lt $ents -and $s -lt $n; $e++) {
        $first = [int](BE32 $moov ($stsc.Start + 8 + 12 * $e)) - 1; $per = [int](BE32 $moov ($stsc.Start + 12 + 12 * $e)); $last = if ($e + 1 -lt $ents) { [int](BE32 $moov ($stsc.Start + 8 + 12 * ($e + 1))) - 1 } else { $chunks }
        for ($c = $first; $c -lt $last -and $s -lt $n; $c++) { $p = $co[$c]; for ($k = 0; $k -lt $per -and $s -lt $n; $k++) { $offs[$s] = $p; $p += $sizes[$s]; $s++ } }
    }
    $stts = Child $moov $stbl 'stts'; $te = [int](BE32 $moov ($stts.Start + 4)); $starts = New-Object int64[] ($n + 1); $s = 0; $t = [int64]0
    for ($e = 0; $e -lt $te; $e++) { $cnt = [int](BE32 $moov ($stts.Start + 8 + 8 * $e)); $d = [int64](BE32 $moov ($stts.Start + 12 + 8 * $e)); for ($k = 0; $k -lt $cnt -and $s -lt $n; $k++) { $starts[$s] = $t; $t += $d; $s++ } }
    $starts[$n] = $t
    $found++
    $cues = New-Object System.Collections.Generic.List[string]; $num = 0
    function Stamp([int64]$ms) { $hh = [int64][math]::Floor($ms / 3600000); $mm = [int64][math]::Floor($ms / 60000) % 60; $ss = [int64][math]::Floor($ms / 1000) % 60; $ff = $ms % 1000; '{0:00}:{1:00}:{2:00},{3:000}' -f @($hh, $mm, $ss, $ff) }
    for ($i = 0; $i -lt $n; $i++) {
        if ($sizes[$i] -lt 2) { continue }
        $fs.Position = $offs[$i]; $raw = $br.ReadBytes($sizes[$i]); $text = ''
        if ($fmt -eq 'tx3g') { $tl = BE16 $raw 0; if ($tl -gt 0 -and $tl + 2 -le $raw.Length) { $text = [Text.Encoding]::UTF8.GetString($raw, 2, $tl) } }
        else { # WebVTT in MP4: vttc cue boxes, each with a payl (text) box; vtte is an empty gap
            $lines = @(); foreach ($cue in (Boxes $raw 0 $raw.Length | Where-Object Type -eq 'vttc')) { $pl = Boxes $raw $cue.Start $cue.End | Where-Object Type -eq 'payl' | Select-Object -First 1; if ($pl) { $lines += [Text.Encoding]::UTF8.GetString($raw, $pl.Start, $pl.End - $pl.Start) } }
            $text = $lines -join "`n" }
        $text = [regex]::Replace($text.Replace("`r", ''), '<[^>]+>', '').Trim(); if (-not $text) { continue }
        $a = [int64]($starts[$i] * 1000 / $ts); $b = [int64]($starts[$i + 1] * 1000 / $ts); $num++
        $cues.Add("$num`r`n$(Stamp $a) --> $(Stamp $b)`r`n$($text.Replace("`n","`r`n"))`r`n")
    }
    "track $found : $fmt language $lang, $num subtitles, default=$enabled" + $(if ($num) { ", first: " + ($cues[[math]::Min(5, $num - 1)] -split "`r`n")[2] } else { "" })
    if (-not $List -and $OutBase) {
        $out = if ($found -eq 1) { "$OutBase.srt" } else { "$OutBase.$lang.$found.srt" }
        [IO.File]::WriteAllText($out, ($cues -join "`r`n"), (New-Object Text.UTF8Encoding $false)); "  wrote $out"
    }
}
$fs.Close()
if (-not $found) { 'no text subtitle tracks (tx3g) in this file' }
