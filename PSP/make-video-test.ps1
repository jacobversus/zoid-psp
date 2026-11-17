# Writes the test video for the emulator with Windows' own encoder (MediaComposition):
# 480x272 H.264 Baseline profile (no B-frames), 2 s of red then 2 s of blue, with a 440 Hz AAC soundtrack.
param([string]$Folder = (Join-Path $PSScriptRoot '..\Build\PSP-QA\emulator\memstick\VIDEO'))
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Folder | Out-Null
$Folder = (Resolve-Path $Folder).Path
$wav = Join-Path $env:TEMP 'zoid-video-tone.wav'
$rate = 44100; $n = $rate * 4
$ms = New-Object IO.MemoryStream; $w = New-Object IO.BinaryWriter $ms
$w.Write([Text.Encoding]::ASCII.GetBytes('RIFF')); $w.Write([int](36 + $n * 4)); $w.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
$w.Write([int]16); $w.Write([int16]1); $w.Write([int16]2); $w.Write([int]$rate); $w.Write([int]($rate * 4)); $w.Write([int16]4); $w.Write([int16]16)
$w.Write([Text.Encoding]::ASCII.GetBytes('data')); $w.Write([int]($n * 4))
for ($i = 0; $i -lt $n; $i++) { $v = [int16](8000 * [math]::Sin(2 * [math]::PI * 440 * $i / $rate)); $w.Write($v); $w.Write($v) }
$w.Flush(); [IO.File]::WriteAllBytes($wav, $ms.ToArray())

Add-Type -AssemblyName System.Runtime.WindowsRuntime
$methods = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 }
$asOp = $methods | Where-Object { $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
$asOpProgress = $methods | Where-Object { $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperationWithProgress`2' } | Select-Object -First 1
$asAction = $methods | Where-Object { $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' } | Select-Object -First 1
function Await($op, [Type]$t) { $task = $asOp.MakeGenericMethod($t).Invoke($null, @($op)); $task.Wait() | Out-Null; $task.Result }
[void][Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
[void][Windows.Media.Editing.MediaComposition, Windows.Media.Editing, ContentType = WindowsRuntime]
[void][Windows.Media.MediaProperties.MediaEncodingProfile, Windows.Media.MediaProperties, ContentType = WindowsRuntime]
[void][Windows.UI.Colors, Windows.UI, ContentType = WindowsRuntime]

$comp = New-Object Windows.Media.Editing.MediaComposition
# PowerShell 5 cannot see Add on WinRT lists, so call it through the .NET collection interface.
function AddTo($list, [Type]$t, $item) { [System.Collections.Generic.ICollection`1].MakeGenericType($t).GetMethod('Add').Invoke($list, @($item)) | Out-Null }
AddTo $comp.Clips ([Windows.Media.Editing.MediaClip]) ([Windows.Media.Editing.MediaClip]::CreateFromColor([Windows.UI.Color]::FromArgb(255, 255, 0, 0), [TimeSpan]::FromSeconds(2)))
AddTo $comp.Clips ([Windows.Media.Editing.MediaClip]) ([Windows.Media.Editing.MediaClip]::CreateFromColor([Windows.UI.Color]::FromArgb(255, 0, 0, 255), [TimeSpan]::FromSeconds(2)))
$src = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($wav)) ([Windows.Storage.StorageFile])
$track = Await ([Windows.Media.Editing.BackgroundAudioTrack]::CreateFromFileAsync($src)) ([Windows.Media.Editing.BackgroundAudioTrack])
AddTo $comp.BackgroundAudioTracks ([Windows.Media.Editing.BackgroundAudioTrack]) $track

$profile = [Windows.Media.MediaProperties.MediaEncodingProfile]::CreateMp4([Windows.Media.MediaProperties.VideoEncodingQuality]::Wvga)
$profile.Video.Width = 480; $profile.Video.Height = 272
$profile.Video.FrameRate.Numerator = 30; $profile.Video.FrameRate.Denominator = 1
$profile.Video.ProfileId = [Windows.Media.MediaProperties.H264ProfileIds]::Baseline
$profile.Video.Bitrate = 800000
$profile.Audio = [Windows.Media.MediaProperties.AudioEncodingProperties]::CreateAac(44100, 2, 128000)

$dir = Await ([Windows.Storage.StorageFolder]::GetFolderFromPathAsync($Folder)) ([Windows.Storage.StorageFolder])
$dst = Await ($dir.CreateFileAsync('Color Test.mp4', [Windows.Storage.CreationCollisionOption]::ReplaceExisting)) ([Windows.Storage.StorageFile])
$render = $comp.RenderToFileAsync($dst, [Windows.Media.Editing.MediaTrimmingPreference]::Precise, $profile)
$task = $asOpProgress.MakeGenericMethod([Windows.Media.Transcoding.TranscodeFailureReason], [double]).Invoke($null, @($render))
$task.Wait() | Out-Null
if ($task.Result -ne [Windows.Media.Transcoding.TranscodeFailureReason]::None) { throw "render failed: $($task.Result)" }
Remove-Item $wav
Get-Item (Join-Path $Folder 'Color Test.mp4') | Select-Object Name, Length
# Subtitles for the test video: ZOID shows "<video name>.srt".
[IO.File]::WriteAllText((Join-Path $Folder 'Color Test.srt'), "1`r`n00:00:00,100 --> 00:00:01,900`r`n<i>The red half</i>`r`n`r`n2`r`n00:00:02,000 --> 00:00:04,000`r`nThe blue half,`r`nwith a second line`r`n")
