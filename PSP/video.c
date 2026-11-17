/* MP4 video: mp4.c reads the H.264 track; each frame is rewritten from MP4's length-prefixed
   NAL units to the start-code form the Media Engine reads, handed to it by DMA
   (sceMpegBasePESpacketCopy) and decoded by sceMpegAvcDecode, which also converts the colours
   straight into video_pixels. The CPU does no per-pixel work, which keeps playback cheap on the
   battery. The hardware takes H.264 Baseline or Main profile up to 720x480. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <psputility.h>
#include <pspmpeg.h>
#include <pspmpegbase.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <stdio.h>
#include "mp4.h"
#include "video.h"

#define LLI_BLOCK 2048
unsigned int *video_pixels;int video_stride;
void (*video_log)(const char *line); /* main.c appends these to video.log next to the EBOOT */
static void vlog(const char *what,int code){if(video_log){char b[96];snprintf(b,sizeof(b),"%s: %08X",what,(unsigned int)code);video_log(b);}}
static int decode_logs;
static int need_params=1;int video_params_sent; /* stream setup goes in once per start or seek */
static void find_es_buffer(void);

static struct {
 SceUID fd;int open,mpeg_ready;char path[512];int reopens;
 unsigned int *offset,*size;unsigned char *sync;int count,next;
 int length_size;unsigned char *params;int params_len; /* SPS and PPS with start codes */
 unsigned char *raw,*au,*au_mem;int au_cap;SceMpegLLI *lli;int lli_cap;
 void *ddr,*mpeg_data;SceMpeg mpeg;SceMpegRingbuffer ring;void *es;SceMpegAu *mau;
 int pics,pic_base;unsigned int frame_us;
 unsigned int *pix_mem,*slot[4];int nslots,cur,q[4],qn; /* picture buffers: shown, waiting, free */
 unsigned char *pts_off;int lead; /* B-frames: each frame's display offset in frames and how many frames the decoder holds back */
} V;

static int modules_loaded;
static int load_modules(void) {
 if(modules_loaded)return 0;
 int r=sceUtilityLoadModule(PSP_MODULE_AV_MPEGBASE);
 if(r<0 && r!=(int)0x80111102)return r; /* already loaded is fine */
 modules_loaded=1;return 0;
}

const char *video_error_text(int code) {
 switch(code) {
 case VIDEO_ERR_OPEN:return "Can't open this video";
 case VIDEO_ERR_NOVIDEO:return "No video track in this file";
 case VIDEO_ERR_CODEC:return "Convert this video with HandBrake's PSP (ZOID) preset";
 case VIDEO_ERR_SIZE:return "Convert this video with HandBrake's PSP (ZOID) preset";
 case VIDEO_ERR_PROFILE:return "Convert this video with HandBrake's PSP (ZOID) preset";
 case VIDEO_ERR_MEMORY:return "Not enough memory for this video";
 case VIDEO_ERR_ENGINE:return "The video decoder didn't start";
 case VIDEO_ERR_BFRAMES:return "Convert this video with HandBrake's PSP (ZOID) preset";
 case VIDEO_ERR_DECODE:return "Convert this video with HandBrake's PSP (ZOID) preset";
 }
 return "Can't play this video";
}

/* avcC: profile, level, NAL length size, then the SPS and PPS sets, rewritten with start codes. */
static int parse_avcc(const unsigned char *c,int n,VideoInfo *info) {
 if(n<7 || c[0]!=1)return 0;
 info->profile=c[1];info->level=c[3];V.length_size=(c[4]&3)+1;
 V.params=malloc(n+64);if(!V.params)return 0;V.params_len=0;
 int p=5;
 for(int set=0;set<2;set++) {
  if(p>=n)return 0;
  int k=set?c[p]:c[p]&31;p++;
  for(int i=0;i<k;i++) {
   if(p+2>n)return 0;
   int len=(c[p]<<8)|c[p+1];p+=2;if(p+len>n)return 0;
   unsigned char *d=V.params+V.params_len;d[0]=d[1]=d[2]=0;d[3]=1;memcpy(d+4,c+p,len);V.params_len+=4+len;p+=len;
  }
 }
 return V.params_len>0;
}

static int start_engine(VideoInfo *info) {
 int r=load_modules();vlog("load mpegbase module",r);if(r<0)return VIDEO_ERR_ENGINE;
 r=sceMpegInit();vlog("sceMpegInit",r);if(r<0)return VIDEO_ERR_ENGINE;
 V.ddr=memalign(0x400000,0x400000); /* the Media Engine reads access units from here */
 int size=sceMpegQueryMemSize(0);V.mpeg_data=size>0?memalign(64,size):NULL;
 V.mau=memalign(64,64);
  vlog("ME buffer (4 MB)",(int)V.ddr);vlog("mpeg data size",size);
 if(!V.ddr || !V.mpeg_data || !V.mau)return VIDEO_ERR_MEMORY;
 memset(V.mau,0,64);
 sceMpegRingbufferConstruct(&V.ring,0,NULL,0,NULL,NULL);
 r=sceMpegCreate(&V.mpeg,V.mpeg_data,size,&V.ring,video_stride,0,(int)V.ddr);vlog("sceMpegCreate",r);if(r<0)return VIDEO_ERR_ENGINE;
 V.mpeg_ready=1;
 SceMpegAvcMode mode={-1,SCE_MPEG_AVC_FORMAT_8888};r=sceMpegAvcDecodeMode(&V.mpeg,&mode);vlog("sceMpegAvcDecodeMode",r);
 V.es=sceMpegMallocAvcEsBuf(&V.mpeg);vlog("sceMpegMallocAvcEsBuf",(int)V.es);if(!V.es)return VIDEO_ERR_ENGINE;
 r=sceMpegInitAu(&V.mpeg,V.es,V.mau);vlog("sceMpegInitAu",r);
 find_es_buffer();
 (void)info;return VIDEO_OK;
}

int video_open(const char *path,VideoInfo *info) {
 video_close();memset(info,0,sizeof(*info));
 decode_logs=0;
 V.fd=sceIoOpen(path,PSP_O_RDONLY,0);if(V.fd<0)return VIDEO_ERR_OPEN;
 V.open=1;snprintf(V.path,sizeof(V.path),"%s",path);
 Mp4Track t,a;
 if(!mp4_find_track(V.fd,"vide",&t)){video_close();return VIDEO_ERR_NOVIDEO;}
 info->has_audio=mp4_find_track(V.fd,"soun",&a);
 unsigned char e[96];
 if(t.stsd_len<8+86 || !mp4_rd(V.fd,t.stsd,e,8+86)){video_close();return VIDEO_ERR_NOVIDEO;}
 if(memcmp(e+12,"avc1",4) && memcmp(e+12,"avc3",4)){video_close();return VIDEO_ERR_CODEC;}
 info->width=(e[8+32]<<8)|e[8+33];info->height=(e[8+34]<<8)|e[8+35];
 unsigned int cl,avcc=mp4_find_box(V.fd,t.stsd+8+86,t.stsd+t.stsd_len,"avcC",&cl);
 unsigned char *c=cl<4096?malloc(cl+1):NULL;
 int ok=avcc && c && mp4_rd(V.fd,avcc,c,cl) && parse_avcc(c,cl,info);free(c);
 if(!ok){video_close();return VIDEO_ERR_CODEC;}
 if(info->width<16 || info->height<16 || info->width>720 || info->height>576 || info->width*info->height>720*480){video_close();return VIDEO_ERR_SIZE;}
 if(info->profile!=66 && info->profile!=77){video_close();return VIDEO_ERR_PROFILE;}
 V.count=mp4_sample_table(V.fd,&t,&V.offset,&V.size);if(V.count<=0){video_close();return VIDEO_ERR_NOVIDEO;}
 V.sync=mp4_sync_samples(V.fd,&t,V.count);
 /* B-frames (frames stored out of display order) show up as varying composition offsets in ctts.
    Each frame gets its display time as its PTS, as in a PSMF movie, so the decoder releases pictures
    in display order; it holds back up to `lead` frames to do that, so the player decodes that many
    frames early (video_next_ms) and the pictures still land on time. */
 {unsigned int ctl,ctts=mp4_find_box(V.fd,t.stbl,t.stbl+t.stbl_len,"ctts",&ctl);unsigned char cb[8];
  if(ctts && ctl>=16 && t.timescale && t.duration && mp4_rd(V.fd,ctts,cb,8)){
   vlog("reading display offsets (ctts), bytes",ctl);
   /* the table is read in small pieces: a full film's runs about 1 MB, more than the PSP has free */
   unsigned int ents=mp4_be32(cb+4);if(ents>(ctl-8)/8)ents=(ctl-8)/8;
   unsigned char *buf=malloc(4096);int lo=0x7FFFFFFF,hi=-0x7FFFFFFF-1;
   for(int pass=0;pass<2 && buf;pass++){int n=0;
    for(unsigned int e=0;e<ents && n<V.count;){unsigned int m=ents-e;if(m>512)m=512;
     if(!mp4_rd(V.fd,ctts+8+e*8,buf,m*8)){ents=e;break;}
     for(unsigned int j=0;j<m && n<V.count;j++){unsigned int k=mp4_be32(buf+j*8);int off=(int)mp4_be32(buf+j*8+4); /* signed in version 1 */
      if(!pass){if(off<lo)lo=off;if(off>hi)hi=off;n+=k;continue;}
      /* stored as whole frames after the earliest; one byte per frame */
      int f=(int)(((long long)(off-lo)*V.count+t.duration/2)/t.duration);if(f>255)f=255;
      while(k-- && n<V.count)V.pts_off[n++]=(unsigned char)f;}
     e+=m;}
    if(!pass){if(hi<=lo)break;V.pts_off=calloc(V.count,1);if(!V.pts_off){vlog("B-frames: no memory for display offsets",V.count);break;}}
    else{V.lead=(int)(((long long)(hi-lo)*V.count+t.duration/2)/t.duration);if(V.lead<1)V.lead=1;if(V.lead>4)V.lead=4;
     char l[80];snprintf(l,sizeof(l),"B-frames: display offsets vary, decoding %d frames ahead",V.lead);if(video_log)video_log(l);}
   }
   free(buf);
  }}
 /* Level 3.1 B-frame films (PC encodes: 4 reference frames, weighted B prediction) make a real PSP's
    decoder fail from the first frame and then hang the whole console, battery-pull hard, even with
    the level relabeled 3.0. Sony's own PSP spec is Main profile level 3.0, so anything above that
    with B-frames is refused before the decoder sees it. */
 if(V.pts_off && info->level>30){vlog("B-frames above level 3.0: refused, level",info->level);video_close();return VIDEO_ERR_BFRAMES;}
 unsigned int biggest=0;for(int i=0;i<V.count;i++)if(V.size[i]>biggest)biggest=V.size[i];
 if(biggest>2*1024*1024){video_close();return VIDEO_ERR_SIZE;}
 V.au_cap=biggest*2+V.params_len+64;
 V.raw=memalign(64,biggest+64);V.au_mem=memalign(64,V.au_cap+64);V.au=V.au_mem?V.au_mem+64:NULL;
 V.lli_cap=V.au_cap/LLI_BLOCK+2;V.lli=memalign(64,sizeof(SceMpegLLI)*V.lli_cap);
 video_stride=info->width<=512?512:1024;
 /* Picture buffers. The decoder can hand back two pictures in one call (at a key frame, and about every
    third frame with B-frames); each goes to its own buffer and the second is shown on the next tick.
    With one shared buffer the second overwrote the first, which made B-frame films choppy on a PSP.
    The last buffer gets room below it, since the GPU reads the texture 512 rows tall. */
 V.nslots=V.pts_off?4:2;
 /* Each buffer holds whole 16-row blocks: the decoder writes them all, so a 480x260 film (HandBrake
    keeps the shape) wrote 272 rows, and the 12 extra flickered over the top of the next buffer. */
 {unsigned int rows=(info->height+15)&~15u,one=video_stride*rows*4;one=(one+63)&~63u;
  V.pix_mem=memalign(64,one*V.nslots+video_stride*(512>rows?512-rows:0)*4+65536);
  for(int k=0;k<V.nslots && V.pix_mem;k++)V.slot[k]=(unsigned int *)((unsigned char *)V.pix_mem+k*one);}
 V.cur=0;V.qn=0;video_pixels=V.slot[0];
 if(!V.raw || !V.au || !V.lli || !V.pix_mem){video_close();return VIDEO_ERR_MEMORY;}
 memset(video_pixels,0,video_stride*info->height*4);sceKernelDcacheWritebackInvalidateAll();
 info->frames=V.count;
 info->duration_ms=t.timescale?(int)(t.duration*1000/t.timescale):V.count*33;
 info->frame_us=V.count?(unsigned int)((unsigned long long)info->duration_ms*1000/V.count):33333;
 if(!info->frame_us)info->frame_us=33333;
 V.frame_us=info->frame_us;
 me_enter();int r=start_engine(info);me_leave();vlog("start result",r);
 {char b[96];snprintf(b,sizeof(b),"video %dx%d profile %d level %d, %d frames, %d ms",info->width,info->height,info->profile,info->level,V.count,info->duration_ms);if(video_log)video_log(b);}
if(r!=VIDEO_OK){video_close();return r;}
 V.next=0;V.pics=0;V.pic_base=0;need_params=1;
 return VIDEO_OK;
}

/* One frame, MP4 length prefixes replaced by start codes. The SPS/PPS (stream setup) go in front
   of the first frame only (and after a seek), as on a real PSP: the decoder wants them first, ahead
   of the access unit delimiter, and repeating them at every key frame crashed it at frame 69. */
/* A frame's bytes. If a read fails, the file is opened again and the read retried: on a real PSP
   the open handle went stale mid-film (the sound, which reopens its file, kept playing while every
   picture read failed). The codes go to video.log. */
static int read_frame(int i) {
 unsigned int n=V.size[i];
 for(int attempt=0;attempt<2;attempt++) {
  int s=sceIoLseek32(V.fd,V.offset[i],PSP_SEEK_SET),r=s<0?s:sceIoRead(V.fd,V.raw,n);
  if(r==(int)n)return 1;
  if(video_log && V.reopens<40){char b[128];snprintf(b,sizeof(b),"frame %d read failed (seek %08X, read %08X of %u bytes); reopening the file",i,(unsigned int)s,(unsigned int)r,n);video_log(b);}
  if(V.reopens>=1000)return 0; /* hopeless: stop hammering the Memory Stick */
  V.reopens++;if(V.fd>=0)sceIoClose(V.fd);V.fd=sceIoOpen(V.path,PSP_O_RDONLY,0);
  if(V.fd<0){vlog("reopen failed",V.fd);return 0;}
 }
 return 0;
}
static int build_au(int i) {
 unsigned int n=V.size[i];if(!read_frame(i))return -1;
 int o=0;
 for(unsigned int p=0;p+V.length_size<=n;) {
  unsigned int len=0;for(int k=0;k<V.length_size;k++)len=(len<<8)|V.raw[p+k];
  p+=V.length_size;if(len>n-p || o+4+(int)len+V.params_len>V.au_cap)break;
  if(need_params){memcpy(V.au+o,V.params,V.params_len);o+=V.params_len;need_params=0;video_params_sent++;}
  V.au[o]=V.au[o+1]=V.au[o+2]=0;V.au[o+3]=1;memcpy(V.au+o+4,V.raw+p,len);o+=4+len;p+=len;
 }
 return o;
}

/* Access units go to the ES buffer sceMpegMallocAvcEsBuf handed out. On a PSP that buffer is in
   the Media Engine's own memory, which only a DMA (sceMpegBasePESpacketCopy) can reach, and
   mpeg.prx keeps its ES buffer table just before the ddrTop pointer in its data:
   [count][in use][address of ES 1][address of ES 2] (read off a real PSP's video.log).
   Emulators decode straight from the DMA list, so there the destination doesn't matter. */
static unsigned int es_addr,es_cap;
static void find_es_buffer(void) {
 es_addr=0;es_cap=0x18000;
 unsigned int *w=(unsigned int *)V.mpeg_data,base=(unsigned int)V.ddr;int words=sceMpegQueryMemSize(0)/4;char b[120];
 for(int i=0;i<words && !es_addr;i++) {
  if(w[i]!=base)continue;
  for(int k=i-16;k<i && !es_addr;k++) {
   if(k<0 || w[k]<1 || w[k]>4)continue;
   unsigned int e1=w[k+2],e2=w[k+3];
   if(e1>=0x1000 && e1<0x400000 && e2>e1 && e2<0x400000 && !(e1&63)) {
    int idx=(int)(unsigned int)V.es-1;if(idx<0 || idx>=(int)w[k])idx=0;
    es_addr=w[k+2+idx];es_cap=e2-e1;
    snprintf(b,sizeof(b),"ES buffer table at +%04X: %u buffers, ES %d at ME %08X (%u bytes)",k*4,w[k],idx+1,es_addr,es_cap);if(video_log)video_log(b);
   }
  }
 }
 if(!es_addr && video_log)video_log("no ES buffer table found; using ddrTop+0x10000");
}
/* On a PSP each 2 KB block goes as its own one-block DMA, the form proven on hardware (a linked
   list of blocks crashed it); emulators decode from the DMA list, so there it stays one list. */
static void dma_to(unsigned char *src,unsigned char *dst,int n) {
 int blocks=(n+LLI_BLOCK-1)/LLI_BLOCK;if(blocks>V.lli_cap)blocks=V.lli_cap;
 sceKernelDcacheWritebackInvalidateAll();
 if(es_addr) {
  for(int b=0;b<blocks;b++){int off=b*LLI_BLOCK,len=n-off<LLI_BLOCK?n-off:LLI_BLOCK;V.lli[0].pSrc=src+off;V.lli[0].pDst=dst+off;V.lli[0].iSize=len;V.lli[0].Next=NULL;sceKernelDcacheWritebackInvalidateRange(V.lli,64);sceMpegbase_BEA18F91(V.lli);}
  return;
 }
 for(int b=0;b<blocks;b++){int off=b*LLI_BLOCK,len=n-off<LLI_BLOCK?n-off:LLI_BLOCK;V.lli[b].pSrc=src+off;V.lli[b].pDst=dst+off;V.lli[b].iSize=len;V.lli[b].Next=b+1<blocks?&V.lli[b+1]:NULL;}
 sceKernelDcacheWritebackInvalidateAll();sceMpegbase_BEA18F91(V.lli);
}
int video_decode(void) {
 if(!V.mpeg_ready || V.next>=V.count)return VIDEO_END;
 int i=V.next++,n=build_au(i);if(n<=0)return 0;
 if(n>video_largest_frame)video_largest_frame=n;
 if(es_addr && n>(int)es_cap){if(decode_logs<24 && video_log){decode_logs++;video_log("frame larger than the ES buffer; skipped");}return 0;}
 {static int big_logs;if(n>LLI_BLOCK && big_logs<6 && video_log){big_logs++;char b[80];snprintf(b,sizeof(b),"frame %d: %d bytes in %d DMA blocks (before decode)",i,n,(n+LLI_BLOCK-1)/LLI_BLOCK);video_log(b);}}
 if(i<40 && video_log){char b[80];snprintf(b,sizeof(b),"frame %d: %d bytes, shown %d frames later, to the decoder",i,n,V.pts_off?V.pts_off[i]:0);video_log(b);}
 me_enter();
 dma_to(V.au,es_addr?(unsigned char *)es_addr:(unsigned char *)V.ddr+0x10000,n);
 V.mau->iAuSize=n;
 /* real 90 kHz timestamps, as every frame in a PSMF movie carries: DTS in file order, PTS when it is shown */
 {unsigned long long ts=(unsigned long long)i*V.frame_us*9/100+3003,pts=ts+(V.pts_off?(unsigned long long)V.pts_off[i]*V.frame_us*9/100:0);
  V.mau->iDts=(unsigned int)ts;V.mau->iDtsMSB=(unsigned int)(ts>>32);V.mau->iPts=(unsigned int)pts;V.mau->iPtsMSB=(unsigned int)(pts>>32);}
 /* At a key frame the decoder can hand back more than one picture at once, writing each through
    the next buffer pointer in this list; with a single pointer the second went to whatever
    followed it in memory, which crashed a real PSP at the film's second key frame. */
 static void *outs[8] __attribute__((aligned(64)));int fr[4],nf=0;
 for(int k=0;k<V.nslots;k++){int used=k==V.cur;for(int j=0;j<V.qn;j++)if(V.q[j]==k)used=1;if(!used)fr[nf++]=k;}
 if(!nf){fr[nf++]=V.q[0];V.qn--;for(int j=0;j<V.qn;j++)V.q[j]=V.q[j+1];} /* all full: drop the oldest waiting picture */
 for(int k=0;k<8;k++)outs[k]=V.slot[fr[k<nf?k:nf-1]];
 SceInt32 init=0;
 unsigned long long t0=sceKernelGetSystemTimeWide();
 int r=sceMpegAvcDecode(&V.mpeg,V.mau,video_stride,outs,&init);
 {int us=(int)(sceKernelGetSystemTimeWide()-t0);if(us>video_slowest_us)video_slowest_us=us;}
 me_leave();
 /* what changes in mpeg.prx's data around its ES buffer table after each decode (read pointers?) */
 {static unsigned int prev[64];static int have;unsigned int *w=(unsigned int *)V.mpeg_data+0x680/4;
  if((i<=5 || (i>=60 && i<=75)) && video_log && have){for(int k=0;k<64;k++)if(w[k]!=prev[k]){char b[80];snprintf(b,sizeof(b),"  frame %d: +%04X %08X -> %08X",i,0x680+k*4,prev[k],w[k]);video_log(b);}}
  memcpy(prev,w,sizeof(prev));have=1;}
 if((i<=3 || (i>=64 && i<=72)) && video_log){unsigned int *c=(unsigned int *)V.mpeg_data+0xB300/4;char b[120];snprintf(b,sizeof(b),"  frame %d codec: au %08X size %08X status %08X out %08X",i,c[9],c[10],c[2],c[4]);video_log(b);}
 {static int err_logs;int want=(decode_logs<24) || (i>=60 && i<=90) || (r<0 && err_logs<20);if(r<0)err_logs++;
  if(want && video_log){decode_logs++;char b[96];snprintf(b,sizeof(b),"decode frame %d: %d bytes -> %08X, picture %d",i,n,(unsigned int)r,(int)init);video_log(b);}}
 if(r<0)return VIDEO_ERR_DECODE; /* the caller stops after several in a row */
 for(int k=0;k<init && k<2 && V.qn<4;k++)V.q[V.qn++]=fr[k<nf?k:nf-1];
 if(V.qn){V.cur=V.q[0];V.qn--;for(int j=0;j<V.qn;j++)V.q[j]=V.q[j+1];video_pixels=V.slot[V.cur];V.pics++;return 1;}
 return 0;
}

int video_frame_index(void){return V.next;}
int video_largest_frame;
/* When the next frame is due: by its place in the film, not by pictures received (the PSP decoder
   sometimes holds pictures back, and counting them made the player race ahead). */
int video_next_ms(void){int f=V.next-V.lead;if(f<0)f=0;return (int)((unsigned long long)f*V.frame_us/1000);}
int video_slowest_us;

int video_seek(int ms) {
 if(!V.count)return 0;
 int target=(int)((unsigned long long)(ms<0?0:ms)*1000/V.frame_us);if(target>=V.count)target=V.count-1;
 int k=target;if(V.sync)while(k>0 && !V.sync[k])k--;
 V.next=k;V.pic_base=k;V.pics=0;V.qn=0;need_params=1;
 return (int)((unsigned long long)k*V.frame_us/1000);
}

void video_test_stale_file(void){if(V.open && V.fd>=0)sceIoClose(V.fd);}
void video_close(void) {
 if(V.mpeg_ready)me_enter();
 if(V.mpeg_ready){if(V.es)sceMpegFreeAvcEsBuf(&V.mpeg,V.es);sceMpegDelete(&V.mpeg);sceMpegRingbufferDestruct(&V.ring);sceMpegFinish();}
 if(V.mpeg_ready)me_leave();
 if(V.open && V.fd>=0)sceIoClose(V.fd);
 free(V.offset);free(V.size);free(V.sync);free(V.params);free(V.raw);free(V.au_mem);free(V.lli);free(V.pts_off);
 free(V.ddr);free(V.mpeg_data);free(V.mau);free(V.pix_mem);
 memset(&V,0,sizeof(V));V.fd=-1;video_pixels=NULL;
}
