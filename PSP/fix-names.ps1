# Renames files and folders on the PSP's Memory Stick so every name is plain ASCII.
# A real PSP can't read names with other letters: it cuts "Ranma ½" off at the ½ (it hands back
# "Ranma "), so the file or folder can never be opened again and ZOID can't show it. Run this after
# copying anything onto the stick:   fix-names.ps1 -Drive E:      (-WhatIf lists the changes only)
#   accents lose their marks (Glühwürmchen -> Gluhwurmchen), curly quotes become ', ½ becomes 1-2,
#   symbols with no plain form (❤ ▲ ♀ ♨) are dropped. A renamed folder in BOOKS keeps the name it should show,
#   with ½ as 1/2, in zoid-title.txt, which ZOID's Books shows instead of the folder's own name.
# Bookmarks, places in books, resume and playlists that point at a renamed item are updated too.
param([string]$Drive='E:',[string[]]$Folders=@('BOOKS','MUSIC','VIDEO','PODCASTS'),[switch]$WhatIf)
$ErrorActionPreference='Stop'
$root=$Drive.TrimEnd('\')+'\'
function Plain([string]$name) {
  $s=$name
  foreach($kv in @(@([char]0x2019,"'"),@([char]0x2018,"'"),@([char]0x00B4,"'"),@([char]0x0060,"'"),@([char]0x201C,''),@([char]0x201D,''),
                   @([char]0x2013,'-'),@([char]0x2014,'-'),@([char]0x2026,'...'),@([char]0x00D7,'x'),@([char]0x00DF,'ss'),
                   @([char]0x042F,'R'),@([char]0x044F,'r'),
                   @([char]0x00C6,'AE'),@([char]0x00E6,'ae'),@([char]0x0152,'OE'),@([char]0x0153,'oe'),@([char]0x00D8,'O'),@([char]0x00F8,'o'),@([char]0x0141,'L'),@([char]0x0142,'l'))){$s=$s.Replace([string]$kv[0],$kv[1])}
  $s=$s.Normalize([Text.NormalizationForm]::FormKD)          # ½ -> 1⁄2, ³ -> 3, é -> e + accent
  $s=$s.Replace([string][char]0x2044,'-')                    # the fraction slash: "/" can't be in a name
  $b=New-Object Text.StringBuilder
  foreach($c in $s.ToCharArray()){
    if([Globalization.CharUnicodeInfo]::GetUnicodeCategory($c) -eq 'NonSpacingMark'){continue}
    if([int]$c -ge 0x20 -and [int]$c -le 0x7E){[void]$b.Append($c)}
  }
  $s=$b.ToString() -replace '\(\s*\)','' -replace '\[\s*\]','' -replace '\s{2,}',' '
  $s=$s -replace '\s+(\.[A-Za-z0-9]+)$','$1'                 # "Book ❤.zip" -> "Book.zip", not "Book .zip"
  $s=$s.Trim().TrimStart('.',' ')                            # a leading dot would hide it, like macOS junk
  if(-not $s){$s='Renamed'}
  return $s
}
# how a renamed folder should read on the PSP: its own name, with fractions as 1/2
function Shown([string]$name){
  $s=[regex]::Replace($name,'[¼-¾⅐-⅞]',{param($m)' '+($m.Value.Normalize([Text.NormalizationForm]::FormKD)).Replace([string][char]0x2044,'/')})
  $s=$s.Replace([string][char]0x2044,'/')
  $s=[regex]::Replace($s,'[\u0000-\u001F\u007F-\u009F]','')   # invisible control characters
  return ($s -replace '\s{2,}',' ').Trim()
}

# the plan: every item with a name to change, its final path worked out from the top down
$plan=New-Object Collections.Generic.List[object]
$final=@{}
function Walk([string]$dir,[string]$finalDir) {
  foreach($e in Get-ChildItem -LiteralPath $dir -Force | Sort-Object Name){
    if($e.Name.StartsWith('._')){continue}                   # macOS copies follow their file below
    $new=$e.Name
    if($e.Name -match '[^\x20-\x7E]'){$new=Plain $e.Name}
    $target=Join-Path $finalDir $new
    if($new -ne $e.Name){
      $n=2;$baseNew=$new;while((Test-Path -LiteralPath (Join-Path $dir $new)) -or $final.ContainsValue($target)){$ext=[IO.Path]::GetExtension($baseNew);if($e.PSIsContainer){$ext=''};$stem=if($ext){$baseNew.Substring(0,$baseNew.Length-$ext.Length)}else{$baseNew};$new="$stem ($n)$ext";$target=Join-Path $finalDir $new;$n++}
      $plan.Add([pscustomobject]@{Path=$e.FullName;Dir=$dir;Old=$e.Name;New=$new;IsDir=$e.PSIsContainer;Depth=($e.FullName.Split('\').Count);Final=$target})
    }
    $final[$e.FullName]=$target
    if($e.PSIsContainer){Walk $e.FullName $target}
  }
}
foreach($f in $Folders){$p=Join-Path $root $f;if(Test-Path -LiteralPath $p){Walk $p $p}}
if(-not $plan.Count){'Every name is already plain ASCII.';return}
$plan | ForEach-Object { '{0}  ->  {1}' -f $_.Path.Substring($root.Length),$_.New }
if($WhatIf){"$($plan.Count) to rename (nothing changed: -WhatIf)";return}

# deepest first, so each parent's path still holds while its children are renamed
foreach($p in ($plan | Sort-Object Depth -Descending)){
  $junk=Join-Path $p.Dir ('._'+$p.Old)
  if(Test-Path -LiteralPath $junk){Rename-Item -LiteralPath $junk -NewName ('._'+$p.New)}
  Rename-Item -LiteralPath $p.Path -NewName $p.New
  if($p.IsDir -and $p.Path.StartsWith((Join-Path $root 'BOOKS\'))){$t=Shown $p.Old;if($t -ne $p.New){[IO.File]::WriteAllText((Join-Path $p.Dir (Join-Path $p.New 'zoid-title.txt')),$t,(New-Object Text.UTF8Encoding $false))}}
}
# references: ZOID's own files and playlists hold ms0:/ paths
$swap=$final.GetEnumerator() | Where-Object { $_.Key -ne $_.Value } | ForEach-Object {
  @{Old='ms0:/'+$_.Key.Substring($root.Length).Replace('\','/');New='ms0:/'+$_.Value.Substring($root.Length).Replace('\','/')}
} | Sort-Object { $_.Old.Length } -Descending
$refs=@(Get-ChildItem -LiteralPath (Join-Path $root 'PSP\GAME\ZOID') -Filter 'zoid-*.txt' -ErrorAction SilentlyContinue)
$refs+=@(Get-ChildItem -LiteralPath (Join-Path $root 'MUSIC') -Recurse -File -Include '*.m3u','*.m3u8' -ErrorAction SilentlyContinue)
foreach($r in $refs){
  $t=[IO.File]::ReadAllText($r.FullName);$o=$t
  foreach($s in $swap){$t=$t.Replace($s.Old,$s.New)}
  if($t -ne $o){[IO.File]::WriteAllText($r.FullName,$t,(New-Object Text.UTF8Encoding $false));"updated $($r.Name)"}
}
"$($plan.Count) renamed"
