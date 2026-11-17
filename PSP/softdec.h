#ifndef SOFTDEC_H
#define SOFTDEC_H
/* Lossless playback (FLAC, WAV) decoded on the CPU. See softdec.c. */
#define SOFT_CHUNK 1152
typedef struct SoftDec SoftDec;
int soft_kind(const char *path); /* 1 FLAC, 2 WAV, 3 M4A (hardware AAC), 4 an internet radio stream, 0 anything else */
SoftDec *soft_open(const char *path,int *rate,int *channels,unsigned long long *frames,int *error);
SoftDec *soft_open_at(const char *path,int start_s,int *rate,int *channels,unsigned long long *frames,int *error); /* start_s: where a streamed episode starts */
int soft_read(SoftDec *s,short *out,int frames); /* interleaved frames read; 0 at the end */
int soft_seek(SoftDec *s,unsigned long long frame);
void soft_close(SoftDec *s);
#endif
