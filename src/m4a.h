#ifndef M4A_H
#define M4A_H
/* M4A (AAC in MP4) through the hardware AAC decoder. See m4a.c. */
typedef struct M4a M4a;
M4a *m4a_open(const char *path,int *rate,int *channels,unsigned long long *frames,int *error);
int m4a_read(M4a *m,short *out); /* stereo frames, up to 2048; 0 at the end */
int m4a_seek(M4a *m,unsigned long long frame);
void m4a_close(M4a *m);
extern int aac_frames,aac_peak;
#endif
