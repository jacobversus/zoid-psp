/* Internet radio: MP3 and AAC (ADTS) streams from Shoutcast/Icecast servers, over HTTP or HTTPS.
   A reader thread (zoid_radio) downloads the stream into a 256 KB ring (about 16 s of 128 kbps
   audio), strips the ICY metadata (the song titles) and reconnects when the stream drops.
   The audio thread takes whole frames out of the ring and decodes them on the PSP's hardware
   decoders (sceAudiocodec MP3 and AAC), so a station costs little more battery than a file.
   Playlist links (.pls, .m3u) are followed to the stream inside. HLS and Ogg are not supported.
   When the ring runs dry, playback waits until it holds about 1.5 s again (buffering). If the
   listener pauses, the stream keeps coming and the oldest audio is dropped. */
#include <pspkernel.h>
#include <pspaudiocodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <malloc.h>
#include <ctype.h>
#include <stddef.h>
#include <stdarg.h>
#include <pspiofilemgr.h>
#include "net.h"
#include "radio.h"
#include "mp4.h"

RadioStatus radio_status;
static int (*keep_going)(void);
static volatile int wifi_config;
static SceUID title_lock=-1;
static struct Radio *volatile current;
static volatile int readers; /* reader threads still running (a closed stream's may linger while it connects) */
int radio_readers_alive(void){return readers;}

#define RING (256*1024)          /* a power of two, so positions can simply wrap */
#define META_N 8
struct Radio {
 unsigned long cb[65] __attribute__((aligned(64)));      /* sceAudiocodec state */
 unsigned char in[0x800] __attribute__((aligned(64)));   /* one compressed frame */
 short out[2048*2] __attribute__((aligned(64)));         /* its PCM */
 char url[1024];
 volatile int stop,refs,dead,error,codec,prebuffer,resync;
 /* on demand (a podcast episode): a file with an end. got: the byte the stream has reached; total: the file size or -1 */
 int ondemand,dur_s,hint_bytes,start_s;volatile int finished,total,got;
 SceUID lock;
 unsigned char *ring;volatile unsigned int wpos,rpos;     /* absolute byte counts */
 struct {unsigned int at;char title[192];} meta[META_N];volatile unsigned int meta_w,meta_r;
 char last_title[192];
 NetConn *volatile conn;
 Http http;
 int rate,codec_ready,have,used,bad_frames;
 unsigned long long played; /* frames output, for the clock lyrics follow */
 short pcm[2048*2];
 char body[16384];unsigned char sniff_buf[16384];
};
typedef struct Radio R;

int radio_is_url(const char *p){return p && (!strncasecmp(p,"http://",7) || !strncasecmp(p,"https://",8));}
int radio_is_episode(const char *p){const char *h;return radio_is_url(p) && (h=strchr(p,'#')) && h[1]=='@';}
void radio_setup(int (*fn)(void),int cfg){keep_going=fn;wifi_config=cfg;if(title_lock<0)title_lock=sceKernelCreateSema("zoid_rtitle",0,1,1,NULL);}
void radio_set_wifi(int cfg){wifi_config=cfg;}
static int wanted(void){return keep_going?keep_going():1;}
/* A short log on the Memory Stick of what streams did, so a problem seen on a real PSP can be traced.
   Kept small: past 48 KB it starts again (the previous one is kept as stream-log-old.txt). */
static char log_path[512];static SceUID log_lock=-1;
void radio_set_log(const char *path){snprintf(log_path,sizeof(log_path),"%s",path);if(log_lock<0)log_lock=sceKernelCreateSema("zoid_rlog",0,1,1,NULL);}
void radio_log(const char *fmt,...) {
 if(!log_path[0] || log_lock<0)return;
 char line[400];int n=snprintf(line,sizeof(line),"%8u ",(unsigned int)(sceKernelGetSystemTimeWide()/1000));
 va_list ap;va_start(ap,fmt);vsnprintf(line+n,sizeof(line)-n-2,fmt,ap);va_end(ap);
 n=strlen(line);line[n++]='\r';line[n++]='\n';
 sceKernelWaitSema(log_lock,1,NULL);
 SceIoStat st;memset(&st,0,sizeof(st));
 if(sceIoGetstat(log_path,&st)>=0 && st.st_size>48*1024){char old[530];snprintf(old,sizeof(old),"%.*s-old.txt",(int)strlen(log_path)-4,log_path);sceIoRemove(old);sceIoRename(log_path,old);}
 int fd=sceIoOpen(log_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
 if(fd>=0){sceIoWrite(fd,line,n);sceIoClose(fd);}
 sceKernelSignalSema(log_lock,1);
}
static unsigned int ms_since(unsigned long long t0){return (unsigned int)((sceKernelGetSystemTimeWide()-t0)/1000);}
static const char *log_host(const char *url,char *out,int cap){int tls,port;char path[8];if(!net_parse_url(url,&tls,out,cap,&port,path,sizeof(path)))snprintf(out,cap,"?");return out;}
const char *radio_error_text(int code) {
 switch(code) {
 case RADIO_E_FORMAT:return "The station's sound is not MP3 or AAC";
 case RADIO_E_HLS:return "The station streams in HLS, which ZOID cannot play";
 case RADIO_E_OGG:return "The station streams Ogg (Vorbis/Opus), which ZOID cannot play";
 case RADIO_E_PLAYLIST:return "The station's playlist link has no stream in it";
 }
 return net_error_text(code);
}
int radio_get_title(char *out,int cap,unsigned int *gen) {
 sceKernelWaitSema(title_lock,1,NULL);snprintf(out,cap,"%s",radio_status.title);if(gen)*gen=radio_status.title_gen;sceKernelSignalSema(title_lock,1);
 return out[0]!=0;
}
static void set_title(const char *t) {
 sceKernelWaitSema(title_lock,1,NULL);snprintf(radio_status.title,sizeof(radio_status.title),"%s",t);
 radio_status.title_ms=radio_status.decoded_ms;radio_status.title_gen++;sceKernelSignalSema(title_lock,1);
}
static void release(R *r) {
 sceKernelWaitSema(r->lock,1,NULL);int last=--r->refs==0;sceKernelSignalSema(r->lock,1);
 if(last){sceKernelDeleteSema(r->lock);free(r->ring);free(r);}
}

/* ---- frame headers ---- */
static int mp3_header(const unsigned char *h,int *len,int *rate,int *spf,int *kbps) {
 if(h[0]!=0xFF || (h[1]&0xE0)!=0xE0)return 0;
 int ver=(h[1]>>3)&3,layer=(h[1]>>1)&3;if(ver==1 || layer!=1)return 0; /* MPEG 1/2/2.5 layer III */
 int bi=h[2]>>4,si=(h[2]>>2)&3,pad=(h[2]>>1)&1;if(!bi || bi==15 || si==3 || (h[3]&3)==2)return 0;
 static const short b1[16]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0},b2[16]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
 static const int sr[3]={44100,48000,32000};
 int b=(ver==3?b1:b2)[bi],s=sr[si];if(ver==2)s/=2;else if(!ver)s/=4;
 *spf=ver==3?1152:576;*len=(ver==3?144000*b/s:72000*b/s)+pad;*rate=s;*kbps=b;
 return *len>=24 && *len<=(int)sizeof(((R *)0)->in);
}
static const int aac_rates[13]={96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350};
static int adts_header(const unsigned char *h,int *len,int *hl,int *rate) {
 if(h[0]!=0xFF || (h[1]&0xF6)!=0xF0)return 0; /* 12-bit sync, layer 00 */
 int fi=(h[2]>>2)&15,fl=((h[3]&3)<<11)|(h[4]<<3)|(h[5]>>5);
 *hl=(h[1]&1)?7:9;if(fi>12 || fl<=*hl || fl>*hl+0x600)return 0;
 *len=fl;*rate=aac_rates[fi];return 1;
}
static int frame_at(int codec,const unsigned char *h,int *len,int *rate) {
 int a,b,c;
 if(codec==1)return mp3_header(h,len,rate,&a,&b);
 return adts_header(h,len,&c,rate);
}
/* Which codec a stream with no useful Content-Type holds: two frames back to back decide it. */
static int sniff(const unsigned char *p,int n) {
 for(int i=0;i+10<n;i++) {
  if(p[i]!=0xFF)continue;
  for(int codec=2;codec>=1;codec--) {
   int len,rate,len2,rate2;
   if(frame_at(codec,p+i,&len,&rate) && i+len+10<n && frame_at(codec,p+i+len,&len2,&rate2) && rate2==rate)return codec;
  }
 }
 return 0;
}

/* ---- the ring ---- */
static void ring_copy(R *r,unsigned int at,unsigned char *dst,int n) {
 unsigned int o=at&(RING-1);int first=RING-o;if(first>n)first=n;
 memcpy(dst,r->ring+o,first);if(n>first)memcpy(dst+first,r->ring,n-first);
}
static void ring_put(R *r,const unsigned char *p,int n) {
 if(n>RING){p+=n-RING;n=RING;}
 sceKernelWaitSema(r->lock,1,NULL);
 if(r->wpos-r->rpos+n>RING){r->rpos=r->wpos+n-RING;r->resync=1;} /* paused or stalled: drop the oldest */
 unsigned int o=r->wpos&(RING-1);int first=RING-o;if(first>n)first=n;
 memcpy(r->ring+o,p,first);if(n>first)memcpy(r->ring,p+first,n-first);
 r->wpos+=n;
 sceKernelSignalSema(r->lock,1);
}
/* Copies the next whole frame into r->in: its length, or 0 when more data is needed. */
static int next_frame(R *r,int *header_len) {
 int got=0;
 sceKernelWaitSema(r->lock,1,NULL);
 for(;;) {
  unsigned int fill=r->wpos-r->rpos;if(fill<10)break;
  unsigned char h[10];ring_copy(r,r->rpos,h,10);
  int len,rate,hl=0,ok=r->codec==1?mp3_header(h,&len,&rate,&hl,&hl):adts_header(h,&len,&hl,&rate);
  if(r->codec==1)hl=0;
  if(!ok){r->rpos++;r->resync=1;continue;}
  if(fill<(unsigned int)len+10)break;
  if(r->resync) { /* after a gap, a frame only counts when another follows it */
   unsigned char h2[10];int l2,rt2;ring_copy(r,r->rpos+len,h2,10);
   if(!frame_at(r->codec,h2,&l2,&rt2) || rt2!=rate){r->rpos++;continue;}
   r->resync=0;
  }
  if(r->rate && rate!=r->rate){r->rpos+=len;continue;} /* the output runs at one rate */
  ring_copy(r,r->rpos,r->in,len);r->rpos+=len;got=len;*header_len=hl;break;
 }
 sceKernelSignalSema(r->lock,1);
 return got;
}

/* ---- decoding ---- */
static int codec_init(R *r) {
 memset(r->cb,0,sizeof(r->cb));
 int type=r->codec==1?PSP_CODEC_MP3:PSP_CODEC_AAC;
 if(r->codec==2)r->cb[10]=r->rate;
 me_enter();
 int e=sceAudiocodecCheckNeedMem(r->cb,type);
 if(e>=0)e=sceAudiocodecGetEDRAM(r->cb,type);
 if(e>=0){r->codec_ready=1;e=sceAudiocodecInit(r->cb,type);}
 me_leave();
 return e;
}
/* Decodes the frame in r->in into r->pcm: frames of stereo PCM (0 for a frame that primes or fails). */
static int decode(R *r,int len,int hl) {
 int spf=1152,type=PSP_CODEC_MP3;
 if(r->codec==1){int a,b,c;mp3_header(r->in,&a,&b,&spf,&c);r->cb[7]=r->cb[10]=len;r->cb[9]=spf*4;}
 else{type=PSP_CODEC_AAC;memmove(r->in,r->in+hl,len-hl);memset(r->in+len-hl,0,0x600-(len-hl));}
 sceKernelDcacheWritebackInvalidateRange(r->in,sizeof(r->in));
 sceKernelDcacheWritebackInvalidateRange(r->out,sizeof(r->out));
 r->cb[6]=(unsigned long)r->in;r->cb[8]=(unsigned long)r->out;
 me_enter();int e=sceAudiocodecDecode(r->cb,type);me_leave();
 sceKernelDcacheInvalidateRange(r->out,sizeof(r->out));
 if(e<0){r->bad_frames++;return 0;}
 int frames=r->codec==1?spf:(int)r->cb[9]/4;
 if(frames<=0)return 0;
 if(frames>2048)frames=2048;
 r->bad_frames=0;
 memcpy(r->pcm,r->out,frames*4);return frames;
}

/* ---- the reader thread ---- */
static int read_exact(Http *h,unsigned char *p,int n) {
 while(n>0){int g=http_read(h,p,n,10000);if(g<=0)return 0;p+=g;n-=g;}
 return 1;
}
/* ICY titles are UTF-8 or (often) Latin-1: Latin-1 is turned into UTF-8. */
static void to_utf8(const char *s,char *out,int cap) {
 int valid=1;
 for(const unsigned char *p=(const unsigned char *)s;*p && valid;p++) {
  if(*p<0x80)continue;
  int l=(*p&0xE0)==0xC0?2:(*p&0xF0)==0xE0?3:(*p&0xF8)==0xF0?4:0;if(!l){valid=0;break;}
  for(int j=1;j<l;j++)if((p[j]&0xC0)!=0x80){valid=0;break;}
  p+=l-1;
 }
 if(valid){snprintf(out,cap,"%s",s);return;}
 int n=0;for(const unsigned char *p=(const unsigned char *)s;*p && n<cap-3;p++){if(*p<0x80)out[n++]=*p;else{out[n++]=0xC0|(*p>>6);out[n++]=0x80|(*p&63);}}
 out[n]=0;
}
static void meta_parse(R *r,char *m) {
 char *s=strstr(m,"StreamTitle='");if(!s)return;s+=13;
 char *e=strstr(s,"';");if(!e)e=strrchr(s,'\'');if(e)*e=0;
 char t[192];to_utf8(s,t,sizeof(t));
 int n=strlen(t);while(n && t[n-1]==' ')t[--n]=0;
 if(!strcmp(t,r->last_title) || r->meta_w-r->meta_r>=META_N)return;
 snprintf(r->last_title,sizeof(r->last_title),"%s",t);
 int k=r->meta_w%META_N;r->meta[k].at=r->wpos;snprintf(r->meta[k].title,sizeof(r->meta[k].title),"%s",t);r->meta_w++;
}
enum {K_UNKNOWN,K_MP3,K_AAC,K_PLAYLIST,K_OGG};
static int classify(const char *type,const char *url) {
 char t[64];int i=0;for(;type[i] && i<63;i++)t[i]=tolower((unsigned char)type[i]);t[i]=0;
 if(strstr(t,"mpegurl") || strstr(t,"scpls") || strstr(t,"pls+xml") || strstr(t,"x-pls"))return K_PLAYLIST;
 if(strstr(t,"ogg") || strstr(t,"opus") || strstr(t,"vorbis"))return K_OGG;
 if(strstr(t,"aac") || strstr(t,"mp4"))return K_AAC;
 if(strstr(t,"mpeg") || strstr(t,"mp3"))return K_MP3;
 char u[1024];snprintf(u,sizeof(u),"%s",url);char *q=strchr(u,'?');if(q)*q=0;
 int n=strlen(u);
 if((n>4 && !strcasecmp(u+n-4,".pls")) || (n>4 && !strcasecmp(u+n-4,".m3u")) || (n>5 && !strcasecmp(u+n-5,".m3u8")))return K_PLAYLIST;
 if(strstr(t,"text/") || strstr(t,"xml"))return K_PLAYLIST; /* some servers send playlists as text */
 return K_UNKNOWN;
}
/* The first stream address in a .pls or .m3u body (or RADIO_E_HLS for an HLS list). */
static int playlist_first(char *body,char *out,int cap) {
 if(strstr(body,"#EXT-X-"))return RADIO_E_HLS;
 for(char *line=body;line && *line;) {
  char *nl=strpbrk(line,"\r\n");if(nl)*nl=0;
  char *p=line;while(*p==' ' || *p=='\t')p++;
  if(!strncasecmp(p,"File",4)){char *eq=strchr(p,'=');if(eq)p=eq+1;}
  if(radio_is_url(p)){int n=strlen(p);while(n && (p[n-1]==' '))p[--n]=0;snprintf(out,cap,"%s",p);return 1;}
  line=nl?nl+1:NULL;while(line && (*line=='\r' || *line=='\n'))line++;
 }
 return RADIO_E_PLAYLIST;
}
static void die(R *r,int e){r->error=e;r->dead=1;if(current==r)radio_status.error=e;radio_log("  [%p] gave up: %d (%s)",(void *)r,e,radio_error_text(e));}
static int reader(SceSize n,void *argp) {
 (void)n;R *r=*(R **)argp;unsigned long long born=sceKernelGetSystemTimeWide();
 char cur[1024];snprintf(cur,sizeof(cur),"%s",r->url);char *hash=strchr(cur,'#');if(hash)*hash=0;
 int attempts=0,hops=0,text_is_audio=0;
 /* on demand: where to start. The feed's file size gives a first guess; the reply's own size corrects it once. */
 int off=0,checked=0;
 if(r->ondemand && r->start_s>0 && r->dur_s>0 && r->hint_bytes>0)off=(int)((long long)r->hint_bytes*r->start_s/r->dur_s);
 while(!r->stop) {
  if(current==r)radio_status.phase=r->wpos?4:1;
  unsigned long long t_up=sceKernelGetSystemTimeWide();int e=net_up(wifi_config);if(e<0 || ms_since(t_up)>2000)radio_log("  [%p] Wi-Fi: %d, %u ms",(void *)r,e,ms_since(t_up));
  if(e<0){if(e==NET_E_SWITCH || e==NET_E_NOCONFIG || ++attempts>=3){die(r,e);break;}sceKernelDelayThread(2000000);continue;}
  Http *h=&r->http;
  char extra[48];if(r->ondemand){if(off>0)snprintf(extra,sizeof(extra),"Range: bytes=%d-\r\n",off);else extra[0]=0;}else snprintf(extra,sizeof(extra),"Icy-MetaData: 1\r\n");
  {unsigned long long t_open=sceKernelGetSystemTimeWide();e=http_open(h,cur,extra,10000);char host[96];radio_log("  [%p] request %d to %s (from byte %d): %d, HTTP %d, %u ms, TLS error %d",(void *)r,attempts+1,log_host(h->final_url[0] && e>=0?h->final_url:cur,host,sizeof(host)),r->ondemand?off:0,e,h->status,ms_since(t_open),e==NET_E_TLS?net_tls_error:0);}
  if(e<0 || (h->status!=200 && !(r->ondemand && h->status==206))) {
   int code=e<0?e:NET_E_HTTP;http_close(h);
   if(r->stop)break;
   if(++attempts>=(r->wpos?6:3) || (e>=0 && h->status>=400 && h->status<500 && h->status!=408)){die(r,code);break;}
   sceKernelDelayThread(1000000*attempts);continue;
  }
  sceKernelWaitSema(r->lock,1,NULL);r->conn=h->c;sceKernelSignalSema(r->lock,1);
  if(r->stop){sceKernelWaitSema(r->lock,1,NULL);r->conn=NULL;sceKernelSignalSema(r->lock,1);http_close(h);break;}
  if(r->ondemand) {
   int total=h->status==206?(h->range_total>0?h->range_total:(h->length>0?off+h->length:-1)):h->length;
   if(total>0)r->total=total;
   /* the guess was off by more than 2%: ask again from the right place (once) */
   if(!checked && r->start_s>0 && r->dur_s>0 && r->total>0) {
    checked=1;int want=(int)((long long)r->total*r->start_s/r->dur_s);
    if(want>=r->total)want=r->total>65536?r->total-65536:0;
    if(abs(want-off)>r->total/50){off=want;sceKernelWaitSema(r->lock,1,NULL);r->conn=NULL;sceKernelSignalSema(r->lock,1);http_close(h);continue;}
   }
   checked=1;
   /* a server that ignores Range sends the whole file: skip to where playback is */
   if(h->status==200 && off>0){unsigned char skip[4096];int left=off;while(left>0 && !r->stop){int g=http_read(h,skip,left<4096?left:4096,10000);if(g<=0)break;left-=g;}if(left>0){sceKernelWaitSema(r->lock,1,NULL);r->conn=NULL;sceKernelSignalSema(r->lock,1);http_close(h);if(++attempts>=6){die(r,NET_E_CLOSED);break;}continue;}}
   r->got=off;
   /* an ID3 tag at the start (often with large cover art) is skipped, not scanned for frames */
   if(off==0) {
    unsigned char id3[10];
    if(read_exact(h,id3,10)) {
     r->got=10;
     if(!memcmp(id3,"ID3",3) && !((id3[6]|id3[7]|id3[8]|id3[9])&128)) {
      int left=((id3[6]<<21)|(id3[7]<<14)|(id3[8]<<7)|id3[9])+((id3[5]&16)?10:0);unsigned char skip[4096];
      while(left>0 && !r->stop){int g=http_read(h,skip,left<4096?left:4096,10000);if(g<=0)break;left-=g;r->got+=g;}
     }else ring_put(r,id3,10);
    }
   }
  }
  int kind=classify(h->content_type,cur);
  if(kind==K_PLAYLIST && text_is_audio)kind=K_UNKNOWN;
  if(kind==K_PLAYLIST) {
   char *body=r->body;int got=0;
   for(;got<(int)sizeof(r->body)-1;){int g=http_read(h,body+got,sizeof(r->body)-1-got,8000);if(g<=0)break;got+=g;}
   body[got]=0;
   sceKernelWaitSema(r->lock,1,NULL);r->conn=NULL;sceKernelSignalSema(r->lock,1);http_close(h);
   /* a stream server that calls its audio "text/..." still starts with frames */
   if(!strstr(body,"http") && sniff((unsigned char *)body,got)){text_is_audio=1;continue;}
   else {
    char next[1024];int pr=playlist_first(body,next,sizeof(next));
    if(pr<0 || ++hops>4){die(r,pr<0?pr:RADIO_E_PLAYLIST);break;}
    snprintf(cur,sizeof(cur),"%s",next);continue;
   }
   continue;
  }
  if(kind==K_OGG){sceKernelWaitSema(r->lock,1,NULL);r->conn=NULL;sceKernelSignalSema(r->lock,1);http_close(h);die(r,RADIO_E_OGG);break;}
  if(!r->codec && kind!=K_UNKNOWN)r->codec=kind==K_MP3?1:2;
  if(current==r) {
   if(!radio_status.name[0] && h->icy_name[0])to_utf8(h->icy_name,radio_status.name,sizeof(radio_status.name));
   if(h->icy_genre[0])to_utf8(h->icy_genre,radio_status.genre,sizeof(radio_status.genre));
   if(h->bitrate>0)radio_status.kbps=h->bitrate;
  }
  {int kbps=h->bitrate>0 && h->bitrate<=512?h->bitrate:128;int pb=kbps*125*3/2;if(pb<12000)pb=12000;if(pb>96000)pb=96000;r->prebuffer=pb;}
  int metaint=h->metaint>0 && h->metaint<1<<20?h->metaint:0,left=metaint,sniffed=0;
  unsigned char *sbuf=r->sniff_buf;int slen=0;
  unsigned long long since=sceKernelGetSystemTimeWide();
  unsigned char buf[4096];
  int ended=0;
  while(!r->stop) {
   int want=sizeof(buf);if(metaint && want>left)want=left;
   /* on demand nothing may be dropped: while the ring is full (paused), wait */
   if(r->ondemand){while(!r->stop && RING-(r->wpos-r->rpos)<(unsigned int)want)sceKernelDelayThread(20000);if(r->stop)break;}
   int g=http_read(h,buf,want,10000);if(g<=0){ended=g==0 && h->eof;break;}
   if(r->ondemand)r->got+=g;
   if(!r->codec && !sniffed) { /* no Content-Type to go by: look at the first frames */
    int c=slen+g<=(int)sizeof(r->sniff_buf)?g:(int)sizeof(r->sniff_buf)-slen;memcpy(sbuf+slen,buf,c);slen+=c;
    int k=sniff(sbuf,slen);if(k)r->codec=k;
    if(!k && slen>=(int)sizeof(r->sniff_buf)){sniffed=1;die(r,RADIO_E_FORMAT);break;}
   }
   ring_put(r,buf,g);
   if(metaint) {
    left-=g;
    if(!left) {
     unsigned char L;if(!read_exact(h,&L,1))break;
     if(L){char m[16*255+1];if(!read_exact(h,(unsigned char *)m,L*16))break;m[L*16]=0;meta_parse(r,m);}
     left=metaint;
    }
   }
   if(sceKernelGetSystemTimeWide()-since>30000000ULL)attempts=0; /* it played a while: start counting failures again */
  }
  sceKernelWaitSema(r->lock,1,NULL);r->conn=NULL;sceKernelSignalSema(r->lock,1);
  http_close(h);
  if(r->stop || r->dead)break;
  /* on demand: the whole file arrived (or a reply of unknown size ended cleanly) */
  if(r->ondemand && ((r->total>0 && r->got>=r->total) || (r->total<=0 && ended))){r->finished=1;break;}
  if(r->ondemand)off=r->got; /* dropped part way: carry on from there */
  if(++attempts>=6){die(r,NET_E_CLOSED);break;}
  sceKernelDelayThread(1000000);
 }
 radio_log("  [%p] reader ends: stop %d dead %d finished %d, %u ms, %d streams left",(void *)r,r->stop,r->dead,r->finished,ms_since(born),readers-1);
 readers--;
 release(r);
 sceKernelExitDeleteThread(0);
 return 0;
}

/* ---- the decoder side (audio thread) ---- */
static void publish_titles(R *r) {
 while(r->meta_r!=r->meta_w) {
  int k=r->meta_r%META_N;
  if((int)(r->meta[k].at-r->rpos)>0)break; /* not heard yet */
  if(current==r)set_title(r->meta[k].title);
  r->meta_r++;
 }
}
/* Waits until the ring holds a prebuffer's worth again. 1 to go on, 0 when the station is no longer wanted, <0 an error. */
static int wait_buffer(R *r) {
 unsigned long long t0=sceKernelGetSystemTimeWide();
 if(current==r && radio_status.phase==3)radio_status.phase=2;
 while(r->wpos-r->rpos<(unsigned int)(r->prebuffer?r->prebuffer:16000) || !r->codec) {
  if(r->finished){if(r->codec)break;return RADIO_E_FORMAT;} /* the end of an episode: play what is left */
  if(!wanted() || r->stop)return 0;
  if(r->dead)return r->error?r->error:NET_E_CLOSED;
  if(sceKernelGetSystemTimeWide()-t0>60000000ULL){radio_log("  [%p] no sound arrived for 60 s (%u bytes buffered, codec %d)",(void *)r,r->wpos-r->rpos,r->codec);return NET_E_TIMEOUT;}
  if(current==r && r->prebuffer)radio_status.fill_ms=(int)((unsigned long long)(r->wpos-r->rpos)*1500/r->prebuffer);
  sceKernelDelayThread(20000);
 }
 if(current==r)radio_status.phase=3;
 return 1;
}
Radio *radio_open(const char *url,int *rate,int *channels,int *error){unsigned long long f;return radio_open_at(url,0,rate,channels,&f,error);}
/* url#@seconds,bytes|...: an episode (on demand) of that length, started start_s in. *frames: its length in frames (0 live). */
Radio *radio_open_at(const char *url,int start_s,int *rate,int *channels,unsigned long long *frames,int *error) {
 *error=0;*frames=0;
 /* A closed stream's reader can linger while it connects (it cannot be stopped mid-handshake). Jumps
    faster than a server answers would otherwise pile up streams (each about 350 KB, more with HTTPS)
    until memory runs out, so while two run, wait a few seconds for an old one to finish. After that a
    new one starts anyway when there is clearly room for it (a reader slow to wind down must never
    keep the next station from playing). */
 {char host[96];radio_log("open %s%s start %d s: %d streams running, %u KB free",log_host(url,host,sizeof(host)),radio_is_episode(url)?" (episode)":"",start_s,readers,(unsigned int)(sceKernelMaxFreeMemSize()/1024));}
 {unsigned long long t0=sceKernelGetSystemTimeWide();int waited=0;
  while(readers>=2) {
   if(!wanted()){radio_log("  not wanted any more (waited %u ms)",ms_since(t0));*error=NET_E_STOPPED;return NULL;}
   unsigned int ms=ms_since(t0);
   if(ms>4000 && sceKernelMaxFreeMemSize()>(int)(sizeof(R)+RING+512*1024)){radio_log("  %d old streams still winding down after %u ms: starting anyway",readers,ms);break;}
   if(ms>30000){radio_log("  no memory for another stream after %u ms",ms);*error=NET_E_MEMORY;return NULL;}
   waited=1;sceKernelDelayThread(20000);
  }
  if(waited && readers<2)radio_log("  waited %u ms for the old stream to close",ms_since(t0));
 }
 R *r=memalign(64,sizeof(R));if(!r){*error=NET_E_MEMORY;return NULL;}
 memset(r,0,sizeof(*r));
 r->ring=malloc(RING);if(!r->ring){free(r);*error=NET_E_MEMORY;return NULL;}
 snprintf(r->url,sizeof(r->url),"%s",url);r->refs=2;r->resync=1;r->total=-1;
 {const char *h=strchr(url,'#');if(h && h[1]=='@'){r->ondemand=1;r->dur_s=atoi(h+2);const char *c=strchr(h,',');if(c)r->hint_bytes=atoi(c+1);r->start_s=start_s>0?start_s:0;}}
 r->lock=sceKernelCreateSema("zoid_ring",0,1,1,NULL);
 current=r;
 /* everything about the last station goes first; the new (empty) title is announced last, so the screen never mixes the two */
 radio_status.phase=1;radio_status.error=radio_status.fill_ms=radio_status.kbps=radio_status.codec=radio_status.rate=0;radio_status.decoded_ms=0;
 {const char *h=strchr(url,'#');if(r->ondemand){h=strrchr(h,'|');if(h)h++;else h="";}else if(h)h++;snprintf(radio_status.name,sizeof(radio_status.name),"%s",h?h:"");radio_status.genre[0]=0;}
 set_title("");
 readers++;
 SceUID th=sceKernelCreateThread("zoid_radio",reader,0x20,0x10000,0,NULL); /* 64 KB: TLS handshakes are deep */
 if(th<0){readers--;current=NULL;sceKernelDeleteSema(r->lock);free(r->ring);free(r);*error=NET_E_MEMORY;return NULL;}
 radio_log("  [%p] reader started",(void *)r);sceKernelStartThread(th,sizeof(r),&r);
 int w=wait_buffer(r);
 if(w<=0){*error=w<0?w:NET_E_STOPPED;radio_close(r);return NULL;}
 /* the first frame sets the output rate */
 for(int guard=0;guard<64 && !r->have;guard++) {
  int hl=0,len=next_frame(r,&hl);
  if(!len){if(r->finished)break;w=wait_buffer(r);if(w<=0){*error=w<0?w:NET_E_STOPPED;radio_close(r);return NULL;}continue;}
  if(!r->rate) {
   int fr;frame_at(r->codec,r->in,&fr,&r->rate);
   static const int ok_rates[9]={8000,11025,12000,16000,22050,24000,32000,44100,48000};int ok=0;for(int i=0;i<9;i++)if(ok_rates[i]==r->rate)ok=1;
   if(!ok){*error=RADIO_E_FORMAT;radio_close(r);return NULL;}
   int e=codec_init(r);if(e<0){*error=e;radio_close(r);return NULL;}
   radio_status.codec=r->codec;radio_status.rate=r->rate;
  }
  r->have=decode(r,len,hl);r->used=0;
 }
 if(!r->have){*error=RADIO_E_FORMAT;radio_close(r);return NULL;}
 *rate=r->rate;*channels=2;radio_status.phase=3;radio_log("  [%p] playing: %s %d Hz",(void *)r,r->codec==1?"MP3":"AAC",r->rate);
 if(r->ondemand) { /* the length: from the feed, else from the file size and the MP3 bit rate */
  int dur=r->dur_s;
  if(dur<=0 && r->total>0 && r->codec==1){int a,b,c,kbps=0;if(mp3_header(r->in,&a,&b,&c,&kbps) && kbps>0)dur=(int)((long long)r->total*8/(kbps*1000));}
  if(dur>0)*frames=(unsigned long long)dur*r->rate;
 }
 return r;
}
int radio_read(Radio *r,short *out,int frames) {
 if(frames>1152)frames=1152;
 while(r->used>=r->have) {
  if(!wanted() || r->stop){memset(out,0,frames*4);return frames;} /* a new station is starting: let the engine move on */
  int hl=0,len=next_frame(r,&hl);
  if(len){r->have=decode(r,len,hl);r->used=0;publish_titles(r);if(r->bad_frames>200)return RADIO_E_FORMAT;continue;}
  if(r->finished)return 0; /* an episode played to its end */
  int w=wait_buffer(r);
  if(w<0)return w;
  if(!w){memset(out,0,frames*4);return frames;}
 }
 int n=r->have-r->used;if(n>frames)n=frames;
 memcpy(out,r->pcm+r->used*2,n*4);r->used+=n;
 if(current==r) {
  r->played+=n;radio_status.decoded_ms=(unsigned int)(r->played*1000/r->rate);
  if(r->prebuffer)radio_status.fill_ms=(int)((unsigned long long)(r->wpos-r->rpos)*1500/r->prebuffer);
 }
 return n;
}
void radio_close(Radio *r) {
 if(!r)return;
 r->stop=1;radio_log("  [%p] closed",(void *)r);
 sceKernelWaitSema(r->lock,1,NULL);net_cancel(r->conn);sceKernelSignalSema(r->lock,1);
 if(r->codec_ready){me_enter();sceAudiocodecReleaseEDRAM(r->cb);me_leave();r->codec_ready=0;}
 if(current==r){current=NULL;radio_status.phase=0;}
 release(r);
}
