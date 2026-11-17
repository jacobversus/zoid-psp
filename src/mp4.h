#ifndef MP4_H
#define MP4_H
/* Shared MP4 container reading for m4a.c (sound) and video.c (pictures). */
#include <pspiofilemgr.h>
typedef struct {
 unsigned int stbl,stbl_len,stsd,stsd_len; /* payload offsets and lengths */
 unsigned int timescale;unsigned long long duration; /* from mdhd, in timescale units */
} Mp4Track;
unsigned int mp4_be32(const unsigned char *p);
int mp4_rd(SceUID fd,unsigned int pos,void *buf,int n);
/* The child box `type` inside [start,end): its payload offset, and its length in *len; 0 if none. */
unsigned int mp4_find_box(SceUID fd,unsigned int start,unsigned int end,const char *type,unsigned int *len);
/* The first track whose handler is `handler` ("soun" or "vide"); 1 if found. */
int mp4_find_track(SceUID fd,const char *handler,Mp4Track *t);
/* Every sample's file offset and size (malloc'd); returns the count, or negative on error. */
int mp4_sample_table(SceUID fd,const Mp4Track *t,unsigned int **offsets,unsigned int **sizes);
/* Which samples are key frames (malloc'd, one byte per sample); NULL when every sample is. */
unsigned char *mp4_sync_samples(SceUID fd,const Mp4Track *t,int count);
/* The Media Engine runs one job at a time: sound (sceAudiocodec) and pictures (sceMpeg) take turns
   through this lock. Two threads calling it at once crashed a real PSP during a film. */
void me_init(void);
void me_enter(void);
void me_leave(void);
extern unsigned int me_wait_us;
#endif
