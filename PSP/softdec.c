/* Software decoders for lossless files (FLAC, WAV) using dr_flac and dr_wav.
   Output is always 16-bit, at most 2 channels and at most 48 kHz, which is what the
   PSP's audio output accepts. Hi-res rates (88.2/96/176.4/192 kHz) are averaged down by 2 or 4. */
#include <pspiofilemgr.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#include "third_party/dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO
#include "third_party/dr_wav.h"
#include "softdec.h"
#include "m4a.h"
#include "radio.h"

struct SoftDec {
 SceUID fd;M4a *m4a;Radio *radio;short m4a_buf[2048*2];int m4a_have,m4a_used;int is_flac,src_ch,ch,factor,rate;
 unsigned long long frames;
 drflac *flac;drwav wav;
 short scratch[SOFT_CHUNK*4*8];
};

static size_t io_read(void *u,void *out,size_t n){int r=sceIoRead(((SoftDec *)u)->fd,out,n);return r>0?(size_t)r:0;}
static int io_seek(SoftDec *s,int off,int origin){int w=origin==0?PSP_SEEK_SET:origin==1?PSP_SEEK_CUR:PSP_SEEK_END;return sceIoLseek32(s->fd,off,w)>=0;}
static drflac_bool32 flac_seek(void *u,int off,drflac_seek_origin o){return io_seek(u,off,o==DRFLAC_SEEK_SET?0:o==DRFLAC_SEEK_CUR?1:2);}
static drwav_bool32 wav_seek(void *u,int off,drwav_seek_origin o){return io_seek(u,off,o==DRWAV_SEEK_SET?0:o==DRWAV_SEEK_CUR?1:2);}
static drflac_bool32 flac_tell(void *u,drflac_int64 *c){int p=sceIoLseek32(((SoftDec *)u)->fd,0,PSP_SEEK_CUR);if(p<0)return 0;*c=p;return 1;}
static drwav_bool32 wav_tell(void *u,drwav_int64 *c){int p=sceIoLseek32(((SoftDec *)u)->fd,0,PSP_SEEK_CUR);if(p<0)return 0;*c=p;return 1;}

static int ends_with(const char *s,const char *ext){int n=strlen(s),m=strlen(ext);if(n<=m || s[n-m-1]!='.')return 0;for(int i=0;i<m;i++)if(tolower((unsigned char)s[n-m+i])!=ext[i])return 0;return 1;}
int soft_kind(const char *path){if(radio_is_url(path))return 4;return ends_with(path,"flac")?1:ends_with(path,"wav")?2:(ends_with(path,"m4a") || ends_with(path,"m4b") || ends_with(path,"mp4"))?3:0;}

SoftDec *soft_open(const char *path,int *rate,int *channels,unsigned long long *frames,int *error){return soft_open_at(path,0,rate,channels,frames,error);}
SoftDec *soft_open_at(const char *path,int start_s,int *rate,int *channels,unsigned long long *frames,int *error) {
 int kind=soft_kind(path);*error=0;if(!kind){*error=-1;return NULL;}
 SoftDec *s=calloc(1,sizeof(SoftDec));if(!s){*error=-2;return NULL;}
 if(kind==4){s->fd=-1;s->radio=radio_open_at(path,start_s,rate,channels,frames,error);if(!s->radio){free(s);return NULL;}s->ch=2;s->factor=1;s->rate=*rate;return s;} /* a live station (no length), or an episode */
 if(kind==3){s->fd=-1;s->m4a=m4a_open(path,rate,channels,frames,error);if(!s->m4a){free(s);return NULL;}s->ch=2;s->factor=1;s->rate=*rate;s->frames=*frames;return s;}
 s->fd=sceIoOpen(path,PSP_O_RDONLY,0);if(s->fd<0){*error=s->fd;free(s);return NULL;}
 unsigned int r;
 if(kind==1) {
  s->is_flac=1;s->flac=drflac_open(io_read,flac_seek,flac_tell,s,NULL);
  if(!s->flac){*error=-3;sceIoClose(s->fd);free(s);return NULL;}
  s->src_ch=s->flac->channels;r=s->flac->sampleRate;s->frames=s->flac->totalPCMFrameCount;
 }else {
  if(!drwav_init(&s->wav,io_read,wav_seek,wav_tell,s,NULL)){*error=-4;sceIoClose(s->fd);free(s);return NULL;}
  s->src_ch=s->wav.channels;r=s->wav.sampleRate;s->frames=s->wav.totalPCMFrameCount;
 }
 if(s->src_ch<1 || s->src_ch>8 || r<8000 || r>192000){*error=-5;soft_close(s);return NULL;}
 s->ch=s->src_ch>2?2:s->src_ch;s->factor=r>96000?4:r>48000?2:1;s->rate=r/s->factor;
 *rate=s->rate;*channels=s->ch;*frames=s->frames/s->factor;return s;
}

static unsigned long long raw_read(SoftDec *s,unsigned long long n,short *out){return s->is_flac?drflac_read_pcm_frames_s16(s->flac,n,out):drwav_read_pcm_frames_s16(&s->wav,n,out);}

int soft_read(SoftDec *s,short *out,int frames) {
 if(frames>SOFT_CHUNK)frames=SOFT_CHUNK;
 if(s->radio)return radio_read(s->radio,out,frames);
 if(s->m4a){
  if(s->m4a_used>=s->m4a_have){int n=m4a_read(s->m4a,s->m4a_buf);if(n<=0)return n;s->m4a_have=n;s->m4a_used=0;}
  int n=s->m4a_have-s->m4a_used;if(n>frames)n=frames;memcpy(out,s->m4a_buf+s->m4a_used*2,n*4);s->m4a_used+=n;return n;
 }
 int got=(int)raw_read(s,(unsigned long long)frames*s->factor,s->scratch);
 int n=got/s->factor,sc=s->src_ch,f=s->factor;
 for(int i=0;i<n;i++)for(int c=0;c<s->ch;c++) {
  int sum=0;for(int k=0;k<f;k++)sum+=s->scratch[(i*f+k)*sc+c];
  out[i*s->ch+c]=(short)(sum/f);
 }
 return n;
}

int soft_seek(SoftDec *s,unsigned long long frame) {
 if(s->radio)return 0;
 if(s->m4a){s->m4a_have=s->m4a_used=0;return m4a_seek(s->m4a,frame);}
 unsigned long long raw=frame*s->factor;
 return s->is_flac?drflac_seek_to_pcm_frame(s->flac,raw):drwav_seek_to_pcm_frame(&s->wav,raw);
}

void soft_close(SoftDec *s) {
 if(!s)return;
 if(s->radio){radio_close(s->radio);free(s);return;}
 if(s->m4a){m4a_close(s->m4a);free(s);return;}
 if(s->is_flac){if(s->flac)drflac_close(s->flac);}else drwav_uninit(&s->wav);
 if(s->fd>=0)sceIoClose(s->fd);
 free(s);
}
