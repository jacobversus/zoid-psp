# Builds the public release zip: the normal EBOOT (never a test build), the user readme,
# license notices and the Windows tools (tools/, as they are). Output: out\ZOID-PSP-<Version>.zip.
param([string]$Version='1.0',[switch]$NoBuild)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not $NoBuild){& (Join-Path $PSScriptRoot 'build.ps1') *> $null}
$eboot=Join-Path $repo 'out\EBOOT.PBP'
# only a test build (ZOID_SMOKE_TEST) writes the smoke.txt report; refuse to ship one
$text=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($eboot))
if($text -match 'smoke\.txt'){throw 'EBOOT.PBP is a test build. Run build.ps1 with no switches.'}
if($text -notmatch [regex]::Escape("ZOID $Version")){throw "EBOOT.PBP does not say ZOID $Version"}
$out=Join-Path $repo 'out'
$stage=Join-Path $out "ZOID-PSP-$Version"
if(Test-Path $stage){Remove-Item -Recurse -Force $stage}
New-Item -ItemType Directory -Force (Join-Path $stage 'PSP\GAME\ZOID'),(Join-Path $stage 'LICENSES') | Out-Null
Copy-Item $eboot (Join-Path $stage 'PSP\GAME\ZOID\EBOOT.PBP')
# the kernel helper loaded from beside the EBOOT: system volume bar and the music-note theme button
Copy-Item (Join-Path $repo 'out\zoidvol.prx') (Join-Path $stage 'PSP\GAME\ZOID\zoidvol.prx')
Copy-Item (Join-Path $repo 'docs\release-readme.txt') (Join-Path $stage 'README.txt')
Copy-Item (Join-Path $repo 'src\third_party\bearssl\LICENSE.txt') (Join-Path $stage 'LICENSES\BearSSL.txt')
# Tools: the launchers, the scripts they run (Tools\scripts) and the HandBrake presets, laid out as in tools/
Copy-Item (Join-Path $repo 'tools') (Join-Path $stage 'Tools') -Recurse
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
