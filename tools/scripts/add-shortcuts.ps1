# Behind "Add Desktop Shortcuts.bat": puts the drag-and-drop tools on the Desktop. Shortcuts keep
# pointing at this Tools folder, so keep it where it is (run this again if you move it).
$ErrorActionPreference='Stop'
$tools=Split-Path $PSScriptRoot -Parent
$desk=[Environment]::GetFolderPath('Desktop');$shell=New-Object -ComObject WScript.Shell
$items=@(
  @{Name='Convert for PSP';Icon='imageres.dll,18';Tip='Drag videos here to convert them for ZOID'},
  @{Name='Shrink Books for PSP';Icon='imageres.dll,107';Tip='Drag books here to make them smaller for ZOID'},
  @{Name='Check Video for PSP';Icon='imageres.dll,76';Tip='Drag an .mp4 here to see whether ZOID can play it'},
  @{Name='Fix PSP Names';Icon='imageres.dll,55';Tip='Rename files on the Memory Stick so the PSP can open them'})
foreach($i in $items){
  $lnk=$shell.CreateShortcut((Join-Path $desk "$($i.Name).lnk"))
  $lnk.TargetPath=Join-Path $tools "$($i.Name).bat";$lnk.WorkingDirectory=$tools;$lnk.IconLocation="$env:SystemRoot\System32\$($i.Icon)";$lnk.Description=$i.Tip
  $lnk.Save();Write-Host "Added: $($i.Name)"}
Write-Host 'Drag videos or books onto these Desktop icons. Double-click Fix PSP Names with the PSP connected.'
