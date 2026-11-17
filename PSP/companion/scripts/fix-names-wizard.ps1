# Behind "Fix PSP Names.bat": finds the Memory Stick (a drive with PSP\GAME), lists the names a PSP
# can't open, and renames them after a Y. fix-names.ps1 does the work.
$ErrorActionPreference='Stop'
$drives=@(Get-PSDrive -PSProvider FileSystem | Where-Object { Test-Path (Join-Path $_.Root 'PSP\GAME') } | ForEach-Object { $_.Root.TrimEnd('\') })
if(-not $drives.Count){Write-Host 'No Memory Stick found. Connect the PSP in USB mode (or put the stick in a card reader) and run this again.' -ForegroundColor Yellow;return}
$drive=$drives[0]
if($drives.Count -gt 1){$pick=Read-Host "Memory Sticks found on $($drives -join ', '). Which drive letter?";$drive=$pick.TrimEnd(':','\')+':';if($drives -notcontains $drive){Write-Host "$drive has no PSP folder." -ForegroundColor Yellow;return}}
Write-Host "Checking the names on $drive (BOOKS, MUSIC, VIDEO, PODCASTS)..."
$fix=Join-Path $PSScriptRoot 'fix-names.ps1'
$list=@(& $fix -Drive $drive -WhatIf)
$list | ForEach-Object { Write-Host $_ }
if($list.Count -le 1 -and "$list" -match 'already plain'){return}
$answer=Read-Host 'Rename these so the PSP can open them? Bookmarks and playlists are updated to match. (Y/N)'
if($answer -notmatch '^[Yy]'){Write-Host 'Nothing changed.';return}
& $fix -Drive $drive | ForEach-Object { Write-Host $_ }
