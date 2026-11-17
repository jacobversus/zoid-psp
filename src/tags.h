/* ID3v2.2/2.3/2.4 and ID3v1 tag reading, plus embedded JPEG/PNG cover art.
   Frames are read one at a time with seeks, so large artwork is skipped
   unless it is wanted. Text is kept as UTF-8: letters the theme fonts lack are drawn from
   the PSP's own fonts (sysfont.h). */
#include <jpeglib.h>
#include <png.h>
#include <setjmp.h>

typedef struct {char title[112],artist[80],album[80];int art_off,art_len,lyr_off,lyr_len,track,disc;} Tags; /* lyr: an embedded USLT lyrics frame; track, disc: 0 when not tagged */
/* "3/12" or "03" -> 3 */
static int tag_number(const unsigned char *v,int n){int k=0,r=0;while(k<n && (v[k]==' ' || v[k]==0))k++;while(k<n && v[k]>='0' && v[k]<='9' && r<100000)r=r*10+v[k++]-'0';return r;}
static void tag_put(char *out,int *o,int cap,unsigned int cp) {
 if(cp<32 || cp==0x7F || cp==0xFEFF || cp>0x10FFFF || (cp>=0xD800 && cp<=0xDFFF))return;
 /* typographic quotes and dashes read better in the theme font */
 if(cp==0x2018 || cp==0x2019)cp='\'';else if(cp==0x201C || cp==0x201D)cp='"';else if(cp==0x2013 || cp==0x2014)cp='-';else if(cp==0xA0 || cp==0x3000)cp=' ';
 char b[4];int n=utf8_put(b,cp);
 if(*o+n<cap){memcpy(out+*o,b,n);*o+=n;}
 else if(*o<cap-1){memset(out+*o,0,cap-*o);*o=cap-1;} /* full: stop here, never half a letter */
}
/* ml keeps line breaks (for lyrics); otherwise control characters are dropped. */
static void tag_put_ml(char *out,int *o,int cap,unsigned int cp,int ml){if(ml && cp==10){if(*o<cap-1)out[(*o)++]=10;}else tag_put(out,o,cap,cp);}
static void tag_decode(const unsigned char *b,int n,char *out,int cap,int ml) {
 int o=0;if(n<1){out[0]=0;return;}
 int enc=b[0];b++;n--;
 if(enc==1 || enc==2) {
  int be=enc==2;if(n>=2 && b[0]==0xFF && b[1]==0xFE){be=0;b+=2;n-=2;}else if(n>=2 && b[0]==0xFE && b[1]==0xFF){be=1;b+=2;n-=2;}
  for(int i=0;i+1<n;i+=2){unsigned int cp=be?(b[i]<<8|b[i+1]):(b[i+1]<<8|b[i]);if(!cp)break;if(cp>=0xD800 && cp<=0xDFFF){unsigned int lo=i+3<n?(be?(b[i+2]<<8|b[i+3]):(b[i+3]<<8|b[i+2])):0;
   if(cp<0xDC00 && lo>=0xDC00 && lo<=0xDFFF){tag_put_ml(out,&o,cap,0x10000+((cp-0xD800)<<10)+(lo-0xDC00),ml);i+=2;}else tag_put_ml(out,&o,cap,'?',ml);continue;}tag_put_ml(out,&o,cap,cp,ml);}
 }else if(enc==3) {
  for(int i=0;i<n && b[i];){unsigned int c=b[i],cp;int len=c<0x80?1:c<0xE0?2:c<0xF0?3:4;
   if(len==1)cp=c;else if(len==2)cp=c&31;else if(len==3)cp=c&15;else cp=c&7;
   for(int k=1;k<len && i+k<n;k++)cp=(cp<<6)|(b[i+k]&63);tag_put_ml(out,&o,cap,cp,ml);i+=len;}
 }else for(int i=0;i<n && b[i];i++)tag_put_ml(out,&o,cap,b[i],ml);
 while(o>0 && (out[o-1]==' ' || out[o-1]==10))o--;out[o]=0;
 int lead=0;while(out[lead]==' ' || out[lead]==10)lead++;if(lead)memmove(out,out+lead,strlen(out+lead)+1);
}
static void tag_text(const unsigned char *b,int n,char *out,int cap){tag_decode(b,n,out,cap,0);}
static int synchsafe(const unsigned char *p){return (p[0]<<21)|(p[1]<<14)|(p[2]<<7)|p[3];}
static int rd_le32(const unsigned char *p){return p[0]|(p[1]<<8)|(p[2]<<16)|(p[3]<<24);}
static int rd_be32(const unsigned char *p){return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];}
/* Text field into out; enc is the ID3 encoding byte (3 UTF-8, 0 Latin-1). */
static void tag_field(int enc,const unsigned char *v,int n,char *out,int cap){if(out[0])return;unsigned char f[260];if(n>258)n=258;f[0]=enc;memcpy(f+1,v,n);f[n+1]=0;tag_text(f,n+1,out,cap);}
static void tag_key(int enc,const unsigned char *k,int kn,const unsigned char *v,int vn,Tags *t) {
 char key[16];int i;for(i=0;i<kn && i<15;i++)key[i]=toupper(k[i]);key[i]=0;if(kn>15)return;
 if(!strcmp(key,"TRACKNUMBER") || !strcmp(key,"ITRK")){if(!t->track)t->track=tag_number(v,vn);return;}
 if(!strcmp(key,"DISCNUMBER")){if(!t->disc)t->disc=tag_number(v,vn);return;}
 if(!strcmp(key,"TITLE") || !strcmp(key,"INAM"))tag_field(enc,v,vn,t->title,sizeof(t->title));
 else if(!strcmp(key,"ARTIST") || !strcmp(key,"IART"))tag_field(enc,v,vn,t->artist,sizeof(t->artist));
 else if(!strcmp(key,"ALBUM") || !strcmp(key,"IPRD"))tag_field(enc,v,vn,t->album,sizeof(t->album));
}
/* FLAC: Vorbis comments (UTF-8 "KEY=value") and the PICTURE block for cover art. */
static void flac_tags(int fd,Tags *t,int want_art) {
 int pos=4;unsigned char h[4],buf[1024];
 for(int guard=0;guard<64;guard++) {
  if(sceIoLseek32(fd,pos,PSP_SEEK_SET)<0 || sceIoRead(fd,h,4)!=4)return;
  int type=h[0]&127,len=(h[1]<<16)|(h[2]<<8)|h[3];
  if(type==4) {
   unsigned char *b=malloc(len);
   if(b && sceIoRead(fd,b,len)==len && len>=8) {
    int p=4+rd_le32(b),count=p+4<=len?rd_le32(b+p):0;p+=4;
    for(int i=0;i<count && p+4<=len;i++){int n=rd_le32(b+p);p+=4;if(n<0 || p+n>len)break;
     const unsigned char *c=b+p,*eq=memchr(c,'=',n);if(eq)tag_key(3,c,eq-c,eq+1,n-(eq-c)-1,t);p+=n;}
   }
   free(b);
  }else if(type==6 && want_art && !t->art_len) {
   int n=len<(int)sizeof(buf)?len:(int)sizeof(buf);
   if(sceIoRead(fd,buf,n)==n && n>8) {
    int p=4,ml=rd_be32(buf+p);p+=4+ml;if(p+4>n)goto next;int dl=rd_be32(buf+p);p+=4+dl+16;if(p+4>n)goto next;
    int size=rd_be32(buf+p);p+=4;
    if(size>0 && p+size<=len){t->art_off=pos+4+p;t->art_len=size;}
   }
  }
  next:
  if(h[0]&128)return;
  pos+=4+len;
 }
}
/* WAV: the LIST/INFO chunk (INAM title, IART artist, IPRD album). */
static void wav_tags(int fd,Tags *t) {
 int pos=12,end=sceIoLseek32(fd,0,PSP_SEEK_END);unsigned char h[12],buf[2048];
 for(int guard=0;guard<64 && pos+8<=end;guard++) {
  if(sceIoLseek32(fd,pos,PSP_SEEK_SET)<0 || sceIoRead(fd,h,12)<8)return;
  int len=rd_le32(h+4);if(len<0)return;
  if(!memcmp(h,"LIST",4) && !memcmp(h+8,"INFO",4)) {
   int n=len-4<(int)sizeof(buf)?len-4:(int)sizeof(buf);
   if(n>0 && sceIoRead(fd,buf,n)==n)for(int p=0;p+8<=n;){int sz=rd_le32(buf+p+4);if(sz<0 || p+8+sz>n)break;tag_key(0,buf+p,4,buf+p+8,sz,t);p+=8+sz+(sz&1);}
  }
  pos+=8+len+(len&1);
 }
}
/* M4A: iTunes-style moov/udta/meta/ilst items, each holding a 'data' box. */
static int mp4_child(int fd,int start,int end,const char *type,int *len) {
 unsigned char h[8];
 for(int pos=start;pos+8<=end;) {
  if(sceIoLseek32(fd,pos,PSP_SEEK_SET)<0 || sceIoRead(fd,h,8)!=8)return 0;
  int sz=rd_be32(h);if(sz<8 || pos+sz>end)return 0;
  if(!memcmp(h+4,type,4)){*len=sz-8;return pos+8;}
  pos+=sz;
 }
 return 0;
}
static void m4a_tags(int fd,Tags *t,int want_art) {
 int end=sceIoLseek32(fd,0,PSP_SEEK_END),l,moov=mp4_child(fd,0,end,"moov",&l);if(!moov)return;
 /* a file can hold several udta boxes (encoders add their own, sometimes empty): read them all */
 unsigned char h[8],buf[260];
 for(int from=moov,ul,udta;(udta=mp4_child(fd,from,moov+l,"udta",&ul));from=udta+ul) {
 int ml,meta=mp4_child(fd,udta,udta+ul,"meta",&ml),il=0,ilst=meta?mp4_child(fd,meta+4,meta+ml,"ilst",&il):0;
 if(!ilst)continue;
 for(int pos=ilst;pos+8<=ilst+il;) {
  if(sceIoLseek32(fd,pos,PSP_SEEK_SET)<0 || sceIoRead(fd,h,8)!=8)return;
  int sz=rd_be32(h);if(sz<8 || pos+sz>ilst+il)return;
  int dl,data=mp4_child(fd,pos+8,pos+sz,"data",&dl);
  if(data && dl>8) {
   char *dst=NULL;int cap=0;
   if(!memcmp(h+4,"\xA9nam",4)){dst=t->title;cap=sizeof(t->title);}
   else if(!memcmp(h+4,"\xA9""ART",4) || (!memcmp(h+4,"aART",4) && !t->artist[0])){dst=t->artist;cap=sizeof(t->artist);}
   else if(!memcmp(h+4,"\xA9""alb",4)){dst=t->album;cap=sizeof(t->album);}
   if(dst){int n=dl-8<256?dl-8:256;if(sceIoLseek32(fd,data+8,PSP_SEEK_SET)>=0 && sceIoRead(fd,buf,n)==n)tag_field(3,buf,n,dst,cap);}
   else if((!memcmp(h+4,"trkn",4) || !memcmp(h+4,"disk",4)) && dl>=12){ /* 2 bytes padding, then the number (big endian) */
    int trk=h[4]=='t';if(sceIoLseek32(fd,data+8,PSP_SEEK_SET)>=0 && sceIoRead(fd,buf,4)==4){int v=(buf[2]<<8)|buf[3];if(trk){if(!t->track)t->track=v;}else if(!t->disc)t->disc=v;}}
   else if(want_art && !t->art_len && !memcmp(h+4,"covr",4)){t->art_off=data+8;t->art_len=dl-8;}
  }
  pos+=sz;
 }
 }
}
/* ID3 frames are read from the first block of the file when they lie in it (one read for most tags),
   otherwise with a seek: a folder sorted by track number reads every song's tag. */
static int id3_get(int fd,const unsigned char *pre,int got,int pos,unsigned char *out,int n) {
 if(pos+n<=got){memcpy(out,pre+pos,n);return n;}
 if(sceIoLseek32(fd,pos,PSP_SEEK_SET)<0)return -1;return sceIoRead(fd,out,n);
}
#define ID3_PRE 8192
static void read_tags(const char *path,Tags *t,int want_art) {
 memset(t,0,sizeof(*t));
 int fd=sceIoOpen(path,PSP_O_RDONLY,0);if(fd<0)return;
 unsigned char h[10],buf[512];
 {unsigned char m[12];if(sceIoRead(fd,m,12)==12){
   if(!memcmp(m,"fLaC",4)){flac_tags(fd,t,want_art);sceIoClose(fd);return;}
   if(!memcmp(m,"RIFF",4) && !memcmp(m+8,"WAVE",4)){wav_tags(fd,t);sceIoClose(fd);return;}
   if(!memcmp(m+4,"ftyp",4)){m4a_tags(fd,t,want_art);sceIoClose(fd);return;}}
  sceIoLseek32(fd,0,PSP_SEEK_SET);}
 unsigned char *pre=malloc(ID3_PRE);int got=0;
 if(pre){got=sceIoRead(fd,pre,ID3_PRE);if(got<0)got=0;if(got>=10)memcpy(h,pre,10);else h[0]=0;}
 else if(sceIoRead(fd,h,10)!=10)h[0]=0;
 if(!memcmp(h,"ID3",3) && h[3]>=2 && h[3]<=4) {
  int ver=h[3],end=10+synchsafe(h+6),pos=10,hdr=ver==2?6:10;
  if((h[5]&0x40) && ver>2){unsigned char e[4];if(id3_get(fd,pre,got,10,e,4)==4)pos+=ver==4?synchsafe(e):4+((e[0]<<24)|(e[1]<<16)|(e[2]<<8)|e[3]);}
  while(pos+hdr<=end) {
   if(id3_get(fd,pre,got,pos,h,hdr)!=hdr)break;
   if(!h[0])break;
   int size=ver==2?(h[3]<<16)|(h[4]<<8)|h[5]:ver==4?synchsafe(h+4):(h[4]<<24)|(h[5]<<16)|(h[6]<<8)|h[7];
   if(size<=0 || pos+hdr+size>end)break;
   char *dst=NULL;int cap=0;
   if(!memcmp(h,ver==2?"TT2":"TIT2",ver==2?3:4)){dst=t->title;cap=sizeof(t->title);}
   else if(!memcmp(h,ver==2?"TP1":"TPE1",ver==2?3:4)){dst=t->artist;cap=sizeof(t->artist);}
   else if(!memcmp(h,ver==2?"TAL":"TALB",ver==2?3:4)){dst=t->album;cap=sizeof(t->album);}
   int num=!memcmp(h,ver==2?"TRK":"TRCK",ver==2?3:4)?1:!memcmp(h,ver==2?"TPA":"TPOS",ver==2?3:4)?2:0;
   if(num){unsigned char nb[24];int n=size<23?size:23;if(id3_get(fd,pre,got,pos+hdr,nb,n)==n && n>1){char s[24];tag_text(nb,n,s,sizeof(s));int v=tag_number((const unsigned char *)s,strlen(s));if(num==1){if(!t->track)t->track=v;}else if(!t->disc)t->disc=v;}}
   else if(dst && !dst[0]){int n=size<(int)sizeof(buf)?size:(int)sizeof(buf);if(id3_get(fd,pre,got,pos+hdr,buf,n)==n)tag_text(buf,n,dst,cap);}
   else if(!t->lyr_len && !memcmp(h,ver==2?"ULT":"USLT",ver==2?3:4)){t->lyr_off=pos+hdr;t->lyr_len=size;}
   else if(want_art && !t->art_len && !memcmp(h,ver==2?"PIC":"APIC",ver==2?3:4)) {
    int n=size<(int)sizeof(buf)?size:(int)sizeof(buf);
    if(id3_get(fd,pre,got,pos+hdr,buf,n)==n) {
     int enc=buf[0],i=1;
     if(ver==2)i+=3;else{while(i<n && buf[i])i++;i++;}
     i++; /* picture type */
     if(enc==1 || enc==2){while(i+1<n && (buf[i] || buf[i+1]))i+=2;i+=2;}else{while(i<n && buf[i])i++;i++;}
     if(i+4<n && ((buf[i]==0xFF && buf[i+1]==0xD8) || (buf[i]==0x89 && buf[i+1]=='P'))){t->art_off=pos+hdr+i;t->art_len=size-i;}
    }
   }
   pos+=hdr+size;
  }
 }
 free(pre);
 if(!t->title[0] && !t->artist[0]) {
  int end=sceIoLseek32(fd,0,PSP_SEEK_END);
  if(end>128 && sceIoLseek32(fd,end-128,PSP_SEEK_SET)>=0 && sceIoRead(fd,buf,128)==128 && !memcmp(buf,"TAG",3)) {
   unsigned char field[32];
   field[0]=0;memcpy(field+1,buf+3,30);tag_text(field,31,t->title,sizeof(t->title));
   memcpy(field+1,buf+33,30);tag_text(field,31,t->artist,sizeof(t->artist));
   memcpy(field+1,buf+63,30);tag_text(field,31,t->album,sizeof(t->album));
  }
 }
 sceIoClose(fd);
}

/* ---- cover art ---- */
struct jpeg_fail {struct jpeg_error_mgr pub;jmp_buf jump;};
static void jpeg_fail_exit(j_common_ptr c){longjmp(((struct jpeg_fail *)c->err)->jump,1);}
static void jpeg_quiet(j_common_ptr c){(void)c;}
static unsigned char *decode_jpeg(unsigned char *src,int len,int *w,int *h) {
 struct jpeg_decompress_struct ci;struct jpeg_fail je;unsigned char *volatile out=NULL;
 ci.err=jpeg_std_error(&je.pub);je.pub.error_exit=jpeg_fail_exit;je.pub.output_message=jpeg_quiet;
 if(setjmp(je.jump)){jpeg_destroy_decompress(&ci);free(out);return NULL;}
 jpeg_create_decompress(&ci);jpeg_mem_src(&ci,src,len);
 if(jpeg_read_header(&ci,TRUE)!=JPEG_HEADER_OK){jpeg_destroy_decompress(&ci);return NULL;}
 ci.out_color_space=JCS_RGB;ci.scale_num=1;int d=1;
 while(d<8 && ci.image_width/(d*2)>=ART_N && ci.image_height/(d*2)>=ART_N)d*=2;
 ci.scale_denom=d;jpeg_start_decompress(&ci);
 *w=ci.output_width;*h=ci.output_height;
 if(ci.output_components!=3 || *w>4096 || *h>4096){jpeg_destroy_decompress(&ci);return NULL;}
 out=malloc(*w * *h * 3);if(!out){jpeg_destroy_decompress(&ci);return NULL;}
 while(ci.output_scanline<ci.output_height){JSAMPROW row=out+ci.output_scanline*(*w)*3;jpeg_read_scanlines(&ci,&row,1);}
 jpeg_finish_decompress(&ci);jpeg_destroy_decompress(&ci);return out;
}
static unsigned char *decode_png(unsigned char *src,int len,int *w,int *h) {
 png_image im;memset(&im,0,sizeof(im));im.version=PNG_IMAGE_VERSION;
 if(!png_image_begin_read_from_memory(&im,src,len))return NULL;
 if(im.width>3000 || im.height>3000){png_image_free(&im);return NULL;}
 im.format=PNG_FORMAT_RGB;
 unsigned char *out=malloc(PNG_IMAGE_SIZE(im));if(!out){png_image_free(&im);return NULL;}
 png_color black={0,0,0};
 if(!png_image_finish_read(&im,&black,out,0,NULL)){free(out);png_image_free(&im);return NULL;}
 *w=im.width;*h=im.height;return out;
}
/* Decoded art waits here until the screen thread copies it into the texture. */
static unsigned int art_back[ART_N*ART_N] __attribute__((aligned(64)));static unsigned int art_back_tone;
/* Center-crops to a square and box-filters down to ART_N x ART_N. Runs on the metadata thread. */
static int load_art(const char *path,int off,int len) {
 if(len<=0 || len>4*1024*1024)return 0;
 int fd=sceIoOpen(path,PSP_O_RDONLY,0);if(fd<0)return 0;
 unsigned char *blob=malloc(len);
 if(!blob || sceIoLseek32(fd,off,PSP_SEEK_SET)<0){sceIoClose(fd);free(blob);return 0;}
 /* 32 KB pieces, so the audio thread's stream reads can get in between. */
 for(int got=0;got<len;) {
  int n=len-got<32768?len-got:32768;
  if(sceIoRead(fd,blob+got,n)!=n){sceIoClose(fd);free(blob);return 0;}
  got+=n;sceKernelDelayThread(2000);
 }
 sceIoClose(fd);
 int w=0,h=0;unsigned char *rgb=blob[0]==0xFF?decode_jpeg(blob,len,&w,&h):decode_png(blob,len,&w,&h);
 free(blob);if(!rgb)return 0;
 int side=w<h?w:h,ox=(w-side)/2,oy=(h-side)/2;unsigned int tr=0,tg=0,tb=0;
 for(int y=0;y<ART_N;y++)for(int x=0;x<ART_N;x++) {
  int x0=ox+x*side/ART_N,x1=ox+(x+1)*side/ART_N,y0=oy+y*side/ART_N,y1=oy+(y+1)*side/ART_N;if(x1<=x0)x1=x0+1;if(y1<=y0)y1=y0+1;
  unsigned int r=0,g=0,b=0,n=0;
  for(int sy=y0;sy<y1;sy++)for(int sx=x0;sx<x1;sx++){unsigned char *p=rgb+(sy*w+sx)*3;r+=p[0];g+=p[1];b+=p[2];n++;}
  r/=n;g/=n;b/=n;tr+=r;tg+=g;tb+=b;
  art_back[y*ART_N+x]=0xff000000|(b<<16)|(g<<8)|r;
 }
 free(rgb);
 art_back_tone=((tr/(ART_N*ART_N))<<16)|((tg/(ART_N*ART_N))<<8)|(tb/(ART_N*ART_N));
 return 1;
}
