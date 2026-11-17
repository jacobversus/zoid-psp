param([string]$Root=(Join-Path $PSScriptRoot '..\Build\PSP-QA'),[string]$Tag='v17')
Add-Type -AssemblyName System.Drawing
$base=(Resolve-Path $Root).Path
foreach($file in Get-ChildItem (Join-Path $base 'emulator\memstick') -Filter '*.rgba') {
    $bytes=[System.IO.File]::ReadAllBytes($file.FullName)
    if($bytes.Length -ne 480*272*4){continue}
    for($i=0;$i -lt $bytes.Length;$i+=4){$red=$bytes[$i];$bytes[$i]=$bytes[$i+2];$bytes[$i+2]=$red;$bytes[$i+3]=255}
    $bmp=[System.Drawing.Bitmap]::new(480,272,[System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $data=$bmp.LockBits([System.Drawing.Rectangle]::new(0,0,480,272),[System.Drawing.Imaging.ImageLockMode]::WriteOnly,$bmp.PixelFormat)
    [System.Runtime.InteropServices.Marshal]::Copy($bytes,0,$data.Scan0,$bytes.Length)
    $bmp.UnlockBits($data)
    $bmp.Save((Join-Path $base ($file.BaseName+'-'+$Tag+'.png')))
    $bmp.Dispose()
}
