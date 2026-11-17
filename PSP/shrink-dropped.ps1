# Behind the "Shrink Books for PSP" desktop icon: every book dropped on it (.cbz/.zip, or a folder of
# them or of page pictures) goes through shrink-books.ps1 into Desktop\PSP Books, keeping the folder
# layout. The originals are not touched. Copy that folder's contents into BOOKS on the PSP.
# Drop the Memory Stick's own BOOKS folder on it to shrink the books already on the PSP, in place.
param([Parameter(ValueFromRemainingArguments)][string[]]$Paths)
$out = Join-Path ([Environment]::GetFolderPath('Desktop')) 'PSP Books'
$script = Join-Path $PSScriptRoot 'shrink-books.ps1'
foreach ($p in $Paths) {
    $item = Get-Item -LiteralPath $p
    $onStick = $item.PSIsContainer -and $item.Name -eq 'BOOKS' -and $item.Parent -and $item.Parent.FullName -eq $item.Root.FullName
    if ($onStick) {
        Write-Host "This is the PSP's BOOKS folder ($($item.FullName))." -ForegroundColor Yellow
        Write-Host 'Its books will be made smaller where they are (each one is replaced only after its smaller copy checks out).'
        if ((Read-Host 'Type Y and press Enter to go ahead') -ne 'Y') { Write-Host 'Skipped.'; continue }
        & $script -Source $item.FullName -InPlace
    }
    elseif ($item.PSIsContainer) { & $script -Source $item.FullName -Dest (Join-Path $out $item.Name) }
    elseif ($item.Extension -in '.cbz', '.zip') { & $script -Source $item.FullName -Dest $out }
    else { Write-Host "Not a book: $($item.Name)" }
}
Write-Host ''
Write-Host "Done. New copies (if any) are in: $out" -ForegroundColor Green
Write-Host 'Copy what is inside that folder into the BOOKS folder on your PSP, then run Fix PSP Names (fix-names.ps1).'
Read-Host 'Press Enter to close'
