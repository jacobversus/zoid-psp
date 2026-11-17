#ifndef RADIO_H
#define RADIO_H
/* Internet radio streams (MP3 and AAC over HTTP/HTTPS, Shoutcast/Icecast). See radio.c.
   A station plays through softdec like a file with no end: radio_open/radio_read/radio_close. */
enum {RADIO_E_FORMAT=-1101,RADIO_E_HLS=-1102,RADIO_E_OGG=-1103,RADIO_E_PLAYLIST=-1104};
typedef struct Radio Radio;
int radio_is_url(const char *path);                       /* http:// or https:// */
void radio_setup(int (*keep_going)(void),int wifi_config); /* keep_going: 0 once the station is no longer wanted */
void radio_set_wifi(int wifi_config);
void radio_set_log(const char *path);   /* stream-log.txt: what each stream did (connects, errors, waits), for troubleshooting */
void radio_log(const char *fmt,...);
Radio *radio_open(const char *url,int *rate,int *channels,int *error);
/* An episode: url#@seconds,bytes|folder|title, a file with an end, opened start_s in (Range requests).
   *frames is its length in frames (0 for a live station). */
Radio *radio_open_at(const char *url,int start_s,int *rate,int *channels,unsigned long long *frames,int *error);
int radio_is_episode(const char *path);   /* a podcast episode streamed on demand */
int radio_readers_alive(void);              /* streams whose reader thread is still running */
int radio_read(Radio *r,short *out,int frames);           /* stereo frames; silence while the next station takes over */
void radio_close(Radio *r);
const char *radio_error_text(int code);

/* What the screen shows for the station playing. */
typedef struct {
 volatile int phase;          /* 0 idle, 1 connecting, 2 buffering, 3 playing, 4 reconnecting */
 volatile int error;          /* why it stopped (NET_E_* / RADIO_E_*) */
 volatile int fill_ms,kbps,codec,rate; /* codec: 1 MP3, 2 AAC */
 volatile unsigned int title_gen,decoded_ms,title_ms; /* title_ms: decoded_ms when the current title began */
 char title[192],name[96],genre[64];
} RadioStatus;
extern RadioStatus radio_status;
int radio_get_title(char *out,int cap,unsigned int *gen);  /* copies the current song title ("Artist - Title") */
#endif
