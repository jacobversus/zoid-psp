# Writes the M4A test song: a tone encoded to AAC by Windows' own encoder (MediaTranscoder),
# then iTunes-style title/artist tags appended inside moov.
param([string]$Folder = (Join-Path $PSScriptRoot '..\Build\PSP-QA\emulator\memstick\MUSIC\Zoid Lossless Test'))
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Folder | Out-Null
$Folder = (Resolve-Path $Folder).Path
$wav = Join-Path $env:TEMP 'zoid-m4a-src.wav'
$rate = 44100; $n = $rate * 4
$ms = New-Object IO.MemoryStream; $w = New-Object IO.BinaryWriter $ms
$w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([int](36 + $n * 4)); $w.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
$w.Write([int]16); $w.Write([int16]1); $w.Write([int16]2); $w.Write([int]$rate); $w.Write([int]($rate * 4)); $w.Write([int16]4); $w.Write([int16]16)
$w.Write([Text.Encoding]::ASCII.GetBytes('data')); $w.Write([int]($n * 4))
for ($i = 0; $i -lt $n; $i++) { $v = [int16](8000 * [math]::Sin(2 * [math]::PI * 440 * $i / $rate)); $w.Write($v); $w.Write($v) }
$w.Flush(); [IO.File]::WriteAllBytes($wav, $ms.ToArray())

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
$asTaskAction = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncActionWithProgress`1' } | Select-Object -First 1
function Await($op, [Type]$t) { $task = $asTask.MakeGenericMethod($t).Invoke($null, @($op)); $task.Wait() | Out-Null; $task.Result }
[void][Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
[void][Windows.Media.Transcoding.MediaTranscoder, Windows.Media.Transcoding, ContentType = WindowsRuntime]
[void][Windows.Media.MediaProperties.MediaEncodingProfile, Windows.Media.MediaProperties, ContentType = WindowsRuntime]
$src = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($wav)) ([Windows.Storage.StorageFile])
$dir = Await ([Windows.Storage.StorageFolder]::GetFolderFromPathAsync($Folder)) ([Windows.Storage.StorageFolder])
$dst = Await ($dir.CreateFileAsync('04 - Aac Tone.m4a', [Windows.Storage.CreationCollisionOption]::ReplaceExisting)) ([Windows.Storage.StorageFile])
$profile = [Windows.Media.MediaProperties.MediaEncodingProfile]::CreateM4a([Windows.Media.MediaProperties.AudioEncodingQuality]::Medium)
$prep = Await ((New-Object Windows.Media.Transcoding.MediaTranscoder).PrepareFileTranscodeAsync($src, $dst, $profile)) ([Windows.Media.Transcoding.PrepareTranscodeResult])
if (-not $prep.CanTranscode) { throw "cannot transcode: $($prep.FailureReason)" }
$t = $asTaskAction.MakeGenericMethod([double]).Invoke($null, @($prep.TranscodeAsync())); $t.Wait() | Out-Null
Remove-Item $wav

# Tags: append udta/meta/ilst to moov. Only safe when moov is the last box (chunk offsets unchanged).
$path = Join-Path $Folder '04 - Aac Tone.m4a'
$b = [IO.File]::ReadAllBytes($path)
function BE([int]$v) { [byte[]]@((($v -shr 24) -band 255), (($v -shr 16) -band 255), (($v -shr 8) -band 255), ($v -band 255)) }
function Box([string]$type, [byte[]]$body) { [byte[]](BE (8 + $body.Length)) + [Text.Encoding]::GetEncoding(28591).GetBytes($type) + $body }
function Item([string]$type, [string]$text) { Box $type (Box 'data' ([byte[]](0, 0, 0, 1, 0, 0, 0, 0) + [Text.Encoding]::UTF8.GetBytes($text))) }
$pos = 0; $moov = -1
while ($pos + 8 -le $b.Length) { $sz = ([int]$b[$pos] -shl 24) -bor ([int]$b[$pos + 1] -shl 16) -bor ([int]$b[$pos + 2] -shl 8) -bor $b[$pos + 3]; $ty = [Text.Encoding]::ASCII.GetString($b, $pos + 4, 4); if ($ty -eq 'moov') { $moov = $pos; $moovSize = $sz }; if ($sz -lt 8) { break }; $pos += $sz }
if ($moov -lt 0) { throw 'no moov' }
if ($moov + $moovSize -ne $b.Length) { Write-Warning 'moov is not last; tags not added'; return }
$ilst = Box 'ilst' ((Item ([char]0xA9 + 'nam') 'Aac Tone') + (Item ([char]0xA9 + 'ART') 'ZOID Test'))
$hdlr = Box 'hdlr' ([byte[]](0, 0, 0, 0, 0, 0, 0, 0) + [Text.Encoding]::ASCII.GetBytes('mdirappl') + (New-Object byte[] 9))
$udta = Box 'udta' (Box 'meta' ([byte[]](0, 0, 0, 0) + $hdlr + $ilst))
$newSize = $moovSize + $udta.Length
$out = New-Object byte[] ($b.Length + $udta.Length); [Array]::Copy($b, $out, $b.Length); [Array]::Copy($udta, 0, $out, $b.Length, $udta.Length)
[Array]::Copy((BE $newSize), 0, $out, $moov, 4)
[IO.File]::WriteAllBytes($path, $out)
Get-Item $path | Select-Object Name, Length
# The same song as .mp4: an MP4 in MUSIC plays as audio.
Copy-Item $path (Join-Path $Folder '05 - Mp4 Tone.mp4') -Force
