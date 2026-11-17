# Builds the public release zip: the normal EBOOT (never a test build), the user readme,
# license notices and the Windows tools. Output: Release\ZOID-PSP-<Version>.zip beside PSP.
param([string]$Version='1.0',[switch]$NoBuild)
$ErrorActionPreference='Stop'
if(-not $NoBuild){& (Join-Path $PSScriptRoot 'build.ps1') *> $null}
$eboot=Join-Path $PSScriptRoot 'EBOOT.PBP'
# only a test build (ZOID_SMOKE_TEST) writes the smoke.txt report; refuse to ship one
$text=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($eboot))
if($text -match 'smoke\.txt'){throw 'EBOOT.PBP is a test build. Run build.ps1 with no switches.'}
if($text -notmatch [regex]::Escape("ZOID $Version")){throw "EBOOT.PBP does not say ZOID $Version"}
$out=Join-Path $PSScriptRoot '..\Release';New-Item -ItemType Directory -Force $out | Out-Null
$stage=Join-Path $out "ZOID-PSP-$Version"
if(Test-Path $stage){Remove-Item -Recurse -Force $stage}
New-Item -ItemType Directory -Force (Join-Path $stage 'PSP\GAME\ZOID'),(Join-Path $stage 'Tools'),(Join-Path $stage 'LICENSES') | Out-Null
Copy-Item $eboot (Join-Path $stage 'PSP\GAME\ZOID\EBOOT.PBP')
# the kernel helper loaded from beside the EBOOT: system volume bar and the music-note theme button
Copy-Item (Join-Path $PSScriptRoot 'zoidvol.prx') (Join-Path $stage 'PSP\GAME\ZOID\zoidvol.prx')
Copy-Item (Join-Path $PSScriptRoot 'release-readme.txt') (Join-Path $stage 'README.txt')
Copy-Item (Join-Path $PSScriptRoot 'third_party\bearssl\LICENSE.txt') (Join-Path $stage 'LICENSES\BearSSL.txt')
# Tools: double-click / drag-and-drop launchers (companion\*.bat) on top, the scripts they run in Tools\scripts
New-Item -ItemType Directory -Force (Join-Path $stage 'Tools\scripts') | Out-Null
Get-ChildItem (Join-Path $PSScriptRoot 'companion') -Filter '*.bat' | Copy-Item -Destination (Join-Path $stage 'Tools')
Get-ChildItem (Join-Path $PSScriptRoot 'companion\scripts') -Filter '*.ps1' | Copy-Item -Destination (Join-Path $stage 'Tools\scripts')
foreach($t in 'fix-names.ps1','shrink-books.ps1','shrink-dropped.ps1','convert-for-psp.ps1','convert-dropped.ps1','embed-extras.ps1','extract-subtitles.ps1','check-video.ps1'){Copy-Item (Join-Path $PSScriptRoot $t) (Join-Path $stage "Tools\scripts\$t")}
Copy-Item (Join-Path $PSScriptRoot 'HandBrake-PSP.json') (Join-Path $stage 'Tools\PSP (ZOID) HandBrake presets.json')
$zip=Join-Path $out "ZOID-PSP-$Version.zip"
if(Test-Path $zip){Remove-Item $zip}
# Compress-Archive in Windows PowerShell 5.1 writes '\' in entry names, which Mac and Linux unzip
# keep as part of the file name, so the entries are written by hand with '/'
Add-Type -AssemblyName System.IO.Compression,System.IO.Compression.FileSystem
$root=(Resolve-Path $stage).Path.TrimEnd('\')+'\'
$archive=[IO.Compression.ZipFile]::Open($zip,'Create')
try{foreach($f in Get-ChildItem $stage -Recurse -File){[void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$f.FullName,$f.FullName.Substring($root.Length).Replace('\','/'),'Optimal')}}
finally{$archive.Dispose()}
Get-Item $zip
