/* MP4 / M4A container reading shared by the sound and video players. */
#include <pspkernel.h>
#include <stdlib.h>
#include <string.h>
#include "mp4.h"

unsigned int mp4_be32(const unsigned char *p){return ((unsigned int)p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];}
int mp4_rd(SceUID fd,unsigned int pos,void *buf,int n){if(sceIoLseek32(fd,pos,PSP_SEEK_SET)<0)return 0;return sceIoRead(fd,buf,n)==n;}

unsigned int mp4_find_box(SceUID fd,unsigned int start,unsigned int end,const char *type,unsigned int *len) {
 unsigned char h[16];
 for(unsigned int pos=start;pos+8<=end;) {
  if(!mp4_rd(fd,pos,h,8))return 0;
  unsigned int sz=mp4_be32(h),hdr=8;
  if(sz==1){if(!mp4_rd(fd,pos,h,16) || mp4_be32(h+8))return 0;sz=mp4_be32(h+12);hdr=16;}
  else if(sz==0)sz=end-pos;
  if(sz<hdr || pos+sz>end)return 0;
  if(!memcmp(h+4,type,4)){*len=sz-hdr;return pos+hdr;}
  pos+=sz;
 }
 return 0;
}

static int track_of(SceUID fd,unsigned int trak,unsigned int trak_len,const char *handler,Mp4Track *t) {
 unsigned int len,mdia=mp4_find_box(fd,trak,trak+trak_len,"mdia",&len);if(!mdia)return 0;
 unsigned int mend=mdia+len,hl,hdlr=mp4_find_box(fd,mdia,mend,"hdlr",&hl);unsigned char b[32];
 if(!hdlr || !mp4_rd(fd,hdlr,b,12) || memcmp(b+8,handler,4))return 0;
 unsigned int dl,mdhd=mp4_find_box(fd,mdia,mend,"mdhd",&dl);
 t->timescale=0;t->duration=0;
 if(mdhd && mp4_rd(fd,mdhd,b,32)) {
  if(b[0]==1){t->timescale=mp4_be32(b+20);t->duration=((unsigned long long)mp4_be32(b+24)<<32)|mp4_be32(b+28);}
  else{t->timescale=mp4_be32(b+12);t->duration=mp4_be32(b+16);}
 }
 unsigned int ml,minf=mp4_find_box(fd,mdia,mend,"minf",&ml);if(!minf)return 0;
 t->stbl=mp4_find_box(fd,minf,minf+ml,"stbl",&t->stbl_len);if(!t->stbl)return 0;
 t->stsd=mp4_find_box(fd,t->stbl,t->stbl+t->stbl_len,"stsd",&t->stsd_len);
 return t->stsd!=0;
}

int mp4_find_track(SceUID fd,const char *handler,Mp4Track *t) {
 unsigned int end=sceIoLseek32(fd,0,PSP_SEEK_END),len,moov=mp4_find_box(fd,0,end,"moov",&len);
 if(!moov)return 0;
 for(unsigned int pos=moov;pos<moov+len;) {
  unsigned int tl,trak=mp4_find_box(fd,pos,moov+len,"trak",&tl);if(!trak)break;
  if(track_of(fd,trak,tl,handler,t))return 1;
  pos=trak+tl;
 }
 return 0;
}

int mp4_sample_table(SceUID fd,const Mp4Track *t,unsigned int **offsets,unsigned int **sizes) {
 unsigned int send=t->stbl+t->stbl_len;unsigned char b[16];
 *offsets=NULL;*sizes=NULL;
 unsigned int zl,stsz=mp4_find_box(fd,t->stbl,send,"stsz",&zl);if(!stsz || !mp4_rd(fd,stsz,b,12))return -5;
 unsigned int fixed=mp4_be32(b+4),count=mp4_be32(b+8);if(!count || count>2000000)return -5;
 unsigned int *off=malloc(count*4),*size=malloc(count*4);if(!off || !size){free(off);free(size);return -6;}
 if(fixed)for(unsigned int i=0;i<count;i++)size[i]=fixed;
 else {
  if(!mp4_rd(fd,stsz+12,size,count*4)){free(off);free(size);return -5;}
  for(unsigned int i=0;i<count;i++)size[i]=mp4_be32((unsigned char *)&size[i]);
 }
 unsigned int cl,co=mp4_find_box(fd,t->stbl,send,"stco",&cl);int wide=0;
 if(!co){co=mp4_find_box(fd,t->stbl,send,"co64",&cl);wide=1;}
 unsigned int chunks=0,w=wide?8:4;unsigned char *ct=NULL,*st=NULL;unsigned int entries=0;
 if(co && mp4_rd(fd,co,b,8)){chunks=mp4_be32(b+4);ct=malloc(chunks*w+1);}
 if(!ct || !mp4_rd(fd,co+8,ct,chunks*w)){free(ct);free(off);free(size);return -7;}
 unsigned int scl,stsc=mp4_find_box(fd,t->stbl,send,"stsc",&scl);
 if(stsc && mp4_rd(fd,stsc,b,8)){entries=mp4_be32(b+4);st=malloc(entries*12+1);}
 if(!st || !mp4_rd(fd,stsc+8,st,entries*12)){free(ct);free(st);free(off);free(size);return -8;}
 unsigned int s=0;
 for(unsigned int e=0;e<entries && s<count;e++) {
  unsigned int first=mp4_be32(st+e*12)-1,per=mp4_be32(st+e*12+4),last=e+1<entries?mp4_be32(st+(e+1)*12)-1:chunks;
  for(unsigned int c=first;c<last && c<chunks && s<count;c++) {
   unsigned int pos=wide?mp4_be32(ct+c*8+4):mp4_be32(ct+c*4);
   for(unsigned int k=0;k<per && s<count;k++){off[s]=pos;pos+=size[s];s++;}
  }
 }
 free(ct);free(st);
 if(!s){free(off);free(size);return -9;}
 *offsets=off;*sizes=size;return (int)s;
}

unsigned char *mp4_sync_samples(SceUID fd,const Mp4Track *t,int count) {
 unsigned int l,stss=mp4_find_box(fd,t->stbl,t->stbl+t->stbl_len,"stss",&l);unsigned char b[8];
 if(!stss || !mp4_rd(fd,stss,b,8))return NULL;
 unsigned int n=mp4_be32(b+4);unsigned char *raw=malloc(n*4+1),*flags=calloc(count,1);
 if(!raw || !flags || !mp4_rd(fd,stss+8,raw,n*4)){free(raw);free(flags);return NULL;}
 for(unsigned int i=0;i<n;i++){unsigned int k=mp4_be32(raw+i*4);if(k>=1 && (int)k<=count)flags[k-1]=1;}
 free(raw);return flags;
}

static SceUID me_sema=-1;
void me_init(void){if(me_sema<0)me_sema=sceKernelCreateSema("zoid_me",0,1,1,NULL);}
unsigned int me_wait_us;
void me_enter(void){if(me_sema>=0){unsigned long long t=sceKernelGetSystemTimeWide();sceKernelWaitSema(me_sema,1,NULL);me_wait_us+=(unsigned int)(sceKernelGetSystemTimeWide()-t);}}
void me_leave(void){if(me_sema>=0)sceKernelSignalSema(me_sema,1);}
