# Behind the "Convert for PSP" desktop icon: every video dropped on it (or inside a dropped folder)
# goes through convert-for-psp.ps1 into Desktop\PSP Videos, keeping the folder layout.
# Copy that folder's contents into VIDEO on the PSP.
param([Parameter(ValueFromRemainingArguments)][string[]]$Paths)
$out = Join-Path ([Environment]::GetFolderPath('Desktop')) 'PSP Videos'
$kinds = '.mkv', '.mp4', '.m4v', '.avi', '.mov', '.wmv', '.webm'
$jobs = @(foreach ($p in $Paths) {
    $item = Get-Item -LiteralPath $p
    if ($item.PSIsContainer) {
        Get-ChildItem -LiteralPath $item.FullName -Recurse -File | Where-Object { $_.Extension -in $kinds } | Sort-Object FullName |
            ForEach-Object { [pscustomobject]@{ File = $_; Folder = Join-Path (Join-Path $out $item.Name) $_.DirectoryName.Substring($item.FullName.Length) } }
    }
    elseif ($item.Extension -in $kinds) { [pscustomobject]@{ File = $item; Folder = $out } }
})
if (-not $jobs) { Write-Host 'No videos found in what was dropped.'; Read-Host 'Press Enter to close'; return }
$n = 0; $failed = @()
foreach ($j in $jobs) {
    $n++; $dest = Join-Path $j.Folder ($j.File.BaseName + '.mp4')
    if (Test-Path -LiteralPath $dest) { Write-Host "[$n/$($jobs.Count)] already done: $($j.File.Name)"; continue }
    Write-Host "[$n/$($jobs.Count)] $($j.File.Name)"
    try { & (Join-Path $PSScriptRoot 'convert-for-psp.ps1') -Source $j.File.FullName -Destination $dest | ForEach-Object { Write-Host "   $_" } }
    catch { Write-Host "   FAILED: $_" -ForegroundColor Red; $failed += $j.File.Name; Remove-Item -LiteralPath $dest -ErrorAction SilentlyContinue }
}
Write-Host ''
Write-Host "Done. Your videos are in: $out" -ForegroundColor Green
Write-Host 'Copy what is inside that folder into the VIDEO folder on your PSP.'
if ($failed) { Write-Host "These didn't convert: $($failed -join ', ')" -ForegroundColor Red }
Read-Host 'Press Enter to close'
