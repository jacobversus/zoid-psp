param([ValidateSet('Queue','Transport','Movie','Net','Books')][string]$Kind='Transport',[string]$MovieFile,[switch]$Online,[string]$BookSource,[string]$BookProgressive) # -BookSource (Books only): a comic collection to cut test books from (see make-book-test.ps1); without it the books from the last run are used; -BookProgressive: a volume of large progressive JPEGs for the memory test # -Online (Net only): also real HTTPS to lrclib.net and SomaFM
$ErrorActionPreference='Stop'
$qa=(Resolve-Path (Join-Path $PSScriptRoot '..\Build\PSP-QA')).Path
$exe=Join-Path $qa 'emulator\PPSSPPWindows64.exe'
# A real PSP has its system fonts in flash0:/font. The emulator's flash0 for games is
# memstick\PSP\NAND\flash0, which starts empty, so its own free stand-in fonts are copied there.
$fontDir=Join-Path $qa 'emulator\memstick\PSP\NAND\flash0\font'
New-Item -ItemType Directory -Force $fontDir | Out-Null
foreach($pgf in 'ltn0.pgf','jpn0.pgf','kr0.pgf'){$dst=Join-Path $fontDir $pgf;if(-not(Test-Path $dst)){Copy-Item (Join-Path $qa "emulator\assets\flash0\font\$pgf") $dst}}
Get-Process PPSSPPWindows64 -ErrorAction SilentlyContinue | Where-Object {$_.Path -eq $exe} | Stop-Process
$ini=Join-Path $qa 'emulator\memstick\PSP\SYSTEM\ppsspp.ini'
if(Test-Path $ini){$settings=Get-Content $ini -Raw}else{$settings="[Sound]`nEnable = True`n"}
foreach($key in @('GlobalVolume','GameVolume','AchievementVolume','UIVolume','GamePreviewVolume','ReverbRelativeVolume')) {
    if($settings -match ('(?m)^'+$key+'\s*=')){$settings=[regex]::Replace($settings,'(?m)^'+$key+'\s*=.*$',$key+' = 0')}
    else{$settings=$settings.Replace('[Sound]',"[Sound]`n$key = 0")}
}
# Software rendering writes frames to emulated VRAM, so screen captures are exact.
if($settings -match '(?m)^SoftwareRenderer\s*='){$settings=[regex]::Replace($settings,'(?m)^SoftwareRenderer\s*=.*$','SoftwareRenderer = True')}else{$settings=$settings.Replace('[Graphics]',"[Graphics]`nSoftwareRenderer = True")}
# Emulated Wi-Fi only for the network test, which talks to net-test-server.cs on 127.0.0.1 (never the internet)
$wlan=if($Kind -eq 'Net'){'True'}else{'False'}
if($settings -match '(?m)^EnableWlan\s*='){$settings=[regex]::Replace($settings,'(?m)^EnableWlan\s*=.*$','EnableWlan = '+$wlan)}
elseif($settings -match '\[Network\]'){$settings=$settings.Replace('[Network]',"[Network]`nEnableWlan = $wlan")}
else{$settings+="`n[Network]`nEnableWlan = $wlan`n"}
# -Online: a public DNS server for real host names (the emulator's default one is for game servers)
if($Online){foreach($kv in @(@('PrimaryDNSServer','1.1.1.1'),@('InfrastructureAutoDNS','False'))){$settings=[regex]::Replace($settings,'(?m)^'+$kv[0]+'\s*=.*$',$kv[0]+' = '+$kv[1])}}
Get-ChildItem (Join-Path $qa 'emulator\memstick') -Filter '*.rgba' | Remove-Item
Set-Content $ini $settings
$controlConfig=Join-Path $qa 'emulator\memstick\PSP\SYSTEM\controls.ini'
if(Test-Path $controlConfig){$keys=Get-Content $controlConfig -Raw;if($keys){$keys=[regex]::Replace($keys,'(?m)^([^=\r\n]+)=.*$','$1=');Set-Content $controlConfig $keys}}
$flags=@{}
if($Kind -eq 'Queue'){$flags.QueueSmoke=$true;$marker='queue complete:'}elseif($Kind -eq 'Movie'){$flags.MovieSmoke=$true;$marker='movie complete:'}elseif($Kind -eq 'Net'){$flags.NetSmoke=$true;$marker='net complete:'}elseif($Kind -eq 'Books'){$flags.BooksSmoke=$true;$marker='books complete:'}else{$flags.TransportSmoke=$true;$marker='transport complete:'}
# Books: trimmed real volumes in BOOKS on the emulator's stick
if($Kind -eq 'Books'){$bd=Join-Path $qa 'emulator\memstick\BOOKS';if($BookSource){& (Join-Path $PSScriptRoot 'make-book-test.ps1') -Source $BookSource -Dest $bd -Progressive $BookProgressive}elseif(-not(Test-Path (Join-Path $bd 'Folder Book'))){throw 'Books test: pass -BookSource the first time'}}
# Movie: a real film is played from MOVIETEST on the emulator's stick (outside VIDEO, which the Queue test counts)
if($Kind -eq 'Movie'){$md=Join-Path $qa 'emulator\memstick\MOVIETEST';New-Item -ItemType Directory -Force $md | Out-Null;if($MovieFile){Copy-Item -LiteralPath $MovieFile (Join-Path $md 'movie.mp4') -Force;$srt=[IO.Path]::ChangeExtension($MovieFile,'.srt');if(Test-Path -LiteralPath $srt){Copy-Item -LiteralPath $srt (Join-Path $md 'movie.srt') -Force}}}
& (Join-Path $PSScriptRoot 'build.ps1') @flags
# Net: test streams made with ffmpeg (30 s of MP3 and of AAC), a tagged song with no lyrics
# (NETTEST, outside MUSIC so the other tests' song counts stay the same), and the stand-in server
$server=$null
if($Kind -eq 'Net') {
    $nd=Join-Path $qa 'net-test';New-Item -ItemType Directory -Force $nd | Out-Null
    $ff=(Get-Command ffmpeg -ErrorAction Stop).Source
    if(-not(Test-Path (Join-Path $nd 'stream.mp3'))){& $ff -v error -y -f lavfi -i 'sine=frequency=330:duration=30' -f lavfi -i 'sine=frequency=495:duration=30' -filter_complex amerge=inputs=2 -ar 44100 -b:a 128k -c:a libmp3lame (Join-Path $nd 'stream.mp3')}
    if(-not(Test-Path (Join-Path $nd 'stream.aac'))){& $ff -v error -y -f lavfi -i 'sine=frequency=262:duration=30' -ac 2 -ar 44100 -c:a aac -b:a 64k -f adts (Join-Path $nd 'stream.aac')}
    # podcasts: a cover, a 30 s MP3 episode whose ID3 tag holds that cover (and a title the feed overrides), a 12 s M4A episode
    if(-not(Test-Path (Join-Path $nd 'art.jpg'))){& $ff -v error -y -f lavfi -i 'color=c=0xc8f000:s=300x300' -frames:v 1 (Join-Path $nd 'art.jpg')}
    if(-not(Test-Path (Join-Path $nd 'ep.mp3'))){& $ff -v error -y -f lavfi -i 'sine=frequency=550:duration=30' -i (Join-Path $nd 'art.jpg') -map 0:a -map 1:v -ac 2 -ar 44100 -c:a libmp3lame -b:a 128k -c:v copy -disposition:v attached_pic -id3v2_version 3 -metadata title='Wrong Tag Title' (Join-Path $nd 'ep.mp3')}
    if(-not(Test-Path (Join-Path $nd 'ep.m4a'))){& $ff -v error -y -f lavfi -i 'sine=frequency=660:duration=12' -ac 2 -ar 44100 -c:a aac -b:a 96k -movflags +faststart (Join-Path $nd 'ep.m4a')}
    $songDir=Join-Path $qa 'emulator\memstick\NETTEST';New-Item -ItemType Directory -Force $songDir | Out-Null
    if(-not(Test-Path (Join-Path $songDir 'Net Song.mp3'))){& $ff -v error -y -f lavfi -i 'sine=frequency=440:duration=20' -ac 2 -ar 44100 -b:a 128k -c:a libmp3lame -id3v2_version 3 -metadata artist='Net Artist' -metadata title='Net Song' -metadata album='Net Album' (Join-Path $songDir 'Net Song.mp3')}
    Remove-Item (Join-Path $songDir 'Net Song.lrc') -ErrorAction SilentlyContinue
    $flag=Join-Path $songDir 'online.txt';if($Online){Set-Content $flag 'online'}else{Remove-Item $flag -ErrorAction SilentlyContinue}
    $server=Start-Process -FilePath 'dotnet' -ArgumentList ('run "'+(Join-Path $PSScriptRoot 'net-test-server.cs')+'" -- "'+$nd+'"') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $qa 'net-server.log')
    $up=(Get-Date).AddSeconds(120);$ok=$false
    do{Start-Sleep -Milliseconds 500;try{$t=New-Object Net.Sockets.TcpClient('127.0.0.1',8088);$t.Close();$ok=$true}catch{$ok=$false}}while(-not $ok -and (Get-Date) -lt $up)
    if(-not $ok){throw 'The network test server did not start'}
}
$candidate=Join-Path $qa ('v2-'+$Kind+'.PBP')
Copy-Item (Join-Path $PSScriptRoot 'EBOOT.PBP') $candidate
$report=Join-Path $qa 'emulator\memstick\smoke.txt';Set-Content $report ''
$arguments='"'+$candidate+'" "--log='+(Join-Path $qa 'v2-emulator.log')+'"'
$emulator=Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
try {
    $deadline=(Get-Date).AddSeconds($(if($Online){480}elseif($Kind -eq 'Movie' -or $Kind -eq 'Net' -or $Kind -eq 'Books'){240}else{120}))
    do {Start-Sleep -Milliseconds 500;$lines=Get-Content $report}while((Get-Date) -lt $deadline -and -not($lines -match $marker))
} finally {
    if(-not $emulator.HasExited){Stop-Process -Id $emulator.Id}
    if($server -and -not $server.HasExited){Stop-Process -Id $server.Id;Get-CimInstance Win32_Process -Filter "ParentProcessId=$($server.Id)" -ErrorAction SilentlyContinue | ForEach-Object {Stop-Process -Id $_.ProcessId -ErrorAction SilentlyContinue}}
}
Copy-Item $report (Join-Path $qa ('v2-'+$Kind.ToLower()+'-report.txt'))
if(($lines -match '^FAIL:') -or -not($lines -match $marker)){$lines;throw 'Emulator verification failed'}
[pscustomobject]@{Test=$Kind;Passed=@($lines | Where-Object {$_ -like 'PASS:*'}).Count;Muted=$true}
$lines | Where-Object {$_ -like 'handoff:*' -or ($Kind -eq 'Net' -and $_ -notlike 'PASS:*' -and $_)}
