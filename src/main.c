#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspctrl.h>
#include <psprtc.h> /* the month, for Zoidstation Portable's By month color */
#include <pspaudio.h>
#include <pspmp3.h>
#include <psputility.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <malloc.h>
#include <ctype.h>
#include <math.h>
#include <psppower.h>
#include <pspsdk.h>
#include "fonts/font.h"
#include "fonts/font_paper.h" /* Books' serif (Manga Paper) */
#include "fonts/font_ms.h"    /* the Zoidstation skins: Arial Rounded, Century Gothic, Segoe UI */
#include "softdec.h"
#include "video.h"
#include "mp4.h"
#include "m4a.h"
#include "net.h"
#include "radio.h"

PSP_MODULE_INFO("ZOID",0,2,1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER|THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);
/* The heap takes all memory but this much, which the system keeps for modules and thread stacks.
   Wi-Fi needs about 850 KB of it once connected (measured in the emulator: 432 KB of network modules,
   a 128 KB pool, stacks), so 1.75 MB rather than the default 1 MB. */
PSP_HEAP_THRESHOLD_SIZE_KB(1792);
#define PATHLEN 512
/* Settings added in v7: auto screen off, time shown as remaining, hold-X time, scrolling speed.
   The sleep timer is for this session only. */
static int sleep_idx,auto_off_idx,time_left,hold_idx=1,scroll_idx=1;static unsigned long long sleep_until;
static const int sleep_minutes[7]={0,15,30,45,60,90,120},auto_off_secs[5]={0,30,60,120,300};
static const int hold_frames[4]={18,27,42,60},scroll_ms[3]={40,28,18};
static int marquee_ms=28; /* ms per pixel of scrolling text (Settings: Scrolling text) */

#include "paths.h"
static char root[24]="ms0:/MUSIC",folder[PATHLEN],active[PATHLEN],pending[PATHLEN];
static int cursor,scroll;
/* While a playlist is open, the library lists its songs, whose full paths are kept here. */
static int in_playlist;static char playlist_file[PATHLEN];static Paths pl_paths;
static int in_radio; /* the Library shows Internet Radio (radio_ui.h) */
static int in_pod;static char pod_root[24]="ms0:/PODCASTS"; /* the Library shows Podcasts (podcast_ui.h) */
static int radio_entry_path(int i,char *out,int cap),pod_entry_path(int i,char *out,int cap);
static int entry_path(int i,char *out,int cap) {
 if(in_radio)return radio_entry_path(i,out,cap);
 if(in_pod)return pod_entry_path(i,out,cap);
 int n=in_playlist?snprintf(out,cap,"%s",paths_get(&pl_paths,i)):snprintf(out,cap,"%s/%s",folder,entries[i].name);
 return n>0 && n<cap;
}
static int theme,visual_count=1,visuals[3]={4,0,2},gapless=1;
/* mixing: 0 Gapless, 1 Trim silence, 2 Crossfade, 3 Normal (a gap between songs); gapless follows it */
static int mix_mode,fade_seconds=6,cpu_fast=1,resume_on=0,screen_off;
static const int fade_choices[6]={2,4,6,8,10,12};
static volatile int hold_on,power_resumed,power_events,power_flags;static int next_request_seek;
static const char *mix_names[4]={"Gapless","Trim silence","Crossfade","Normal"};
static void set_mix(int m){mix_mode=(m%4+4)%4;gapless=mix_mode!=3;}
/* Online: the Wi-Fi network (0 = the first saved one that works) and when to look up lyrics (0 off, 1 when Lyrics is open, 2 every song) */
static int wifi_config,online_lyrics=1;
#ifdef ZOID_NET_TEST
static const char *radio_api="http://127.0.0.1:8088",*lyrics_api="http://127.0.0.1:8088",*ih_api="http://127.0.0.1:8088"; /* the test server (net-test-server.cs) */
#else
static const char *radio_api="http://all.api.radio-browser.info",*lyrics_api="https://lrclib.net",*ih_api="http://api.iheart.com";
#endif
/* CPU speed: 0 = 222 MHz, 1 = 333 MHz, 2 = set by ARK-5. In the ARK-5 mode ZOID never touches the clock
   (not at startup, for video or with the screen off), so ARK-5's own overclock stays in place. */
static void set_clock(int fast){if(cpu_fast==2)return;if(fast)scePowerSetClockFrequency(333,333,166);else scePowerSetClockFrequency(222,222,111);}
static int bindings[4]={0,1,2,6},seek_step_index=1;
static const int seek_steps[4]={1,5,10,30};
static const char *binding_names[]={"Play / Pause","Previous","Next","Stop","Shuffle","Repeat","Settings","Seek back","Seek forward","Disabled","Library / Now Playing","Screen off","Equalizer","Lyrics"};
static char settings_path[PATHLEN];
static volatile unsigned int seek_ack;
static int seek_preview=-1;
static int pending_seek,pending_continuous;
static volatile int duration;
static volatile int running=1,paused,state,elapsed,audio_ready;
static volatile unsigned int serial,error_code;
static volatile int stage,levels[24];
static volatile short scope[128],wave[512];
static volatile int beat_pulse;
static SceUID command_lock,queue_lock,meta_lock=-1,art_lock=-1;
static volatile unsigned int browse_gen;
static void lock(SceUID s){if(s>=0)sceKernelWaitSema(s,1,NULL);}
static void unlock(SceUID s){if(s>=0)sceKernelSignalSema(s,1);}

#include "mp3index.h"
static char startup_path[PATHLEN]="ms0:/PSP/GAME/ZOID/startup.log";
static void checkpoint(const char *message,int result) {
 char line[160];int n=snprintf(line,sizeof(line),"%s: %08X\n",message,(unsigned int)result);
 int fd=sceIoOpen(startup_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);
 if(fd>=0){sceIoWrite(fd,line,n);sceIoClose(fd);}
}

/* every playable song type: MP3, plus FLAC and WAV through softdec.c */
static int mp3(const char *s){int n=strlen(s);if(soft_kind(s))return 1;return n>4 && s[n-4]=='.' && tolower((unsigned char)s[n-3])=='m' && tolower((unsigned char)s[n-2])=='p' && s[n-1]=='3';}
static int m3u(const char *s){int n=strlen(s);return n>4 && s[n-4]=='.' && tolower((unsigned char)s[n-3])=='m' && s[n-2]=='3' && tolower((unsigned char)s[n-1])=='u';}
#define PLAYLIST_DIR "Playlists"
/* Videos: a pinned entry at the top of MUSIC (folder 3) opens VIDEO, which lists only folders and videos */
static char video_root[24]="ms0:/VIDEO";
static int is_video_file(const char *s){int n=strlen(s);return n>4 && s[n-4]=='.' && (!strcasecmp(s+n-3,"mp4") || !strcasecmp(s+n-3,"m4v"));}
static int in_video_folder(void){int n=strlen(video_root);return !in_radio && !in_pod && !strncmp(folder,video_root,n) && (folder[n]==0 || folder[n]=='/');}
static int dir_exists(const char *p){int d=sceIoDopen(p);if(d<0)return 0;sceIoDclose(d);return 1;}
/* Natural order, ignoring case: numbers compare by value, so "Episode 2" sorts before "Episode 10". */
static int natural_compare(const char *a,const char *b) {
 while(*a && *b) {
  if(isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
   while(*a=='0' && isdigit((unsigned char)a[1]))a++;
   while(*b=='0' && isdigit((unsigned char)b[1]))b++;
   const char *sa=a,*sb=b;while(isdigit((unsigned char)*a))a++;while(isdigit((unsigned char)*b))b++;
   if(a-sa!=b-sb)return (int)(a-sa)-(int)(b-sb);
   int c=strncmp(sa,sb,a-sa);if(c)return c;
   continue;
  }
  int ca=tolower((unsigned char)*a),cb=tolower((unsigned char)*b);if(ca!=cb)return ca-cb;a++;b++;
 }
 return tolower((unsigned char)*a)-tolower((unsigned char)*b);
}
/* Sort orders (hold X > Sort). MUSIC and VIDEO each remember their own (settings v13).
   Album order reads each song's tags: album, then disc, then track, so a two-disc set kept in one
   folder plays disc 1 then disc 2, and a continuous mix plays track 1, 2, 3... Songs without tags
   fall back to their file names, in natural order (1, 2, 10). Play all and the queue follow the
   same order. Playlists keep their own order. */
enum {SORT_ALBUM,SORT_TRACK,SORT_NAME,SORT_NAME_ZA,SORT_FILE,SORT_NEW,SORT_OLD,SORT_N};
static const char *sort_names[SORT_N]={"Album, disc, track","Track number","Name A-Z","Name Z-A","File name (1, 2, 10)","Newest first","Oldest first"};
static int music_sort=SORT_ALBUM,video_sort=SORT_FILE;
static int sort_needs_tags(int m){return m<=SORT_NAME_ZA;}
static int sort_for_video(int m){return m==SORT_FILE || m==SORT_NAME_ZA || m==SORT_NEW || m==SORT_OLD;}
static unsigned long long sort_date(const ScePspDateTime *t){return ((unsigned long long)t->year<<40)|((unsigned long long)t->month<<32)|((unsigned long long)t->day<<24)|(t->hour<<16)|(t->minute<<8)|t->second;}
typedef struct {const char *name,*album,*title;int disc,track;unsigned long long date;} SortKey;
static int date_cmp(unsigned long long a,unsigned long long b){return a<b?-1:a>b?1:0;}
static int song_order(int m,const SortKey *x,const SortKey *y) {
 int c=0;
 switch(m) {
 case SORT_ALBUM: {int ex=!x->album[0],ey=!y->album[0];if(ex!=ey)return ex-ey;c=natural_compare(x->album,y->album);if(c)return c;} /* fall through */
 case SORT_TRACK: {
  int dx=x->disc?x->disc:1,dy=y->disc?y->disc:1;if(dx!=dy)return dx<dy?-1:1; /* no disc tag counts as disc 1 */
  unsigned int tx=x->track?(unsigned)x->track:~0u,ty=y->track?(unsigned)y->track:~0u;if(tx!=ty)return tx<ty?-1:1; /* untagged after tagged */
  break;
 }
 case SORT_NAME:case SORT_NAME_ZA:c=natural_compare(x->title[0]?x->title:x->name,y->title[0]?y->title:y->name);if(c)return m==SORT_NAME_ZA?-c:c;break;
 case SORT_NEW:c=date_cmp(y->date,x->date);if(c)return c;break;
 case SORT_OLD:c=date_cmp(x->date,y->date);if(c)return c;break;
 }
 c=natural_compare(x->name,y->name);return m==SORT_NAME_ZA?-c:c;
}
/* folders: by name (Z-A reversed), or by date */
static int folder_order(int m,const SortKey *x,const SortKey *y) {
 int c=m==SORT_NEW?date_cmp(y->date,x->date):m==SORT_OLD?date_cmp(x->date,y->date):0;if(c)return c;
 c=natural_compare(x->name,y->name);return m==SORT_NAME_ZA?-c:c;
}
static void entry_key(const Entry *e,SortKey *k){k->name=e->name;k->album=e->album;k->title=e->title;k->disc=e->disc;k->track=e->track;k->date=e->date;}
static int sort_pin; /* at the top of MUSIC the Playlists folder comes first */
static int sort_mode; /* the order of the folder being listed */
static int compare(const void *a,const void *b) {
 const Entry *x=a,*y=b;
 if(sort_pin){int px=x->folder==1 && !strcmp(x->name,PLAYLIST_DIR),py=y->folder==1 && !strcmp(y->name,PLAYLIST_DIR);if(px!=py)return py-px;}
 /* Shuffle all songs, Internet Radio, Videos, playlists, folders, songs */
 int rx=x->folder==4?7:x->folder==5?6:x->folder,ry=y->folder==4?7:y->folder==5?6:y->folder;
 if(rx!=ry)return ry-rx;
 SortKey kx,ky;entry_key(x,&kx);entry_key(y,&ky);
 if(!x->folder)return song_order(sort_mode,&kx,&ky);
 if(x->folder==1)return folder_order(sort_mode,&kx,&ky);
 return natural_compare(x->name,y->name);
}
static void sort_read_tags(void),sort_notice(void); /* below, after tags.h */
#define SHUFFLE_ALL_NAME "Shuffle all songs"
static void browse(void) {
 lock(meta_lock);browse_gen++;count=entries_total=0;names_reset();scroll=0;cursor=0;in_playlist=0;in_radio=0;in_pod=0;
 int d=sceIoDopen(folder);if(d<0){unlock(meta_lock);return;}
 SceIoDirent e;memset(&e,0,sizeof(e));
 int vf=in_video_folder();
 while(sceIoDread(d,&e)>0) {
  if(e.d_name[0]!='.') {
   int dir=FIO_S_ISDIR(e.d_stat.st_mode),list=!dir && !vf && m3u(e.d_name);
   if(dir || list || (vf?is_video_file(e.d_name):mp3(e.d_name))){Entry *n=entry_add(e.d_name,dir?1:list?2:0);if(n)n->date=sort_date(&e.d_stat.sce_st_mtime);}
  }
  memset(&e,0,sizeof(e));
 }
 sceIoDclose(d);
 sort_pin=!strcmp(folder,root);sort_mode=vf?video_sort:music_sort;
 /* sorting by tags: read every song's tags now (the rows show them too, so the tag thread has nothing left to do) */
 if(!vf && sort_needs_tags(sort_mode)) {
  int songs=0;for(int i=0;i<count;i++)if(!entries[i].folder)songs++;
  if(songs>=80){unlock(meta_lock);sort_notice();lock(meta_lock);}
  sort_read_tags();
 }
 /* top of MUSIC: Shuffle all songs (when there is anything to shuffle). Videos, Podcasts and Internet Radio are tiles on Home. */
 if(sort_pin && count){entries_total--;entry_add(SHUFFLE_ALL_NAME,4);}
 qsort(entries,count,sizeof(Entry),compare);unlock(meta_lock);
}
/* rows the folder has that the list could not hold (more than ENTRY_MAX) */
static int entries_hidden(void){return entries_total>count?entries_total-count:0;}
static void radio_back(void),pod_back(void),go_home(int sel);
static void parent(void) {
 if(in_radio){radio_back();return;}
 if(in_pod){pod_back();return;}
 if(in_playlist) { /* back out of an open playlist to the list of playlists */
  char name[256];const char *last=strrchr(playlist_file,'/');snprintf(name,sizeof(name),"%s",last?last+1:playlist_file);
  browse();for(int i=0;i<count;i++)if(!strcmp(entries[i].name,name)){cursor=i;break;}
  return;
 }
 if(!strcmp(folder,video_root)){go_home(1);return;} /* the top of VIDEO: Home, on Videos */
 if(!strcmp(folder,root) || strlen(folder)<=strlen(root)){cursor=0;return;}
 char child[256];const char *last=strrchr(folder,'/');snprintf(child,sizeof(child),"%s",last?last+1:"");
 char *slash=strrchr(folder,'/');if(slash)*slash=0;browse();
 for(int i=0;i<count;i++)if(strcmp(entries[i].name,child)==0){cursor=i;break;}
}
static int request_mode_guarded(const char *path,int continuous,unsigned int expected) {
 sceKernelWaitSema(command_lock,1,NULL);
 if(expected && serial!=expected){sceKernelSignalSema(command_lock,1);return 0;}
 snprintf(pending,sizeof(pending),"%s",path?path:"");
 if(path && path!=active)snprintf(active,sizeof(active),"%s",path);
 if(!continuous)paused=0;
 error_code=0;stage=0;serial++;
 pending_seek=next_request_seek;next_request_seek=0;pending_continuous=continuous && gapless;if(path)duration=0;elapsed=0;seek_preview=-1;
 sceKernelSignalSema(command_lock,1);return 1;
}
static void request(const char *path){request_mode_guarded(path,0,0);}
static void seek_to(int seconds) {
 if(!active[0] || duration<=0)return;
 if(seconds<0)seconds=0;
 if(seconds>=duration)seconds=duration-1;
 sceKernelWaitSema(command_lock,1,NULL);
 snprintf(pending,sizeof(pending),"%s",active);pending_seek=seconds;pending_continuous=0;
 elapsed=seconds;seek_preview=seconds;
 error_code=0;stage=0;serial++;
 sceKernelSignalSema(command_lock,1);
}
#include "queue.h"
#include "eq.h"
static int theme_color[9];static void themes_apply(void); /* Theme color (below): saved in settings v10 */
#include "settings.h"
static void fail(int where,int code) {
 stage=where;error_code=(unsigned int)code;
 /* Persist the exact code: it can be read from the PC after a device test. */
 FILE *f=fopen("audio-error.log","a");
 if(f){fprintf(f,"stage=%d code=%08X file=%s\n",where,(unsigned int)code,active);fclose(f);}
}
/* ID3 lengths are synchsafe: album artwork may occupy hundreds of KB. */
static int stream_start(int fd,int end) {
 unsigned char h[10];int pos=0;
 while(pos+10<end) {
  if(sceIoLseek32(fd,pos,PSP_SEEK_SET)<0)return -1;
  if(sceIoRead(fd,h,10)!=10)return -1;
  if(memcmp(h,"ID3",3)!=0)break;
  if((h[6]|h[7]|h[8]|h[9])&128)return -1;
  int size=(h[6]<<21)|(h[7]<<14)|(h[8]<<7)|h[9];
  int next=pos+10+size+((h[3]==4 && (h[5]&16))?10:0);
  if(next<=pos || next>=end)return -1;
  pos=next;
 }
 return pos;
}
#include "audioengine.h"
static int exit_callback(int a,int b,void *c){(void)a;(void)b;(void)c;running=0;return 0;}
/* Power events: the HOLD switch turns the screen off; waking from sleep reopens the song. */
static int power_callback(int unknown,int flags,void *common){(void)unknown;(void)common;power_flags=flags;power_events++;hold_on=(flags&PSP_POWER_CB_HOLD_SWITCH)!=0;if(flags&(PSP_POWER_CB_RESUME_COMPLETE|PSP_POWER_CB_RESUMING))power_resumed=1;return 0;}
static int callbacks(SceSize a,void *b){(void)a;(void)b;int cb=sceKernelCreateCallback("exit",exit_callback,NULL);sceKernelRegisterExitCallback(cb);
 int pcb=sceKernelCreateCallback("power",power_callback,NULL);if(pcb>=0 && scePowerRegisterCallback(0,pcb)<0)scePowerRegisterCallback(-1,pcb);
 sceKernelSleepThreadCB();return 0;}

#include "gfx.h"
/* Cyberpunk, Phosphor, Y2K and Studio colors are copied from desktop ZOID's theme files (Themes folder).
   Zoid uses the rounded layout; the other four are HUD skins (hud.h). */
#define THEME_N 9
#define MODERN (theme==0)
/* Paper (desktop Paper.xaml): a page of warm off-white stock, black ink in Cambria, red-pen accents, matte e-ink displays, no glow (paper.h) */
#define PAPER (theme==5)
#define ITALIC(...) do{text_italic=0.2f;__VA_ARGS__;text_italic=0;}while(0)
typedef struct {unsigned int bg,bg2,panel,raised,line,fg,dim,faint,accent,on_accent,second;const char *name;
 unsigned int hot,label,header_bg,header_fg,playing,play_row,folder,rim_mid,list_a,list_b,select;} Theme;
static const Theme themes_base[THEME_N]={
 {0x111113,0x1d1d22,0x1c1c21,0x2c2c33,0x2c2c32,0xf2f2f4,0x9c9ca8,0x60606c,0xc8f000,0x141414,0x6f8a00,"Zoid",
  0xf2f2f4,0xc8f000,0x2c2c33,0xf2f2f4,0xc8f000,0xc8f000,0xc8f000,0x2c2c32,0x111113,0x111113,0x2c2c33},
 {0x07070d,0x0a0a12,0x0c0c14,0x1a1f33,0x1e2a3a,0xd6f4ff,0x6e86a8,0x2e5a70,0x00f0ff,0x0a0a12,0xff2a6d,"Cyberpunk",
  0xe8f7ff,0xfcee0a,0xfcee0a,0x0a0a12,0xfcee0a,0xff2a6d,0xfcee0a,0xb44cff,0x0b0b15,0x07070d,0x3a0f2e},
 {0x010603,0x010804,0x020b05,0x0b2a14,0x0e3a1c,0x3dff73,0x1f9442,0x0b3a19,0x3dff73,0x010603,0x26b04f,"Phosphor",
  0xc4ffd3,0x26b04f,0x26b04f,0x010603,0xc4ffd3,0x3dff73,0x3dff73,0x167a36,0x010603,0x010603,0x14532a},
 /* Y2K: deep water blue glass, lime jelly accent (Y2K.xaml) */
 {0x063a55,0x052f44,0x0a557a,0x2a86b4,0x052f44,0xffffff,0xa8d8ea,0x5f9db8,0xffffff,0x0b3a10,0xb4f05a,"Y2K",
  0xffffff,0xb4f05a,0xd8eef7,0x0a4a6a,0xffffff,0x3db8e0,0xb4f05a,0x2a8fba,0x095070,0x063d57,0x2a86b4},
 /* Studio: graphite hardware with red LED readouts (Studio.xaml / RackStudio.xaml) */
 {0x0e0e0e,0x121212,0x070606,0x2c2c2c,0x262626,0xdadada,0x808080,0x4a4a4a,0xff3b2a,0x0e0e0e,0xffb020,"Studio Rack",
  0xffffff,0xbdbdbd,0x2e2e2e,0xd0d0d0,0xff4a38,0x3a0d08,0xff3b2a,0x2c2c2c,0x181818,0x141414,0x3a100c},
 /* Paper: ink is the main color (accent), the red pen the second; panels are the e-ink display (#E4E0D6, hairline #BDB5A4) */
 {0xEFEBE3,0xEDE9E0,0xE4E0D6,0xFAF8F3,0xD3CCBE,0x1F1D1A,0x6E685E,0x9A9284,0x1F1D1A,0xFAF8F3,0xB83A2E,"Paper",
  0xB83A2E,0xB83A2E,0xF2EEE6,0x6E685E,0xB83A2E,0xF0DCD3,0x8A7F6E,0xBDB5A4,0xFAF8F3,0xF5F2EB,0xF0DCD3},
 /* The Zoidstation skins (zoidstation.h). 1: the console's gray, black type, the logo's exact colors, all four as accents; blue selections */
 {0x80848c,0xa7aab2,0xb8bdc1,0xc9cdd1,0x575a60,0x1c1d21,0x4b4e54,0x80848c,0xde0029,0xffffff,0x00aa9e,"Zoidstation 1",
  0xffffff,0x326db3,0x3c3f45,0xffffff,0xde0029,0xf3c202,0x326db3,0x575a60,0xc9cdd1,0xbfc3c8,0x326db3},
 /* 2: deep blue night, pale blue light */
 {0x00040c,0x000818,0x06204a,0x0c3470,0x1d4f9a,0xd6e6ff,0x7f9cd0,0x40598a,0x6fb4ff,0x00040c,0xa0e0ff,"Zoidstation 2",
  0xffffff,0x9fd0ff,0x06204a,0xa8c6f0,0x9fd8ff,0x2a6fd0,0x6fb4ff,0x2a5ab0,0x000a1e,0x02102a,0x103a7a},
 /* Portable: the cross media bar's blue, white type and icons, glass */
 {0x0e3d7a,0x1a6db4,0x2a7cc0,0x4a96d0,0x8fc0e8,0xffffff,0xc4d8ee,0x8eb0d4,0xffffff,0x0e3d7a,0xbfe3ff,"Zoidstation Portable",
  0xffffff,0xffffff,0x2a7cc0,0xffffff,0xffffff,0xffffff,0xffffff,0x8fc0e8,0x0e3d7a,0x0b3570,0x5aa0dc},
};
/* Theme color: Zoid, Phosphor, Y2K and Zoidstation Portable come in several colors. themes[] is themes_base
   with the picked colors applied (themes_apply), so everything that reads T follows them.
   Zoid: its accent. Phosphor: the monitor phosphors (P1 green, P3 amber, P4 white) and cyan, the whole
   tube in that one color. Portable: the cross media bar's twelve monthly colors, or By month, which
   follows the calendar the way the handheld did. */
static Theme themes[THEME_N];
static const Theme *T; /* the theme in use: &themes[theme] (set below) */

static const char *zoid_color_names[]={"Lime","Orange","Cyan","Pink","Violet","Red","Yellow"};
static const unsigned int zoid_colors[]={0xc8f000,0xff8c1a,0x1ad4ff,0xff5fa8,0xa98bff,0xff4d4d,0xffd21a};
static const char *phos_color_names[]={"Green","Amber","White","Cyan"};
static const unsigned int phos_colors[]={0x3dff73,0xffb000,0xe6eeff,0x40e8ff};
static const char *xmb_color_names[]={"By month","Blue","Silver","Gold","Lime","Pink","Green","Purple","Teal","Indigo","Orange","Brown","Red"};
static const unsigned int xmb_top[]={0,0x2b8ad8,0xa9b2bc,0xd2ae3c,0x8cc83c,0xe07aa8,0x3cae5a,0x9a6ad0,0x2ab4c0,0x5a64d0,0xe08a2a,0xa87a52,0xd0443c};
static const unsigned int xmb_bottom[]={0,0x0b2e70,0x434b56,0x6a500e,0x376214,0x7a2a52,0x145a28,0x3e2470,0x0c5662,0x1c2470,0x7a3a0a,0x4a2e18,0x6a1414};
static const int xmb_month[12]={2,3,4,5,6,7,8,1,9,10,11,12}; /* January silver ... August blue ... December red */
/* Y2K: the translucent colors of the era's computers. Y2K draws with many fixed blues (hud.h), so
   rather than a new table it is recolored as it draws: every blue of the glass takes the picked hue
   (and saturation, for Graphite), the lime jelly takes that color's own accent; whites, grays and the
   amber warning stay as they are. */
static const char *y2k_color_names[]={"Bondi Blue","Blueberry","Grape","Strawberry","Tangerine","Lime","Graphite"};
static const short y2k_hue[]={200,222,276,346,26,96,205};
/* each color's own accent, mostly across the wheel from it: Bondi keeps the desktop's lime jelly; Blueberry purple,
   Grape yellow, Strawberry aqua, Tangerine sky blue, Lime violet, Graphite orange */
static const short y2k_acc[]={84,285,60,170,195,280,28};
static const unsigned char y2k_sat[]={255,255,235,245,255,235,34};
static unsigned int y2k_remap(unsigned int c) {
 int k=theme_color[3];if(theme!=3 || k<=0 || k>6)return c;
 static unsigned int cin[64],cout[64];static int ck=-1;if(ck!=k){memset(cin,0xff,sizeof(cin));ck=k;}
 unsigned int slot=(c^(c>>7)^(c>>15))&63;if(cin[slot]==c)return cout[slot];
 float r=((c>>16)&255)/255.0f,g=((c>>8)&255)/255.0f,b=(c&255)/255.0f;
 float mx=r>g?(r>b?r:b):(g>b?g:b),mn=r<g?(r<b?r:b):(g<b?g:b),d=mx-mn,s=mx>0?d/mx:0,h=0;
 unsigned int out=c;
 if(s>=0.12f && d>0) {
  if(mx==r)h=60*fmodf((g-b)/d+6,6);else if(mx==g)h=60*((b-r)/d+2);else h=60*((r-g)/d+4);
  int blue=h>=165 && h<=250,lime=h>=60 && h<130;
  if(blue || lime) {
   float nh=blue?y2k_hue[k]:y2k_acc[k];if(blue)s=s*y2k_sat[k]/255.0f;
   float cc=mx*s,x=cc*(1-fabsf(fmodf(nh/60,2)-1)),m=mx-cc,rr,gg,bb;int sec=(int)(nh/60)%6;
   rr=sec==0||sec==5?cc:sec==1||sec==4?x:0;gg=sec==1||sec==2?cc:sec==0||sec==3?x:0;bb=sec==3||sec==4?cc:sec==2||sec==5?x:0;
   out=((unsigned int)((rr+m)*255+0.5f)<<16)|((unsigned int)((gg+m)*255+0.5f)<<8)|(unsigned int)((bb+m)*255+0.5f);
  }
 }
 cin[slot]=c;cout[slot]=out;return out;
}
static int theme_color_count(int t){return t==0?7:t==2?4:t==3?7:t==8?13:0;}
static const char *theme_color_name(int t,int i){return t==0?zoid_color_names[i]:t==2?phos_color_names[i]:t==3?y2k_color_names[i]:t==8?xmb_color_names[i]:"";}
static unsigned int tint(unsigned int c,int k){unsigned int o=0;for(int s=0;s<24;s+=8){int v=((c>>s)&255)*k/255;o|=(unsigned int)(v>255?255:v)<<s;}return o;}
static int xmb_pick(void) {
 if(theme_color[8]>0)return theme_color[8];
 ScePspDateTime t;memset(&t,0,sizeof(t));if(sceRtcGetCurrentClockLocalTime(&t)<0 || t.month<1 || t.month>12)return 1;
 return xmb_month[t.month-1];
}
static void themes_apply(void) {
 memcpy(themes,themes_base,sizeof(themes));
 for(int t=0;t<THEME_N;t++)if(theme_color[t]<0 || theme_color[t]>=(theme_color_count(t)?theme_color_count(t):1))theme_color[t]=0;
 if(theme_color[0]) { /* Zoid: the accent everywhere it shows */
  Theme *z=&themes[0];unsigned int a=zoid_colors[theme_color[0]];
  z->accent=z->label=z->playing=z->play_row=z->folder=a;z->second=tint(a,140);
 }
 if(theme_color[2]) { /* Phosphor: every shade of the tube in the new phosphor */
  Theme *p=&themes[2];unsigned int h=phos_colors[theme_color[2]];
  p->bg=p->on_accent=p->list_a=p->list_b=p->header_fg=tint(h,6);p->bg2=tint(h,8);p->panel=tint(h,11);p->raised=tint(h,43);p->line=tint(h,58);
  p->fg=p->accent=p->play_row=p->folder=h;p->dim=tint(h,148);p->faint=tint(h,58);p->second=p->label=p->header_bg=tint(h,176);
  p->hot=p->playing=mix(h,0xffffff,170);p->rim_mid=tint(h,122);p->select=tint(h,84);
 }
 { /* Zoidstation Portable: its blue, or the month's color */
  int k=xmb_pick();
  if(k!=1) {
   Theme *x=&themes[8];unsigned int top=xmb_top[k],bot=xmb_bottom[k];
   x->bg=x->on_accent=x->list_a=bot;x->bg2=top;x->list_b=tint(bot,215);
   x->panel=x->header_bg=mix(top,0xffffff,20);x->raised=mix(top,0xffffff,60);x->line=x->rim_mid=mix(top,0xffffff,130);
   x->dim=mix(top,0xffffff,190);x->faint=mix(top,0xffffff,120);x->second=mix(top,0xffffff,170);x->select=mix(top,0xffffff,70);
  }
 }
 color_remap=theme_color[3]?y2k_remap:NULL; /* Y2K: only while Y2K is the theme (y2k_remap checks) */
 T=&themes[theme];
}
static const Theme *T=&themes[0];
#include "visualizers.h"
#include "tags.h"
#include "lyrics.h"

/* ---- screens ---- */
enum {SCR_LIBRARY,SCR_NOW,SCR_SETTINGS,SCR_EQ,SCR_LYRICS,SCR_VIDEO,SCR_HOME,SCR_BOOKS};
enum {HOME_MUSIC,HOME_VIDEOS,HOME_PODCASTS,HOME_RADIO,HOME_BOOKS,HOME_SETTINGS}; /* the Home tiles (home.h) */
#define HOME_TILES 5
static void video_start(const char *path); /* video_ui.h */
static void video_log_flush(void);
static int screen=SCR_HOME,return_screen=SCR_HOME;
static int np_return=SCR_LIBRARY; /* where Circle on Now Playing goes back to: the Library, Home or Books */
static int np_focus=2;          /* 0 shuffle 1 prev 2 play 3 next 4 repeat 5 seek bar 6+ visualizer panel */
static int set_item,set_scroll;
#define LIB_TOP 40
#define ROW_H 24
#define LIB_ROWS 8
/* Settings: each option keeps a fixed id (used by code and tests); set_order sets the
   on-screen order, grouped into sections. set_item is a row. */
#define SET_N 29
#define SET_EXIT 19
static int show_fps;static float fps;
#define SET_ROWS 6
static const int set_order[SET_N]={0,28,1,2,3,4,22,24, 18,27,8,9, 6,7,5,11,20, 12,13,14,15,23, 25,26, 10,21,17,16,19};
static const char *set_section_of(int row){return row<8?"Look":row<12?"Sound":row<17?"Playback":row<22?"Buttons":row<24?"Online":"System";}
#define SID(r) set_order[r]
static int set_row(int id){for(int r=0;r<SET_N;r++)if(set_order[r]==id)return r;return 0;}
static const char *set_labels[SET_N]={"Theme","Visualizers","Panel 1 effect","Panel 2 effect","Panel 3 effect","Seek step","Shuffle","Repeat","Mixing","Crossfade length",
 "CPU speed","Resume on start","START button","L button","R button","SQUARE button","Frame rate","Turn screen off","Equalizer","Exit ZOID",
 "Sleep timer","Auto screen off","Time display","Hold X time","Scrolling text","Wi-Fi network","Online lyrics","Volume boost","Theme color"};
static const char *set_desc[SET_N]={
 "The look of every screen. Nine skins, from neon to paper to Zoidstation.",
 "How many visualizer panels Now Playing shows, from none to three.",
 "The effect in panel 1. On Now Playing, Triangle on a panel lists every effect.",
 "The effect in panel 2 (when two or three panels are shown).",
 "The effect in panel 3 (when three panels are shown).",
 "How far Left / Right on the seek bar jumps.",
 "Plays the queue in a random order.",
 "Off, repeat this song, or repeat the whole queue.",
 "How songs join: Gapless, Trim silence (cuts quiet ends), Crossfade, or Normal with a short gap.",
 "How long two songs overlap when Mixing is set to Crossfade.",
 "333 MHz is the smoothest. 222 MHz saves battery. Screen off always runs at 222 MHz.",
 "When ZOID starts, it reopens the last song where you left off, paused.",
 "What START does anywhere in ZOID.",
 "What the L button does anywhere in ZOID.",
 "What the R button does anywhere in ZOID.",
 "What SQUARE does anywhere in ZOID.",
 "Shows the frame rate and drawing time in the corner.",
 "Turns the screen off now. The music keeps playing; any button wakes it.",
 "Opens the 10-band equalizer: presets, your saved presets, preamp and auto preamp.",
 "Closes ZOID and returns to the PSP menu.",
 "Pauses the music after this long. It is not saved: it is off each time ZOID starts.",
 "Turns the screen off after this long with no button pressed, to save battery.",
 "Show the clock and the song's end time as time played or as time remaining.",
 "How long to hold X for the item menu: play all, playlists, lyrics, delete.",
 "How fast long titles and names scroll.",
 "The PSP network (Network Settings) used for radio and lyrics. Auto tries each. Wi-Fi turns off after 3 idle minutes.",
 "Find synced lyrics on lrclib.net for songs with none: when Lyrics is open, or for every song. They are saved as .lrc files.",
 "How loud movies, podcasts and radio play: each step is about 3 dB louder, up to Max (as loud as music). Nothing clips. Music is left as is.",
 "The color of Zoid, Phosphor, Y2K or Zoidstation Portable. Portable can follow the month, as the PSP did."};
/* Draws s word-wrapped into at most two lines of width w; returns the lines used. */
static int wrap2(float x,float y,float w,const char *s,unsigned int c,int f,float lh) {
 char line[160];int n=strlen(s),start=0,lines=0;
 while(start<n && lines<2) {
  int wd=0,end=start,brk=-1;
  while(end<n){int next=end;wd+=adv_at(f,s,&next);if(wd>w)break;if(s[end]==' ')brk=end;end=next;}
  if(end<n && brk>start)end=brk;
  int len=end-start;if(len>159)len=159;memcpy(line,s+start,len);line[len]=0;
  text(x,y+lines*lh,line,c,f);lines++;
  start=end;while(start<n && s[start]==' ')start++;
 }
 return lines;
}
/* The song's end time: its length, or "-m:ss" left when Time display is Remaining. */
static void time_text(char *out,int cap,int s);
static void end_text(char *out,int cap,int pos) {
 if(duration<=0){snprintf(out,cap,"%s",radio_is_url(active)?"LIVE":"--:--");return;} /* a station has no end */
 if(time_left){char t[16];int r=duration-pos;time_text(t,sizeof(t),r<0?0:r);snprintf(out,cap,"-%s",t);}
 else time_text(out,cap,duration);
}
static int clock_secs(int pos){if(!time_left || duration<=0)return pos;return duration-pos>0?duration-pos:0;}
static float lib_scroll_f,lib_sel_f,set_scroll_f,set_sel_f;
static char toast[96];static int toast_panel;static unsigned long long toast_until;
static char np_path[PATHLEN],np_title[128],np_artist[96],np_album[96],np_folder[96];static unsigned int np_key;
static Marquee mq_title,mq_artist,mq_album,mq_row,mq_mini;

static void entry_label(const Entry *e,char *out,int cap) {
 if(!e->folder && e->title[0]){snprintf(out,cap,"%s",e->title);return;}
 snprintf(out,cap,"%s",e->name);if(!e->folder){char *dot=strrchr(out,'.');if(dot)*dot=0;}
}
/* All Memory Stick metadata reads (tags, cover art) happen on a low-priority
   thread, so the screen never waits on file I/O or JPEG decoding. */
static volatile unsigned int np_req,np_done;static volatile int np_ready,np_has_art;
static int np_loading; /* tags and art for the song playing are still being read */
static int np_pulse(void){return 150+(int)(105*(0.5f+0.5f*sinf((now_ms%10472)*0.006f)));}
static char np_req_path[PATHLEN],np_new_title[128],np_new_artist[96],np_new_album[96];
static float wave_hist[98];
static unsigned int np_radio_gen=~0u; /* the station title shown (radio_ui.h) */
static int try_cover_file(const char *dir,const char *name) {
 char path[PATHLEN];int n=snprintf(path,sizeof(path),"%s/%s",dir,name);if(n<0 || n>=(int)sizeof(path))return 0;
 int fd=sceIoOpen(path,PSP_O_RDONLY,0);if(fd<0)return 0;
 int size=sceIoLseek32(fd,0,PSP_SEEK_END);sceIoClose(fd);
 return size>0 && load_art(path,0,size);
}
static int pod_meta(const char *path,char *title,int tc,char *show,int sc,char *album,int ac,char *dir,int dcap); /* podcast_ui.h */
static void meta_episode(const char *path,unsigned int req) {
 char t[128],s[96],al[96],dir[64];pod_meta(path,t,sizeof(t),s,sizeof(s),al,sizeof(al),dir,sizeof(dir));
 lock(art_lock);
 int got=0;if(dir[0]){char d[PATHLEN];snprintf(d,sizeof(d),"%s/%s",pod_root,dir);got=try_cover_file(d,"cover.jpg");}
 if(req==np_req){snprintf(np_new_title,sizeof(np_new_title),"%s",t);snprintf(np_new_artist,sizeof(np_new_artist),"%s",s[0]?s:"Podcast");snprintf(np_new_album,sizeof(np_new_album),"%s",al);np_has_art=got;np_ready=1;}
 np_done=req;unlock(art_lock);
}
static void meta_now_playing(void) {
 unsigned int req=np_req;char path[PATHLEN];
 lock(meta_lock);snprintf(path,sizeof(path),"%s",np_req_path);unlock(meta_lock);
 if(radio_is_episode(path)){meta_episode(path,req);return;} /* a streamed podcast episode: titles from its show, art from its cover.jpg */
 if(radio_is_url(path)){np_done=req;return;} /* a station: its titles come from the stream (radio_np_tick) */
 Tags t;read_tags(path,&t,1);
 {char pt[128],ps[96],pa[96],pd[64]; /* a saved podcast episode: the feed names it better than its tags */
  if(pod_meta(path,pt,sizeof(pt),ps,sizeof(ps),pa,sizeof(pa),pd,sizeof(pd))){if(pt[0])snprintf(t.title,sizeof(t.title),"%s",pt);if(ps[0])snprintf(t.artist,sizeof(t.artist),"%s",ps);snprintf(t.album,sizeof(t.album),"%s",pa);}}
 {static Lyrics tmp;lyrics_load(path,&t,&tmp); /* small reads, so before the playback wait: lyrics show early */
  lock(meta_lock);if(req==np_req){memcpy(&lyr_back,&tmp,sizeof(tmp));lyr_back_req=req;lyr_back_ready=1;}unlock(meta_lock);}
 wait_playback_settled();
 char dir[PATHLEN];snprintf(dir,sizeof(dir),"%s",path);char *slash=strrchr(dir,'/');if(slash)*slash=0;
 lock(art_lock);
 int got=t.art_len?load_art(path,t.art_off,t.art_len):0;
 static const char *covers[]={"cover.jpg","folder.jpg","Folder.jpg","Cover.jpg","front.jpg","cover.png","folder.png","AlbumArtSmall.jpg"};
 for(int i=0;i<8 && !got;i++)got=try_cover_file(dir,covers[i]);
 if(req==np_req) {
  snprintf(np_new_title,sizeof(np_new_title),"%s",t.title);snprintf(np_new_artist,sizeof(np_new_artist),"%s",t.artist);snprintf(np_new_album,sizeof(np_new_album),"%s",t.album);
  np_has_art=got;np_ready=1;
 }
 np_done=req;unlock(art_lock);
}
static int meta_library(void) {
 char path[PATHLEN];int index=-1;unsigned int gen;
 lock(meta_lock);gen=browse_gen;
 int from=scroll-2<0?0:scroll-2;
 for(int i=from;i<count && i<scroll+LIB_ROWS+6;i++)if(!entries[i].folder && !entries[i].tagged){if(entry_path(i,path,sizeof(path)))index=i;else entries[i].tagged=1;break;}
 unlock(meta_lock);
 if(index<0)return 0;
 Tags t;read_tags(path,&t,0);
 lock(meta_lock);
 if(gen==browse_gen && index<count && !entries[index].tagged){Entry *e=&entries[index];snprintf(e->title,sizeof(e->title),"%s",t.title);snprintf(e->artist,sizeof(e->artist),"%s",t.artist);e->tagged=1;}
 unlock(meta_lock);return 1;
}
static int meta_thread(SceSize a,void *b) {
 (void)a;(void)b;
 while(running) {
  if(np_req!=np_done)meta_now_playing();
  else if(playback_busy())sceKernelDelayThread(50000);
  else if(!meta_library())sceKernelDelayThread(20000);
 }
 return 0;
}
static void radio_np_tick(void),radio_poll(void),lyrics_online_tick(void),pod_poll(void);
static void pod_np_quick(const char *path);
/* Screen side: show the filename at once, then swap in tags and art when the thread has them. */
static void np_refresh(void) {
 if(strcmp(np_path,active)) {
  snprintf(np_path,sizeof(np_path),"%s",active);np_key++;art_ready=0;memset(wave_hist,0,sizeof(wave_hist));
  np_title[0]=np_artist[0]=np_album[0]=0;np_loading=0;lyr.n=0;lyr_state=active[0]?1:0;lyr_gen++;
  if(radio_is_url(active)) { /* a station: no file to read; radio_np_tick fills in the titles */
   np_folder[0]=0;lyr_state=3;np_radio_gen=~0u;
   if(radio_is_episode(active)){const char *t=strrchr(active,'|');snprintf(np_title,sizeof(np_title),"%s",t?t+1:"Episode");strcpy(np_artist,"Loading...");np_album[0]=0;np_loading=1;}
   lock(meta_lock);np_ready=0;snprintf(np_req_path,sizeof(np_req_path),"%s",active);np_req++;unlock(meta_lock);
  }else if(active[0]) {
   const char *base=strrchr(active,'/');base=base?base+1:active;
   snprintf(np_title,sizeof(np_title),"%s",base);char *dot=strrchr(np_title,'.');if(dot)*dot=0;
   char dir[PATHLEN];snprintf(dir,sizeof(dir),"%s",active);char *slash=strrchr(dir,'/');if(slash)*slash=0;
   const char *dir_name=strrchr(dir,'/');snprintf(np_folder,sizeof(np_folder),"%s",dir_name?dir_name+1:dir);
   strcpy(np_album,"Loading album...");np_loading=1;
   strcpy(np_artist,"Loading artist...");
   pod_np_quick(active); /* a saved podcast episode: its title and show from the list, at once */
   lock(meta_lock);np_ready=0;snprintf(np_req_path,sizeof(np_req_path),"%s",active);np_req++;unlock(meta_lock);
  }
 }
 /* lyrics arrive on their own, usually well before the cover */
 if(lyr_back_ready){lock(meta_lock);if(lyr_back_ready && lyr_back_req==np_req){memcpy(&lyr,&lyr_back,sizeof(lyr));lyr_state=lyr.n?2:3;lyr_gen++;}lyr_back_ready=0;unlock(meta_lock);}
 if(np_ready && sceKernelPollSema(art_lock,1)>=0) {
  if(np_ready && np_done==np_req) {
   if(np_new_title[0])snprintf(np_title,sizeof(np_title),"%s",np_new_title);
   snprintf(np_artist,sizeof(np_artist),"%s",np_new_artist[0]?np_new_artist:"Unknown artist");
   snprintf(np_album,sizeof(np_album),"%s",np_new_album[0]?np_new_album:np_folder);
   if(np_has_art){memcpy(art_tex,art_back,sizeof(art_tex));art_tone=art_back_tone;sceKernelDcacheWritebackRange(art_tex,sizeof(art_tex));art_ready=1;}
   np_key++;np_ready=0;np_loading=0;
  }
  unlock(art_lock);
 }
 /* internet radio: station titles, finished directory lists, online lyrics (radio_ui.h) */
 radio_np_tick();radio_poll();pod_poll();lyrics_online_tick();
 /* the waveform seek bar fills in with the loudness actually heard */
 if(state==2 && !paused && duration>0) {
  int b=elapsed*98/duration;if(b>97)b=97;if(b<0)b=0;
  float v=0;for(int i=0;i<24;i++)v+=levels[i];v=sqrtf(v/(24*28.0f));if(v>wave_hist[b])wave_hist[b]=v;
 }
}

/* ---- actions ---- */
static int is_playing(void){return state && !paused;}
static void toggle_play(void){if(state)paused^=1;else if(active[0] && audio_ready)request(active);}
/* A streamed episode reopens its stream on every jump, so steps add up on screen and it jumps once,
   0.7 s after the last press (holding Right made a connection per step and ran out of memory). */
static unsigned long long stream_seek_at;
/* seek.log: what each seek did, on the card beside the EBOOT (a seek-bar problem seen only on the PSP) */
static void seek_log(const char *what,int a,int b) {
 static int lines;if(lines>=400)return;lines++;
 FILE *f=fopen("seek.log","a");if(!f)return;
 fprintf(f,"%llu %s %d %d: screen %d focus %d state %d paused %d elapsed %d preview %d duration %d serial %u ack %u code %08X file %s\n",
  (unsigned long long)now_ms,what,a,b,screen,np_focus,state,paused,elapsed,seek_preview,duration,serial,seek_ack,error_code,active);
 fclose(f);
}
static unsigned long long seek_check_at;
static void seek_rel(int secs) {
 seek_log("seek_rel",secs,radio_is_episode(active));
 int target=(seek_preview>=0?seek_preview:elapsed)+secs;
 if(radio_is_episode(active) && duration>0){if(target<0)target=0;if(target>=duration)target=duration-1;seek_preview=target;stream_seek_at=now_ms+700;return;}
 seek_to(target);seek_log("after seek_to",target,0);seek_check_at=now_ms+1500;
}
static void seek_by(int dir){seek_rel(dir*seek_steps[seek_step_index]);}
/* Podcast episodes (streamed or saved): Previous and Next skip back 15 s and forward 30 s, as podcast players do. */
#define POD_SKIP_BACK 15
#define POD_SKIP_FWD 30
static int pod_is_episode(const char *path); /* podcast_ui.h */
static int skipping(void){return pod_is_episode(active) && duration>0;}
static void skip_back(void){if(skipping())seek_rel(-POD_SKIP_BACK);else queue_previous();}
static void skip_fwd(void){if(skipping())seek_rel(POD_SKIP_FWD);else queue_next(0,0);}
static void show_toast(int panel,const char *s){snprintf(toast,sizeof(toast),"%s",s);toast_panel=panel;toast_until=now_ms+1200;}
static void open_eq(void),eq_close(void),eq_input(unsigned int press),open_lyrics(void),lyrics_input(unsigned int press);
static void radio_item_menu(void),radio_save(const char *path),radio_remove(const char *path),radio_enter(void);static int radio_activate(int i); /* radio_ui.h */
static void home_input(unsigned int press); /* home.h */
static void open_settings(void){if(screen==SCR_EQ)eq_close();if(screen!=SCR_SETTINGS){return_screen=screen;screen=SCR_SETTINGS;}}
static void np_back(void); /* after books_ui.h */
static void toggle_screen(void){if(screen==SCR_NOW)np_back();else if((screen==SCR_LIBRARY || screen==SCR_HOME) && active[0]){np_return=screen;screen=SCR_NOW;np_focus=2;}}
static void bound_action(int action) {
 switch(action) {
 case 0:toggle_play();break;case 1:skip_back();break;case 2:skip_fwd();break;case 3:request(NULL);break;
 case 4:queue_toggle_shuffle();break;case 5:queue_cycle_repeat();break;case 6:open_settings();break;
 case 7:seek_by(-1);break;case 8:seek_by(1);break;case 10:toggle_screen();break;case 11:screen_off=1;break;case 12:open_eq();break;case 13:open_lyrics();break;
 }
}
/* browse(), sorting by tags: every song's tags (the rows show them too, so the tag thread has nothing left to do). Holds meta_lock. */
static void sort_read_tags(void) {
 for(int i=0;i<count;i++) {
  Entry *n=&entries[i];char path[PATHLEN];if(n->folder || !entry_path(i,path,sizeof(path)))continue;
  static Tags t;read_tags(path,&t,0);
  snprintf(n->title,sizeof(n->title),"%s",t.title);snprintf(n->artist,sizeof(n->artist),"%s",t.artist);
  n->album=name_keep(t.album);n->track=t.track;n->disc=t.disc;n->tagged=1;
 }
}
static void draw(void);
static void sort_notice(void){show_toast(-2,"Sorting by tags...");toast_until=now_ms+5000;draw();}
#include "playlists.h"
#include "radio_ui.h"
#include "podcast_ui.h"
static void lib_activate(void) {
 if(cursor<0 || cursor>=count)return;
 char path[PATHLEN];if(!entry_path(cursor,path,sizeof(path)))return;
 if(in_radio && radio_activate(cursor))return; /* Search, Popular, Genres, a genre */
 if(in_pod && pod_activate(cursor))return;    /* Search, a show, Subscribe... (an episode plays below) */
 if(entries[cursor].folder==5){radio_enter();return;}
 if(entries[cursor].folder==4){if(audio_ready)shuffle_library();}
 else if(entries[cursor].folder==2){playlist_open(path);lib_scroll_f=lib_sel_f=0;}
 else if(entries[cursor].folder==3){strcpy(folder,video_root);browse();lib_scroll_f=lib_sel_f=0;}
 else if(entries[cursor].folder){strcpy(folder,path);browse();lib_scroll_f=lib_sel_f=0;}
 else if(in_video_folder()){video_start(path);}
 /* the song already playing just reopens Now Playing; it does not restart */
 else if(state && !strcmp(path,active)){screen=SCR_NOW;np_focus=2;}
 else if(audio_ready){queue_start(cursor);screen=SCR_NOW;np_focus=2;}
}
/* Visualizer chooser: a list over the panel, in desktop rack order. */
static int chooser,chooser_slot,chooser_item,chooser_scroll;
static int order_pos(int id){for(int i=0;i<VIS_COUNT;i++)if(visual_order[i]==id)return i;return 0;}
static void step_effect(int slot,int dir){visuals[slot]=visual_order[(order_pos(visuals[slot])+VIS_COUNT+dir)%VIS_COUNT];}
static void open_chooser(int slot){chooser=1;chooser_slot=slot;chooser_item=order_pos(visuals[slot]);}
static void np_activate(void) {
 if(np_focus==0)queue_toggle_shuffle();else if(np_focus==1)skip_back();else if(np_focus==2)toggle_play();
 else if(np_focus==3)skip_fwd();else if(np_focus==4)queue_cycle_repeat();
 else if(np_focus==9){set_mix(mix_mode+1);save_settings();show_toast(-1,mix_names[mix_mode]);}
 else if(np_focus>=6){int p=np_focus-6;step_effect(p,1);show_toast(p,visual_names[visuals[p]]);save_settings();}
}
static void set_change(int item,int dir) {
 switch(item) {
 case 0:theme=(theme+THEME_N+dir)%THEME_N;T=&themes[theme];fonts_build(theme);break;
 case 1:visual_count=(visual_count+4+dir)%4;break;
 case 2:case 3:case 4:step_effect(item-2,dir);break;
 case 5:seek_step_index=(seek_step_index+4+dir)%4;break;
 case 6:queue_toggle_shuffle();break;
 case 7:queue_cycle_repeat();if(dir<0)queue_cycle_repeat();break;
 case 8:set_mix(mix_mode+dir);break;
 case 9:{int k=0;for(int i=0;i<6;i++)if(fade_choices[i]==fade_seconds)k=i;fade_seconds=fade_choices[(k+6+dir)%6];break;}
 case 10:cpu_fast=(cpu_fast+3+dir)%3;set_clock(cpu_fast);break;
 case 11:resume_on^=1;break;
 case 12:case 13:case 14:case 15:bindings[item-12]=(bindings[item-12]+14+dir)%14;break;
 case 16:show_fps^=1;return;
 case 17:screen_off=1;return;
 case 18:open_eq();return;
 case 19:if(!dir)running=0;return;
 case 20:sleep_idx=(sleep_idx+7+dir)%7;sleep_until=sleep_idx?now_ms+sleep_minutes[sleep_idx]*60000ULL:0;return;
 case 21:auto_off_idx=(auto_off_idx+5+dir)%5;break;
 case 22:time_left^=1;break;
 case 23:hold_idx=(hold_idx+4+dir)%4;break;
 case 24:scroll_idx=(scroll_idx+3+dir)%3;marquee_ms=scroll_ms[scroll_idx];break;
 case 25:{int n=net_config_count();wifi_config=(wifi_config+n+1+dir)%(n+1);radio_set_wifi(wifi_config);if(net_status==2)net_down();break;} /* the next use joins the chosen one */
 case 26:online_lyrics=(online_lyrics+3+dir)%3;lj_tried=~0u;break;
 case 27:volume_boost=(volume_boost+BOOST_LEVELS+dir)%BOOST_LEVELS;break;
 case 28:{int n=theme_color_count(theme);if(n){theme_color[theme]=(theme_color[theme]+n+dir)%n;themes_apply();}break;}
 }
 save_settings();
}
static void input(unsigned int press) {
 if(osk_active)return; /* the on-screen keyboard reads the buttons itself */
 if(screen==SCR_LIBRARY)np_return=SCR_LIBRARY; /* a song started from a list goes back to that list */
 if(menu_open){menu_input(press);return;}
 if(press&PSP_CTRL_SELECT){chooser=0;if(screen==SCR_SETTINGS)screen=return_screen;else open_settings();return;}
 if(screen==SCR_SETTINGS) {
  if(press&PSP_CTRL_CIRCLE){screen=return_screen;return;}
  if(press&PSP_CTRL_UP)set_item=(set_item+SET_N-1)%SET_N;
  if(press&PSP_CTRL_DOWN)set_item=(set_item+1)%SET_N;
  {int id=SID(set_item),action=id==SET_EXIT || id==17 || id==18; /* rows that do something rather than hold a value */
   if(press&PSP_CTRL_LEFT && !action)set_change(id,-1);
   if(press&PSP_CTRL_RIGHT && !action)set_change(id,1);
   if(press&PSP_CTRL_CROSS)set_change(id,id==SET_EXIT?0:1);}
  return;
 }
 if(chooser && screen==SCR_NOW) {
  if(press&PSP_CTRL_CIRCLE){chooser=0;return;}
  if(press&PSP_CTRL_UP)chooser_item=(chooser_item+VIS_COUNT-1)%VIS_COUNT;
  if(press&PSP_CTRL_DOWN)chooser_item=(chooser_item+1)%VIS_COUNT;
  if(press&(PSP_CTRL_LEFT|PSP_CTRL_RIGHT) && visual_count>1){chooser_slot=(chooser_slot+visual_count+(press&PSP_CTRL_LEFT?-1:1))%visual_count;chooser_item=order_pos(visuals[chooser_slot]);np_focus=6+chooser_slot;}
  if(press&PSP_CTRL_CROSS){visuals[chooser_slot]=visual_order[chooser_item];chooser=0;save_settings();show_toast(chooser_slot,visual_names[visuals[chooser_slot]]);}
  return;
 }
 const unsigned int keys[]={PSP_CTRL_START,PSP_CTRL_LTRIGGER,PSP_CTRL_RTRIGGER,PSP_CTRL_SQUARE};
 for(int i=0;i<4;i++)if(press&keys[i])bound_action(bindings[i]);
 if(screen==SCR_SETTINGS)return;
 if(screen==SCR_EQ){eq_input(press);return;}
 if(screen==SCR_LYRICS){lyrics_input(press);return;}
 if(screen==SCR_HOME){home_input(press);return;}
 if(press&PSP_CTRL_TRIANGLE){if(screen==SCR_NOW && np_focus>=6 && np_focus<9 && visual_count)open_chooser(np_focus-6);else toggle_screen();return;}
 if(screen==SCR_LIBRARY) {
  if(press&PSP_CTRL_UP && cursor>0)cursor--;
  if(press&PSP_CTRL_DOWN && cursor<count-1)cursor++;
  if(press&PSP_CTRL_LEFT){cursor-=LIB_ROWS;if(cursor<0)cursor=0;}
  if(press&PSP_CTRL_RIGHT){cursor+=LIB_ROWS;if(cursor>count-1)cursor=count>0?count-1:0;}
  if(press&PSP_CTRL_CROSS)lib_activate();
  else if(press&PSP_CTRL_CIRCLE){if(!strcmp(folder,root) && !in_radio && !in_pod && !in_playlist)go_home(HOME_MUSIC);else{parent();lib_scroll_f=lib_sel_f=cursor>LIB_ROWS?cursor-LIB_ROWS/2:0;}}
  return;
 }
 if(press&PSP_CTRL_CIRCLE){np_back();return;}
 /* Every theme: the controls row (shuffle, previous, play, next, repeat, mixing = focus 0-4, 9)
    sits right under the seek bar (5). Zoid stacks visualizers - seek bar - controls;
    the HUD skins stack seek bar - controls - visualizers (6+). */
 static const int row[6]={0,1,2,3,4,9};
 if(np_focus>=6 && np_focus<9 && np_focus-6>=visual_count)np_focus=visual_count?6:2;
 int ri=-1;for(int i=0;i<6;i++)if(row[i]==np_focus)ri=i;
 int mid_vis=visual_count?6+(visual_count-1)/2:-1;
 if(press&(PSP_CTRL_LEFT|PSP_CTRL_RIGHT|PSP_CTRL_UP|PSP_CTRL_DOWN))seek_log("press",(int)press,ri);
 if(ri>=0) {
  if(press&PSP_CTRL_LEFT && ri>0)np_focus=row[ri-1];
  if(press&PSP_CTRL_RIGHT && ri<5)np_focus=row[ri+1];
  if(press&PSP_CTRL_UP)np_focus=5;
  if(press&PSP_CTRL_DOWN && !MODERN && mid_vis>=0)np_focus=mid_vis;
 }else if(np_focus==5) {
  if(press&PSP_CTRL_LEFT)seek_by(-1);
  if(press&PSP_CTRL_RIGHT)seek_by(1);
  if(press&PSP_CTRL_DOWN)np_focus=2;
  if(press&PSP_CTRL_UP && MODERN && mid_vis>=0)np_focus=mid_vis;
 }else {
  if(press&PSP_CTRL_LEFT && np_focus>6)np_focus--;
  if(press&PSP_CTRL_RIGHT && np_focus<5+visual_count)np_focus++;
  if(press&(MODERN?PSP_CTRL_DOWN:PSP_CTRL_UP))np_focus=MODERN?5:2;
 }
 if(press&PSP_CTRL_CROSS)np_activate();
}

/* ---- drawing ---- */
static void time_text(char *out,int cap,int s){if(s<0)s=0;snprintf(out,cap,"%d:%02d",s/60,s%60);}
static float hint(float x,float y,int ic,const char *label) {
 icon(ic,x+7,y+8.5f,15,C(T->dim,255));return x+18+text_vc(x+18,y+8.5f,label,C(T->dim,255),F_SMALL)+16;
}
static float hint_key(float x,float y,const char *key,const char *label) {
 int w=text_w(key,F_SMALL);rrect(x,y,w+10,17,4,C(T->raised,255));text_vc(x+5,y+8.5f,key,C(T->dim,255),F_SMALL);
 return x+w+16+text_vc(x+w+16,y+8.5f,label,C(T->dim,255),F_SMALL)+16;
}
static void draw_mini(void) {
 float y=234;
 rect(0,y,480,38,C(T->panel,235));rect(0,y,480,1,C(T->line,255));
 if(!active[0]) {
  float x=16;x=hint(x,y+10,IC_BTN_CROSS,"Open");x=hint(x,y+10,IC_BTN_CIRCLE,"Back");
  (void)x;return;
 }
 int pos=seek_preview>=0?seek_preview:elapsed;
 if(duration>0){float f=(float)pos/duration;if(f>1)f=1;rect(0,y,480*f,2,C(T->accent,255));}
 if(art_ready)art_draw(10,y+6,27,0xffffffff);else{rrect(10,y+6,27,27,5,C(T->raised,255));icon(row_live()?IC_RADIO:IC_NOTE,23.5f,y+19.5f,18,C(T->faint,255));}
 marquee(&mq_mini,np_key,46,y+3,np_title,C(T->fg,255),F_SMALL,300);
 text_fit(46,y+18,np_artist,C(T->dim,255),F_SMALL,300);
 icon(is_playing()?IC_WAVE:IC_PAUSE,362,y+19,16,C(is_playing()?T->accent:T->dim,255));
 icon(IC_BTN_TRIANGLE,384,y+19,15,C(T->dim,255));text_vc(394,y+19,"Now Playing",C(T->dim,255),F_SMALL);
}
static void draw_library(void) {
 const char *name=strrchr(folder,'/');name=(strlen(folder)<=strlen(root) || !name)?"Music":name+1;
 char pln[128];if(in_playlist){const char *b=strrchr(playlist_file,'/');snprintf(pln,sizeof(pln),"%s",b?b+1:playlist_file);char *d=strrchr(pln,'.');if(d)*d=0;name=pln;}
 int back=1; /* Circle always goes somewhere: up a folder, or Home */
 if(in_radio)name=radio_title;
 if(in_pod)name=pod_title;
 if(back)icon(IC_CHEV_L,18,19,18,C(T->dim,255));
 text_fit(back?30:16,8,name,C(T->fg,255),F_MEDIUM,330);
 char info[64];if(in_radio)radio_info(info,sizeof(info));else if(in_pod)pod_info(info,sizeof(info));else if(entries_hidden())snprintf(info,sizeof(info),"Showing %d of %d  -  hold X for more",count,entries_total);else snprintf(info,sizeof(info),"%d %s  -  hold X for more",count,count==1?"item":"items");text_right(400,11,info,C(T->dim,255),F_SMALL);
 rect(0,36,480,1,C(T->line,255));
 if(cursor<scroll)scroll=cursor;if(cursor>=scroll+LIB_ROWS)scroll=cursor-LIB_ROWS+1;
 lib_scroll_f+=(scroll-lib_scroll_f)*0.35f;lib_sel_f+=(cursor-lib_sel_f)*0.4f;
 if(fabsf(scroll-lib_scroll_f)<0.01f)lib_scroll_f=scroll;if(fabsf(cursor-lib_sel_f)<0.01f)lib_sel_f=cursor;
 int bar=count>LIB_ROWS,right=bar?462:470;
 clip(0,LIB_TOP-2,480,LIB_ROWS*ROW_H+2);
 if(count)rrect(8,LIB_TOP+(lib_sel_f-lib_scroll_f)*ROW_H,right-8,ROW_H-2,6,C(T->raised,255));
 for(int i=(int)lib_scroll_f;i<count && i<=(int)lib_scroll_f+LIB_ROWS;i++) {
  Entry *e=&entries[i];float y=LIB_TOP+(i-lib_scroll_f)*ROW_H;int sel=i==cursor;
  char label[256],path[PATHLEN];entry_label(e,label,sizeof(label));
  int playing=0;if(!e->folder && active[0] && entry_path(i,path,sizeof(path)))playing=!strcmp(path,active);
  unsigned int tc=playing?C(T->accent,255):sel?C(T->fg,255):C(mix(T->dim,T->fg,150),255);
  if(e->folder)icon(row_icon(e),28,y+11,18,C(sel?T->accent:mix(T->accent,T->dim,140),255));
  else icon(playing?IC_WAVE:in_radio?IC_RADIO:in_pod?pod_ep_icon(i):IC_NOTE,28,y+11,17,C(playing?T->accent:T->faint,255));
  int tw=right-52-16;
  if(e->folder && e->folder!=4 && !e->artist[0] && !row_is_action(e->folder))icon(IC_CHEV_R,right-12,y+11,14,C(T->faint,255));
  else if(e->artist[0]){int aw=text_w(e->artist,F_SMALL);if(aw>150)aw=150;text_fit(right-10-aw,y+3,e->artist,C(T->dim,255),F_SMALL,150);tw-=aw+12;}
  if(sel)marquee(&mq_row,i*7919+count,44,y,label,tc,F_MEDIUM,tw);else text_fit(44,y,label,tc,F_MEDIUM,tw);
 }
 unclip();
 if(bar){float track=LIB_ROWS*ROW_H-4,thumb=track*LIB_ROWS/count;if(thumb<14)thumb=14;
  rrect(468,LIB_TOP,4,track,2,C(T->line,255));rrect(468,LIB_TOP+(track-thumb)*lib_scroll_f/(count-LIB_ROWS),4,thumb,2,C(T->dim,255));}
 {char l1[96],l2[96];if(radio_empty(l1,sizeof(l1),l2,sizeof(l2)) || pod_empty(l1,sizeof(l1),l2,sizeof(l2))){icon(in_pod?IC_MIC:IC_RADIO,240,110,40,C(T->faint,radio_waiting() || pod_waiting()?np_pulse():255));text_center(240,136,l1,C(T->dim,255),F_MEDIUM);text_center(240,160,l2,C(T->faint,255),F_SMALL);}
 else if(!count){int vf=in_video_folder();icon(vf?IC_FILM:IC_FOLDER,240,110,40,C(T->faint,255));text_center(240,136,vf?"No videos here":"No songs or folders here",C(T->dim,255),F_MEDIUM);
  text_center(240,160,vf?"Put .mp4 videos in the VIDEO folder on your memory stick":"Put music in the MUSIC folder on your memory stick",C(T->faint,255),F_SMALL);}}
 draw_mini();
}
/* Zoid Now Playing: cover art on the left, a wide info column to its right with the
   visualizers under the song details, then the seek bar and a full-width row of controls
   along the bottom (shuffle, previous, play, next, repeat, mixing).
   Up/Down: visualizers - seek bar - controls. */
static void mix_label(char *out,int cap){if(mix_mode==2)snprintf(out,cap,"Crossfade %ds",fade_seconds);else snprintf(out,cap,"%s",mix_names[mix_mode]);}
static void draw_now(void) {
 char buf[64];int vc=visual_count;
 if(error_code){if(row_live() || (radio_is_url(active) && (int)error_code<=-1001 && (int)error_code>=-1199))snprintf(buf,sizeof(buf),"%s",radio_error_text((int)error_code));else snprintf(buf,sizeof(buf),"Audio error %d / %08X",stage,error_code);text_right(400,4,buf,C(T->second,255),F_SMALL);}
 else{const char *rp=radio_is_url(active)?radio_phase_text():NULL;
  if(rp)snprintf(buf,sizeof(buf),"%s",rp);else if(state==1)strcpy(buf,"Loading...");else if(!state)strcpy(buf,"Stopped");else if(paused)strcpy(buf,"Paused");
  else if(row_live())snprintf(buf,sizeof(buf),"Live  -  station %d of %d",queue_count?queue_pos+1:0,queue_count);
  else snprintf(buf,sizeof(buf),pod_is_episode(active)?"Episode %d of %d":"Track %d of %d",queue_count?queue_pos+1:0,queue_count);
  text_right(400,4,buf,C(T->dim,255),F_SMALL);}
 /* cover art */
 const float ax=12,ay=28,art=182;
 if(art_ready) {
  glow(ax+art/2,ay+art/2+6,art*0.72f,C(art_tone,90));
  rrect(ax+3,ay+5,art,art,8,C(0,120));
  art_draw(ax,ay,art,0xffffffff);
 }else{rrect(ax,ay,art,art,12,C(T->raised,255));icon(row_live()?IC_RADIO:IC_NOTE,ax+art/2,ay+art/2-(np_loading?10:0),art*0.42f,C(T->faint,255));
  if(np_loading)text_center(ax+art/2,ay+art-30,"Loading cover...",C(T->dim,np_pulse()),F_SMALL);}
 /* song details: the whole column is theirs when the visualizers are off */
 const float cx0=208,cw=260;
 float ty=vc?30:56;
 marquee(&mq_title,np_key,cx0,ty,np_title[0]?np_title:"Nothing playing",C(T->fg,255),F_LARGE,cw);
 marquee(&mq_artist,np_key,cx0,ty+32,np_artist,np_loading?C(T->dim,np_pulse()):C(T->accent,255),F_MEDIUM,cw);
 marquee(&mq_album,np_key,cx0,ty+56,np_album,C(T->dim,np_loading?np_pulse():255),F_MEDIUM,cw);
 /* visualizers, side by side under the details */
 if(vc) {
  float vy=124,vh=84,gap=6,vw=(cw-gap*(vc-1))/vc;
  for(int i=0;i<vc;i++) {
   float px=cx0+i*(vw+gap);int focused=np_focus==6+i;
   if(focused)rrect(px-2,vy-2,vw+4,vh+4,11,C(T->accent,255));
   rrect(px,vy,vw,vh,9,C(T->panel,255));
   clip((int)px+3,(int)vy+3,(int)vw-6,(int)vh-6);visualizer(visuals[i],i,px+4,vy+4,vw-8,vh-8);unclip();
   if(toast_panel==i && now_ms<toast_until){int a=toast_until-now_ms>300?255:(int)(toast_until-now_ms)*255/300;
    int w=text_w(toast,F_SMALL)+14;if(w>vw-4)w=vw-4;rrect(px+vw/2-w/2,vy+vh/2-10,w,20,7,C(T->bg,a*220/255));text_mid(px+vw/2,vy+vh/2,toast,C(T->fg,a),F_SMALL);}
  }
 }
 /* seek bar */
 int pos=seek_preview>=0?seek_preview:elapsed,focus=np_focus==5;
 const float sx=54,sw=372,by=223;float bh=focus?6:4,frac=duration>0?(float)pos/duration:row_live() && state==2?1:0;if(frac>1)frac=1;
 rrect(sx,by-bh/2,sw,bh,bh/2,C(T->line,255));rrect(sx,by-bh/2,sw*frac,bh,bh/2,C(T->accent,255));
 float kx=sx+sw*frac;
 if(focus){glow(kx,by,18,C(T->accent,150));disk(kx,by,7,C(T->fg,255));}else disk(kx,by,5,C(T->fg,255));
 time_text(buf,sizeof(buf),pos);text_right(sx-10,by-7,buf,C(focus?T->accent:T->dim,255),F_SMALL);
 end_text(buf,sizeof(buf),pos);text(sx+sw+10,by-7,buf,C(T->dim,255),F_SMALL);
 /* controls: six wide keys across the bottom */
 static const int row[6]={0,1,2,3,4,9};
 const float ky=236,kh=30,gap=6,kw=(456-gap*5)/6;
 for(int k=0;k<6;k++) {
  int id=row[k],focused=np_focus==id;float x=12+k*(kw+gap),cx=x+kw/2,cy=ky+kh/2;
  int on=(id==0 && shuffle) || (id==4 && repeat_mode) || (id==9 && mix_mode!=3);
  if(focused)rrect(x-2,ky-2,kw+4,kh+4,11,C(T->accent,255));
  if(id==2) {
   rrect(x,ky,kw,kh,9,C(T->accent,255));
   icon(is_playing()?IC_PAUSE:IC_PLAY,is_playing()?cx:cx+1.5f,cy,22,C(T->on_accent,255));
   continue;
  }
  rrect(x,ky,kw,kh,9,C(focused?mix(T->raised,T->fg,30):T->raised,255));
  unsigned int c=(id==1 || id==3)?T->fg:on?T->accent:T->dim;
  if(id==9){char ml[24];mix_label(ml,sizeof(ml));text_mid(cx,cy,ml,C(on?T->accent:T->dim,255),F_SMALL);}
  else if((id==1 || id==3) && skipping())text_mid(cx,cy,id==1?"-15 sec":"+30 sec",C(c,255),F_SMALL); /* skip keys on an episode */
  else {int ic=id==0?IC_SHUFFLE:id==1?IC_PREV:id==3?IC_NEXT:repeat_mode==1?IC_REPEAT1:IC_REPEAT;icon(ic,cx,cy,id==1 || id==3?20:18,C(c,255));}
  if(on && id!=9)disk(cx,ky+kh-5,1.6f,C(T->accent,255));
 }
}
static void setting_value(int item,char *out,int cap) {
 switch(item) {
 case 0:snprintf(out,cap,"%s",themes[theme].name);break;
 case 1:if(visual_count)snprintf(out,cap,"%d panel%s",visual_count,visual_count>1?"s":"");else snprintf(out,cap,"Off");break;
 case 2:case 3:case 4:snprintf(out,cap,"%s",visual_names[visuals[item-2]]);break;
 case 5:snprintf(out,cap,"%d second%s",seek_steps[seek_step_index],seek_step_index?"s":"");break;
 case 6:snprintf(out,cap,"%s",shuffle?"On":"Off");break;
 case 7:snprintf(out,cap,"%s",repeat_mode==1?"Song":repeat_mode==2?"All":"Off");break;
 case 8:snprintf(out,cap,"%s",mix_names[mix_mode]);break;
 case 9:snprintf(out,cap,"%d seconds",fade_seconds);break;
 case 10:snprintf(out,cap,"%s",cpu_fast==2?"Set by ARK-5":cpu_fast?"333 MHz (fast)":"222 MHz (saves battery)");break;
 case 11:snprintf(out,cap,"%s",resume_on?"On":"Off");break;
 case 12:case 13:case 14:case 15:snprintf(out,cap,"%s",binding_names[bindings[item-12]]);break;
 case 16:snprintf(out,cap,"%s",show_fps?"Show":"Hide");break;
 case 18:snprintf(out,cap,"%s - %s",eq_on?"On":"Off",eq_preset_name());break;
 case 20:if(!sleep_idx)snprintf(out,cap,"Off");else{int left=sleep_until>now_ms?(int)((sleep_until-now_ms+59999)/60000):0;snprintf(out,cap,"%d min (%d left)",sleep_minutes[sleep_idx],left);}break;
 case 21:{static const char *n[5]={"Never","30 seconds","1 minute","2 minutes","5 minutes"};snprintf(out,cap,"%s",n[auto_off_idx]);break;}
 case 22:snprintf(out,cap,"%s",time_left?"Remaining":"Elapsed");break;
 case 23:{static const char *n[4]={"Short (0.3 s)","Normal (0.45 s)","Long (0.7 s)","Longer (1 s)"};snprintf(out,cap,"%s",n[hold_idx]);break;}
 case 24:{static const char *n[3]={"Slow","Normal","Fast"};snprintf(out,cap,"%s",n[scroll_idx]);break;}
 case 25:{static char names[11][40];static unsigned long long at;if(!at || now_ms-at>3000){at=now_ms;for(int i=1;i<=10;i++)net_config_name(i,names[i],sizeof(names[i]));}
  const char *on=net_status==2?" (on)":net_status==1?" (joining)":"";
  if(!wifi_config)snprintf(out,cap,"Auto%s",on);else snprintf(out,cap,"%d: %.24s%s",wifi_config,names[wifi_config][0]?names[wifi_config]:"(empty)",on);break;}
 case 26:{static const char *n[3]={"Off","When Lyrics is open","For every song"};snprintf(out,cap,"%s",n[online_lyrics]);break;}
 case 27:{static const char *n[BOOST_LEVELS]={"Off","Low","Medium","High","Max"};snprintf(out,cap,"%s",n[volume_boost]);break;}
 case 28:if(theme_color_count(theme)){if(theme==8 && !theme_color[8]){static const char *mn[12]={"Silver","Gold","Lime","Pink","Green","Purple","Teal","Blue","Indigo","Orange","Brown","Red"};ScePspDateTime t;memset(&t,0,sizeof(t));sceRtcGetCurrentClockLocalTime(&t);snprintf(out,cap,"By month (%s)",t.month>=1 && t.month<=12?mn[t.month-1]:"Blue");}else snprintf(out,cap,"%s",theme_color_name(theme,theme_color[theme]));}else snprintf(out,cap,"Not in this theme");break;
 default:out[0]=0;
 }
}
static int set_off(int id){return (id==28 && !theme_color_count(theme)) || (id==3 && visual_count<2) || (id==4 && visual_count<3) || (id==2 && !visual_count) || (id==9 && mix_mode!=2);}
static void draw_settings(void) {
 text(16,8,"Settings",C(T->fg,255),F_MEDIUM);text_right(400,11,"ZOID 1.0",C(T->faint,255),F_SMALL);
 rect(0,36,480,1,C(T->line,255));
 if(set_item<set_scroll)set_scroll=set_item;if(set_item>=set_scroll+SET_ROWS)set_scroll=set_item-SET_ROWS+1;
 set_scroll_f+=(set_scroll-set_scroll_f)*0.35f;set_sel_f+=(set_item-set_sel_f)*0.4f;
 if(fabsf(set_scroll-set_scroll_f)<0.01f)set_scroll_f=set_scroll;if(fabsf(set_item-set_sel_f)<0.01f)set_sel_f=set_item;
 const int top=42,rh=27;
 clip(0,top-2,480,SET_ROWS*rh+2);
 rrect(8,top+(set_sel_f-set_scroll_f)*rh,456,rh-3,7,C(T->raised,255));
 for(int i=(int)set_scroll_f;i<SET_N && i<=(int)set_scroll_f+SET_ROWS;i++) {
  int id=SID(i);float y=top+(i-set_scroll_f)*rh+1;int sel=i==set_item,off=set_off(id);
  /* a section's first row carries its name at the left edge */
  if(i==0 || strcmp(set_section_of(i),set_section_of(i-1))){char up[16];upper(up,set_section_of(i),sizeof(up));text(16,y+4,up,C(T->accent,sel?255:170),F_SMALL);}
  unsigned int lc=id==SET_EXIT?(sel?T->second:mix(T->second,T->dim,120)):off?T->faint:sel?T->fg:mix(T->dim,T->fg,150);
  text(92,y,set_labels[id],C(lc,255),F_MEDIUM);
  char v[48];setting_value(id,v,sizeof(v));
  if(v[0]){unsigned int vc=off?T->faint:sel?T->accent:T->dim;
   if(sel){icon(IC_CHEV_R,450,y+11,13,C(T->accent,255));int w=text_w(v,F_MEDIUM);text(440-w,y,v,C(vc,255),F_MEDIUM);icon(IC_CHEV_L,430-w,y+11,13,C(T->accent,255));}
   else text_right(450,y,v,C(vc,255),F_MEDIUM);}
 }
 unclip();
 /* what the selected option does */
 rect(0,206,480,1,C(T->line,255));
 wrap2(16,210,448,set_desc[SID(set_item)],C(T->dim,255),F_SMALL,13);
 rect(0,238,480,1,C(T->line,255));
 int id=SID(set_item);
 float x=16;x=hint(x,246,IC_BTN_CROSS,id==SET_EXIT?"Exit":(id==17 || id==18)?"Open":"Change");x=hint(x,246,IC_BTN_CIRCLE,"Back");
 text_vc(x,254.5f,"Left / Right to adjust",C(T->faint,255),F_SMALL);
}
/* Saves the resume point every 15 s while playing and whenever the song changes. */static void resume_tick(void){static unsigned long long last;static unsigned int key;if(!active[0] || video_active)return; /* a film's soundtrack is not a song to resume */if((state==2 && now_ms-last>15000) || key!=np_key){last=now_ms;key=np_key;save_resume();}}
#include "hud.h"
#include "home.h"
#include "eq_ui.h"
#include "lyrics_ui.h"
#include "volume.h"
#include "video_ui.h"
/* ---- battery: always in the top-right corner, drawn in each theme's style ---- */
static int batt_pct=-1,batt_charging,batt_ac;
static void battery_poll(void) {
 static unsigned long long last;static int polled;
 if(polled && now_ms-last<2000)return;polled=1;last=now_ms;
 batt_pct=scePowerIsBatteryExist()?scePowerGetBatteryLifePercent():-1;if(batt_pct>100)batt_pct=100;
 batt_charging=scePowerIsBatteryCharging()>0;batt_ac=scePowerIsPowerOnline()>0;
}
static void battery_bolt(float cx,float cy,unsigned int c) {
 seg(cx+2,cy-4,cx-2,cy+0.5f,1.6f,c);seg(cx-2,cy+0.5f,cx+2,cy-0.5f,1.6f,c);seg(cx+2,cy-0.5f,cx-2,cy+4,1.6f,c);
}
static void draw_battery(void) {
 battery_poll();
 /* On Now Playing the HUD skins draw a frame along the top edge, so the gauge sits inside it */
 const int inset=!MODERN && screen==SCR_NOW;
 const float bw=22,bh=11,x=inset?436:446,y=inset?12:7,cy=y+bh/2;
 int p=batt_pct<0?0:batt_pct,low=batt_pct>=0 && batt_pct<=15 && !batt_charging,blink=low && ((now_ms/500)&1);
 char txt[12];if(batt_pct<0)snprintf(txt,sizeof(txt),"%s",batt_ac?"AC":"--");else snprintf(txt,sizeof(txt),"%d%%",batt_pct);
 unsigned int tc;float frac=p/100.0f;
 if(theme==1) { /* Cyberpunk: square neon frame, four cells, magenta when low */
  unsigned int on=low?T->second:T->accent;
  rect(x,y,bw,bh,C(0x07070e,255));frame_rect(x,y,bw,bh,1,C(on,255));rect(x+bw,y+3,2,bh-6,C(on,255));
  int lit=p?(p*4+99)/100:0;for(int i=0;i<4;i++)rect(x+2+i*4.6f,y+2,3.6f,bh-4,C(on,i<lit && !blink?255:30));
  tc=low?T->second:T->label;
 }else if(theme==2) { /* Phosphor: green tube blocks with a soft glow */
  if(!blink)glow(x+bw/2,cy,16,C(T->accent,40));
  frame_rect(x,y,bw,bh,1,C(T->accent,255));rect(x+bw,y+3,2,bh-6,C(T->accent,255));
  int lit=p?(p*5+99)/100:0;for(int i=0;i<5;i++)rect(x+2+i*3.6f,y+2,2.6f,bh-4,C(T->accent,i<lit && !blink?255:40));
  tc=T->accent;
 }else if(theme==3) { /* Y2K: a glass capsule like the bubble buttons, filled with lime jelly */
  rrect(x-1,y-1,bw+2,bh+2,6,C(0x0b4f72,255));glass_rrect(x,y,bw,bh,5,0x4a9fc6,0x2f86ad,0x13698f,0x0a557a);
  rrect(x+bw+1,y+3.5f,2.5f,bh-7,1,C(0x0b4f72,255));
  float fw=(bw-4)*frac;
  if(fw>=2 && !blink) {
   if(low)glass_rrect(x+2,y+2,fw,bh-4,3.5f,0xffb0a0,0xff7a5a,0xe04a30,0xff6a4a);
   else glass_rrect(x+2,y+2,fw,bh-4,3.5f,0xd8ffa0,0x9ce050,0x6cc02a,0x94dc4c);
  }
  tc=0xffffff;
 }else if(theme==4) { /* Studio: recessed slot with red LED segments, green while charging */
  unsigned int led=batt_charging?0x3bff6a:low?0xffb020:0xff2a1a,off=batt_charging?0x082a10:0x2a0a08;
  rect(x,y,bw,bh,C(0x050505,255));frame_rect(x,y,bw,bh,1,C(0x333333,255));rect(x+bw,y+3,2,bh-6,C(0x333333,255));
  int lit=p?(p*5+99)/100:0;for(int i=0;i<5;i++){int on=i<lit && !blink;float sx=x+2+i*3.6f;rect(sx,y+2,2.6f,bh-4,C(on?led:off,255));}
  tc=0xbdbdbd;
 }else if(PAPER) { /* Paper: a hairline ink outline filled with ink, red pen when low */
  rrect(x,y,bw,bh,1.5f,C(PP_INK,255));rect(x+1,y+1,bw-2,bh-2,C(PP_DISPLAY,255));rect(x+bw,y+3,2,bh-6,C(PP_INK,255));
  float fw=(bw-4)*frac;if(fw>0 && !blink)rect(x+2,y+2,fw,bh-4,C(low?PP_RED:PP_INK,255));
  tc=PP_MUTED;
 }else if(theme==8) { /* Zoidstation Portable: a white outline over the blue, white fill, nothing dark */
  rrect(x,y,bw,bh,3,C(0xffffff,230));rrect(x+1,y+1,bw-2,bh-2,2,C(T->bg2,255));rrect(x+1,y+1,bw-2,bh-2,2,C(0xffffff,40));rrect(x+bw+0.5f,y+3,2,bh-6,1,C(0xffffff,230));
  float fw=(bw-4)*frac;if(fw>0 && !blink)rrect(x+2,y+2,fw,bh-4,1.5f,C(low?0xff8a7a:0xffffff,255));
  tc=0xffffff;
 }else { /* Zoid: rounded outline with an accent fill */
  unsigned int fill=low?0xff5a4a:theme==6?0x00aa9e:T->accent; /* Zoidstation 1: logo teal, as its red accent would read as low */
  rrect(x,y,bw,bh,3,C(T->dim,255));rrect(x+1,y+1,bw-2,bh-2,2,C(T->bg,255));rrect(x+bw+0.5f,y+3,2,bh-6,1,C(T->dim,255));
  float fw=(bw-4)*frac;if(fw>0 && !blink)rrect(x+2,y+2,fw,bh-4,1.5f,C(fill,255));
  tc=T->dim;
 }
 if(batt_charging || (batt_pct<0 && batt_ac))battery_bolt(x+bw/2,cy,C(theme==4?0x050505:theme==3?0x063a55:PAPER?PP_LIST:T->bg,255));
 int w=text_w(txt,F_SMALL);text_vc(x-5-w,cy,txt,C(low?(theme==1?T->second:theme==4?0xffb020:PAPER?PP_RED:0xff5a4a):tc,255),F_SMALL);
}
#include "books_ui.h"
/* The music-note button on the front of the PSP: each press moves to the next theme (and saves it).
   The PSP also mutes the sound on that button; if it wasn't muted before the press and turns muted
   within 1.5 s after, the mute is undone (zoidvol_unmute), so the music keeps playing.
   Only zoidvol.prx can see that button, so without it (stock firmware, the emulator) nothing happens. */
static void theme_next(void) {
 theme=(theme+1)%THEME_N;T=&themes[theme];fonts_build(theme);save_settings();
 char b[64];snprintf(b,sizeof(b),"Theme: %s",themes[theme].name);
 if(screen==SCR_BOOKS)bk_say(b);else show_toast(-2,b);
}
static void note_poll(void) {
 static int was,muted_before;static unsigned long long watch_until;if(!vol_loaded)return;
 int v=zoidvol_get(),muted=v>=0 && (v&0x100);
 int on=(zoidvol_buttons()&PSP_CTRL_NOTE)!=0;
 if(on && !was){theme_next();watch_until=now_ms+1500;}
 if(now_ms<watch_until && muted && !muted_before){if(zoidvol_unmute()>=0){muted=0;vol_last=v&0xff;}} /* the button's mute, not one you chose */
 if(now_ms>=watch_until)muted_before=muted;
 was=on;
}
/* Circle (or Triangle) on Now Playing: back where you came from */
static void np_back(void) {
 int to=np_return;np_return=SCR_LIBRARY;
 if(to==SCR_HOME)go_home(home_sel);else if(to==SCR_BOOKS)books_return();else screen=SCR_LIBRARY;
}
static void draw(void) {
 visual_tick++;
 frame_begin(C(T->bg,255));
 if(MODERN) {
  unsigned int top=T->bg2;if(screen==SCR_NOW && art_ready)top=mix(T->bg2,mix(art_tone,T->bg,110),150);
  grad(0,0,480,200,C(top,255),C(T->bg,255));
  if(screen==SCR_HOME)draw_home();else if(screen==SCR_LIBRARY)draw_library();else if(screen==SCR_NOW)draw_now();else if(screen==SCR_EQ)draw_eq();else if(screen==SCR_LYRICS)draw_lyrics();else draw_settings();
 }else {
  if(Y2K){grad(0,0,480,136,C(0x0b4f72,255),C(0x084563,255));grad(0,136,480,136,C(0x084563,255),C(0x063a55,255));}
  if(MS)ms_background();
  if(screen==SCR_HOME)draw_home();else if(screen==SCR_LIBRARY)draw_hud_library();else if(screen==SCR_NOW)draw_hud_now();else if(screen==SCR_EQ)draw_eq();else if(screen==SCR_LYRICS)draw_lyrics();else draw_hud_settings();
  if(PHOS)phosphor_glass();
 }
 if(chooser && screen==SCR_NOW)draw_chooser();
 if(menu_open)draw_menu();
 if(toast_panel==-2 && now_ms<toast_until && !menu_open)draw_notice();
 draw_battery();
 draw_volume();
 if(show_fps){char b[40];snprintf(b,sizeof(b),"%2.0f FPS  %4.1f MS",fps,cpu_ms);rect(380,254,98,16,C(0,200));text(384,252,b,C(0x00ff00,255),F_SMALL);}
 frame_end();
}

#ifdef ZOID_SMOKE_TEST
static void capture(const char *path){snprintf(capture_path,sizeof(capture_path),"%s",path);}
static void report(const char *label) {
 FILE *f=fopen("ms0:/smoke.txt","a");if(f){fprintf(f,"%s: state=%d elapsed=%d duration=%d paused=%d stage=%d code=%08X screen=%d focus=%d queue=%d/%d shuffle=%d repeat=%d file=%s\n",label,state,elapsed,duration,paused,stage,error_code,screen,np_focus,queue_pos+1,queue_count,shuffle,repeat_mode,active);fclose(f);}
}
static void check(const char *label,int condition){FILE *f=fopen("ms0:/smoke.txt","a");if(f){fprintf(f,"%s: %s\n",condition?"PASS":"FAIL",label);fclose(f);}}
/* opens the first real folder at each level, past the Videos entry, down to the songs */
static void test_open_folders(void){for(int guard=0;guard<8;guard++){int c=0;while(c<count-1 && entries[c].folder>=3)c++;if(!count || !entries[c].folder || entries[c].folder>=3)return;cursor=c;lib_activate();}}
/* a song file that is only an ID3v2.3 tag (title, album, track, disc): enough to list and sort */
static void test_tagged(const char *path,const char *title,const char *album,int track,int disc) {
 unsigned char b[300];int o=10;const char *ids[4]={"TIT2","TALB","TRCK","TPOS"};char v[4][64];
 snprintf(v[0],64,"%s",title);snprintf(v[1],64,"%s",album);
 if(track)snprintf(v[2],64,"%02d/12",track);else v[2][0]=0;if(disc)snprintf(v[3],64,"%d/2",disc);else v[3][0]=0;
 for(int k=0;k<4;k++){int n=strlen(v[k]);if(!n)continue;memcpy(b+o,ids[k],4);b[o+4]=b[o+5]=b[o+6]=0;b[o+7]=n+1;b[o+8]=b[o+9]=b[o+10]=0;memcpy(b+o+11,v[k],n);o+=11+n;}
 int body=o-10;memcpy(b,"ID3\3\0\0",6);b[6]=(body>>21)&127;b[7]=(body>>14)&127;b[8]=(body>>7)&127;b[9]=body&127;
 int fd=sceIoOpen(path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(fd>=0){sceIoWrite(fd,b,o);sceIoClose(fd);}
}
static const char *base_of(const char *p){const char *s=strrchr(p,'/');return s?s+1:p;}
static void test_copy(const char *src,const char *dst){int in=sceIoOpen(src,PSP_O_RDONLY,0),out=sceIoOpen(dst,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);static char buf[16384];int n;if(in>=0 && out>=0)while((n=sceIoRead(in,buf,sizeof(buf)))>0)sceIoWrite(out,buf,n);if(in>=0)sceIoClose(in);if(out>=0)sceIoClose(out);}
static unsigned int test_px(void){return video_pixels?video_pixels[(vid.info.height/2)*video_stride+vid.info.width/2]:0;}
#ifdef ZOID_NET_TEST
static int test_pod_ep(const char *title,PodEp *out){PodShow s;plock();int ok=show_load("Zoid Test Podcast",&s,1);punlock();int f=0;if(ok){for(int i=0;i<s.n;i++)if(!strcmp(s.eps[i].title,title)){*out=s.eps[i];f=1;}free(s.eps);}return f;}
#endif
#ifdef ZOID_BOOKS_TEST
#include "../tests/books_test.h"
#endif
#endif
int main(int argc,char **argv) {
 themes_apply(); /* themes[] before anything draws */
 if(argc>0 && argv[0]) {
  const char *slash=strrchr(argv[0],'/');
  if(slash && slash-argv[0]<(int)sizeof(startup_path)-14){int len=slash-argv[0]+1;memcpy(startup_path,argv[0],len);strcpy(startup_path+len,"startup.log");}
 }
 int log=sceIoOpen(startup_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(log>=0)sceIoClose(log);
 checkpoint("ZOID 1.0 main entered",0);
 /* the clock is set once the settings are read (set_clock below), since CPU speed may leave it to ARK-5 */
 checkpoint("cpu clock",scePowerGetCpuClockFrequencyInt());
 me_init(); /* one Media Engine job at a time (sound and video) */
 meta_lock=sceKernelCreateSema("zoid_meta",0,1,1,NULL);art_lock=sceKernelCreateSema("zoid_art",0,1,1,NULL);
 queue_random=(unsigned int)sceKernelGetSystemTimeWide();
 atlas_build(0);spectrum_init();
 gfx_init();checkpoint("gpu ready",0);
 draw();checkpoint("first screen complete",0);
 const char *setting_slash=strrchr(startup_path,'/');
 int prefix=setting_slash?(int)(setting_slash-startup_path+1):0;
 snprintf(eq_user_path,sizeof(eq_user_path),"%.*szoid-eq-presets.txt",prefix,startup_path);eq_user_load();
 snprintf(settings_path,sizeof(settings_path),"%.*szoid-settings.txt",prefix,startup_path);load_settings();
 snprintf(resume_file,sizeof(resume_file),"%.*szoid-resume.txt",prefix,startup_path);
 snprintf(video_log_path,sizeof(video_log_path),"%.*svideo.log",prefix,startup_path);
 radio_ui_init(startup_path,prefix);checkpoint("radio ready",saved_n); /* radio.txt, the zoid_net thread */
 books_init(startup_path,prefix); /* zoid-books.txt: where you are in each book */
 {char vp[PATHLEN];snprintf(vp,sizeof(vp),"%.*szoidvol.prx",prefix,startup_path);volume_init(vp);}
 sceCtrlSetSamplingCycle(0);sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
 if(argc>0 && argv[0] && strncmp(argv[0],"ef0:",4)==0){strcpy(root,"ef0:/MUSIC");strcpy(video_root,"ef0:/VIDEO");strcpy(pod_root,"ef0:/PODCASTS");strcpy(books_root,"ef0:/BOOKS");}
 pod_ui_init();checkpoint("podcasts ready",pod_sub_n); /* subscriptions, the zoid_pod and zoid_dl threads */
 strcpy(folder,root);checkpoint("folder scan begin",0);browse();checkpoint("folder scan complete",count);
 queue_lock=sceKernelCreateSema("zoid_queue",0,1,1,NULL);
 if(queue_lock<0){sceKernelExitGame();return 1;}
 command_lock=sceKernelCreateSema("zoid_command",0,1,1,NULL);
 if(command_lock<0){sceKernelExitGame();return 1;}
 checkpoint("command semaphore",command_lock);
 int cb=sceKernelCreateThread("zoid_callbacks",callbacks,0x11,0x1000,0,NULL);if(cb>=0)sceKernelStartThread(cb,0,NULL);
 SceUID thread=sceKernelCreateThread("zoid_audio",audio,0x18,0x10000,0,NULL);
 if(thread>=0)sceKernelStartThread(thread,0,NULL);else fail(14,thread);
 checkpoint("audio thread created",thread);
 SceUID meta=sceKernelCreateThread("zoid_meta",meta_thread,0x30,0x10000,0,NULL);if(meta>=0)sceKernelStartThread(meta,0,NULL);
 checkpoint("metadata thread created",meta);
 unsigned int prior=0;int held=0;
#ifdef ZOID_SMOKE_TEST
 int test_frame=0,test_value=0;(void)test_value;
 theme=0;visual_count=1;visuals[0]=4;visuals[1]=0;visuals[2]=2;seek_step_index=1;shuffle=repeat_mode=0;set_mix(0);fade_seconds=6;
 bindings[0]=0;bindings[1]=1;bindings[2]=2;bindings[3]=6;cpu_fast=1;
 int report_fd=sceIoOpen("ms0:/smoke.txt",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(report_fd>=0)sceIoClose(report_fd);
 {playlist_scan();char d[PATHLEN];playlist_dir(d,sizeof(d));for(int i=0;i<pl_count;i++){char f[PATHLEN];snprintf(f,sizeof(f),"%s/%s",d,pl_names[i]);sceIoRemove(f);}sceIoRmdir(d);browse();} /* no playlists left from an earlier run */
#endif
 themes_apply();fonts_build(theme);set_clock(cpu_fast);
#ifndef ZOID_SMOKE_TEST
 /* Resume: reopen the last song at its position, paused on Now Playing; X or START plays. */
 if(resume_on){char rp[PATHLEN];int rs;
  if(load_resume(rp,sizeof(rp),&rs) && !strncmp(rp,root,strlen(root))) {
   for(int w=0;w<300 && !audio_ready;w++)sceKernelDelayThread(10000);
   char dir[PATHLEN];snprintf(dir,sizeof(dir),"%s",rp);char *sl=strrchr(dir,'/');
   if(sl && audio_ready) {
    *sl=0;snprintf(folder,sizeof(folder),"%s",dir);browse();int found=-1;
    for(int i=0;i<count;i++)if(!entries[i].folder && !strcmp(entries[i].name,sl+1))found=i;
    if(found>=0){cursor=found;next_request_seek=rs;queue_start(found);paused=1;screen=SCR_NOW;np_focus=2;checkpoint("resumed at",rs);}
    else{strcpy(folder,root);browse();}
   }
  }
 }
#endif
 while(running) {
  frame_start_us=sceKernelGetSystemTimeWide();now_ms=frame_start_us/1000;
  {static unsigned long long fps_t;static int fps_n;fps_n++;if(now_ms-fps_t>=1000){fps=fps_n*1000.0f/(now_ms-fps_t);fps_n=0;fps_t=now_ms;}}
#ifdef ZOID_SMOKE_TEST
  test_frame++;
#ifdef ZOID_TRANSPORT_TEST
  if(test_frame==20){check("ZOID starts on the Home screen, on Music",screen==SCR_HOME && home_sel==HOME_MUSIC);capture("ms0:/home-zoid.rgba");}
  if(test_frame==25){input(PSP_CTRL_CROSS);check("X on the Music tile opens MUSIC",screen==SCR_LIBRARY && !strcmp(folder,root) && !in_radio && !in_pod);
   int v=0,r=0;for(int i=0;i<count;i++){if(entries[i].folder==3)v=1;if(entries[i].folder==5)r=1;}check("MUSIC no longer lists Videos or Internet Radio (they are on Home)",!v && !r);}
  if(test_frame==30){test_open_folders();check("library opens folders",count>0 && screen==SCR_LIBRARY);}
  if(test_frame==40)capture("ms0:/library.rgba");
  if(test_frame==46){check("song info shows Loading, not Unknown, while it is read",!np_loading || (strstr(np_artist,"Loading") && strstr(np_album,"Loading")));capture("ms0:/now-loading.rgba");}
  if(test_frame==45){cursor=0;input(PSP_CTRL_CROSS);np_refresh();check("X on a song opens full-screen Now Playing",screen==SCR_NOW && queue_count>0);}
  if(test_frame==90){check("song plays",state==2 && !error_code);capture("ms0:/now-zoid.rgba");}
  if(test_frame==95){input(PSP_CTRL_CIRCLE);check("Circle backs out to the file list",screen==SCR_LIBRARY && state==2);}
  if(test_frame==100)capture("ms0:/library-playing.rgba");
  if(test_frame==105){input(PSP_CTRL_TRIANGLE);check("Triangle returns to Now Playing",screen==SCR_NOW);input(PSP_CTRL_CIRCLE);test_value=serial;cursor=0;input(PSP_CTRL_CROSS);check("X on the playing song reopens it without restarting",screen==SCR_NOW && serial==(unsigned)test_value);}
  if(test_frame==110){np_focus=2;input(PSP_CTRL_LEFT);check("Left moves focus to previous",np_focus==1);input(PSP_CTRL_RIGHT);input(PSP_CTRL_RIGHT);check("Right moves focus to next",np_focus==3);
   input(PSP_CTRL_LEFT);input(PSP_CTRL_CROSS);check("X on play pauses",paused);input(PSP_CTRL_CROSS);check("X again resumes",!paused);}
  if(test_frame==120){input(PSP_CTRL_UP);check("Up from the controls reaches the seek bar",np_focus==5);test_value=elapsed;input(PSP_CTRL_RIGHT);check("Right on seek bar seeks immediately",pending_seek>=test_value+4);}
  if(test_frame==150){check("seek keeps playing",state==2 && elapsed>=5 && !error_code);capture("ms0:/now-spectrum.rgba");}
  if(test_frame==129){char m[96];snprintf(m,sizeof(m),"seek bar: was %d, now %d, preview %d, duration %d",test_value,elapsed,seek_preview,duration);report(m);
   check("the seek bar's jump sticks: the song plays on from there",state==2 && !error_code && elapsed>=test_value+4);}
  if(test_frame==130)seek_to(150);
  if(test_frame==155){input(PSP_CTRL_UP);check("Up from the seek bar reaches the visualizer in Zoid",np_focus==6);input(PSP_CTRL_CROSS);check("X steps to the next effect",visuals[0]==10 && !chooser);input(PSP_CTRL_TRIANGLE);check("Triangle opens the visualizer list",chooser && screen==SCR_NOW);input(PSP_CTRL_DOWN);}
  if(test_frame==158)capture("ms0:/chooser.rgba");
  if(test_frame==159){input(PSP_CTRL_CROSS);check("chooser sets the next effect",!chooser && visuals[0]==11);}
  if(test_frame==160)capture("ms0:/now-toast.rgba");
  if(test_frame==165){input(PSP_CTRL_DOWN);check("Down from a visualizer reaches the seek bar",np_focus==5);input(PSP_CTRL_DOWN);check("Down returns to play",np_focus==2);np_focus=3;input(PSP_CTRL_CROSS);}
  if(test_frame==190)check("next skips track",queue_pos==1 && state==2 && !error_code);
  if(test_frame==195){input(PSP_CTRL_SELECT);check("Select opens Settings",screen==SCR_SETTINGS);}
  if(test_frame==200)capture("ms0:/settings.rgba");
  if(test_frame==205){set_item=0;input(PSP_CTRL_RIGHT);check("theme changes to Cyberpunk",theme==1);input(PSP_CTRL_CIRCLE);check("Circle closes Settings",screen==SCR_NOW);}
  if(test_frame==220)capture("ms0:/now-cyberpunk.rgba");
  if(test_frame==221)screen=SCR_LIBRARY;
  if(test_frame==222)capture("ms0:/library-cyberpunk.rgba");
  if(test_frame==223){screen=SCR_SETTINGS;return_screen=SCR_NOW;}
  if(test_frame==224){capture("ms0:/settings-cyberpunk.rgba");}
  if(test_frame==225)screen=SCR_NOW;
  if(test_frame==226){input(PSP_CTRL_SELECT);set_item=0;input(PSP_CTRL_RIGHT);set_item=set_row(1);input(PSP_CTRL_RIGHT);input(PSP_CTRL_RIGHT);check("three visualizer panels",theme==2 && visual_count==3);visuals[1]=15;visuals[2]=18;input(PSP_CTRL_CIRCLE);np_focus=4;input(PSP_CTRL_RIGHT);check("the mixing key sits right of repeat",np_focus==9);test_value=mix_mode;input(PSP_CTRL_CROSS);check("X on the mixing key cycles the mode",mix_mode==(test_value+1)%4);input(PSP_CTRL_CROSS);input(PSP_CTRL_CROSS);input(PSP_CTRL_CROSS);check("four presses return to the start",mix_mode==test_value);input(PSP_CTRL_UP);check("Up from the controls reaches the seek bar",np_focus==5);input(PSP_CTRL_DOWN);input(PSP_CTRL_DOWN);check("Down from the controls reaches the middle panel",np_focus==7);input(PSP_CTRL_UP);check("Up from a panel returns to play",np_focus==2);}
  if(test_frame==240)capture("ms0:/now-phosphor.rgba");
  if(test_frame==241)screen=SCR_LIBRARY;
  if(test_frame==242)capture("ms0:/library-phosphor.rgba");
  if(test_frame==243)screen=SCR_NOW;
  if(test_frame==245){input(PSP_CTRL_SELECT);set_item=0;input(PSP_CTRL_RIGHT);check("Y2K follows Phosphor",theme==3);input(PSP_CTRL_RIGHT);check("then Studio",theme==4);input(PSP_CTRL_RIGHT);check("then Paper",theme==5);input(PSP_CTRL_RIGHT);check("then Zoidstation 1",theme==6);input(PSP_CTRL_RIGHT);check("then Zoidstation 2",theme==7);input(PSP_CTRL_RIGHT);check("then Zoidstation Portable",theme==8);input(PSP_CTRL_RIGHT);check("and round to Zoid",theme==0);set_item=set_row(1);input(PSP_CTRL_RIGHT);check("visualizers can be turned off",theme==0 && visual_count==0);input(PSP_CTRL_CIRCLE);}
  if(test_frame==260)capture("ms0:/now-novis.rgba");
  if(test_frame==265){theme=1;visual_count=2;load_settings();check("settings persist",theme==0 && visual_count==0 && visuals[0]==11);T=&themes[theme];fonts_build(theme);}
  if(test_frame==270){visual_count=1;visuals[0]=4;save_settings();bound_action(3);}
  if(test_frame==300)check("stop resets clock",state==0 && elapsed==0 && !error_code);
  if(test_frame==305)input(PSP_CTRL_START);
  if(test_frame==330){check("START plays again",state==2 && !error_code);seek_to(150);theme=1;T=&themes[1];fonts_build(1);visual_count=3;screen=SCR_NOW;np_focus=2;}
  /* Y2K and Studio screens, mid-song */
  if(test_frame==340){theme=3;T=&themes[3];fonts_build(3);screen=SCR_NOW;}
  if(test_frame==344)capture("ms0:/now-y2k.rgba");
  if(test_frame==346)screen=SCR_LIBRARY;
  if(test_frame==348)capture("ms0:/library-y2k.rgba");
  if(test_frame==350){theme=4;T=&themes[4];fonts_build(4);screen=SCR_NOW;}
  if(test_frame==352)capture("ms0:/now-studio.rgba");
  if(test_frame==354)screen=SCR_LIBRARY;
  if(test_frame==356)capture("ms0:/library-studio.rgba");
  if(test_frame==357){theme=5;T=&themes[5];fonts_build(5);screen=SCR_NOW;}
  if(test_frame==358)capture("ms0:/now-paper-playing.rgba");
  if(test_frame==359){theme=1;T=&themes[1];fonts_build(1);screen=SCR_NOW;}
  /* gallery: every visualizer, three at a time, mid-song */
  if(test_frame>=360 && test_frame<414 && (test_frame-360)%6==0){int g=(test_frame-360)/6;for(int p=0;p<3;p++)visuals[p]=visual_order[(g*3+p)%VIS_COUNT];}
  if(test_frame>=360 && test_frame<414 && (test_frame-360)%6==5){char f[40];snprintf(f,sizeof(f),"ms0:/gallery-%d.rgba",(test_frame-360)/6);capture(f);}
  if(test_frame==420){test_value=visual_tick;screen_off=1;}
  if(test_frame==430){check("screen off stops drawing",visual_tick==(unsigned)test_value);screen_off=0;}
  if(test_frame==435)check("screen on draws again",visual_tick>(unsigned)test_value);
  if(test_frame==436){char p[PATHLEN];int s=-1;save_resume();check("resume point saved and read back",load_resume(p,sizeof(p),&s) && !strcmp(p,active) && s>=elapsed-1 && s<=elapsed+1);}
  /* whole folders and playlists (memory stick only; leaves no playlists behind) */
  if(test_frame==437){strcpy(folder,root);browse();int q=-1;for(int i=0;i<count;i++)if(!strcmp(entries[i].name,"Queue Test"))q=i;cursor=q;
   screen=SCR_LIBRARY;open_item_menu();check("hold-X menu on a folder offers Play all",menu_open && menu_act[0]==A_PLAY_ALL && menu_act[1]==A_SHUFFLE_ALL);
   snprintf(menu_target,sizeof(menu_target),"%s",root);menu_target_kind=1;stage_target();check("a whole folder includes its subfolders (MP3, FLAC, WAV, M4A and MP4)",picked.n==9);
   open_item_menu();menu_run(A_PLAY_ALL,0);check("Play all queues the folder",queue_count==3 && screen==SCR_NOW);}
  /* a folder with more than the old 512-row limit: every song is listed and gathered (empty files, never played) */
  if(test_frame==438){char d[PATHLEN];snprintf(d,sizeof(d),"%s/ZZ Big Folder",root);sceIoMkdir(d,0777);
   for(int i=0;i<600;i++){char f[PATHLEN];snprintf(f,sizeof(f),"%s/Song %03d.mp3",d,i);int fd=sceIoOpen(f,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(fd>=0)sceIoClose(fd);}
   snprintf(folder,sizeof(folder),"%s",d);browse();check("a folder of 600 songs lists all 600",count==600 && !entries_hidden() && !strcmp(entries[599].name,"Song 599.mp3"));
   snprintf(menu_target,sizeof(menu_target),"%s",d);menu_target_kind=1;stage_target();check("all 600 are gathered for Play all",picked.n==600);
   remove_tree(d,0);strcpy(folder,root);browse();}
  /* Shuffle all songs: a row at the top of MUSIC (after Playlists) that shuffles every song in every folder */
  if(test_frame==440){strcpy(folder,root);browse();int s=-1;for(int i=0;i<count;i++)if(entries[i].folder==4)s=i;
   check("MUSIC has a Shuffle all songs row near the top",s>=0 && s<=1 && !strcmp(entries[s].name,SHUFFLE_ALL_NAME));
   cursor=s<0?0:s;screen=SCR_LIBRARY;open_item_menu();check("hold-X on it offers Shuffle all, never Delete",menu_open && menu_act[0]==A_SHUFFLE_ALL && menu_act[menu_n-2]!=A_PL_DELETE);menu_open=0;
   lib_activate();check("Shuffle all songs queues every song in MUSIC, shuffled",queue_count==9 && shuffle && screen==SCR_NOW);
   int vids=0;for(int i=0;i<queue_count;i++)if(!strncmp(paths_get(&queue_paths,i),video_root,strlen(video_root)))vids++;check("and no videos",!vids);
   check("Shuffle all turns shuffle on for that queue only (never saved on)",shuffle && shuffle_oneoff);
   shuffle=0;shuffle_oneoff=0;}
  if(test_frame==442){strcpy(folder,root);browse();int q=-1;for(int i=0;i<count;i++)if(!strcmp(entries[i].name,"Queue Test"))q=i;cursor=q;screen=SCR_LIBRARY;
   open_item_menu();stage_target();menu_open=0;playlist_create("Road Trip");
   char f[PATHLEN];snprintf(f,sizeof(f),"%s/%s/Road Trip.m3u",root,PLAYLIST_DIR);check("a new playlist is saved",file_exists(f) && playlist_read(f,&picked)==3);
   stage_target();check("adding the same songs again adds none",playlist_add_stage(f)==0);}
  if(test_frame==446){strcpy(folder,root);browse();check("Playlists is first in the library",count && !strcmp(entries[0].name,PLAYLIST_DIR));cursor=0;lib_activate();
   check("the playlist is listed",count==1 && entries[0].folder==2);lib_activate();check("opening a playlist lists its songs",in_playlist && count==3);}
  if(test_frame==448)capture("ms0:/library-playlist.rgba");
  if(test_frame==450){cursor=1;open_item_menu();check("songs in a playlist can be removed",menu_act[2]==A_REMOVE);}
  if(test_frame==452)capture("ms0:/menu.rgba");
  if(test_frame==454){menu_run(A_REMOVE,2);check("removing leaves two songs",count==2 && in_playlist);}
  if(test_frame==456){parent();check("Circle backs out to the playlists",!in_playlist && count==1);open_item_menu();menu_run(A_PL_DELETE_YES,0);check("a playlist can be deleted",count==0);
   char d[PATHLEN];playlist_dir(d,sizeof(d));sceIoRmdir(d);strcpy(folder,root);browse();}
  /* equalizer page: opens from Settings, faders move, presets apply, drawn in every skin */
  if(test_frame==458){theme=0;T=&themes[0];fonts_build(0);screen=SCR_NOW;open_settings();set_item=set_row(18);input(PSP_CTRL_CROSS);check("Settings opens the equalizer",screen==SCR_EQ);
   eq_apply_preset(4);check("Rock preset applies",eq_gain[0]==5 && eq_preset==4);eq_focus=1;eq_input(PSP_CTRL_UP);check("Up raises a band and makes it Custom",eq_gain[0]==6 && eq_preset==-1);
   eq_focus=11;eq_input(PSP_CTRL_DOWN);eq_input(PSP_CTRL_DOWN);eq_input(PSP_CTRL_DOWN);eq_input(PSP_CTRL_DOWN);eq_input(PSP_CTRL_CROSS);check("auto preamp follows the highest band",eq_auto_pre && eq_pre==-6);eq_focus=4;}
  if(test_frame==459)capture("ms0:/eq-zoid.rgba");
  if(test_frame>=460 && test_frame<468 && !(test_frame&1)){int n=(test_frame-458)/2;theme=n;T=&themes[n];fonts_build(n);}
  if(test_frame>=461 && test_frame<468 && (test_frame&1)){char f[40];snprintf(f,sizeof(f),"ms0:/eq-%d.rgba",theme);capture(f);}
  if(test_frame==468){save_settings();eq_apply_preset(0);eq_on=0;load_settings();check("equalizer settings persist",eq_on && eq_auto_pre && eq_gain[0]==6 && eq_preset==-1);
   input(PSP_CTRL_CIRCLE);check("Circle returns to Settings",screen==SCR_SETTINGS);input(PSP_CTRL_CIRCLE);eq_apply_preset(0);eq_auto_pre=0;eq_pre=0;eq_changed();save_settings();theme=0;T=&themes[0];fonts_build(0);}
  /* saved EQ presets */
if(test_frame==469){time_left=1;hold_idx=2;scroll_idx=2;auto_off_idx=3;save_settings();time_left=0;hold_idx=1;scroll_idx=1;auto_off_idx=0;load_settings();   check("new settings persist",time_left==1 && hold_idx==2 && scroll_idx==2 && auto_off_idx==3 && marquee_ms==18);   screen=SCR_SETTINGS;set_item=set_row(20);input(PSP_CTRL_RIGHT);check("sleep timer sets",sleep_idx==1 && sleep_until>now_ms);input(PSP_CTRL_LEFT);check("and turns off",!sleep_idx && !sleep_until);   time_left=0;hold_idx=1;scroll_idx=1;marquee_ms=28;auto_off_idx=0;save_settings();screen=SCR_EQ;}
  if(test_frame==470){eq_apply_preset(1);check("a preset can be saved",eq_user_save("Test Preset") && eq_preset==EQ_PRESET_N+eq_user_n-1);int k=eq_user_n-1;eq_apply_preset(0);eq_user_load();check("saved presets are read back",eq_user_n>k && eq_user[k].gain[0]==7);
   screen=SCR_EQ;eq_focus=12;eq_input(PSP_CTRL_CROSS);check("Presets lists saved presets",menu_open && menu_act[EQ_PRESET_N+k]==A_EQ_PICK);menu_run(A_EQ_PICK,EQ_PRESET_N+k);check("a saved preset applies",eq_gain[0]==7 && eq_preset==EQ_PRESET_N+k);
   eq_presets_menu();menu_run(A_EQ_DELMENU,0);menu_run(A_EQ_DEL,k);check("deleting a preset warns first",menu_open && menu_warn && menu_act[0]==A_CANCEL);}
  if(test_frame==472)capture("ms0:/eq-delete-zoid.rgba");
  if(test_frame==474){int k=eq_user_n;menu_run(A_EQ_DEL_YES,1);check("the preset is deleted",eq_user_n==k-1 && eq_preset==-1);eq_apply_preset(0);eq_touch();eq_save_now();screen=SCR_LIBRARY;}
  /* deleting from the library: Delete, then a second "cannot be undone" popup */
  if(test_frame==476){strcpy(folder,root);char d[PATHLEN];snprintf(d,sizeof(d),"%s/ZZ Delete Test",root);sceIoMkdir(d,0777);char f[PATHLEN];snprintf(f,sizeof(f),"%s/gone.mp3",d);int fd=sceIoOpen(f,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(fd>=0){sceIoWrite(fd,"ID3",3);sceIoClose(fd);}
   browse();int q=-1;for(int i=0;i<count;i++)if(!strcmp(entries[i].name,"ZZ Delete Test"))q=i;cursor=q;open_item_menu();check("hold-X on a folder offers Delete",menu_open && menu_act[menu_n-2]==A_PL_DELETE);
   menu_run(A_PL_DELETE,0);check("Delete asks first",menu_open && menu_act[0]==A_DEL_CONFIRM && !menu_warn);menu_run(A_DEL_CONFIRM,0);check("then warns it cannot be undone, Cancel first",menu_open && menu_warn && menu_act[0]==A_CANCEL);theme=1;T=&themes[1];fonts_build(1);}
  if(test_frame==478)capture("ms0:/delete-warn-cyberpunk.rgba");
  if(test_frame==479){theme=0;T=&themes[0];fonts_build(0);}
  if(test_frame==480)capture("ms0:/delete-warn-zoid.rgba");
  if(test_frame==482){menu_run(A_PL_DELETE_YES,1);char d[PATHLEN];snprintf(d,sizeof(d),"%s/ZZ Delete Test",root);check("the folder and its songs are deleted",!file_exists(d));int q=-1;for(int i=0;i<count;i++)if(!strcmp(entries[i].name,"ZZ Delete Test"))q=i;check("the library no longer lists it",q<0);}
  /* deleted songs leave the queue; the EQ limiter keeps boosted peaks below hard clipping */
  /* sorting: a two-disc album in one folder, named so file-name order mixes the discs (like a real compilation) */
  if(test_frame==483){char d[PATHLEN];snprintf(d,sizeof(d),"%s/ZZ Sort Test",root);sceIoMkdir(d,0777);
   const char *files[5]={"01 Alpha.mp3","01 Bravo.mp3","02 Charlie.mp3","02 Delta.mp3","Zulu.mp3"},*titles[5]={"Alpha","Bravo","Charlie","Delta",""};
   const int tracks[5]={1,1,2,2,0},discs[5]={2,1,1,2,0};
   for(int i=0;i<5;i++){char f[PATHLEN];snprintf(f,sizeof(f),"%s/%s",d,files[i]);test_tagged(f,titles[i],tracks[i]?"Sort Album":"",tracks[i],discs[i]);}
   int ms=music_sort;music_sort=SORT_ALBUM;strcpy(folder,d);browse();
   const char *want[5]={"01 Bravo.mp3","02 Charlie.mp3","01 Alpha.mp3","02 Delta.mp3","Zulu.mp3"};
   int ok=count==5;for(int i=0;i<5 && ok;i++)ok=!strcmp(entries[i].name,want[i]);
   check("album order: disc 1, then disc 2, each by track; untagged songs last",ok);
   check("the rows show the tag titles at once",entries[0].tagged && !strcmp(entries[0].title,"Bravo"));
   snprintf(menu_target,sizeof(menu_target),"%s",d);menu_target_kind=1;stage_target();
   ok=picked.n==5;for(int i=0;i<5 && ok;i++)ok=!strcmp(base_of(paths_get(&picked,i)),want[i]);check("Play all on the folder gathers it in the same order",ok);
   shuffle=1;shuffle_oneoff=1;queue_start(0);
   check("picking a song after Shuffle all plays the album in order",!shuffle && queue_count==5 && queue_order[queue_pos]==0 && !strcmp(base_of(paths_get(&queue_paths,queue_order[1])),"02 Charlie.mp3"));
   cursor=0;screen=SCR_LIBRARY;open_item_menu();int si=-1;for(int i=0;i<menu_n;i++)if(menu_act[i]==A_SORT)si=i;
   check("hold-X on a song offers Sort, above Delete",si>=0 && menu_act[si+1]==A_PL_DELETE);
   menu_run(A_SORT,si);check("the Sort menu opens on the current order",menu_open && menu_act[menu_sel]==A_SORT_PICK && sort_menu_mode[menu_sel]==SORT_ALBUM);
   int fi=-1;for(int i=0;i<menu_n;i++)if(menu_act[i]==A_SORT_PICK && sort_menu_mode[i]==SORT_FILE)fi=i;menu_run(A_SORT_PICK,fi);
   check("File name order puts 01 Alpha first; the cursor stays on the same song",music_sort==SORT_FILE && !strcmp(entries[0].name,"01 Alpha.mp3") && !strcmp(entries[cursor].name,"01 Bravo.mp3"));
   check("the queue playing from the folder takes the new order, same song playing",queue_count==5 && queue_pos==1 && !strcmp(base_of(paths_get(&queue_paths,queue_order[queue_pos])),"01 Bravo.mp3") && !strcmp(base_of(paths_get(&queue_paths,queue_order[2])),"02 Charlie.mp3"));
   music_sort=SORT_ALBUM;video_sort=SORT_NEW;load_settings();check("the sort orders are saved",music_sort==SORT_FILE && video_sort==SORT_FILE);
   music_sort=SORT_NAME_ZA;browse();check("Name Z-A",!strcmp(entries[0].name,"Zulu.mp3") && !strcmp(entries[1].name,"02 Delta.mp3"));
   music_sort=ms;save_settings();remove_tree(d,0);strcpy(folder,root);browse();}
  if(test_frame==484){request(NULL);sceKernelWaitSema(queue_lock,1,NULL);paths_clear(&queue_paths);shuffle=0;for(int i=0;i<4;i++){char p[PATHLEN];snprintf(p,PATHLEN,"%s/%s/s%d.mp3",root,i<2?"A":"B",i);paths_add(&queue_paths,p);queue_order[i]=i;}queue_count=queue_paths.n;queue_pos=2;sceKernelSignalSema(queue_lock,1);
   char a[PATHLEN];snprintf(a,sizeof(a),"%s/A",root);queue_forget(a,1);check("deleting a folder drops its songs from the queue",queue_count==2 && queue_pos==0 && strstr(paths_get(&queue_paths,queue_order[0]),"s2.mp3"));
   snprintf(a,sizeof(a),"%s/B/s3.mp3",root);queue_forget(a,0);check("deleting a song drops it from the queue",queue_count==1);
   static short buf[1024*2];for(int i=0;i<1024;i++)buf[i*2]=buf[i*2+1]=(short)(30000*sinf(i*0.02f));eq_on=1;eq_auto_pre=0;eq_pre=0;eq_apply_preset(1);eq_process(buf,1024,44100);
   int clipped=0,peak=0;for(int i=0;i<2048;i++){int v=abs(buf[i]);if(v>=32767)clipped++;if(v>peak)peak=v;}check("boosted peaks are limited, not clipped",clipped==0 && peak>28000);eq_apply_preset(0);queue_count=0;}
  /* lyrics: a synced .lrc beside the song, opened from the hold-X menu, drawn in every skin */
  if(test_frame==488){char d[PATHLEN];snprintf(d,sizeof(d),"%s/Queue Test/02 - Queue Test.lrc",root);int fd=sceIoOpen(d,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);
   const char *lrc="[ar:Test]\n[ti:Queue Test]\n[00:00.00]Counting the seconds in the dark\n[00:01.00]A little light on the memory stick\n[00:02.00]Every line arrives right on time\n"
    "[00:03.00]This one is long enough that it has to wrap onto a second row across the screen\n[00:04.00]Still going\n[00:05.00][00:06.00]Same words twice\n[00:07.00]The end\n";
   if(fd>=0){sceIoWrite(fd,lrc,strlen(lrc));sceIoClose(fd);}
   snprintf(folder,sizeof(folder),"%s/Queue Test",root);browse();screen=SCR_LIBRARY;cursor=1;lib_activate();}
  if(test_frame==560){check("synced lyrics load from a .lrc",lyr_state==2 && lyr.synced && lyr.n==8 && lyr.line[6].ms==6000);
   screen=SCR_NOW;open_item_menu();check("Now Playing hold-X menu offers Lyrics",menu_open && menu_act[0]==A_LYRICS);menu_run(A_LYRICS,0);check("Lyrics opens full screen",screen==SCR_LYRICS);
   check("the current line follows the song",lyrics_current()>=1);}
  if(test_frame==564)capture("ms0:/lyrics-zoid.rgba");
  if(test_frame>=566 && test_frame<574 && !(test_frame&1)){int t=(test_frame-564)/2;theme=t;T=&themes[t];fonts_build(t);}
  if(test_frame>=567 && test_frame<575 && (test_frame&1)){char f[40];snprintf(f,sizeof(f),"ms0:/lyrics-%d.rgba",theme);capture(f);}
  if(test_frame==576){static Lyrics p;lyrics_parse("First\n\nSecond\nThird\n\n",&p);check("plain lyrics keep inner blank lines, trim the ends",!p.synced && p.n==4);
   theme=0;T=&themes[0];fonts_build(0);input(PSP_CTRL_CIRCLE);check("Circle returns to Now Playing",screen==SCR_NOW);
   char d[PATHLEN];snprintf(d,sizeof(d),"%s/Queue Test/02 - Queue Test.lrc",root);sceIoRemove(d);request(NULL);}
  /* names in other scripts: kept as UTF-8 from the tags, drawn from the PSP's own fonts */
  if(test_frame==578){
   {const unsigned char u16[]={1,0xFF,0xFE,0x87,0x5B,0x1A,0x59,0x30,0x75};char out[32];tag_text(u16,sizeof(u16),out,sizeof(out));check("UTF-16 tags keep Japanese letters",!strcmp(out,"\xE5\xAE\x87\xE5\xA4\x9A\xE7\x94\xB0"));}
   {const unsigned char l1[]={0,'B','j',0xF6,'r','k'};char out[32];tag_text(l1,sizeof(l1),out,sizeof(out));check("Latin-1 tags keep their accents",!strcmp(out,"Bj\xC3\xB6rk"));}
   {char out[8];const unsigned char u8[]={3,'a',0xE5,0xAE,0x87,0xE5,0xA4,0x9A};tag_text(u8,sizeof(u8),out,6);check("a full tag field never ends in half a letter",!strcmp(out,"a\xE5\xAE\x87"));}
   check("the PSP fonts have Japanese, Korean, Cyrillic and accented letters",sys_glyph(0x5B87) && sys_glyph(0xBC29) && sys_glyph(0x41C) && sys_glyph(0xF6));
   check("Japanese text takes room on screen",text_w("\xE5\xAE\x87\xE5\xA4\x9A\xE7\x94\xB0",F_MEDIUM)>30);
   char d[PATHLEN];snprintf(d,sizeof(d),"%s/ZZ World Music",root);sceIoMkdir(d,0777);
   static const char *names[]={"宇多田ヒカル - First Love.mp3","방탄소년단 - 봄날.mp3","Мумий Тролль - Владивосток 2000.mp3","Björk - Jóga.mp3","Sigur Rós - Hoppípolla.mp3","Ελληνικά - Τραγούδι.mp3","坂本龍一 - 戦場のメリークリスマス.mp3","Plain English.mp3","Łódź - Zażółć gęślą jaźń.mp3"};
   for(int i=0;i<9;i++){char f[PATHLEN];snprintf(f,sizeof(f),"%s/%s",d,names[i]);int fd=sceIoOpen(f,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(fd>=0)sceIoClose(fd);}
   snprintf(folder,sizeof(folder),"%s",d);browse();screen=SCR_LIBRARY;cursor=1;
   check("file names in other scripts are listed",count==9);}
  if(test_frame==581)capture("ms0:/library-world-zoid.rgba");
  if(test_frame==582){theme=1;T=&themes[1];fonts_build(1);}
  if(test_frame==584)capture("ms0:/library-world-cyberpunk.rgba");
  if(test_frame==585){theme=0;T=&themes[0];fonts_build(0);char d[PATHLEN];snprintf(d,sizeof(d),"%s/ZZ World Music",root);remove_tree(d,0);strcpy(folder,root);browse();}
  /* Home: Circle ends there, the tiles open their sections and come back, drawn in every skin */
  if(test_frame==586){strcpy(folder,root);browse();screen=SCR_LIBRARY;input(PSP_CTRL_CIRCLE);check("Circle at the top of MUSIC goes to Home, on Music",screen==SCR_HOME && home_sel==HOME_MUSIC);
   input(PSP_CTRL_RIGHT);input(PSP_CTRL_RIGHT);check("Right moves along the tiles",home_sel==HOME_PODCASTS);input(PSP_CTRL_DOWN);check("Down reaches Settings",home_sel==HOME_SETTINGS);
   input(PSP_CTRL_UP);check("Up returns to the tile above",home_sel==HOME_PODCASTS);input(PSP_CTRL_DOWN);input(PSP_CTRL_CROSS);check("X on Settings opens Settings",screen==SCR_SETTINGS);
   input(PSP_CTRL_CIRCLE);check("Circle closes Settings back to Home",screen==SCR_HOME);
   home_sel=HOME_VIDEOS;input(PSP_CTRL_CROSS);check("the Videos tile opens VIDEO",screen==SCR_LIBRARY && in_video_folder());input(PSP_CTRL_CIRCLE);check("Circle at the top of VIDEO goes Home, on Videos",screen==SCR_HOME && home_sel==HOME_VIDEOS);
   home_sel=HOME_RADIO;input(PSP_CTRL_CROSS);check("the Internet Radio tile opens Internet Radio",screen==SCR_LIBRARY && in_radio);input(PSP_CTRL_CIRCLE);check("Circle there goes Home, on Internet Radio",screen==SCR_HOME && home_sel==HOME_RADIO);
   home_sel=HOME_PODCASTS;input(PSP_CTRL_CROSS);check("the Podcasts tile opens Podcasts: Search, Top, Categories",screen==SCR_LIBRARY && in_pod && count>=3 && entries[0].folder==20 && entries[1].folder==21 && entries[2].folder==22);
   input(PSP_CTRL_CIRCLE);check("Circle there goes Home, on Podcasts",screen==SCR_HOME && home_sel==HOME_PODCASTS);home_sel=HOME_MUSIC;}
  if(test_frame>=588 && test_frame<598 && !(test_frame&1)){int t=(test_frame-588)/2;theme=t;T=&themes[t];fonts_build(t);home_sel=t%4;screen=SCR_HOME;}
  if(test_frame>=589 && test_frame<599 && (test_frame&1)){char f[40];snprintf(f,sizeof(f),"ms0:/home-%d.rgba",theme);capture(f);}
  /* Volume boost: quiet sound is raised toward the target, loud sound is left alone, nothing clips, music is untouched */
  if(test_frame==599) {
   static short b[1024*2];int keep=boost_src,kb=volume_boost;
   #define BOOST_RUN(amp,blocks) do{for(int k=0;k<(blocks);k++){for(int i=0;i<1024;i++){short v=(short)((amp)*sinf((k*1024+i)*0.06f));b[i*2]=b[i*2+1]=v;}boost_process(b,1024);}}while(0)
   #define PEAK(p) do{p=0;for(int i=0;i<2048;i++){int a=abs(b[i]);if(a>p)p=a;}}while(0)
   int p;
   boost_src=1;
   /* a quiet -30 dB sine at each level: Low +12, Medium +18, High +22, Max +26 dB, each clearly louder */
   {static const int lo[BOOST_LEVELS]={0,3700,7400,11800,18800},hi[BOOST_LEVELS]={0,4100,8000,12800,20200};int prev=1000,steps=1;
    for(int lv=1;lv<BOOST_LEVELS;lv++){volume_boost=lv;boost_reset();BOOST_RUN(1000,300);PEAK(p);
     char m[96];snprintf(m,sizeof(m),"boost level %d: quiet sine 1000 -> peak %d, gain %.2f",lv,p,boost_gain);report(m);
     if(p<lo[lv] || p>hi[lv] || p<prev*1.4f)steps=0;prev=p;}
    check("each Volume boost level raises quiet sound clearly more than the last (+12 to +26 dB)",steps);}
   /* a sudden loud burst while High has quiet sound raised +22 dB: the limiter holds every sample under full scale */
   {volume_boost=3;boost_reset();BOOST_RUN(1000,300);int worst=0,clipped=0;
    for(int k=0;k<20;k++){for(int i=0;i<1024;i++){short v=(short)(20000*sinf(i*0.06f));b[i*2]=b[i*2+1]=v;}boost_process(b,1024);
     for(int i=0;i<2048;i++){int a=abs(b[i]);if(a>worst)worst=a;if(a>=32767)clipped++;}}
    char m[96];snprintf(m,sizeof(m),"boost burst: worst peak %d, clipped %d, gain %.2f",worst,clipped,boost_gain);report(m);
    check("a loud burst is limited, not clipped, and the leveler backs off within a few blocks",clipped==0 && worst<=32000 && boost_gain<1.6f);}
   /* speech-like peaks (a sine with short spikes 4x its level) at Max: loud, and still under full scale */
   {volume_boost=4;boost_reset();int worst=0,clipped=0;
    for(int k=0;k<300;k++){for(int i=0;i<1024;i++){float s=2000*sinf((k*1024+i)*0.06f);if((i&255)<6)s*=4;b[i*2]=b[i*2+1]=(short)s;}boost_process(b,1024);
     if(k>=200)for(int i=0;i<2048;i++){int a=abs(b[i]);if(a>worst)worst=a;if(a>=32767)clipped++;}}
    check("Max pushes speech-like peaks to the limiter without clipping",clipped==0 && worst>29000 && worst<=32000);}
   volume_boost=3;boost_reset();BOOST_RUN(20000,200);PEAK(p);check("loud sound is left as it is",boost_gain==1 && p>=19900 && p<=20000);
   boost_reset();BOOST_RUN(30,200);check("near-silence is not pumped up",boost_gain==1);
   boost_src=0;boost_reset();BOOST_RUN(1000,50);PEAK(p);check("music is never boosted",boost_gain==1 && p<=1000);
   volume_boost=4;save_settings();volume_boost=0;load_settings();check("Volume boost is saved",volume_boost==4);
   /* a v11 file's Normal becomes High */
   volume_boost=1;save_settings();{FILE *f=fopen(settings_path,"r+");if(f){fputs("ZOID_UI_11",f);fclose(f);}}volume_boost=0;load_settings();
   check("an older Normal boost becomes High",volume_boost==3);
   volume_boost=kb;save_settings();boost_src=keep;boost_reset();
  }
  /* Paper (desktop Paper.xaml) on Home, the music library, Now Playing, Settings, the menu, the equalizer and lyrics */
  if(test_frame==600){theme=5;T=&themes[5];fonts_build(5);home_sel=HOME_MUSIC;screen=SCR_HOME;check("Paper uses the Cambria serif and its own skin, not the Zoid layout",fonts[0].data==&font_paper_small[0][0] && !MODERN && ui_paper);}
  if(test_frame==601)capture("ms0:/home-paper.rgba");
  if(test_frame==602){strcpy(folder,root);browse();screen=SCR_LIBRARY;}
  if(test_frame==603)capture("ms0:/library-paper.rgba");
  if(test_frame==604){screen=SCR_NOW;np_focus=2;if(paused)input(PSP_CTRL_START);}
  if(test_frame==605)capture("ms0:/now-paper.rgba");
  if(test_frame==606){open_settings();set_item=0;}
  if(test_frame==607){capture("ms0:/settings-paper.rgba");save_settings();theme=0;load_settings();check("Paper is saved as the theme",theme==5);}
  if(test_frame==608){screen=SCR_NOW;open_item_menu();}
  if(test_frame==609){capture("ms0:/menu-paper.rgba");}
  if(test_frame==610){menu_open=0;screen=SCR_EQ;eq_focus=3;}
  if(test_frame==611)capture("ms0:/eq-paper.rgba");
  if(test_frame==612){screen=SCR_NOW;np_focus=6;}
  if(test_frame==613)capture("ms0:/now-paper-focus.rgba");
  /* the three Zoidstation skins, 20 frames each: Home, the library, Now Playing, Settings, the menu, the equalizer,
     the seek bar focused, and Home on Settings (Portable: Left from Music reaches Settings, as on the cross media bar) */
  if(test_frame>=614 && test_frame<674) {
   static const char *nm[3]={"ms1","ms2","msp"};int k=(test_frame-614)/20,s=(test_frame-614)%20,th=6+k;char p[48];
   if(s==0){theme=th;T=&themes[th];fonts_build(th);menu_open=0;chooser=0;home_sel=HOME_MUSIC;screen=SCR_HOME;
    const unsigned char *f=k==0?&font_ms1_small[0][0]:k==1?&font_ms2_small[0][0]:&font_msp_small[0][0];
    check(k==0?"Zoidstation 1 has its own font and skin":k==1?"Zoidstation 2 has its own font and skin":"Zoidstation Portable has its own font and skin",fonts[0].data==f && MS && !MODERN && !ui_paper && ui_keepcase && !strncmp(T->name,"Zoidstation",11));}
   if(s==2){snprintf(p,sizeof(p),"ms0:/home-%s.rgba",nm[k]);capture(p);}
   if(s==3){strcpy(folder,root);browse();screen=SCR_LIBRARY;cursor=count>1?1:0;}
   if(s==5){snprintf(p,sizeof(p),"ms0:/library-%s.rgba",nm[k]);capture(p);}
   if(s==6){screen=SCR_NOW;np_focus=2;if(paused)input(PSP_CTRL_START);}
   if(s==8){snprintf(p,sizeof(p),"ms0:/now-%s.rgba",nm[k]);capture(p);}
   if(s==9){open_settings();set_item=0;}
   if(s==11){snprintf(p,sizeof(p),"ms0:/settings-%s.rgba",nm[k]);capture(p);}
   if(s==12){screen=SCR_NOW;open_item_menu();}
   if(s==13){snprintf(p,sizeof(p),"ms0:/menu-%s.rgba",nm[k]);capture(p);}
   if(s==14){menu_open=0;screen=SCR_EQ;eq_focus=3;}
   if(s==15){snprintf(p,sizeof(p),"ms0:/eq-%s.rgba",nm[k]);capture(p);}
   if(s==16){screen=SCR_NOW;np_focus=5;}
   if(s==17){snprintf(p,sizeof(p),"ms0:/now-focus-%s.rgba",nm[k]);capture(p);}
   if(s==18){np_focus=2;screen=SCR_HOME;home_sel=HOME_MUSIC;
    if(k==2){home_input(PSP_CTRL_LEFT);check("Portable: Left from Music goes to Settings, first on the bar",home_sel==HOME_SETTINGS);
     home_input(PSP_CTRL_LEFT);check("and stops there",home_sel==HOME_SETTINGS);home_input(PSP_CTRL_RIGHT);home_input(PSP_CTRL_RIGHT);check("Right goes back along the bar to Videos",home_sel==HOME_VIDEOS);}
    else{home_input(PSP_CTRL_DOWN);check("Down from the tiles goes to Settings",home_sel==HOME_SETTINGS);}}
   if(s==19){snprintf(p,sizeof(p),"ms0:/home2-%s.rgba",nm[k]);capture(p);}
  }
  if(test_frame==674){save_settings();theme=0;load_settings();check("a Zoidstation theme is saved and read back",theme==8);
   theme_next();check("the note button goes on to the next theme, round to Zoid, and says so",theme==0 && T==&themes[0] && !strcmp(toast,"Theme: Zoid"));
   theme_next();theme=0;load_settings();check("and saves it",theme==1);}
  /* Theme color: Zoid in orange, Phosphor in amber and white, Portable in gold, pink and red; saved; off for other themes */
  if(test_frame>=675 && test_frame<703) {
   static const int th[7]={0,2,2,8,8,8,1},col[7]={1,1,2,3,5,12,0};static const char *nm[7]={"zoid-orange","phos-amber","phos-white","msp-gold","msp-pink","msp-red","cyber"};
   int k=(test_frame-675)/4,s=(test_frame-675)%4;
   if(s==0){theme=th[k];T=&themes[theme];fonts_build(theme);open_settings();set_item=set_row(28);
    if(k<6){theme_color[theme]=col[k]-1;set_change(28,1);check(k==0?"Theme color turns Zoid orange":k==1?"and Phosphor amber, the whole tube":k==3?"and Portable gold":"another color applies",theme_color[theme]==col[k] && T==&themes[theme] && (k!=0 || T->accent==0xff8c1a) && (k!=1 || (T->fg==0xffb000 && T->bg!=themes_base[2].bg)));}
    else check("Theme color is off for a theme without colors",set_off(28) && theme_color_count(theme)==0);
    screen=SCR_HOME;home_sel=HOME_MUSIC;}
   if(s==2 && k<6){char p[48];snprintf(p,sizeof(p),"ms0:/color-%s.rgba",nm[k]);capture(p);}
   if(s==3 && k==5){save_settings();theme_color[0]=theme_color[2]=theme_color[8]=0;load_settings();check("the colors are saved and read back",theme_color[0]==1 && theme_color[2]==2 && theme_color[8]==12);}
  }
  if(test_frame==703){theme_color[0]=theme_color[2]=theme_color[8]=0;themes_apply();check("By month picks a color from the calendar",xmb_pick()>=1 && xmb_pick()<=12);}
  /* Y2K's colors: Tangerine turns the glass orange and leaves white alone; Now Playing and Settings in it; saved; off again */
  if(test_frame==704){theme=3;T=&themes[3];fonts_build(3);open_settings();set_item=set_row(28);theme_color[3]=3;set_change(28,1);
   unsigned int g=y2k_remap(0x2a86b4);
   check("Theme color turns Y2K tangerine, the glass and not the white",theme_color[3]==4 && color_remap==y2k_remap && ((g>>16)&255)>((g)&255) && y2k_remap(0xffffff)==0xffffff && !set_off(28));}
  if(test_frame==706)capture("ms0:/settings-y2k-tangerine.rgba");
  if(test_frame==707){screen=SCR_NOW;np_focus=2;}
  if(test_frame==709)capture("ms0:/now-y2k-tangerine.rgba");
  if(test_frame==710){save_settings();theme_color[3]=0;load_settings();check("the Y2K color is saved and read back",theme_color[3]==4);
   theme_color[3]=0;themes_apply();check("and Bondi Blue draws Y2K as it was",color_remap==NULL);}
  if(test_frame==712){np_focus=2;screen=SCR_HOME;theme=0;T=&themes[0];fonts_build(0);save_settings();report("transport complete");running=0;}
#elif defined(ZOID_MOVIE_TEST)
  /* a real film for ~40 s: the same start-up, tag reading and drawing as on a PSP, reported every 2 s */
  if(test_frame==30){strcpy(folder,root);browse();video_start("ms0:/MOVIETEST/movie.mp4");check("the movie opens",screen==SCR_VIDEO);}
  if(test_frame>30 && test_frame%75==0 && test_frame<1600){char b[160];snprintf(b,sizeof(b),"t=%d frame %d: screen %d pics %d clock %d next %d state %d code %08X subs %d cue %d",test_frame,vid.info.frames,screen,vid.pics_shown,video_clock(),video_next_ms(),state,(unsigned int)error_code,sub_n,vid.sub_shown);report(b);}
  /* the handle going stale mid-film, as on a real PSP: the picture must keep coming */
  static int pics_at_stale;if(test_frame==900){pics_at_stale=vid.pics_shown;video_test_stale_file();}
  /* jump a minute ahead, then pause and play with X, as a viewer would */
  static int seek_pics,seek_clock;
  if(test_frame==1000 || test_frame==1004){video_key(PSP_CTRL_RTRIGGER);}
  if(test_frame==1008)check("presses add up before the jump",vid.jump_pending && vid.jump_target>120000 && video_params_sent==1);
  if(test_frame==1100){char b[120];snprintf(b,sizeof(b),"after +2 min: state %d paused %d clock %d next %d pics %d code %08X stage %d",state,paused,video_clock(),video_next_ms(),vid.pics_shown,(unsigned int)error_code,stage);report(b);
   check("two R presses make one 2-minute jump and the film plays on from there",state==2 && !paused && video_clock()>130000 && video_params_sent==2);seek_pics=vid.pics_shown;video_key(PSP_CTRL_CROSS);}
  if(test_frame==1150){seek_clock=video_clock();check("X pauses after a jump",video_paused());}
  if(test_frame==1200){check("the paused film stays put",video_clock()==seek_clock);video_key(PSP_CTRL_CROSS);}
  if(test_frame==1350){char b[120];snprintf(b,sizeof(b),"after X: state %d paused %d clock %d (was %d) pics %d (was %d) code %08X",state,paused,video_clock(),seek_clock,vid.pics_shown,seek_pics,(unsigned int)error_code);report(b);
   check("X plays on from the jump point",state==2 && !paused && video_clock()>seek_clock+2000 && vid.pics_shown>seek_pics+40);
   test_sound_fail=1;video_key(PSP_CTRL_LEFT);}
  /* the sound refused after a jump, as on a real PSP: the picture must go on, and X bring the sound back */
  static int lost_pics;
  if(test_frame==1440){lost_pics=vid.pics_shown;check("when the sound can't restart, the film doesn't stall",vid.sound_lost && !vid.has_audio);}
  if(test_frame==1520){check("the picture goes on without the sound",vid.pics_shown>lost_pics+20);test_sound_fail=0;video_key(PSP_CTRL_CROSS);}
  if(test_frame==1595)check("X brings the sound back where the picture is",state==2 && vid.has_audio && !vid.sound_lost && !paused);
  if(test_frame==1500){capture("ms0:/movie.rgba");check("the picture keeps coming after the video file's handle goes stale",vid.pics_shown>pics_at_stale+100);}
  if(test_frame==1600){check("the movie is still playing after 40 s",screen==SCR_VIDEO && vid.pics_shown>500 && !error_code);check("stream setup (SPS/PPS) was sent at the start and after each jump only, not at every key frame",video_params_sent==3);
   {int p=0;for(int k=0;k<24;k++)if(levels[k]>p)p=levels[k];check("the film plays the MP3 soundtrack stored inside its MP4",emb_path[0] && !strcmp(active,emb_path) && state==2);check("the subtitles stored inside the MP4 load",sub_n==915);check("the sound is not silent",p>0 || clock_ms>20000);}video_key(PSP_CTRL_CIRCLE);report("movie complete");running=0;}
#elif defined(ZOID_QUEUE_TEST)
  if(test_frame==30){test_open_folders();if(count)lib_activate();visuals[0]=3;}
  if(test_frame==90){report("initial queue");check("three-track queue",queue_count==3 && queue_pos==0 && state==2);capture("ms0:/queue.rgba");}
  if(test_frame==100)seek_to(duration-1);
  if(test_frame==200){report("automatic next");check("natural EOF advances to second",queue_pos==1 && state==2 && !error_code);capture("ms0:/tunnel.rgba");}
  if(test_frame==210){screen=SCR_LIBRARY;parent();}
  if(test_frame==220)seek_to(duration-1);
  if(test_frame==320){report("next while browsing parent");check("queue survives folder navigation",queue_pos==2 && state==2 && !error_code);}
  if(test_frame==330){repeat_mode=1;seek_to(duration-1);}
  if(test_frame==430){report("repeat song");check("repeat song stays on third",queue_pos==2 && state==2 && elapsed<5 && !error_code);}
  if(test_frame==440){repeat_mode=2;seek_to(duration-1);theme=1;T=&themes[1];}
  if(test_frame==540){report("repeat all");check("repeat all wraps to first",queue_pos==0 && state==2 && !error_code);}
  if(test_frame==550){queue_toggle_shuffle();check("shuffle preserves current",shuffle && queue_order[queue_pos]==0);static int used[SONG_MAX];memset(used,0,sizeof(used));int valid=1;for(int i=0;i<queue_count;i++){int n=queue_order[i];if(n<0 || n>=queue_count || used[n])valid=0;else used[n]=1;}check("shuffle permutation has no duplicates",valid);}
  if(test_frame==560)queue_next(0,0);
  if(test_frame==600){report("shuffle next");check("manual next advances shuffled queue",queue_pos==1 && queue_order[queue_pos]!=0 && state==2 && !error_code);}
  if(test_frame==605)input(PSP_CTRL_LTRIGGER);
  if(test_frame==610){check("previous returns through shuffle history",queue_pos==0);queue_toggle_shuffle();repeat_mode=0;queue_pos=queue_count-1;queue_play();}
  if(test_frame==630)seek_to(duration-1);
  if(test_frame==730){report("end with repeat off");check("last track ends without wrapping",queue_pos==2 && state==0 && !error_code);}
  if(test_frame==740)request(NULL);
  if(test_frame==750)input(PSP_CTRL_START);
  if(test_frame==770)check("Start restarts playback",state==2 && !paused && !error_code);
  if(test_frame==780)input(PSP_CTRL_START);
  if(test_frame==800)check("Start pauses",state==2 && paused);
  if(test_frame==810)input(PSP_CTRL_START);
  if(test_frame==820)request(NULL);
  if(test_frame==850){report("integrated stop");check("Stop cancels advancement and resets time",state==0 && elapsed==0 && queue_pos==2 && !error_code);check("next decoders prepared ahead",prefetch_hits>=4);check("continuous output crossed track boundaries",transition_count>=3);check("handoff fed within one audio block",max_boundary_feed_us>0 && max_boundary_feed_us<23000);FILE *f=fopen("ms0:/smoke.txt","a");if(f){fprintf(f,"handoff: prepared=%d boundaries=%d worst_feed_us=%d\n",prefetch_hits,transition_count,max_boundary_feed_us);fclose(f);}}
  if(test_frame==860){unsigned int before=serial;int pos=queue_pos;queue_next(1,serial-1);check("stale EOF cannot override manual command",serial==before && queue_pos==pos);}
  /* crossfade: the next song must keep playing from inside the fade, not restart */
  if(test_frame==862){set_mix(2);fade_seconds=4;repeat_mode=0;if(shuffle)queue_toggle_shuffle();queue_pos=0;queue_play();}
  if(test_frame==900)seek_to(duration-8);
  if(test_frame==1500){report("crossfade");check("crossfade mixed into the next song",crossfade_count>=1 && queue_pos==1 && state==2 && !error_code);check("incoming song continued, not restarted",elapsed>=4);}
  /* lossless: FLAC with Vorbis tags, a 96 kHz FLAC played at 48 kHz, then a WAV, gapless */
  if(test_frame==1510){set_mix(0);request(NULL);snprintf(folder,sizeof(folder),"%s/Zoid Lossless Test",root);browse();screen=SCR_LIBRARY;
   check("library lists FLAC, WAV, M4A and MP4 songs",count==5);cursor=0;lib_activate();}
  if(test_frame==1560){report("flac");check("FLAC plays",state==2 && queue_pos==0 && !error_code && output_rate==44100);check("FLAC length is exact",duration==4);
   {static Tags g;read_tags(active,&g,1);check("FLAC Vorbis tags are read",!strcmp(g.title,"Lossless Tone") && !strcmp(g.artist,"ZOID Test"));}seek_to(2);}
  if(test_frame==1580){check("FLAC seeks",state==2 && elapsed>=2 && !error_code);}
  if(test_frame==1700){report("hi-res flac");check("FLAC advances to the 96 kHz song",queue_pos==1 && state==2 && !error_code);check("96 kHz plays at 48 kHz",output_rate==48000 && duration==3);}
  if(test_frame==1700+200){report("wav");check("WAV plays after it",queue_pos==2 && state==2 && !error_code);{static Tags g;read_tags(active,&g,0);check("WAV INFO title is read",!strcmp(g.title,"Wave Tone"));}}
  if(test_frame==1902)seek_to(duration-1);
  if(test_frame==2010){report("m4a");check("M4A (hardware AAC) plays after the WAV",queue_pos==3 && state==2 && !error_code && output_rate==44100);check("M4A length is read from the sample table",duration>=4 && duration<=5);{int p=0;for(int k=0;k<24;k++)if(levels[k]>p)p=levels[k];check("M4A decodes to sound, not silence",p>0);}
   {static Tags g;read_tags(active,&g,0);check("M4A iTunes tags are read",!strcmp(g.title,"Aac Tone") && !strcmp(g.artist,"ZOID Test"));}seek_to(2);}
  if(test_frame==2040)check("M4A seeks and keeps decoding",state==2 && elapsed>=2 && !error_code);
  if(test_frame==2042)seek_to(duration-1);
  if(test_frame==2150){report("mp4 audio");check("an MP4 in MUSIC plays as audio",queue_pos==4 && state==2 && !error_code);}
  /* video: Videos entry, hardware decode, picture follows the sound, seek, pause, zoom, back to the music */
  static char music_before[PATHLEN];static int clock_before;
  if(test_frame==2160){snprintf(music_before,sizeof(music_before),"%s",active);go_home(HOME_VIDEOS);home_activate(HOME_VIDEOS);
   check("the Videos tile on Home lists the VIDEO folder (videos only)",in_video_folder() && count==1 && screen==SCR_LIBRARY);cursor=0;lib_activate();}
  if(test_frame==2166){check("a video opens full screen",screen==SCR_VIDEO && video_active && vid.info.width==480 && vid.info.height==272);check("video runs the CPU at 222 MHz",scePowerGetCpuClockFrequencyInt()<=223);}
  if(test_frame==2240){report("video");unsigned int p=test_px();check("the Media Engine decodes pictures",vid.pics_shown>3);check("first picture is red",(p&255)>180 && ((p>>16)&255)<90);check("the video sound plays",state==2 && !error_code && clock_ms>200);check("subtitles load from the .srt (tags stripped)",sub_n==2 && !strcmp(subs[0].text,"The red half"));capture("ms0:/video.rgba");video_seek_to(3000);}
  if(test_frame==2310){unsigned int p=test_px();check("the subtitle follows the picture",vid.sub_shown==1);vol_test=1;vol_last=18;vol_until=now_ms+1500;check("the volume bar shows over a video",volume_showing());capture("ms0:/video-subs.rgba");check("seeking reaches the blue half",((p>>16)&255)>180 && (p&255)<90 && video_clock()>=3000);video_toggle_pause();clock_before=video_clock();}
  if(test_frame==2340){check("pause holds the picture and the clock",video_paused() && video_clock()-clock_before<100);video_toggle_pause();video_key(PSP_CTRL_TRIANGLE);check("Triangle zooms",vid.zoom==1);video_key(PSP_CTRL_TRIANGLE);}
  if(test_frame==2346){video_key(PSP_CTRL_CIRCLE);check("Circle leaves the video",screen!=SCR_VIDEO && !video_active);check("CPU speed comes back",scePowerGetCpuClockFrequencyInt()>=330);check("the music comes back",!strcmp(active,music_before));
   /* Set by ARK-5: ZOID leaves the clock where it is, even when it would drop to 222 */
   cpu_fast=2;set_clock(0);check("Set by ARK-5 leaves the CPU clock alone",scePowerGetCpuClockFrequencyInt()>=330);
   {char v[64];setting_value(10,v,sizeof(v));check("CPU speed shows Set by ARK-5",!strcmp(v,"Set by ARK-5"));}
   cpu_fast=1;}
  if(test_frame==2420){report("music after video");check("the music plays again after the video",state==2 && !error_code);strcpy(folder,video_root);browse();cursor=0;lib_activate();}
  if(test_frame==2426)video_seek_to(3200);
  if(test_frame==2560){report("video end");check("a video that ends returns to the library",screen!=SCR_VIDEO && !video_active);}
  /* a season folder inside VIDEO: listed, opened, episodes in natural order, one plays, back out */
  if(test_frame==2570){char d[PATHLEN],s[PATHLEN];snprintf(d,sizeof(d),"%s/Season 1",video_root);sceIoMkdir(d,0777);snprintf(s,sizeof(s),"%s/Color Test.mp4",video_root);
   const char *eps[2]={"Episode 10.mp4","Episode 2.mp4"};for(int e=0;e<2;e++){char t[PATHLEN];snprintf(t,sizeof(t),"%s/%s",d,eps[e]);test_copy(s,t);}
   screen=SCR_LIBRARY;strcpy(folder,video_root);browse();int f=-1;for(int i=0;i<count;i++)if(entries[i].folder==1 && !strcmp(entries[i].name,"Season 1"))f=i;
   check("a folder inside VIDEO is listed, ahead of the videos",f==0);cursor=f<0?0:f;lib_activate();
   check("opening it lists its episodes in natural order (2 before 10)",count==2 && !strcmp(entries[0].name,"Episode 2.mp4") && !strcmp(entries[1].name,"Episode 10.mp4"));
   cursor=0;lib_activate();}
  if(test_frame==2600){check("an episode in a season folder plays",screen==SCR_VIDEO && vid.pics_shown>0);video_key(PSP_CTRL_CIRCLE);}
  if(test_frame==2605){{char b[PATHLEN+64];snprintf(b,sizeof(b),"after Circle: screen %d count %d folder %s",screen,count,folder);report(b);}check("Circle goes back to the season folder",screen==SCR_LIBRARY && in_video_folder() && count==2);input(PSP_CTRL_CIRCLE);check("Circle again goes up to VIDEO",!strcmp(folder,video_root));
   char d[PATHLEN];snprintf(d,sizeof(d),"%s/Season 1",video_root);const char *eps[2]={"Episode 10.mp4","Episode 2.mp4"};for(int e=0;e<2;e++){char t[PATHLEN];snprintf(t,sizeof(t),"%s/%s",d,eps[e]);sceIoRemove(t);}sceIoRmdir(d);}
  if(test_frame==2610){report("queue complete");running=0;}
#elif defined(ZOID_NET_TEST)
  /* Internet radio and online lyrics, against net-test-server.cs on 127.0.0.1:8088 (run-test.ps1 -Kind Net).
     Each step waits for what it needs (a list, a stream starting), up to a time limit. */
  static int step,step_at;int waited=test_frame-step_at;
  static int station_peak;
  radio_poll();pod_poll(); /* a finished list is shown before the checks look at it */
#define NEXT_STEP() do{step++;step_at=test_frame;}while(0)
#define STARTED() ((state==2 && seek_ack==serial) || (error_code && state!=1) || waited>1500) /* this request, not the station before it */
  /* a screenshot is taken as the frame ends: the action after it waits a few frames */
  static int after;
  if(after){if(waited>=3){int a=after;after=0;if(a==1)open_lyrics();else if(a==2)queue_previous();else if(a==3){cursor=0;lib_activate();}else if(a==5){cursor=1;lib_activate();}else if(a==6){radio_show_home();cursor=1;lib_activate();}else{screen=SCR_LIBRARY;request(NULL);}NEXT_STEP();}}
  else switch(step) {
  case 0:
   if(test_frame<20)break;
   {FILE *f=fopen(radio_file,"w");if(f){fprintf(f,"# test stations\nLocal MP3|http://127.0.0.1:8088/stream.mp3\n  Local PLS | http://127.0.0.1:8088/station.pls\nnot a station\n");fclose(f);}radio_load_saved();}
   check("radio.txt stations load (names trimmed, bad lines skipped)",saved_n==2 && !strcmp(saved_st[1].name,"Local PLS") && !strcmp(saved_st[1].url,"http://127.0.0.1:8088/station.pls"));
   home_activate(HOME_VIDEOS); /* Videos first, as a person might: Radio must not inherit VIDEO */
   go_home(HOME_RADIO);check("Internet Radio is a tile on Home",screen==SCR_HOME && home_sel==HOME_RADIO);
   input(PSP_CTRL_CROSS);check("after Videos, Internet Radio rows are not taken for videos",in_radio && !in_video_folder());
   check("Internet Radio opens: Search, Local, iHeartRadio, Popular, Genres, then my stations",in_radio && count==7 && entries[0].folder==6 && entries[1].folder==10 && entries[2].folder==30 && entries[3].folder==7 && entries[4].folder==8 && !entries[5].folder);
   capture("ms0:/radio-home.rgba");NEXT_STEP();break;
  case 1:cursor=3;lib_activate();check("Popular stations asks the directory",radio_waiting() && in_radio && radio_view==RV_LIST);NEXT_STEP();break;
  case 2:
   if(radio_waiting() && waited<1500)break;
   {char b[200];snprintf(b,sizeof(b),"directory: error %d (%s), wifi %d %s, %d rows, start-up step %d code %08X",rjob_error,rjob_error?net_error_text(rjob_error):"-",net_status,net_connected_name,count,net_fail_step,net_fail_code);report(b);snprintf(b,sizeof(b),"free memory: %d KB before, %d KB after modules; total free %d KB",net_free_kb[0],net_free_kb[1],(int)(sceKernelTotalFreeMemSize()/1024));report(b);}
   check("the station list arrives over Wi-Fi",!radio_waiting() && !rjob_error && net_status==2);
   check("only MP3 and AAC stations are listed (no Ogg, no HLS)",count==5);
   check("station names are decoded from JSON (\\u escapes)",count>0 && !strcmp(entries[0].title,"Zoid Test FM \xC3\xA9t\xC3\xA9"));
   capture("ms0:/radio-list.rgba");NEXT_STEP();break;
  case 3:cursor=0;lib_activate();check("X plays the station on Now Playing",screen==SCR_NOW && row_live() && queue_count==5);NEXT_STEP();break;
  case 4:
   if(!STARTED())break;
   {char b[160];snprintf(b,sizeof(b),"mp3 station: state %d code %08X phase %d codec %d rate %d kbps %d after %d frames",state,(unsigned int)error_code,radio_status.phase,radio_status.codec,radio_status.rate,radio_status.kbps,waited);report(b);}
   check("the MP3 station plays (live, no length)",state==2 && !error_code && duration==0 && radio_status.codec==1);NEXT_STEP();break;
  case 5:
   if(waited<360)break;
   check("it keeps playing (the clock counts up)",state==2 && elapsed>=4 && !error_code);
   {char b[200];snprintf(b,sizeof(b),"title '%s' artist '%s' album '%s'",np_title,np_artist,np_album);report(b);}
   check("the song title and artist come from the stream",!strcmp(np_title,"Song One") && !strcmp(np_artist,"Zoid Test Artist"));
   {char e[16];end_text(e,sizeof(e),elapsed);check("the end time reads LIVE",!strcmp(e,"LIVE"));}
   {int p=0;for(int k=0;k<24;k++)if(levels[k]>p)p=levels[k];check("the visualizers get the station's sound",p>0);}
   station_peak=test_raw_peak;{char b[80];snprintf(b,sizeof(b),"station decoded peak %d, boost %d",station_peak,boost_src);report(b);}
   check("a station gets the volume boost",boost_src==1);
   capture("ms0:/radio-now.rgba");after=1;step_at=test_frame;break;
  case 6:
   if((lyr_state==3 || lyr_state==4) && waited<1500)break;
   {char b[120];snprintf(b,sizeof(b),"station lyrics: state %d lines %d synced %d msg %s",lyr_state,lyr.n,lyr.synced,lj_msg);report(b);}
   check("the station's song gets synced lyrics from LRCLIB",lyr_state==2 && lyr.synced && lyr.n==4);
   NEXT_STEP();break;
  case 7:if(waited==40)capture("ms0:/radio-lyrics.rgba");if(waited<45)break;screen=SCR_NOW;theme=1;T=&themes[1];fonts_build(1);NEXT_STEP();break;
  case 8:if(waited<4)break;capture("ms0:/radio-now-cyber.rgba");NEXT_STEP();break;
  case 9:if(waited<4)break;theme=0;T=&themes[0];fonts_build(0);queue_next(0,0);NEXT_STEP();break;
  case 10:
   if(!STARTED())break;
   {char b[160];snprintf(b,sizeof(b),"aac station: state %d code %08X codec %d rate %d after %d frames",state,(unsigned int)error_code,radio_status.codec,radio_status.rate,waited);report(b);}
   check("Next plays the AAC station",state==2 && !error_code && radio_status.codec==2 && strstr(active,"stream.aac"));NEXT_STEP();break;
  case 11:if(waited<200)break;check("the AAC station keeps playing",state==2 && elapsed>=2 && !error_code);queue_next(0,0);NEXT_STEP();break;
  case 12:if(!STARTED())break;check("a .pls link plays the stream inside it",state==2 && !error_code && radio_status.codec==1 && strstr(active,"station.pls"));queue_next(0,0);NEXT_STEP();break;
  case 13:if(!STARTED())break;check("a station that redirects plays",state==2 && !error_code && strstr(active,"/redir"));queue_next(0,0);NEXT_STEP();break;
  case 14:
   if(!STARTED())break;
   {char b[160];snprintf(b,sizeof(b),"missing station: state %d code %08X: %s",state,(unsigned int)error_code,radio_error_text((int)error_code));report(b);}
   check("a missing station shows why, without a crash",error_code && state!=2 && (int)error_code==NET_E_HTTP);
   capture("ms0:/radio-error.rgba");after=2;step_at=test_frame;break;
  case 15:
   if(!STARTED())break;
   check("Previous goes back a station",state==2 && strstr(active,"/redir"));
   screen=SCR_LIBRARY;cursor=1;open_item_menu();check("hold X on a listed station offers Save",menu_open && menu_act[1]==A_RADIO_SAVE);
   menu_run(A_RADIO_SAVE,1);radio_load_saved();check("the station is saved in radio.txt",saved_n==3 && !strcmp(saved_st[2].name,"Zoid AAC Radio"));
   input(PSP_CTRL_CIRCLE);check("Circle goes back to Internet Radio",in_radio && radio_view==RV_HOME && count==8);
   cursor=7;open_item_menu();check("a saved station can be removed",menu_open && menu_act[1]==A_RADIO_REMOVE);
   menu_run(A_RADIO_REMOVE,1);check("and is removed",saved_n==2 && count==7);
   cursor=4;lib_activate();NEXT_STEP();break;
  case 16:
   if(radio_waiting() && waited<1500)break;
   check("Genres lists the directory's genres (not tiny ones)",radio_view==RV_GENRES && count==2 && !strcmp(entries[0].name,"Jazz"));
   capture("ms0:/radio-genres.rgba");after=3;step_at=test_frame;break;
  case 17:
   if(radio_waiting() && waited<1500)break;
   check("a genre lists its stations",radio_view==RV_LIST && count==5 && !strcmp(radio_title,"Jazz"));
   input(PSP_CTRL_CIRCLE);check("Circle goes back to the genres without asking again",radio_view==RV_GENRES && !radio_waiting() && count==2 && cursor==0);
   input(PSP_CTRL_CIRCLE);check("again to Internet Radio",in_radio && radio_view==RV_HOME && cursor==4);
   input(PSP_CTRL_CIRCLE);check("again to Home, on Internet Radio",screen==SCR_HOME && home_sel==HOME_RADIO);
   home_activate(HOME_RADIO);radio_search_done("zoid"); /* the keyboard itself cannot be typed on in a test */
   NEXT_STEP();break;
  case 18:
   if(radio_waiting() && waited<1500)break;
   check("Search lists matching stations",in_radio && radio_view==RV_LIST && count==5 && !strcmp(radio_title,"\"zoid\""));
   /* Local stations: pick a country, then a place; it is remembered */
   region_cc[0]=region_country[0]=region_state[0]=0;region_save();free(country_names);country_names=NULL;country_n=0;
   radio_show_home();cursor=1;lib_activate();
   check("Local stations with no place yet asks for the countries",radio_waiting() && radio_view==RV_COUNTRIES);
   step=30;step_at=test_frame;break;
  case 30:
   if(radio_waiting() && waited<1500)break;
   check("the countries are listed (not ones with no stations), after Anywhere",radio_view==RV_COUNTRIES && count==2 && entries[0].folder==14 && !strcmp(entries[1].name,"Testland"));
   cursor=1;lib_activate();step=31;step_at=test_frame;break;
  case 31:
   if(radio_waiting() && waited<1500)break;
   check("a country lists its places, after All of it",radio_view==RV_REGIONS && count==3 && !strcmp(entries[0].name,"All of Testland") && !strcmp(entries[1].name,"Test State"));
   capture("ms0:/radio-places.rgba");after=5;step_at=test_frame;break;
  case 32:
   if(radio_waiting() && waited<1500)break;
   check("a place lists its own stations, under Change local place",radio_view==RV_LIST && count==3 && entries[0].folder==13 && !strcmp(entries[1].title,"Test State Public Radio") && !strcmp(radio_title,"Local: Test State"));
   region_cc[0]=region_state[0]=0;region_load();check("the place is remembered",!strcmp(region_cc,"TL") && !strcmp(region_country,"Testland") && !strcmp(region_state,"Test State"));
   capture("ms0:/radio-local.rgba");after=6;step_at=test_frame;break;
  case 33:
   if(radio_waiting() && waited<1500)break;
   check("Local stations opens the place's list straight away next time",radio_view==RV_LIST && count==3 && entries[0].folder==13);
   input(PSP_CTRL_CIRCLE);check("Circle goes back to Internet Radio, on Local stations",radio_view==RV_HOME && cursor==1 && !strcmp(entries[1].name,"Local stations: Test State") && !strcmp(entries[3].name,"Popular in your country"));
   cursor=3;lib_activate();step=34;step_at=test_frame;break;
  case 34:
   if(radio_waiting() && waited<1500)break;
   check("Popular lists the country's stations once a place is picked",radio_view==RV_LIST && count==5 && !strcmp(radio_title,"Popular in Testland"));
   radio_show_countries();cursor=0;lib_activate();
   check("Anywhere forgets the place",!region_cc[0] && radio_view==RV_HOME && !strcmp(entries[1].name,"Local stations"));
   {int fd=sceIoOpen(region_file,PSP_O_RDONLY,0);check("and removes radio-region.txt",fd<0);if(fd>=0)sceIoClose(fd);}
   /* iHeartRadio: a state, then a city (remembered), its lineup, search */
   ih_id=0;ih_save();free(ih_mk);ih_mk=NULL;ih_mk_n=0;
   radio_show_home();check("iHeartRadio is on Internet Radio, with no city yet",!strcmp(entries[2].name,"iHeartRadio stations"));
   cursor=2;lib_activate();check("iHeartRadio with no city yet asks for its cities",radio_waiting() && radio_view==RV_IHSTATES);
   step=35;step_at=test_frame;break;
  case 35:
   if(radio_waiting() && waited<1500)break;
   {char b[200];snprintf(b,sizeof(b),"iheart states: error %d, %d rows, first '%s'",rjob_error,count,count?entries[0].name:"");report(b);}
   check("iHeartRadio lists states A to Z, the US first",radio_view==RV_IHSTATES && count==3 && !strcmp(entries[0].name,"Alpha State") && !strcmp(entries[1].name,"Test State") && !strcmp(entries[2].name,"Baja Test (Mexico)"));
   cursor=1;lib_activate();
   check("a state lists its cities, busiest first (none without stations)",radio_view==RV_IHCITIES && count==2 && !strcmp(entries[0].name,"Zoid City") && !strcmp(entries[1].name,"Small Town"));
   input(PSP_CTRL_CIRCLE);check("Circle goes back to the states, on that state",radio_view==RV_IHSTATES && cursor==1 && !radio_waiting());
   lib_activate();cursor=0;lib_activate();
   check("a city asks for its lineup",radio_waiting() && radio_view==RV_LIST && !strcmp(radio_title,"iHeart: Zoid City"));
   ih_id=0;ih_load();check("the city is remembered",ih_id==27 && !strcmp(ih_city,"Zoid City") && !strcmp(ih_state,"Test State"));
   step=36;step_at=test_frame;break;
  case 36:
   if(radio_waiting() && waited<1500)break;
   {char b[200];snprintf(b,sizeof(b),"iheart lineup: error %d, %d rows, '%s' '%s'",rjob_error,count,count>2?entries[2].title:"",count>2?entries[2].artist:"");report(b);}
   check("the lineup lists stations with a plain stream (no HLS-only ones), under Search and Change city",radio_view==RV_LIST && count==4 && entries[0].folder==31 && entries[1].folder==32 && !strcmp(entries[2].title,"Zoid 93.3") && !strcmp(entries[3].title,"Zoid News 600"));
   check("each shows its dial position and call letters",count==4 && !strcmp(entries[2].artist,"93.3 FM  KZOI") && !strcmp(entries[3].artist,"600 AM  KZNW"));
   check("the plain HTTP stream is used",list_n==2 && !strcmp(list_st[0].url,"http://127.0.0.1:8088/stream.mp3"));
   capture("ms0:/radio-iheart.rgba");step=37;step_at=test_frame;break;
  case 37:
   if(waited<3)break;
   cursor=3;lib_activate();check("X plays an iHeartRadio station",screen==SCR_NOW && row_live() && queue_count==2);
   step=38;step_at=test_frame;break;
  case 38:
   if(!STARTED())break;
   check("the iHeartRadio station plays",state==2 && !error_code && radio_status.codec==2 && strstr(active,"stream.aac"));
   screen=SCR_LIBRARY;request(NULL);
   input(PSP_CTRL_CIRCLE);check("Circle goes back to Internet Radio, on iHeartRadio, named for the city",radio_view==RV_HOME && cursor==2 && !strcmp(entries[2].name,"iHeartRadio: Zoid City"));
   ih_search_done("zoid");step=39;step_at=test_frame;break;
  case 39:
   if(radio_waiting() && waited<1500)break;
   {char b[200];snprintf(b,sizeof(b),"iheart search: error %d, %d rows, '%s' '%s'",rjob_error,count,count?entries[0].title:"",count?entries[0].artist:"");report(b);}
   check("iHeartRadio search lists stations in its own order, with their city",radio_view==RV_LIST && count==2 && !strcmp(entries[0].title,"Zoid News 600") && !strcmp(entries[0].artist,"600 AM  Far City, TS") && !strcmp(entries[1].title,"Zoid 93.3"));
   input(PSP_CTRL_CIRCLE);check("Circle goes back to Internet Radio, on iHeartRadio",radio_view==RV_HOME && cursor==2);
   cursor=2;lib_activate();check("next time iHeartRadio opens the city's lineup straight away",radio_waiting() && radio_view==RV_LIST && radio_list_kind==10);
   step=22;step_at=test_frame;break;
  case 22:
   if(radio_waiting() && waited<1500)break;
   check("and lists it",radio_view==RV_LIST && count==4 && !strcmp(radio_title,"iHeart: Zoid City"));
   ih_id=0;ih_save();
   step=40;step_at=test_frame;break;
  /* Podcasts: the directory (search, top chart, categories), a feed, subscribing, streaming, saving, new episodes.
     A screenshot is taken as the frame ends, so the step after one waits a few frames before acting. */
  case 40:
   remove_tree(pod_root,0);pod_scan_subs();
   home_activate(HOME_VIDEOS); /* Videos first: Podcasts must not inherit VIDEO */
   go_home(HOME_PODCASTS);input(PSP_CTRL_CROSS);check("after Videos, Podcasts rows are not taken for videos",in_pod && !in_video_folder());
   check("the Podcasts tile opens Podcasts: Search, Top podcasts, Categories",screen==SCR_LIBRARY && in_pod && pod_view==PV_HOME && count==3 && entries[0].folder==20 && entries[1].folder==21 && entries[2].folder==22);
   capture("ms0:/podcasts-empty.rgba");NEXT_STEP();break;
  case 41:if(waited<3)break;pod_search_done("zoid");check("Search asks the podcast directory",pod_waiting() && pod_view==PV_RESULTS);NEXT_STEP();break;
  case 42:
   if(pod_waiting() && waited<1500)break;
   {char b[160];snprintf(b,sizeof(b),"podcast search: error %d (%s), %d rows",pod_err,pod_err?pod_error_text(pod_err):"-",count);report(b);}
   check("Search lists podcasts from the directory, with their authors",count==2 && !strcmp(entries[0].name,"Zoid Test Podcast") && !strcmp(entries[0].artist,"Zoid Test Author"));
   capture("ms0:/podcasts-search.rgba");NEXT_STEP();break;
  case 43:if(waited<3)break;cursor=0;lib_activate();check("a podcast opens and its feed is read",pod_view==PV_SHOW && pod_waiting());NEXT_STEP();break;
  case 44:
   if(pod_waiting() && waited<1500)break;
   {char b[200];snprintf(b,sizeof(b),"feed: error %d, %d episodes, first '%s' date %s dur %d",pod_err,pod_n,pod_n?pod_eps[0].title:"",pod_n?pod_eps[0].date:"",pod_n?pod_eps[0].dur:0);report(b);}
   check("its episodes come from the RSS feed, newest first, under Subscribe",count==4 && entries[0].folder==28 && !strcmp(entries[1].title,"Episode 3 & the CDATA") && !strcmp(entries[2].title,"Episode 2") && !strcmp(entries[3].title,"Episode 1"));
   check("each episode's length and date are read",pod_n==3 && pod_eps[0].dur==30 && pod_eps[1].dur==30 && pod_eps[2].dur==30 && !strcmp(pod_eps[0].date,"2026-11-03"));
   capture("ms0:/podcasts-show.rgba");NEXT_STEP();break;
  case 45:
   if(waited<3)break;
   cursor=0;lib_activate();
   {char f[PATHLEN];show_file("Zoid Test Podcast",f,sizeof(f));check("Subscribe saves the show in PODCASTS",!strcmp(pod_cur_dir,"Zoid Test Podcast") && file_exists(f) && entries[0].folder==29);}
   NEXT_STEP();break;
  case 46:
   {char f[PATHLEN];snprintf(f,sizeof(f),"%s/Zoid Test Podcast/cover.jpg",pod_root);if(!file_exists(f) && waited<1500)break;check("the show's cover is saved as cover.jpg",file_exists(f));}
   cursor=1;lib_activate();check("X on an episode streams it, the list as the queue",screen==SCR_NOW && radio_is_episode(active) && queue_count==3);NEXT_STEP();break;
  case 47:
   if(!STARTED())break;
   {char b[160];snprintf(b,sizeof(b),"episode stream: state %d code %08X duration %d after %d frames",state,(unsigned int)error_code,duration,waited);report(b);}
   check("the episode streams on demand, its length known",state==2 && !error_code && duration==30);NEXT_STEP();break;
  case 48:
   if(waited<90)break;
   {char b[300];snprintf(b,sizeof(b),"episode now playing: '%s' / '%s' / '%s' art %d",np_title,np_artist,np_album,art_ready);report(b);}
   check("Now Playing names the episode, the show and the date",!strcmp(np_title,"Episode 3 & the CDATA") && !strcmp(np_artist,"Zoid Test Podcast") && strstr(np_album,"Nov 3"));
   check("and shows the show's cover",art_ready);
   capture("ms0:/podcasts-now.rgba");NEXT_STEP();break;
  case 49:if(waited<3)break;seek_to(20);NEXT_STEP();break;
  case 50:
   if(!STARTED())break;
   {char b[160];snprintf(b,sizeof(b),"episode seek: state %d code %08X elapsed %d",state,(unsigned int)error_code,elapsed);report(b);}
   check("seeking a streamed episode starts it from there (a Range request)",state==2 && !error_code && elapsed>=20);NEXT_STEP();break;
  case 51:
   if(queue_pos!=1 && waited<4000)break;
   check("the episode plays to its end, then the next one starts",queue_pos==1 && strstr(active,"ep2.mp3"));NEXT_STEP();break;
  case 52:
   if(waited<20)break;
   {PodEp e;check("a finished episode is marked played",test_pod_ep("Episode 3 & the CDATA",&e) && e.played);}
   request(NULL);screen=SCR_LIBRARY;cursor=2;open_item_menu();
   check("hold X on an episode offers Save for offline",menu_open && menu_act[1]==A_POD_SAVE);
   menu_run(A_POD_SAVE,1);check("and it starts saving",dl_n>=1);NEXT_STEP();break;
  case 53:
   if((dl_busy || dl_n) && waited<3000)break;
   {char f[PATHLEN];snprintf(f,sizeof(f),"%s/Zoid Test Podcast/2026-11-02 Episode 2.mp3",pod_root);report(dl_msg);check("Save for offline puts the episode in the show's folder",file_exists(f) && pod_eps[1].file[0]);}
   pod_row_text(2);check("its row then says Saved",!strcmp(entries[2].artist,"Saved"));
   NEXT_STEP();break;
  case 54:if(waited<3)break;cursor=2;lib_activate();NEXT_STEP();break;
  case 55:
   if(!STARTED())break;
   check("a saved episode plays from the Memory Stick",state==2 && !error_code && !radio_is_url(active) && strstr(active,"/PODCASTS/"));NEXT_STEP();break;
  case 56:
   if(waited<60)break;
   {char b[300];snprintf(b,sizeof(b),"saved episode: '%s' / '%s'",np_title,np_artist);report(b);}
   check("its title comes from the feed, not the file's own tag",!strcmp(np_title,"Episode 2") && !strcmp(np_artist,"Zoid Test Podcast"));
   seek_to(12);NEXT_STEP();break;
  case 57:if(!STARTED() || waited<40)break;request(NULL);NEXT_STEP();break;
  case 58:
   if(waited<10)break;
   {PodEp e;int ok=test_pod_ep("Episode 2",&e);char b[100];snprintf(b,sizeof(b),"left at %d played %d",ok?e.pos:-1,ok?e.played:-1);report(b);check("where an episode was left is kept",ok && e.pos>=10 && e.pos<25 && !e.played);}
   {char *b=NULL;int st;http_get("http://127.0.0.1:8088/test/add-episode",&b,1024,&st);free(b);}
   screen=SCR_LIBRARY;cursor=0;lib_activate();check("Load new episodes checks the feed",pod_waiting() && pod_want==4);NEXT_STEP();break;
  case 59:
   if(pod_waiting() && waited<1500)break;
   {char b[200];snprintf(b,sizeof(b),"after refresh: %d rows, first '%s' fresh %d artist '%s'",count,pod_n?pod_eps[0].title:"",pod_n?pod_eps[0].fresh:0,count>1?entries[1].artist:"");report(b);}
   check("the new episode is listed first, marked New",count==5 && !strcmp(pod_eps[0].title,"Episode 4 (M4A)") && pod_eps[0].fresh && !strncmp(entries[1].artist,"New",3));
   check("played and saved episodes keep their state",pod_eps[1].played && pod_eps[2].file[0] && pod_eps[2].pos>=10);
   capture("ms0:/podcasts-new.rgba");NEXT_STEP();break;
  case 60:
   if(waited<3)break;
   cursor=1;lib_activate();check("an M4A episode is saved before it plays",screen==SCR_LIBRARY && (dl_n || dl_busy));NEXT_STEP();break;
  case 61:
   if((dl_busy || dl_n) && waited<3000)break;
   check("the M4A episode is saved",pod_eps[0].file[0] && strstr(pod_eps[0].file,".m4a"));
   cursor=1;lib_activate();NEXT_STEP();break;
  case 62:
   if(!STARTED())break;
   check("the saved M4A episode plays",state==2 && !error_code && strstr(active,".m4a"));
   request(NULL);screen=SCR_LIBRARY;theme=1;T=&themes[1];fonts_build(1);NEXT_STEP();break;
  case 63:if(waited<4)break;capture("ms0:/podcasts-show-cyber.rgba");NEXT_STEP();break;
  case 64:
   if(waited<3)break;
   theme=0;T=&themes[0];fonts_build(0);
   input(PSP_CTRL_CIRCLE);check("Circle goes back to the search results, on that podcast",pod_view==PV_RESULTS && cursor==0);
   input(PSP_CTRL_CIRCLE);{char b[160];snprintf(b,sizeof(b),"podcasts home: %d rows, last '%s' '%s'",count,count?entries[count-1].name:"",count?entries[count-1].artist:"");report(b);}
   check("again to Podcasts, which lists the subscription",pod_view==PV_HOME && count==6 && entries[5].folder==25 && !strcmp(entries[5].name,"Zoid Test Podcast") && !strcmp(entries[5].artist,"2 saved"));
   capture("ms0:/podcasts-home.rgba");NEXT_STEP();break;
  case 65:if(waited<3)break;cursor=1;lib_activate();NEXT_STEP();break;
  case 66:
   if(pod_waiting() && waited<1500)break;
   check("Top podcasts lists the chart in its own order",pod_view==PV_RESULTS && count==2 && !strcmp(entries[0].name,"Zoid Test Podcast") && !strcmp(entries[1].name,"Zoid Video Cast"));
   input(PSP_CTRL_CIRCLE);check("Circle goes back to Podcasts, on Top podcasts",pod_view==PV_HOME && cursor==1);
   cursor=2;lib_activate();check("Categories lists 18 categories",pod_view==PV_CATS && count==18);
   capture("ms0:/podcasts-categories.rgba");NEXT_STEP();break;
  case 67:if(waited<3)break;cursor=9;lib_activate();NEXT_STEP();break;
  case 68:
   if(pod_waiting() && waited<1500)break;
   check("a category lists its top podcasts",pod_view==PV_RESULTS && count==2 && !strcmp(pod_title,"Music"));
   input(PSP_CTRL_CIRCLE);check("Circle goes back to the categories, on that one",pod_view==PV_CATS && cursor==9);
   input(PSP_CTRL_CIRCLE);check("again to Podcasts, on Categories",pod_view==PV_HOME && cursor==2);
   cursor=4;lib_activate();check("Saved episodes lists every saved episode, with its show",pod_view==PV_SAVED && count==2 && !strcmp(entries[0].artist,"Zoid Test Podcast"));
   capture("ms0:/podcasts-saved.rgba");NEXT_STEP();break;
  case 69:
   if(waited<3)break;
   input(PSP_CTRL_CIRCLE);check("Circle goes back to Podcasts, on Saved episodes",pod_view==PV_HOME && cursor==4);
   cursor=5;open_item_menu();check("hold X on a show offers Unsubscribe",menu_open && menu_act[1]==A_POD_UNSUB);
   menu_run(A_POD_UNSUB,1);check("which warns first when episodes are saved, Cancel first",menu_open && menu_warn && menu_act[0]==A_CANCEL);
   capture("ms0:/podcasts-unsubscribe.rgba");NEXT_STEP();break;
  case 70:
   if(waited<3)break;
   menu_run(A_POD_UNSUB_YES,1);
   {char d[PATHLEN];snprintf(d,sizeof(d),"%s/Zoid Test Podcast",pod_root);check("Unsubscribe removes the show and its saved episodes",!dir_exists(d) && pod_view==PV_HOME && count==3);}
   input(PSP_CTRL_CIRCLE);check("Circle from Podcasts goes Home, on Podcasts",screen==SCR_HOME && home_sel==HOME_PODCASTS);
   capture("ms0:/home-podcasts.rgba");
   step=71;step_at=test_frame;break;
  /* Seeking a streamed episode on a slow server (a real podcast host takes a second or two to connect):
     holding Right must not open a connection per step, and a burst of jumps must not pile them up. */
  case 71:
   if(waited<3)break;
   {static Paths p;paths_clear(&p);paths_add(&p,"http://127.0.0.1:8088/slow/ep3.mp3#@30,0||Slow Episode");queue_start_paths(&p,0);}screen=SCR_NOW;np_focus=5;
   NEXT_STEP();break;
  case 72:if(!STARTED())break;check("an episode on a slow server streams",state==2 && !error_code);seek_step_index=0;test_value=serial;NEXT_STEP();break;
  case 73: {
   static int presses,maxr,minfree;if(!presses){maxr=0;minfree=1<<30;}
   int rr=radio_readers_alive(),fr=(int)(sceKernelTotalFreeMemSize()/1024);if(rr>maxr)maxr=rr;if(fr<minfree)minfree=fr;
   if(waited%4==0 && presses<24){presses++;input(PSP_CTRL_RIGHT);} /* Right held on the seek bar: about 15 steps a second */
   if(presses<24 || waited<200){if(presses==24 && waited==100){char b[120];snprintf(b,sizeof(b),"seek presses: %d jumps asked, %d streams open at most, %d KB free at least",(int)(serial-test_value),maxr,minfree);report(b);}break;}
   {char b[120];snprintf(b,sizeof(b),"after holding Right: %d jumps, %d streams open at most, %d KB free at least, elapsed %d",(int)(serial-test_value),maxr,minfree,elapsed);report(b);}
   check("holding Right on a streamed episode's seek bar makes one jump, not one per step",serial-test_value<=2);
   check("and never keeps more than 2 streams open",maxr<=2);
   presses=0;NEXT_STEP();break;
  }
  case 74:if(!STARTED())break;check("it lands where the steps added up to",state==2 && !error_code && elapsed>=20);test_value=0;NEXT_STEP();break;
  case 75: {
   static int maxr;if(!test_value)maxr=0;
   int rr=radio_readers_alive();if(rr>maxr)maxr=rr;
   if(waited%3==0 && test_value<8){test_value++;seek_to(2+test_value*2);break;} /* jumps that come faster than the server answers */
   if(waited<40)break;
   {char b[100];snprintf(b,sizeof(b),"burst of jumps: %d streams open at most",maxr);report(b);}
   check("a burst of jumps never keeps more than 2 streams open",maxr<=2);
   NEXT_STEP();break;
  }
  case 76:
   if(!STARTED())break;
   check("after the burst the episode plays from the last jump",state==2 && !error_code && elapsed>=17);
   /* podcast skips: L back 15 s, R forward 30 s; on a stream the presses add up into one jump */
   {int at=elapsed,want=at-15<0?0:at-15;
    input(PSP_CTRL_LTRIGGER);check("L skips back 15 s on an episode",seek_preview==want && stream_seek_at);
    np_focus=3;input(PSP_CTRL_CROSS);want=want+30>29?29:want+30;check("the Next key on Now Playing skips forward 30 s",seek_preview==want);
    np_focus=1;input(PSP_CTRL_CROSS);want=want-15<0?0:want-15;check("the Previous key skips back 15 s",seek_preview==want);
    test_value=want;}
   NEXT_STEP();break;
  case 77:if(waited==4)capture("ms0:/podcasts-skip.rgba");if(waited<100 || !STARTED())break;
   {char b[80];snprintf(b,sizeof(b),"skips: wanted %d, at %d",test_value,elapsed);report(b);}
   check("the skips land as one jump, where they added up to",state==2 && !error_code && elapsed>=test_value && elapsed<=test_value+3);
   request(NULL);seek_step_index=1;
   /* a song with no lyrics of its own: found by LRCLIB's search (its exact lookup says 404) and kept as a .lrc */
   online_lyrics=2;sceIoRemove("ms0:/NETTEST/Net Song.lrc");
   {static Paths p;paths_clear(&p);paths_add(&p,"ms0:/NETTEST/Net Song.mp3");queue_start_paths(&p,0);}screen=SCR_NOW;
   step=19;step_at=test_frame;break;
  case 19:
   if(lyr_state!=2 && lyr_state<5 && waited<1500)break;
   {char b[160];snprintf(b,sizeof(b),"song lyrics: state %d lines %d synced %d first '%s' msg %s",lyr_state,lyr.n,lyr.synced,lyr.n?lyr.line[0].text:"",lj_msg);report(b);}
   check("a song without lyrics gets synced lyrics from LRCLIB",lyr_state==2 && lyr.synced && !strcmp(lyr.line[0].text,"First line of Net Song"));
   {char b[80];snprintf(b,sizeof(b),"song decoded peak %d (station %d), boost %d",test_raw_peak,station_peak,boost_src);report(b);}
   /* both tones were made at 1/8 of full scale (4096); the song's mono tone was spread over two channels (-3 dB, 2896) */
   check("the radio decoder plays at the stream's own level, no quieter",station_peak>=3600 && station_peak<=4600);
   check("and the music decoder at the file's",test_raw_peak>=2500 && test_raw_peak<=3300);
   check("a song gets no volume boost",boost_src==0);
   {int fd=sceIoOpen("ms0:/NETTEST/Net Song.lrc",PSP_O_RDONLY,0);check("and keeps them as a .lrc beside the song",fd>=0);if(fd>=0)sceIoClose(fd);}
   /* run-test.ps1 -Online: real HTTPS through BearSSL (lrclib.net, and an https SomaFM stream) */
   {int fd=sceIoOpen("ms0:/NETTEST/online.txt",PSP_O_RDONLY,0);if(fd>=0){sceIoClose(fd);
    unsigned long long t0=sceKernelGetSystemTimeWide();char *body=NULL;int st=0;
    int len=http_get("https://lrclib.net/api/get?artist_name=Daft%20Punk&track_name=One%20More%20Time&album_name=Discovery&duration=320",&body,256*1024,&st);
    {char b[200];snprintf(b,sizeof(b),"lrclib.net over HTTPS: %d bytes, status %d, BearSSL error %d, %u ms",len,st,net_tls_error,(unsigned int)((sceKernelGetSystemTimeWide()-t0)/1000));report(b);}
    check("real lrclib.net answers over HTTPS (BearSSL, certificate checked)",len>0 && st==200 && body && strstr(body,"\"syncedLyrics\":\"["));free(body);
    static Http h;t0=sceKernelGetSystemTimeWide();int r=http_open(&h,"https://ice1.somafm.com/groovesalad-128-mp3",NULL,10000),got=0;
    if(r>0 && h.status==200){static unsigned char sb[4096];while(got<64*1024){int g=http_read(&h,sb,sizeof(sb),10000);if(g<=0)break;got+=g;}}
    {char b[200];snprintf(b,sizeof(b),"https stream: open %d status %d type '%s' %d bytes in %u ms",r,h.status,h.content_type,got,(unsigned int)((sceKernelGetSystemTimeWide()-t0)/1000));report(b);}
    http_close(&h);check("a real HTTPS radio stream reads",got>=64*1024);
    /* 91X: its host sends no intermediate certificate (GlobalSign AlphaSSL 2025 is in trust_anchors.h for it) */
    t0=sceKernelGetSystemTimeWide();got=0;r=http_open(&h,"https://lweb04.lcaat.ca:8443/91x.mp3",NULL,10000);
    if(r>0 && h.status==200){static unsigned char sb[4096];while(got<64*1024){int g=http_read(&h,sb,sizeof(sb),10000);if(g<=0)break;got+=g;}}
    {char b[200];snprintf(b,sizeof(b),"91X over https: open %d status %d BearSSL error %d, %d bytes in %u ms",r,h.status,net_tls_error,got,(unsigned int)((sceKernelGetSystemTimeWide()-t0)/1000));report(b);}
    http_close(&h);check("an HTTPS stream whose server leaves out its intermediate certificate reads",got>=64*1024);
   }}
   wifi_config=2;online_lyrics=0;save_settings();wifi_config=0;online_lyrics=1;load_settings();
   check("the Wi-Fi network and Online lyrics settings persist",wifi_config==2 && online_lyrics==0);
   wifi_config=0;online_lyrics=1;save_settings();
   open_settings();set_item=set_row(25);NEXT_STEP();break;
  case 20:if(waited<20)break;capture("ms0:/radio-settings.rgba");after=4;step_at=test_frame;break;
  case 21:if(waited<30)break;
   {static int switched;int fd=sceIoOpen("ms0:/NETTEST/online.txt",PSP_O_RDONLY,0);if(fd>=0)sceIoClose(fd);if(fd>=0 && !switched){switched=1;step=80;step_at=test_frame;break;}}
   {const char *sl=strrchr(startup_path,'/');char lp[PATHLEN];snprintf(lp,sizeof(lp),"%.*sstream-log.txt",sl?(int)(sl-startup_path+1):0,startup_path);
    FILE *f=fopen(lp,"r");int opens=0,plays=0;char line[400];if(f){while(fgets(line,sizeof(line),f)){if(strstr(line,"open "))opens++;if(strstr(line,"playing: "))plays++;}fclose(f);}
    {char b[120];snprintf(b,sizeof(b),"stream log: %d opens, %d playing",opens,plays);report(b);}
    check("stream-log.txt records each stream opening and playing",opens>=5 && plays>=5);}
   report("net complete");running=0;break;
  /* -Online: switching between real HTTPS stations and episodes (a real PSP once stuck on Connecting for the second) */
  case 80:{static Paths p;paths_clear(&p);
   paths_add(&p,"https://ice1.somafm.com/groovesalad-128-mp3#Groove Salad");paths_add(&p,"https://ice2.somafm.com/dronezone-128-mp3#Drone Zone");
   paths_add(&p,"https://www.soundhelix.com/examples/mp3/SoundHelix-Song-1.mp3#@372,0||Helix One");paths_add(&p,"https://www.soundhelix.com/examples/mp3/SoundHelix-Song-2.mp3#@425,0||Helix Two");
   paths_add(&p,"https://ice1.somafm.com/groovesalad-128-mp3#Groove Salad");
   queue_start_paths(&p,0);screen=SCR_NOW;NEXT_STEP();break;}
  case 81:case 82:case 83:case 84:case 85:
   if(!STARTED())break;
   if(waited<240 && !error_code)break; /* let it play a few seconds */
   {char b[240];snprintf(b,sizeof(b),"switch %d: state %d code %08X phase %d readers %d elapsed %d after %d frames: %s",step-80,state,(unsigned int)error_code,radio_status.phase,radio_readers_alive(),elapsed,waited,active);report(b);}
   {char b[80];snprintf(b,sizeof(b),"real stream %d plays after switching",step-80);check(b,state==2 && !error_code && elapsed>=2);}
   if(step==85){step=21;step_at=test_frame;break;}
   queue_next(0,0);NEXT_STEP();break;
  }
#endif
#ifdef ZOID_BOOKS_TEST
  books_test_frame(test_frame);
#endif
#endif
  SceCtrlData pad;sceCtrlPeekBufferPositive(&pad,1);unsigned int buttons=pad.Buttons;
  if(pad.Ly<48)buttons|=PSP_CTRL_UP;
  if(pad.Ly>208)buttons|=PSP_CTRL_DOWN;
  if(pad.Lx<48)buttons|=PSP_CTRL_LEFT;
  if(pad.Lx>208)buttons|=PSP_CTRL_RIGHT;
  unsigned int press=buttons&~prior;
  /* X on the library or Now Playing acts when let go; held for 0.45 s it opens the item menu instead */
  {static int x_frames,x_wait;
   if(press&PSP_CTRL_CROSS){x_frames=0;x_wait=!menu_open && !chooser && !osk_active && (screen==SCR_LIBRARY || screen==SCR_NOW);if(x_wait)press&=~PSP_CTRL_CROSS;}
   else if(x_wait && (buttons&PSP_CTRL_CROSS)){if(++x_frames>=hold_frames[hold_idx]){x_wait=0;if(menu_allowed())open_item_menu();}}
   else if(x_wait){x_wait=0;press|=PSP_CTRL_CROSS;}
  }
  unsigned int repeat_keys=PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT;
  unsigned int directions=buttons&repeat_keys;
  /* Held directions repeat after 0.4 s, then 15 times a second. */
  if(directions && directions==(prior&repeat_keys)){held++;if(held>24 && held%4==0)press|=directions;}else held=0;
  prior=buttons;
  /* sleep timer: pause when it runs out */
  if(sleep_until && now_ms>=sleep_until){sleep_until=0;sleep_idx=0;if(is_playing())toggle_play();show_toast(-2,"Sleep timer: paused");}
  /* auto screen off after a stretch with no buttons */
  {static unsigned long long last_touch;if(buttons || !last_touch || osk_active || menu_open)last_touch=now_ms;
#ifndef ZOID_SMOKE_TEST
   /* while a book is open a page can take a while to read: never sooner than 5 minutes */
   {unsigned long long idle=auto_off_secs[auto_off_idx]*1000ULL;if(screen==SCR_BOOKS && bk_mode==1 && idle<300000)idle=300000;
    if(auto_off_idx && !screen_off && now_ms-last_touch>idle)screen_off=1;}
#endif
  }
  /* woke from sleep: file handles are stale, so reopen the song where it was */
  if(power_resumed){power_resumed=0;if(active[0] && state==2){int at=elapsed;engine_drop_prepared();seek_to(at);}}
  /* screen off (HOLD switch, a mapped button or Settings): no drawing, 222 MHz, any button wakes */
  {static int was_off;int off=screen_off || hold_on || (buttons&PSP_CTRL_HOLD)!=0;
   if(off && !was_off){set_clock(0);frame_begin(C(0,255));frame_end();}
   if(!off && was_off)set_clock(cpu_fast);
   was_off=off;
   if(off) {
#ifndef ZOID_SMOKE_TEST
    if(screen_off && !hold_on && !(buttons&PSP_CTRL_HOLD) && (press&~PSP_CTRL_HOLD))screen_off=0;
#endif
    resume_tick();np_refresh();sceKernelDelayThread(50000);continue;
   }
  }
  if(volume_poll() && screen==SCR_VIDEO)vid.new_pic=1;
  note_poll(); /* the note button: next theme */
  /* a video owns the screen: it decodes, draws only when needed and reads its own keys */
  if(screen==SCR_VIDEO) {
#ifndef ZOID_SMOKE_TEST
   video_frame(press);
#else
   video_frame(0);sceKernelDelayThread(10000);
#endif
   np_refresh();resume_tick();continue;
  }
  /* Books draws in its own Manga Paper look and reads its own keys (the stick scrolls pages) */
  if(screen==SCR_BOOKS) {
#ifndef ZOID_SMOKE_TEST
   books_frame(press,pad.Buttons,&pad);
#else
   books_frame(0,0,NULL);sceKernelDelayThread(10000);
#endif
   np_refresh();resume_tick();continue;
  }
#ifndef ZOID_SMOKE_TEST
  input(press);
#else
  (void)press; /* Scripted tests must not consume unrelated desktop keyboard input. */
#endif
  if(stream_seek_at){if(now_ms>=stream_seek_at || !radio_is_episode(active)){stream_seek_at=0;if(radio_is_episode(active))seek_to(seek_preview);else seek_preview=-1;}}
  else if(seek_preview>=0 && seek_ack==serial)seek_preview=-1;
  if(seek_check_at && now_ms>=seek_check_at){seek_check_at=0;seek_log("1.5 s later",0,0);}
  np_refresh();resume_tick();
  spectrum_update();
  draw();
#ifdef ZOID_SMOKE_TEST
  sceKernelDelayThread(10000); /* keeps scripted frame timing the same as earlier test builds */
#endif
 }
 if(!video_active)save_resume();
 books_shutdown();
 if(thread>=0)sceKernelWaitThreadEnd(thread,NULL);
 sceGuTerm();
 sceKernelDeleteSema(queue_lock);sceKernelDeleteSema(command_lock);sceKernelExitGame();return 0;
}
