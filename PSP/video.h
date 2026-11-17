#ifndef VIDEO_H
#define VIDEO_H
/* H.264 video from MP4 files, decoded and colour-converted by the PSP's Media Engine. See video.c. */
enum {VIDEO_OK=0,VIDEO_ERR_OPEN=-1,VIDEO_ERR_NOVIDEO=-2,VIDEO_ERR_CODEC=-3,VIDEO_ERR_SIZE=-4,
 VIDEO_ERR_PROFILE=-5,VIDEO_ERR_MEMORY=-6,VIDEO_ERR_ENGINE=-7,VIDEO_ERR_BFRAMES=-8,VIDEO_ERR_DECODE=-9,VIDEO_END=-100};
typedef struct {
 int width,height,profile,level,frames,duration_ms,has_audio;
 unsigned int frame_us; /* average time per picture */
} VideoInfo;
int video_open(const char *path,VideoInfo *info);
/* Decodes the next frame; 1 when a new picture is in video_pixels, 0 when not yet, VIDEO_ERR_DECODE when
   the decoder rejected it, VIDEO_END after the last. */
int video_decode(void);
/* When the next picture is due, in ms from the start. */
int video_next_ms(void);
int video_frame_index(void);
extern int video_largest_frame,video_slowest_us,video_params_sent;
/* Moves to the key frame at or before ms; returns its time in ms. */
int video_seek(int ms);
void video_close(void);
void video_test_stale_file(void); /* tests: closes the open file behind the player's back */
const char *video_error_text(int code);
extern unsigned int *video_pixels; /* ABGR8888, video_stride pixels per row */
extern int video_stride;
extern void (*video_log)(const char *line);
#endif
