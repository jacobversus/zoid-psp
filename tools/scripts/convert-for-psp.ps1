# Re-encodes a DRM-free video for ZOID's PSP player with Windows' own encoders:
#  - <Destination>.mp4: 480x272 H.264 + AAC. With ffmpeg installed: Main profile level 3.0 with B-frames (smaller,
#    sharper); without it: Windows' encoder, Baseline
#  - the soundtrack as MP3, put inside that MP4 (embed-extras.ps1): ZOID plays it through the PSP's MP3 path (on a real PSP
#    the AAC decoder gave silence for film soundtracks)
# -SoundOnly writes just the .mp3 next to an existing converted video.
param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$Destination, [switch]$SoundOnly,
      [int]$VideoKbps = 700, [double]$Fps = 23.976, [string]$Language = 'jpn')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$methods = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 }
$asOp = $methods | Where-Object { $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
$asProgress = $methods | Where-Object { $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncActionWithProgress`1' } | Select-Object -First 1
function Await($op, [Type]$t) { $task = $asOp.MakeGenericMethod($t).Invoke($null, @($op)); $task.Wait() | Out-Null; $task.Result }
[void][Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
[void][Windows.Media.Transcoding.MediaTranscoder, Windows.Media.Transcoding, ContentType = WindowsRuntime]
[void][Windows.Media.MediaProperties.MediaEncodingProfile, Windows.Media.MediaProperties, ContentType = WindowsRuntime]

$src = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync((Resolve-Path -LiteralPath $Source).Path)) ([Windows.Storage.StorageFile])
$destDir = Split-Path $Destination; New-Item -ItemType Directory -Force $destDir | Out-Null
$destDir = (Resolve-Path $destDir).Path
$dir = Await ([Windows.Storage.StorageFolder]::GetFolderFromPathAsync($destDir)) ([Windows.Storage.StorageFolder])

function Transcode([string]$leaf, $profile) {
    $dst = Await ($dir.CreateFileAsync($leaf, [Windows.Storage.CreationCollisionOption]::ReplaceExisting)) ([Windows.Storage.StorageFile])
    $tc = New-Object Windows.Media.Transcoding.MediaTranscoder
    $tc.HardwareAccelerationEnabled = $true
    $prep = Await ($tc.PrepareFileTranscodeAsync($src, $dst, $profile)) ([Windows.Media.Transcoding.PrepareTranscodeResult])
    if (-not $prep.CanTranscode) { throw "cannot transcode to $leaf : $($prep.FailureReason)" }
    $start = Get-Date; $path = Join-Path $destDir $leaf
    $task = $asProgress.MakeGenericMethod([double]).Invoke($null, @($prep.TranscodeAsync()))
    while (-not $task.Wait(30000)) { Write-Output ("{0}: {1:N0} min, {2:N1} MB" -f $leaf, ((Get-Date) - $start).TotalMinutes, ((Get-Item -LiteralPath $path).Length / 1MB)) }
    if ($task.IsFaulted) { throw $task.Exception }
    Write-Output ("{0} done in {1:N1} min: {2:N0} MB" -f $leaf, ((Get-Date) - $start).TotalMinutes, ((Get-Item -LiteralPath $path).Length / 1MB))
}

$base = [IO.Path]::GetFileNameWithoutExtension($Destination)
# ffmpeg (winget install Gyan.FFmpeg), when present, makes the picture the way Sony's PSP spec allows:
# Main profile level 3.0 with B-frames, but no weighted prediction, at most 2 reference frames and no 8x8
# transform. That plays on a real PSP; PC encodes at level 3.1 (4 references, weighted B) hang it.
$ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $ffmpeg) { $ffmpeg = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Gyan.FFmpeg*\*\bin\ffmpeg.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName }
# ZOID plays one soundtrack per video, so the language is chosen here: the first track tagged $Language
# (Japanese by default, for anime with an English dub too), or the first track when none is.
$audioMap = '0:a:0'
if ($ffmpeg) {
    $probe = Join-Path (Split-Path $ffmpeg) 'ffprobe.exe'
    $langs = @(& $probe -v error -select_streams a -show_entries stream_tags=language -of csv=p=0 (Resolve-Path -LiteralPath $Source).Path)
    for ($k = 0; $k -lt $langs.Count; $k++) { if ($langs[$k] -eq $Language) { $audioMap = "0:a:$k"; break } }
    Write-Output ("sound: track {0} of {1} ({2})" -f ([int]$audioMap.Split(':')[2] + 1), $langs.Count, $(if ($langs.Count) { $langs[[int]$audioMap.Split(':')[2]] } else { 'none' }))
    # English subtitles when the sound isn't English: text ones become "<name>.srt" (stored inside the MP4,
    # ZOID shows them); picture ones (DVD, Blu-ray) are painted onto the video, since ZOID can't read those.
    $burn = $null
    if ($langs.Count -and $langs[[int]$audioMap.Split(':')[2]] -ne 'eng') {
        $subs = @(& $probe -v error -select_streams s -show_entries stream=codec_name:stream_tags=language -of csv=p=0 (Resolve-Path -LiteralPath $Source).Path)
        for ($k = 0; $k -lt $subs.Count; $k++) {
            $codec, $lang = $subs[$k].Split(',')
            if ($lang -ne 'eng' -and $subs.Count -gt 1) { continue }
            if ($codec -in 'dvd_subtitle', 'hdmv_pgs_subtitle', 'dvb_subtitle') { $burn = $k }
            elseif (-not $SoundOnly) { & $ffmpeg -v error -y -i (Resolve-Path -LiteralPath $Source).Path -map "0:s:$k" -c:s srt (Join-Path $destDir "$base.srt") }
            Write-Output "subtitles: track $($k + 1) ($codec, $lang)$(if ($burn -ne $null) { ', painted onto the picture' })"; break
        }
    }
}
if (-not $SoundOnly -and $ffmpeg) {
    $out = Join-Path $destDir "$base.mp4"; $rate = if ([math]::Abs($Fps - 23.976) -lt 0.01) { '24000/1001' } else { "$Fps" }
    $start = Get-Date
    # The picture keeps its own shape (4:3 TV shows aren't stretched to widescreen): it is made to fit inside
    # 480x272, and ZOID centers it with black bars (Triangle zooms to fill).
    $picture = if ($burn -ne $null) { @('-filter_complex', "[0:v:0][0:s:$burn]overlay,scale=trunc(iw*sar/2)*2:ih,setsar=1,scale=480:272:force_original_aspect_ratio=decrease:force_divisible_by=16[v]", '-map', '[v]') } else { @('-map', '0:v:0', '-vf', 'scale=trunc(iw*sar/2)*2:ih,setsar=1,scale=480:272:force_original_aspect_ratio=decrease:force_divisible_by=16') }
    & $ffmpeg -v error -nostats -y -i (Resolve-Path -LiteralPath $Source).Path @picture -map "$audioMap`?" -r $rate `
        -c:v libx264 -profile:v main -level:v 3.0 -preset slow -b:v "${VideoKbps}k" -maxrate 1500k -bufsize 1500k `
        -x264-params 'ref=2:bframes=2:b-pyramid=none:weightb=0:weightp=0:8x8dct=0' -pix_fmt yuv420p `
        -c:a aac -b:a 128k -ar 48000 -ac 2 -movflags +faststart $out
    if ($LASTEXITCODE) { throw "ffmpeg failed ($LASTEXITCODE)" }
    Write-Output ("{0}.mp4 done in {1:N1} min: {2:N0} MB" -f $base, ((Get-Date) - $start).TotalMinutes, ((Get-Item -LiteralPath $out).Length / 1MB))
}
elseif (-not $SoundOnly) {
    $profile = [Windows.Media.MediaProperties.MediaEncodingProfile]::CreateMp4([Windows.Media.MediaProperties.VideoEncodingQuality]::Wvga)
    $profile.Video.Width = 480; $profile.Video.Height = 272
    $profile.Video.ProfileId = [Windows.Media.MediaProperties.H264ProfileIds]::Baseline
    $profile.Video.Bitrate = $VideoKbps * 1000
    $profile.Video.FrameRate.Numerator = [uint32][math]::Round($Fps * 1000); $profile.Video.FrameRate.Denominator = 1000
    if ([math]::Abs($Fps - 23.976) -lt 0.01) { $profile.Video.FrameRate.Numerator = 24000; $profile.Video.FrameRate.Denominator = 1001 }
    $profile.Audio = [Windows.Media.MediaProperties.AudioEncodingProperties]::CreateAac(48000, 2, 128000)
    Transcode "$base.mp4" $profile
}
$mp3 = [Windows.Media.MediaProperties.MediaEncodingProfile]::CreateMp3([Windows.Media.MediaProperties.AudioEncodingQuality]::High)
$mp3.Audio = [Windows.Media.MediaProperties.AudioEncodingProperties]::CreateMp3(44100, 2, 160000) # 44.1 kHz: the PSP's MP3 library refuses 48 kHz (0x80671302) unless an app declares SDK 3.09.05+
# Film mixes sit around -24 to -28 LUFS, leaving room for explosions; music is near -10, so films came out
# barely audible at full PSP volume. With ffmpeg the soundtrack is brought to -14 LUFS (loudnorm narrows the
# range a little, as a TV's night mode does) with peaks held under -1 dB so nothing clips.
if ($ffmpeg) {
    $mp3Out = Join-Path $destDir "$base.mp3"
    & $ffmpeg -v error -nostats -y -i (Resolve-Path -LiteralPath $Source).Path -map $audioMap -af 'loudnorm=I=-14:TP=-1:LRA=11' `
        -ar 44100 -ac 2 -c:a libmp3lame -b:a 160k $mp3Out
    if ($LASTEXITCODE) { throw "ffmpeg (sound) failed ($LASTEXITCODE)" }
    Write-Output ("{0}.mp3 done: {1:N0} MB" -f $base, ((Get-Item -LiteralPath $mp3Out).Length / 1MB))
}
else { Transcode "$base.mp3" $mp3 }
# One file: put the soundtrack (and "<name>.srt", if there is one) inside the MP4.
if (-not $SoundOnly) { & (Join-Path $PSScriptRoot 'embed-extras.ps1') -Video (Join-Path $destDir "$base.mp4") }
