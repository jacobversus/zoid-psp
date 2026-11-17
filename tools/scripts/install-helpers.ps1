# Behind "Install Helpers.bat": the free programs some tools use, through Windows' own winget.
#   7-Zip   lets Shrink Books read .cbr (RAR) books
#   FFmpeg  makes Convert for PSP's videos smaller and sharper (it works without it, using Windows' encoder)
$ErrorActionPreference='Continue'
if(-not (Get-Command winget -ErrorAction SilentlyContinue)){Write-Host 'winget is missing. Install 7-Zip from 7-zip.org and FFmpeg from ffmpeg.org yourself.' -ForegroundColor Yellow;return}
$has7=@("$env:ProgramFiles\7-Zip\7z.exe","${env:ProgramFiles(x86)}\7-Zip\7z.exe") | Where-Object { Test-Path $_ }
$hasFF=(Get-Command ffmpeg -ErrorAction SilentlyContinue) -or (Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\Gyan.FFmpeg*\*\bin\ffmpeg.exe" -ErrorAction SilentlyContinue)
foreach($p in @(@{Name='7-Zip';Id='7zip.7zip';Has=$has7;Why='reads .cbr books'},@{Name='FFmpeg';Id='Gyan.FFmpeg';Has=$hasFF;Why='smaller, sharper videos'})){
  if($p.Has){Write-Host "$($p.Name) is already installed.";continue}
  $a=Read-Host "Install $($p.Name) ($($p.Why))? (Y/N)"
  if($a -match '^[Yy]'){winget install --id $p.Id -e --accept-package-agreements --accept-source-agreements}
}
Write-Host 'Done.'
