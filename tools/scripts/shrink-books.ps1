# Makes books (.cbz/.zip volumes and folders of page pictures) much smaller for the PSP.
# Scans are often 2000-4000 pixels tall, but Books never shows a page wider than 960 (2x zoom
# sideways; upright 2x is 544), so every page bigger than that is scaled down to -MaxWidth and saved
# as a baseline JPEG:
#   - black-and-white pages are saved as one-channel gray JPEGs (smaller, and Books keeps them gray)
#   - PNG pages become JPEGs (page.jpg instead of page.png; Books reads either)
#   - progressive JPEGs become baseline (a big progressive page can need more memory than a PSP has)
#   - pages already small enough and baseline are kept byte for byte (no second round of JPEG loss)
#   - macOS junk (__MACOSX, ._ files) and Thumbs.db are left out
# Page order and names stay the same, so places and bookmarks still match.
#   shrink-books.ps1 -Source <folder or .cbz> -Dest <folder>   new copies under -Dest, same layout
#   shrink-books.ps1 -Source E:\BOOKS -InPlace                 replaces each volume, only once its
#                                                              smaller copy is written and checked
# A volume whose copy already exists in -Dest is skipped, so a stopped run carries on where it was.
#   .cbr/.rar volumes (which the PSP can't open) are unpacked with 7-Zip and written as shrunk .cbz;
#   with -InPlace the RAR is then deleted, or moved under -OriginalsTo <folder> if given
param([Parameter(Mandatory)][string]$Source,[string]$Dest,[switch]$InPlace,[int]$MaxWidth=960,[int]$Quality=80,[string]$OriginalsTo)
$ErrorActionPreference='Stop'
if(-not $InPlace -and -not $Dest){throw 'Give -Dest <folder>, or -InPlace to replace the originals.'}
Add-Type -AssemblyName System.Drawing,PresentationCore,WindowsBase
Add-Type -ReferencedAssemblies System.Drawing,System.IO.Compression,System.IO.Compression.FileSystem,PresentationCore,WindowsBase,System.Xaml -Language CSharp @"
using System; using System.IO; using System.IO.Compression; using System.Drawing; using System.Drawing.Drawing2D;
using System.Drawing.Imaging; using System.Linq; using System.Threading.Tasks; using System.Collections.Generic;
using System.Windows.Media.Imaging; using PixelFormat=System.Drawing.Imaging.PixelFormat; using PixelFormats=System.Windows.Media.PixelFormats;
public static class Shrink {
 static bool IsJpeg(byte[] d){return d.Length>3&&d[0]==0xFF&&d[1]==0xD8;}
 static bool IsPng(byte[] d){return d.Length>8&&d[0]==0x89&&d[1]==0x50&&d[2]==0x4E&&d[3]==0x47;}
 // a JPEG that is baseline and no wider than maxW is kept as it is
 static bool JpegNeedsWork(byte[] d,int maxW){
  int p=2;
  while(p+9<d.Length){
   if(d[p]!=0xFF){p++;continue;} int m=d[p+1]; if(m==0xFF){p++;continue;}
   if(m==0xD8||m==0x01||(m>=0xD0&&m<=0xD7)){p+=2;continue;}
   if(m>=0xC0&&m<=0xCF&&m!=0xC4&&m!=0xC8&&m!=0xCC){int w=(d[p+7]<<8)|d[p+8];return (m!=0xC0&&m!=0xC1)||w>maxW;}
   p+=2+((d[p+2]<<8)|d[p+3]);
  }
  return true;
 }
 // the page scaled to maxW wide (never up), as a baseline JPEG: one gray channel when the page has no color
 static byte[] Recode(byte[] d,int maxW,int quality){
  using(var ms=new MemoryStream(d)) using(var src=Image.FromStream(ms,false,false)){
   int w=src.Width,h=src.Height;
   if(w>maxW){h=Math.Max(1,(int)Math.Round((double)h*maxW/w));w=maxW;}
   using(var bmp=new Bitmap(w,h,PixelFormat.Format24bppRgb)){
    using(var g=Graphics.FromImage(bmp)){
     g.Clear(System.Drawing.Color.White);   // a transparent PNG reads as paper, not black
     g.InterpolationMode=InterpolationMode.HighQualityBicubic;g.PixelOffsetMode=PixelOffsetMode.HighQuality;g.CompositingQuality=CompositingQuality.HighQuality;
     using(var ia=new ImageAttributes()){ia.SetWrapMode(WrapMode.TileFlipXY);g.DrawImage(src,new Rectangle(0,0,w,h),0,0,src.Width,src.Height,GraphicsUnit.Pixel,ia);}
    }
    var bd=bmp.LockBits(new Rectangle(0,0,w,h),ImageLockMode.ReadOnly,PixelFormat.Format24bppRgb);
    int stride=bd.Stride; var px=new byte[stride*h];
    System.Runtime.InteropServices.Marshal.Copy(bd.Scan0,px,0,px.Length); bmp.UnlockBits(bd);
    // gray unless more than 1 pixel in 2000 is clearly colored (scans of black-and-white pages are a little tinted)
    long colored=0,limit=(long)w*h/2000;
    for(int y=0;y<h&&colored<=limit;y++){int o=y*stride;for(int x=0;x<w;x++,o+=3){
     int b=px[o],gg=px[o+1],r=px[o+2];int mx=Math.Max(r,Math.Max(gg,b)),mn=Math.Min(r,Math.Min(gg,b));if(mx-mn>28)colored++;}}
    BitmapSource bs;
    if(colored<=limit){
     var gray=new byte[w*h];
     for(int y=0;y<h;y++){int o=y*stride,q=y*w;for(int x=0;x<w;x++,o+=3)gray[q+x]=(byte)((px[o]*114+px[o+1]*587+px[o+2]*299+500)/1000);}
     bs=BitmapSource.Create(w,h,96,96,PixelFormats.Gray8,null,gray,w);
    } else bs=BitmapSource.Create(w,h,96,96,PixelFormats.Bgr24,null,px,stride);
    var enc=new JpegBitmapEncoder(); enc.QualityLevel=quality; enc.Frames.Add(BitmapFrame.Create(bs));
    using(var o=new MemoryStream()){enc.Save(o);return o.ToArray();}
   }
  }
 }
 static bool Junk(string name){
  string n=name.Replace('\\','/'); string leaf=n.Substring(n.LastIndexOf('/')+1);
  return n.StartsWith("__MACOSX/")||n.Contains("/__MACOSX/")||leaf.StartsWith("._")||leaf.Equals("Thumbs.db",StringComparison.OrdinalIgnoreCase)||leaf.Equals(".DS_Store");
 }
 static bool PageName(string n){n=n.ToLowerInvariant();return n.EndsWith(".jpg")||n.EndsWith(".jpeg")||n.EndsWith(".png");}
 // one page: its new bytes, or null to keep it as it is
 public static byte[] Page(byte[] d,int maxW,int quality){
  try{
   if(IsJpeg(d))return JpegNeedsWork(d,maxW)?Recode(d,maxW,quality):null;
   if(!IsPng(d))return null;
   var r=Recode(d,maxW,quality);
   return r.Length<d.Length?r:null;     // a tiny PNG can be smaller than any JPEG of it
  }catch{return null;}    // a page that won't decode here is kept as it was
 }
 // page.png -> page.jpg once it holds a JPEG (unless that name is taken)
 static string NewName(string name,byte[] outData,HashSet<string> taken){
  if(!name.ToLowerInvariant().EndsWith(".png")||!IsJpeg(outData))return name;
  string j=name.Substring(0,name.Length-4)+".jpg";
  return taken.Contains(j.ToLowerInvariant())?name:j;
 }
 // one .cbz/.zip volume; returns "pages changed/pages"
 public static string Volume(string from,string to,int maxW,int quality,bool skipIfSame){
  int junk=0; var names=new List<string>(); var data=new List<byte[]>(); var times=new List<DateTimeOffset>();
  using(var z=ZipFile.OpenRead(from)) foreach(var e in z.Entries){
   if(e.FullName.EndsWith("/"))continue; if(Junk(e.FullName)){junk++;continue;}
   using(var s=e.Open()) using(var m=new MemoryStream()){s.CopyTo(m);names.Add(e.FullName);data.Add(m.ToArray());times.Add(e.LastWriteTime);}
  }
  var outData=new byte[names.Count][]; int done=0,pages=0;
  Parallel.For(0,names.Count,new ParallelOptions{MaxDegreeOfParallelism=Environment.ProcessorCount},i=>{
   if(!PageName(names[i])){outData[i]=data[i];return;}
   System.Threading.Interlocked.Increment(ref pages);
   var r=Page(data[i],maxW,quality);
   if(r!=null){outData[i]=r;System.Threading.Interlocked.Increment(ref done);} else outData[i]=data[i];
  });
  if(skipIfSame&&done==0&&junk==0)return "same";
  var taken=new HashSet<string>(names.Select(n=>n.ToLowerInvariant()));
  Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(to)));
  string tmp=to+".part"; if(File.Exists(tmp))File.Delete(tmp);
  using(var z=ZipFile.Open(tmp,ZipArchiveMode.Create)) for(int i=0;i<names.Count;i++){
   bool img=PageName(names[i]);
   var e=z.CreateEntry(img?NewName(names[i],outData[i],taken):names[i],img?CompressionLevel.NoCompression:CompressionLevel.Optimal); e.LastWriteTime=times[i];
   using(var s=e.Open())s.Write(outData[i],0,outData[i].Length);
  }
  // check the new volume reads back whole before anything is replaced
  using(var z=ZipFile.OpenRead(tmp)){ if(z.Entries.Count!=names.Count)throw new Exception("the new copy didn't read back"); foreach(var e in z.Entries)using(var s=e.Open())s.CopyTo(Stream.Null); }
  if(File.Exists(to))File.Delete(to); File.Move(tmp,to);
  return done+"/"+pages;
 }
 // a folder of pages (a .cbr unpacked by 7-Zip) as one new .cbz volume; returns "pages changed/pages"
 public static string FolderVolume(string dir,string to,int maxW,int quality){
  dir=Path.GetFullPath(dir).TrimEnd('\\');
  var files=Directory.GetFiles(dir,"*",SearchOption.AllDirectories).Select(f=>f.Substring(dir.Length+1).Replace('\\','/')).Where(n=>!Junk(n)).OrderBy(n=>n,StringComparer.Ordinal).ToList();
  var outData=new byte[files.Count][]; int done=0,pages=0;
  Parallel.For(0,files.Count,new ParallelOptions{MaxDegreeOfParallelism=Environment.ProcessorCount},i=>{
   var d=File.ReadAllBytes(Path.Combine(dir,files[i]));
   if(!PageName(files[i])){outData[i]=d;return;}
   System.Threading.Interlocked.Increment(ref pages);
   var r=Page(d,maxW,quality);
   if(r!=null){outData[i]=r;System.Threading.Interlocked.Increment(ref done);} else outData[i]=d;
  });
  if(pages==0)throw new Exception("no page pictures inside");
  var taken=new HashSet<string>(files.Select(n=>n.ToLowerInvariant()));
  Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(to)));
  string tmp=to+".part"; if(File.Exists(tmp))File.Delete(tmp);
  using(var z=ZipFile.Open(tmp,ZipArchiveMode.Create)) for(int i=0;i<files.Count;i++){
   bool img=PageName(files[i]);
   var e=z.CreateEntry(img?NewName(files[i],outData[i],taken):files[i],img?CompressionLevel.NoCompression:CompressionLevel.Optimal);
   using(var s=e.Open())s.Write(outData[i],0,outData[i].Length);
  }
  using(var z=ZipFile.OpenRead(tmp)){ if(z.Entries.Count!=files.Count)throw new Exception("the new copy didn't read back"); foreach(var e in z.Entries)using(var s=e.Open())s.CopyTo(Stream.Null); }
  if(File.Exists(to))File.Delete(to); File.Move(tmp,to);
  return done+"/"+pages;
 }
 // one loose page picture (a folder-of-images book); returns the path written, or null if it was kept
 public static string LoosePage(string from,string toDir,int maxW,int quality,bool inPlace){
  var d=File.ReadAllBytes(from); var r=Page(d,maxW,quality); string name=Path.GetFileName(from);
  if(r==null){ if(!inPlace){Directory.CreateDirectory(toDir);File.Copy(from,Path.Combine(toDir,name),true);} return null; }
  string newName=name;
  if(name.ToLowerInvariant().EndsWith(".png")){string j=name.Substring(0,name.Length-4)+".jpg";if(!File.Exists(Path.Combine(Path.GetDirectoryName(from),j)))newName=j;}
  Directory.CreateDirectory(toDir); string to=Path.Combine(toDir,newName);
  File.WriteAllBytes(to+".part",r); if(File.Exists(to))File.Delete(to); File.Move(to+".part",to);
  if(inPlace&&!newName.Equals(name))File.Delete(from);
  return to;
 }
}
"@

$src=(Get-Item -LiteralPath $Source).FullName.TrimEnd('\')    # the same spelling as the files under it (not an 8.3 short form such as C:\...\USERNA~1)
$pageKinds='.jpg','.jpeg','.png'
if(Test-Path -LiteralPath $src -PathType Leaf){$files=@(Get-Item -LiteralPath $src);$base=Split-Path $src}
else{$base=$src;$files=@(Get-ChildItem -LiteralPath $src -Recurse -File -Force | Where-Object { -not $_.Name.StartsWith('._') -and $_.FullName -notmatch '\\__MACOSX\\' -and $_.Name -ne 'Thumbs.db' -and $_.Name -ne '.DS_Store' } | Sort-Object FullName)}
if($Dest){New-Item -ItemType Directory -Force $Dest | Out-Null; $Dest=(Resolve-Path -LiteralPath $Dest).Path}
$before=0L;$after=0L;$n=0
$volumes=@($files | Where-Object { $_.Extension -in '.cbz','.zip','.cbr','.rar' })
$sevenZip=@("$env:ProgramFiles\7-Zip\7z.exe","${env:ProgramFiles(x86)}\7-Zip\7z.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
foreach($f in $files){
  $rel=$f.FullName.Substring($base.Length).TrimStart('\')
  $out=if($InPlace){$f.FullName}else{Join-Path $Dest $rel}
  if($f.Extension -in '.cbr','.rar'){
    # RAR books (the PSP can't read them): unpacked with 7-Zip, then made into a shrunk .cbz of the same name
    $n++
    $cbz=[IO.Path]::ChangeExtension($out,'.cbz')
    if(Test-Path -LiteralPath $cbz){Write-Host ("[{0}/{1}] already done: {2}" -f $n,$volumes.Count,$rel);continue}
    if(-not $sevenZip){Write-Host "[$n/$($volumes.Count)] SKIPPED, needs 7-Zip to unpack: $rel" -ForegroundColor Red;continue}
    $size=$f.Length;$t=[Diagnostics.Stopwatch]::StartNew()
    $tmpDir=Join-Path $env:TEMP ('zoid-cbr-'+[guid]::NewGuid().ToString('N'))
    try{
      & $sevenZip x -y -bso0 -bsp0 ("-o"+$tmpDir) $f.FullName | Out-Null
      if($LASTEXITCODE -ne 0){throw "7-Zip couldn't unpack it (code $LASTEXITCODE)"}
      $r=[Shrink]::FolderVolume($tmpDir,$cbz,$MaxWidth,$Quality)
      (Get-Item -LiteralPath $cbz).LastWriteTime=$f.LastWriteTime
      $new=(Get-Item -LiteralPath $cbz).Length
      if($InPlace){ if($OriginalsTo){$keep=Join-Path $OriginalsTo $rel;New-Item -ItemType Directory -Force (Split-Path $keep) | Out-Null;Move-Item -LiteralPath $f.FullName $keep -Force}else{Remove-Item -LiteralPath $f.FullName} }
      $before+=$size;$after+=$new
      Write-Host ('[{0}/{1}] {2,6:N0} MB -> {3,5:N0} MB  (RAR to .cbz, {4} pages made smaller, {5:N0} s)  {6}' -f $n,$volumes.Count,($size/1MB),($new/1MB),$r,$t.Elapsed.TotalSeconds,$rel)
    }catch{
      Write-Host "[$n/$($volumes.Count)] FAILED, left as it was: $rel  ($($_.Exception.Message))" -ForegroundColor Red
      Remove-Item -LiteralPath "$cbz.part" -ErrorAction SilentlyContinue
    }finally{ Remove-Item -LiteralPath $tmpDir -Recurse -Force -ErrorAction SilentlyContinue }
  }
  elseif($f.Extension -in '.cbz','.zip'){
    $n++
    if(-not $InPlace -and (Test-Path -LiteralPath $out)){Write-Host ("[{0}/{1}] already done: {2}" -f $n,$volumes.Count,$rel);continue}
    $size=$f.Length;$t=[Diagnostics.Stopwatch]::StartNew()
    try{
      $tmpOut=if($InPlace){$f.FullName+'.shrunk'}else{$out}
      $r=[Shrink]::Volume($f.FullName,$tmpOut,$MaxWidth,$Quality,[bool]$InPlace)
      if($r -eq 'same'){Write-Host ("[{0}/{1}] already small: {2}" -f $n,$volumes.Count,$rel);$before+=$size;$after+=$size;continue}
      $new=(Get-Item -LiteralPath $tmpOut).Length
      if($InPlace){ if($new -lt $size){Remove-Item -LiteralPath $f.FullName;Move-Item -LiteralPath $tmpOut $f.FullName}else{Remove-Item -LiteralPath $tmpOut;$new=$size} }
      (Get-Item -LiteralPath $out).LastWriteTime=$f.LastWriteTime
      $before+=$size;$after+=$new
      Write-Host ('[{0}/{1}] {2,6:N0} MB -> {3,5:N0} MB  ({4} pages made smaller, {5:N0} s)  {6}' -f $n,$volumes.Count,($size/1MB),($new/1MB),$r,$t.Elapsed.TotalSeconds,$rel)
    }catch{
      Write-Host "[$n/$($volumes.Count)] FAILED, left as it was: $rel  ($($_.Exception.Message))" -ForegroundColor Red
      foreach($p in "$out.part","$($f.FullName).shrunk","$($f.FullName).shrunk.part"){Remove-Item -LiteralPath $p -ErrorAction SilentlyContinue}
    }
  }
  elseif($f.Extension -in $pageKinds){
    $toDir=if($InPlace){$f.DirectoryName}else{Split-Path (Join-Path $Dest $rel)}
    $size=$f.Length
    $w=[Shrink]::LoosePage($f.FullName,$toDir,$MaxWidth,$Quality,[bool]$InPlace)
    $before+=$size;$after+=$(if($w){(Get-Item -LiteralPath $w).Length}else{$size})
  }
  elseif(-not $InPlace){ New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null; Copy-Item -LiteralPath $f.FullName $out -Force }
}
if($before){Write-Host ('Total: {0:N0} MB -> {1:N0} MB, {2:N0} MB saved' -f ($before/1MB),($after/1MB),(($before-$after)/1MB)) -ForegroundColor Green}
