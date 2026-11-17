param(
    [string]$Toolchain = (Join-Path $PSScriptRoot '..\pspdev-win'),
    [switch]$QueueSmoke,
    [switch]$TransportSmoke,
    [switch]$MovieSmoke,
    [switch]$NetSmoke,
    [switch]$BooksSmoke
)
# Continue: gcc warnings on stderr must not abort a captured build; each step checks $LASTEXITCODE.
$ErrorActionPreference = 'Continue'
$tool = (Resolve-Path -LiteralPath $Toolchain).Path
$sdk = (Join-Path $tool 'psp\sdk').Replace('\', '/')
$psp = (Join-Path $tool 'psp').Replace('\', '/')
$bin = Join-Path $tool 'bin'
$env:PATH = "$bin;$env:PATH"
Push-Location $PSScriptRoot
try {
    $extra = @()
    if ($QueueSmoke) { $extra += '-DZOID_SMOKE_TEST'; $extra += '-DZOID_QUEUE_TEST' }
    if ($TransportSmoke) { $extra += '-DZOID_SMOKE_TEST'; $extra += '-DZOID_TRANSPORT_TEST' }
    if ($MovieSmoke) { $extra += '-DZOID_SMOKE_TEST'; $extra += '-DZOID_MOVIE_TEST' }
    if ($NetSmoke) { $extra += '-DZOID_SMOKE_TEST'; $extra += '-DZOID_NET_TEST' }
    if ($BooksSmoke) { $extra += '-DZOID_SMOKE_TEST'; $extra += '-DZOID_BOOKS_TEST' }
    & (Join-Path $bin 'psp-gcc.exe') -O2 -G0 -Wall -Wno-misleading-indentation -D_PSP_FW_VERSION=600 @extra "-I$sdk/include" "-I$psp/include" -c main.c -o main.o
    if ($LASTEXITCODE) { throw 'PSP compile failed' }
    & (Join-Path $bin 'psp-gcc.exe') -O2 -G0 -w -D_PSP_FW_VERSION=600 "-I$sdk/include" "-I$psp/include" -c softdec.c -o softdec.o
    if ($LASTEXITCODE) { throw 'PSP compile failed' }
    & (Join-Path $bin 'psp-gcc.exe') -O2 -G0 -Wall -D_PSP_FW_VERSION=600 @extra "-I$sdk/include" "-I$psp/include" -c m4a.c -o m4a.o
    if ($LASTEXITCODE) { throw 'PSP compile failed' }
    foreach ($unit in 'mp4','video','net','radio','comic','page') { & (Join-Path $bin 'psp-gcc.exe') -O2 -G0 -Wall -D_PSP_FW_VERSION=600 @extra "-I$sdk/include" "-I$psp/include" -c "$unit.c" -o "$unit.o"; if ($LASTEXITCODE) { throw 'PSP compile failed' } }
    # BearSSL (HTTPS for lyrics and https radio streams): built once into third_party/bearssl/libbearssl.a,
    # and again only when one of its sources is newer than the library
    $brDir = Join-Path $PSScriptRoot 'third_party\bearssl'
    $brLib = Join-Path $brDir 'libbearssl.a'
    $brSrc = Get-ChildItem (Join-Path $brDir 'src') -Recurse -Filter '*.c'
    if (-not (Test-Path $brLib) -or ($brSrc | Where-Object { $_.LastWriteTime -gt (Get-Item $brLib).LastWriteTime })) {
        $brObj = Join-Path $brDir 'obj'
        New-Item -ItemType Directory -Force $brObj | Out-Null
        Remove-Item (Join-Path $brObj '*.o') -ErrorAction SilentlyContinue
        $brFlags = @('-O2','-G0','-w','-DBR_USE_URANDOM=0','-DBR_USE_UNIX_TIME=0','-DBR_USE_WIN32_RAND=0','-DBR_USE_WIN32_TIME=0','-DBR_RDRAND=0',"-I$brDir\inc","-I$brDir\src")
        foreach ($c in $brSrc) {
            $o = Join-Path $brObj ($c.FullName.Substring((Join-Path $brDir 'src').Length + 1).Replace('\', '_').Replace('.c', '.o'))
            & (Join-Path $bin 'psp-gcc.exe') @brFlags -c $c.FullName -o $o
            if ($LASTEXITCODE) { throw "BearSSL compile failed: $($c.Name)" }
        }
        Remove-Item $brLib -ErrorAction SilentlyContinue
        # bare object names from inside obj: ~290 full paths overflow Windows' 32K command line
        Push-Location $brObj
        try { & (Join-Path $bin 'psp-ar.exe') rcs $brLib (Get-ChildItem -Filter '*.o' | ForEach-Object Name) }
        finally { Pop-Location }
        if ($LASTEXITCODE) { throw 'BearSSL archive failed' }
    }
    if ($LASTEXITCODE) { throw 'PSP compile failed' }
    # zoidvol.prx: the kernel helper that reads the system volume (custom firmware only), and the
    # weak import stub that lets ZOID call it once it is loaded
    Push-Location zoidvol
    try {
        & (Join-Path $bin 'psp-build-exports.exe') -b exports.exp | Set-Content -Encoding ascii exports.c
        & (Join-Path $bin 'psp-gcc.exe') -O2 -G0 -Wall -D_PSP_FW_VERSION=600 "-I$sdk/include" "-I$psp/include" -c zoidvol.c exports.c systemctrl.S
        if ($LASTEXITCODE) { throw 'zoidvol compile failed' }
        & (Join-Path $bin 'psp-gcc.exe') zoidvol.o exports.o systemctrl.o -nostartfiles "-specs=$sdk/lib/prxspecs" '-Wl,-q' "-Wl,-T$sdk/lib/linkfile.prx" "-L$sdk/lib" -lpspsdk -lpspctrl -lpspkernel -o zoidvol.elf
        if ($LASTEXITCODE) { throw 'zoidvol link failed' }
        & (Join-Path $bin 'psp-fixup-imports.exe') zoidvol.elf
        & (Join-Path $bin 'psp-prxgen.exe') zoidvol.elf ..\zoidvol.prx
        if ($LASTEXITCODE) { throw 'zoidvol packaging failed' }
        & (Join-Path $bin 'psp-build-exports.exe') -s exports.exp
        & (Join-Path $bin 'psp-gcc.exe') "-I$sdk/include" -c zoidvol.S -o ..\zoidvol_stub.o
        if ($LASTEXITCODE) { throw 'zoidvol stub failed' }
    } finally { Pop-Location }
    & (Join-Path $bin 'psp-gcc.exe') main.o softdec.o m4a.o mp4.o video.o net.o radio.o comic.o page.o zoidvol_stub.o "$PSScriptRoot/third_party/bearssl/libbearssl.a" "$sdk/lib/prxexports.o" "-L$psp/lib" "-L$sdk/lib" "-specs=$sdk/lib/prxspecs" '-Wl,-q' "-Wl,-T$sdk/lib/linkfile.prx" '-Wl,-zmax-page-size=128' -lpng16 -lz -ljpeg -lpspgu -lpsppower -lm -lpspaudio -lpspmp3 -lpspaudiocodec -lpspmpeg -lpspmpegbase -lpspsdk -lpspdebug -lpspdisplay -lpspge -lpspctrl -lpspnet -lpspnet_apctl -lpspnet_inet -lpspnet_resolver -lpsputility -lpsprtc -lpspwlan -o zoidpsp.elf
    if ($LASTEXITCODE) { throw 'PSP link failed' }
    & (Join-Path $bin 'psp-fixup-imports.exe') zoidpsp.elf
    if ($LASTEXITCODE) { throw 'PSP import fixup failed' }
    & (Join-Path $bin 'psp-prxgen.exe') zoidpsp.elf zoidpsp.prx
    if ($LASTEXITCODE) { throw 'PSP PRX packaging failed' }
    # MEMSIZE=1: the PSP-2000/3000's extended memory (about 52 MB for ZOID instead of 24); Books needs it for
    # large progressive JPEGs, which decode whole (a 4500 x 3206 spread takes about 29 MB)
    & (Join-Path $bin 'mksfoex.exe') -d MEMSIZE=1 'ZOID' PARAM.SFO
    if ($LASTEXITCODE) { throw 'PSP SFO packaging failed' }
    # ICON0.PNG (144x80) and PIC1.PNG (480x272) are the XMB icon and background, drawn by make-icon.ps1
    & (Join-Path $bin 'pack-pbp.exe') EBOOT.PBP PARAM.SFO ICON0.PNG NULL NULL PIC1.PNG NULL zoidpsp.prx NULL
    if ($LASTEXITCODE) { throw 'PSP EBOOT packaging failed' }
} finally {
    Pop-Location
}
