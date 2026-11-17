/* M4A / MP4 audio: the container is read by mp4.c, and each AAC frame goes raw (no ADTS header) to
   the PSP's hardware decoder (sceAudiocodec), as Sony's MP4 library passes it.
   Only AAC-LC is decoded; HE-AAC files play their AAC-LC core. ALAC is not supported. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <pspaudiocodec.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "mp4.h"
#include "m4a.h"

int aac_frames,aac_peak; /* decoded frames and loudest sample since last read (video.log) */
#define AAC_IN 0x600   /* the decoder reads this many bytes from the input every call */

struct M4a {
 SceUID fd;
 unsigned long codec[65] __attribute__((aligned(64)));
 unsigned char in[AAC_IN+64] __attribute__((aligned(64)));
 short out[1024*2*2] __attribute__((aligned(64)));
 unsigned int *offset,*size;
 int samples,next,rate,channels,object,freq_index,codec_ready;
};

/* Descriptor length in an esds box: up to four 7-bit bytes. */
static int desc_len(const unsigned char *b,int *p,int n){int v=0;for(int i=0;i<4 && *p<n;i++){int c=b[(*p)++];v=(v<<7)|(c&127);if(!(c&128))break;}return v;}

static int parse_audio(M4a *m,unsigned long long *frames) {
 Mp4Track t;unsigned char b[64];
 if(!mp4_find_track(m->fd,"soun",&t))return -1;
 if(t.stsd_len<16+28 || !mp4_rd(m->fd,t.stsd,b,16+28))return -2;
 if(memcmp(b+12,"mp4a",4))return -3; /* e.g. ALAC */
 m->channels=(b[16+16]<<8)|b[16+17];m->rate=mp4_be32(b+16+24)>>16;
 /* esds inside mp4a: AudioSpecificConfig gives the object type, rate index and channels */
 unsigned int el,esds=mp4_find_box(m->fd,t.stsd+16+28,t.stsd+t.stsd_len,"esds",&el);
 m->object=2;m->freq_index=-1;
 if(esds && el<512) {
  unsigned char e[512];
  if(mp4_rd(m->fd,esds,e,el)) {
   int p=4;
   while(p<(int)el) {
    int tag=e[p++],n=desc_len(e,&p,el);
    if(tag==3){p+=3;continue;}
    if(tag==4){p+=13;continue;}
    if(tag==5 && n>=2 && p+2<=(int)el){m->object=e[p]>>3;m->freq_index=((e[p]&7)<<1)|(e[p+1]>>7);int ch=(e[p+1]>>3)&15;if(ch)m->channels=ch;break;}
    p+=n;
   }
  }
 }
 static const int rates[]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};
 if(m->object==5 || m->object==29)m->object=2; /* HE-AAC: decode the core */
 if(m->freq_index>=0 && m->freq_index<13)m->rate=rates[m->freq_index];
 else for(int i=0;i<13;i++)if(rates[i]==m->rate)m->freq_index=i;
 if(m->freq_index<0 || m->rate>48000 || m->object!=2 || m->channels<1 || m->channels>2)return -4;
 int n=mp4_sample_table(m->fd,&t,&m->offset,&m->size);if(n<=0)return n;
 m->samples=n;*frames=(unsigned long long)n*1024;
 return 1;
}
static int codec_start(M4a *m) {
 memset(m->codec,0,sizeof(m->codec));
 m->codec[10]=m->rate; /* 0x28: sample rate */
 me_enter();
 int r=sceAudiocodecCheckNeedMem(m->codec,PSP_CODEC_AAC);
 if(r>=0)r=sceAudiocodecGetEDRAM(m->codec,PSP_CODEC_AAC);
 if(r>=0){m->codec_ready=1;r=sceAudiocodecInit(m->codec,PSP_CODEC_AAC);}
 me_leave();if(r<0)return r;
 return 0;
}

M4a *m4a_open(const char *path,int *rate,int *channels,unsigned long long *frames,int *error) {
 M4a *m=memalign(64,sizeof(M4a));if(!m){*error=-2;return NULL;}
 memset(m,0,sizeof(*m));
 m->fd=sceIoOpen(path,PSP_O_RDONLY,0);if(m->fd<0){*error=m->fd;free(m);return NULL;}
 int r=parse_audio(m,frames);
 if(r!=1){*error=r<0?r:-1;m4a_close(m);return NULL;}
 r=codec_start(m);if(r<0){*error=r;m4a_close(m);return NULL;}
 *rate=m->rate;*channels=2;*error=0;return m;
}

/* Decodes the next frame into out as stereo; returns frames, 0 at the end, negative on error.
   Each frame goes in raw, as Sony's own MP4 library passes it: on a real PSP a frame with an ADTS
   header in front decoded "successfully" to pure silence (pspautotests audio/audiocodec, recorded
   on hardware, decodes raw frames). */
int m4a_read(M4a *m,short *out) {
 for(int guard=0;guard<8;guard++) {
  if(m->next>=m->samples)return 0;
  int n=m->size[m->next];if(n>AAC_IN)n=AAC_IN;
  unsigned char *a=m->in;
  if(!mp4_rd(m->fd,m->offset[m->next],a,n))return -10;
  memset(a+n,0,AAC_IN-n);
  m->next++;
  sceKernelDcacheWritebackInvalidateRange(m->in,sizeof(m->in));
  sceKernelDcacheWritebackInvalidateRange(m->out,sizeof(m->out));
  m->codec[6]=(unsigned long)m->in;m->codec[8]=(unsigned long)m->out;
  me_enter();int r=sceAudiocodecDecode(m->codec,PSP_CODEC_AAC);me_leave();
  sceKernelDcacheInvalidateRange(m->out,sizeof(m->out)); /* the Media Engine wrote it past the cache */
  if(r<0)continue; /* a bad frame is skipped, like the hardware does */
  int bytes=(int)m->codec[9]; /* bytes written: 0 for the first two frames, which prime the decoder */
  if(bytes<=0)continue;
  int fr=bytes/4;if(fr>1024*2)fr=1024*2;
  aac_frames++;for(int k=0;k<fr*2;k+=16){int v=m->out[k];if(v<0)v=-v;if(v>aac_peak)aac_peak=v;}
  memcpy(out,m->out,fr*4);return fr;
 }
 return -11;
}

/* Seeking always follows a fresh open, whose decoder drops its first two frames while it warms
   up, so start two frames early: the output then begins at the frame asked for. */
int m4a_seek(M4a *m,unsigned long long frame){long long s=(long long)(frame/1024);if(s>=m->samples)return 0;s-=2;m->next=s<0?0:(int)s;return 1;}

void m4a_close(M4a *m) {
 if(!m)return;
 if(m->codec_ready){me_enter();sceAudiocodecReleaseEDRAM(m->codec);me_leave();}
 if(m->fd>=0)sceIoClose(m->fd);
 free(m->offset);free(m->size);free(m);
}
