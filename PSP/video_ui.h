/* ---- video screen ----
   Battery first: the Media Engine decodes and colour-converts, the CPU drops to 222 MHz, and a
   frame is drawn only when a new picture is due or the overlay changes; otherwise the loop just
   waits for the next vblank. The picture follows the sound's clock (clock_ms) so they stay in
   sync; a video without sound runs on the system timer. Music stops while a video plays and
   comes back where it was afterwards. */

static struct {
 VideoInfo info;char path[PATHLEN],title[128],sound_path[PATHLEN];
 int has_audio,zoom,own_paused,ended,new_pic,pics_shown,last_second,logged,sub_shown,heartbeat,beats,bad_frames,loops,vol_drawn,sound_lost,jump_pending,jump_target;unsigned long long jump_at;unsigned int decode_us,draw_us;
 unsigned long long own_start_us,own_paused_us;int own_base_ms;
 unsigned long long overlay_until,last_pass;int overlay_drawn;
 char music_path[PATHLEN];int music_pos,music_playing,music_screen;
} vid;

static int video_clock(void) {
 if(vid.has_audio)return state==2?(int)clock_ms:vid.own_base_ms;
 if(vid.own_paused)return vid.own_base_ms;
 return vid.own_base_ms+(int)((sceKernelGetSystemTimeWide()-vid.own_start_us)/1000);
}
static int video_paused(void){return vid.has_audio?paused:vid.own_paused;}
static void video_overlay(void){vid.overlay_until=now_ms+3000;vid.new_pic=1;}

/* Subtitles: "<video name>.srt" beside the video. Text is folded to the ASCII font like tags.
   A line is drawn only when the cue changes, so subtitles cost next to nothing. */
typedef struct {int a,b;char text[200];} SubCue;
static SubCue *subs;static int sub_n,subs_on=1;
static void subs_free(void){free(subs);subs=NULL;sub_n=0;}
static int srt_ms(const char *s){int h,m,sec,ms;if(sscanf(s,"%d:%d:%d%*[,.]%d",&h,&m,&sec,&ms)!=4)return -1;return ((h*60+m)*60+sec)*1000+ms;}
/* convert-for-psp.ps1 keeps a video's extras inside its MP4, in top-level 'uuid' boxes that other
   players skip: the MP3 soundtrack (the PSP's AAC decoder gave silence for film sound) and the
   subtitles. So a video is one .mp4; a .mp3 or .srt beside it still works too. */
#define ZOID_SOUND_UUID "ZOID-MP3-SOUND01"
#define ZOID_SUBS_UUID  "ZOID-SRT-SUBS-01"
static int find_zoid_box(const char *path,const char *uuid,unsigned int *off,unsigned int *len) {
 int fd=sceIoOpen(path,PSP_O_RDONLY,0);if(fd<0)return 0;
 unsigned int end=sceIoLseek32(fd,0,PSP_SEEK_END),pos=0;int found=0;unsigned char h[32];
 for(int guard=0;guard<64 && pos+8<=end && !found;guard++) {
  if(sceIoLseek32(fd,pos,PSP_SEEK_SET)<0 || sceIoRead(fd,h,32)<24)break;
  unsigned int sz=(h[0]<<24)|(h[1]<<16)|(h[2]<<8)|h[3],hdr=8;
  if(sz==1){sz=(h[12]<<24)|(h[13]<<16)|(h[14]<<8)|h[15];hdr=16;}else if(sz==0)sz=end-pos;
  if(sz<hdr || pos+sz>end)break;
  if(!memcmp(h+4,"uuid",4) && sz>=hdr+16 && !memcmp(h+hdr,uuid,16)){*off=pos+hdr+16;*len=sz-hdr-16;found=1;}
  pos+=sz;
 }
 sceIoClose(fd);return found;
}
/* The MP4's own subtitle track, as HandBrake and ffmpeg write it (tx3g): each sample is a 2-byte
   length and UTF-8 text, shown for that sample's duration (stts); empty samples are the gaps. */
static void subs_load_track(const char *video) {
 SceUID fd=sceIoOpen(video,PSP_O_RDONLY,0);if(fd<0)return;
 Mp4Track t;unsigned int *off=NULL,*size=NULL,*dur=NULL;unsigned char *stts=NULL;int n=0;
 if(!mp4_find_track(fd,"sbtl",&t) && !mp4_find_track(fd,"text",&t))goto done;
 if(!t.timescale)goto done;
 unsigned int sl,so=mp4_find_box(fd,t.stbl,t.stbl+t.stbl_len,"stts",&sl);
 if(!so || sl<8 || sl>512*1024)goto done;
 n=mp4_sample_table(fd,&t,&off,&size);if(n<=0 || n>20000)goto done;
 stts=malloc(sl);dur=calloc(n,sizeof(unsigned int));if(!stts || !dur || !mp4_rd(fd,so,stts,sl))goto done;
 {unsigned int entries=mp4_be32(stts+4);int k=0;
  for(unsigned int e=0;e<entries && 16+e*8<=sl && k<n;e++){unsigned int c=mp4_be32(stts+8+e*8),d=mp4_be32(stts+12+e*8);while(c-- && k<n)dur[k++]=d;}}
 int cues=0;for(int i=0;i<n;i++)if(size[i]>2)cues++;
 subs=cues?malloc(cues*sizeof(SubCue)):NULL;if(!subs)goto done;
 {unsigned long long at=0;
  for(int i=0;i<n && sub_n<cues;i++) {
   unsigned long long a=at;at+=dur[i];
   if(size[i]<=2)continue;
   unsigned char b[400];int got=size[i]<sizeof(b)-1?(int)size[i]:(int)sizeof(b)-1;
   if(!mp4_rd(fd,off[i],b+1,got))continue;
   int tl=(b[1]<<8)|b[2];if(tl>got-2)tl=got-2;if(tl<=0)continue;
   memmove(b+1,b+3,tl);b[0]=3; /* UTF-8, for tag_decode */
   SubCue *c=&subs[sub_n];tag_decode(b,tl+1,c->text,sizeof(c->text),1);
   if(c->text[0]){c->a=(int)(a*1000/t.timescale);c->b=(int)(at*1000/t.timescale);if(c->b>c->a)sub_n++;}
  }}
 if(!sub_n)subs_free();
 done:
 free(off);free(size);free(dur);free(stts);sceIoClose(fd);
}
static void subs_load(const char *video) {
 subs_free();
 /* ZOID's box inside the MP4 first, then "<video>.srt" beside it, then the MP4's own subtitle track */
 unsigned int boff=0,blen=0;int fd,len;
 if(find_zoid_box(video,ZOID_SUBS_UUID,&boff,&blen)){fd=sceIoOpen(video,PSP_O_RDONLY,0);if(fd<0)return;len=(int)blen;sceIoLseek32(fd,boff,PSP_SEEK_SET);}
 else{char path[PATHLEN];snprintf(path,sizeof(path),"%s",video);char *dot=strrchr(path,'.');if(!dot || strlen(path)+4>=sizeof(path))return;strcpy(dot,".srt");
  fd=sceIoOpen(path,PSP_O_RDONLY,0);if(fd<0){subs_load_track(video);return;}len=sceIoLseek32(fd,0,PSP_SEEK_END);sceIoLseek32(fd,0,PSP_SEEK_SET);}
 char *buf=len>0 && len<4*1024*1024?malloc(len+1):NULL;
 if(!buf || sceIoRead(fd,buf,len)!=len){free(buf);sceIoClose(fd);return;}
 sceIoClose(fd);buf[len]=0;
 int cap=0;for(int i=0;i<len;i++)if(buf[i]=='-' && buf[i+1]=='-' && buf[i+2]=='>')cap++;
 subs=cap?malloc(cap*sizeof(SubCue)):NULL;if(!subs){free(buf);return;}
 char *p=buf;if((unsigned char)p[0]==0xEF && (unsigned char)p[1]==0xBB && (unsigned char)p[2]==0xBF)p+=3; /* UTF-8 mark */
 while(*p && sub_n<cap) {
  char *arrow=strstr(p,"-->");if(!arrow)break;
  char *ls=arrow;while(ls>p && ls[-1]!='\n')ls--;
  int a=srt_ms(ls),b=srt_ms(arrow+3+strspn(arrow+3," "));
  char *t=strchr(arrow,'\n');if(!t)break;t++;
  /* text runs to the next blank line */
  unsigned char raw[400];int n=0;raw[n++]=3; /* UTF-8, for tag_decode */
  while(*t && *t!='\r' && *t!='\n') {
   char *e=strchr(t,'\n');int l=e?(int)(e-t):(int)strlen(t);if(l && t[l-1]=='\r')l--;
   for(int i=0;i<l && n<(int)sizeof(raw)-2;i++){if(t[i]=='<'){while(i<l && t[i]!='>')i++;continue;}raw[n++]=t[i];} /* drop <i> and friends */
   if(n<(int)sizeof(raw)-2)raw[n++]='\n';
   t=e?e+1:t+l;if(!e)break;
  }
  if(a>=0 && b>a){SubCue *c=&subs[sub_n];tag_decode(raw,n,c->text,sizeof(c->text),1);if(c->text[0]){c->a=a;c->b=b;sub_n++;}}
  p=t;
 }
 free(buf);
}
static int subs_at(int ms){static int last;if(!sub_n)return -1;if(last<0 || last>=sub_n || subs[last].a>ms)last=0;while(last<sub_n && subs[last].b<=ms)last++;return last<sub_n && subs[last].a<=ms?last:-1;}
/* Word-wrapped to 450 px, centred, bottom-up from `bottom`. */
static void draw_subtitle(int cue,float bottom) {
 if(cue<0 || cue>=sub_n)return;
 char lines[6][120];int nl=0;const char *s=subs[cue].text;
 while(*s && nl<6) {
  int take=0,best=0;
  for(;s[take] && s[take]!='\n' && take<119;take++){char tmp[120];memcpy(tmp,s,take+1);tmp[take+1]=0;if(text_w(tmp,F_MEDIUM)>450)break;if(s[take+1]==' ' || s[take+1]=='\n' || !s[take+1])best=take+1;}
  if(s[take]=='\n' || !s[take] || !best)best=take?take:1;
  memcpy(lines[nl],s,best);lines[nl][best]=0;nl++;s+=best;while(*s==' ' || *s=='\n')s++;
 }
 float lh=fonts[F_MEDIUM].ch+2,y=bottom-nl*lh;
 for(int i=0;i<nl;i++,y+=lh){int w=text_w(lines[i],F_MEDIUM);rect(240-w/2-5,y,w+10,lh,C(0,150));text(240-w/2,y,lines[i],C(0xffffff,255),F_MEDIUM);}
}
static void video_stop(void) {
 if(screen!=SCR_VIDEO)return;
 video_close();subs_free();video_active=0;video_log_flush();emb_path[0]=0;
 request(NULL);
 if(vid.music_path[0]){next_request_seek=vid.music_pos;request(vid.music_path);if(!vid.music_playing)paused=1;}
 screen=vid.music_screen==SCR_NOW?SCR_NOW:SCR_LIBRARY;
 set_clock(cpu_fast);
}

/* video.log next to the EBOOT: each start-up step and the first decodes, for diagnosing hardware */
static char video_log_path[PATHLEN];
/* Lines collect in memory and go to the Memory Stick every 10 s and when the video closes:
   writing each line as it came stalled playback on a real PSP. */
static char vlog_buf[16384];static int vlog_len;
static void video_log_flush(void){if(!vlog_len || !video_log_path[0])return;int fd=sceIoOpen(video_log_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0777);if(fd>=0){sceIoWrite(fd,vlog_buf,vlog_len);sceIoClose(fd);}vlog_len=0;}
static void video_log_line(const char *line){int n=strlen(line);if(vlog_len+n+1>(int)sizeof(vlog_buf))video_log_flush();if(n+1>(int)sizeof(vlog_buf))return;memcpy(vlog_buf+vlog_len,line,n);vlog_len+=n;vlog_buf[vlog_len++]='\n';
 /* the first lines of each video go straight to the stick, so a freeze while starting still leaves a trail */
 {static int early;if(!strncmp(line,"ms0:",4))early=0;if(early<150){early++;video_log_flush();}}}
static void video_start(const char *path) {
 if(video_log_path[0]){int fd=sceIoOpen(video_log_path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(fd>=0)sceIoClose(fd);video_log=video_log_line;video_log_line(path);}
 {char b[120];snprintf(b,sizeof(b),"before opening: heap free %u KB, system free %u KB (largest %u KB)",(unsigned int)(mallinfo().fordblks/1024),(unsigned int)(sceKernelTotalFreeMemSize()/1024),(unsigned int)(sceKernelMaxFreeMemSize()/1024));video_log_line(b);}
 int r=video_open(path,&vid.info);
 if(r!=VIDEO_OK){show_toast(-2,video_error_text(r));toast_until=now_ms+5000;return;} /* long enough to read */
 /* remember the music, then hand the sound engine to the video */
 vid.music_path[0]=0;
 if(active[0] && state){snprintf(vid.music_path,sizeof(vid.music_path),"%s",active);vid.music_pos=elapsed;vid.music_playing=!paused;}
 vid.music_screen=screen;
 snprintf(vid.path,sizeof(vid.path),"%s",path);
 const char *b=strrchr(path,'/');snprintf(vid.title,sizeof(vid.title),"%s",b?b+1:path);char *dot=strrchr(vid.title,'.');if(dot)*dot=0;
 vid.has_audio=vid.info.has_audio;vid.own_paused=0;vid.ended=0;vid.pics_shown=0;vid.last_second=-1;vid.logged=0;vid.heartbeat=0;vid.beats=0;vid.sound_lost=0;vid.jump_pending=0;vid.bad_frames=0;vid.sub_shown=-1;vid.last_pass=0;subs_load(path);
 vid.own_base_ms=0;vid.own_start_us=sceKernelGetSystemTimeWide();
 video_active=1;video_audio_ended=0;clock_ms=0;
 /* Sound: "<video>.mp3" beside it plays through the hardware MP3 path that music uses; the
    PSP's AAC decoder returned silence for film soundtracks on a real PSP. Without one, the file's
    own AAC track is tried. */
 emb_path[0]=0;
 unsigned int soff,slen;
 if(find_zoid_box(path,ZOID_SOUND_UUID,&soff,&slen) && slen>1024) {
  /* the MP3 soundtrack inside the MP4: skip its ID3 tag, then the engine plays that byte range */
  int fd=sceIoOpen(path,PSP_O_RDONLY,0);unsigned char h[10];
  if(fd>=0){if(sceIoLseek32(fd,soff,PSP_SEEK_SET)>=0 && sceIoRead(fd,h,10)==10 && !memcmp(h,"ID3",3)){unsigned int t=10+((h[6]<<21)|(h[7]<<14)|(h[8]<<7)|h[9])+((h[5]&16)?10:0);if(t<slen){soff+=t;slen-=t;}}sceIoClose(fd);}
  emb_start=(int)soff;emb_end=(int)(soff+slen);snprintf(emb_path,sizeof(emb_path),"%s",path);
  vid.has_audio=1;snprintf(vid.sound_path,sizeof(vid.sound_path),"%s",path);video_log_line("sound: MP3 inside the MP4");
 }
 else {char mp3p[PATHLEN];snprintf(mp3p,sizeof(mp3p),"%s",path);char *d=strrchr(mp3p,'.');
  if(d && strlen(mp3p)+4<sizeof(mp3p)){strcpy(d,".mp3");int fd=sceIoOpen(mp3p,PSP_O_RDONLY,0);if(fd>=0){sceIoClose(fd);vid.has_audio=1;snprintf(vid.sound_path,sizeof(vid.sound_path),"%s",mp3p);}else snprintf(vid.sound_path,sizeof(vid.sound_path),"%s",path);}
  else snprintf(vid.sound_path,sizeof(vid.sound_path),"%s",path);
  video_log_line(vid.sound_path);}
 if(vid.has_audio)request(vid.sound_path);else request(NULL);
 screen=SCR_VIDEO;set_clock(0);video_overlay();
}

/* A jump: the picture moves to the key frame at or before the spot and shows it at once; the sound
   restarts there and the picture follows it. (Sound that failed at the last spot is tried again.) */
static void video_seek_to(int ms) {
 if(ms<0)ms=0;if(ms>vid.info.duration_ms-1000)ms=vid.info.duration_ms-1000;if(ms<0)ms=0;
 int k=video_seek(ms);vid.ended=0;video_audio_ended=0;
 {char b[64];snprintf(b,sizeof(b),"jump to %d ms (key frame at %d ms)",ms,k);video_log_line(b);}
 if(vid.has_audio || vid.sound_lost) {
  vid.has_audio=1;vid.sound_lost=0;
  clock_ms=k;vid.own_base_ms=k; /* until the sound is back the clock stands at the key frame, so it shows */
  int was=paused;next_request_seek=k/1000;request(vid.sound_path);paused=was;
 }
 else{vid.own_base_ms=k;vid.own_start_us=sceKernelGetSystemTimeWide();}
 video_overlay();
}
/* Left/Right and L/R add up while pressed (held, they repeat); the jump happens once they stop,
   so scrubbing restarts the sound once, not at every press. The overlay shows where it will land. */
static void video_jump(int delta) {
 int t=(vid.jump_pending?vid.jump_target:video_clock())+delta;
 if(t<0)t=0;if(t>vid.info.duration_ms-1000)t=vid.info.duration_ms-1000;if(t<0)t=0;
 vid.jump_target=t;vid.jump_pending=1;vid.jump_at=now_ms+450;video_overlay();
}
static void video_jump_now(void){if(vid.jump_pending){vid.jump_pending=0;video_seek_to(vid.jump_target);}}
/* The sound could not restart at this spot: the picture carries on silently on the system timer,
   and X (or another jump) tries the sound again. */
static void video_sound_lost(void) {
 int at=video_next_ms();
 vid.has_audio=0;vid.sound_lost=1;vid.own_paused=0;vid.own_base_ms=at;vid.own_start_us=sceKernelGetSystemTimeWide();
 char b[80];snprintf(b,sizeof(b),"sound lost at %d ms (stage %d code %08X): picture goes on",at,stage,(unsigned int)error_code);video_log_line(b);
 show_toast(-2,"No sound at this spot - press X to retry");
}

static void video_toggle_pause(void) {
 if(vid.jump_pending){video_jump_now();if(paused){paused=0;vid.own_paused=0;}video_overlay();return;} /* X during a jump: go there and play */
 /* the sound stopped (lost at a jump, an error, or it ran out): start it again where the picture is */
 if(vid.sound_lost){int at=video_clock();vid.sound_lost=0;vid.has_audio=1;clock_ms=at;vid.own_base_ms=at;next_request_seek=at/1000;request(vid.sound_path);}
 else if(vid.has_audio && !state){next_request_seek=video_next_ms()/1000;request(vid.sound_path);}
 else if(vid.has_audio)toggle_play();
 else if(vid.own_paused){vid.own_paused=0;vid.own_start_us=sceKernelGetSystemTimeWide();}
 else{vid.own_base_ms=video_clock();vid.own_paused=1;}
 video_overlay();
}

static void video_key(unsigned int press) {
 if(!press)return;
 if(press&PSP_CTRL_CIRCLE){video_stop();return;}
 if(press&(PSP_CTRL_CROSS|PSP_CTRL_START))video_toggle_pause();
 if(press&PSP_CTRL_LEFT)video_jump(-10000);
 if(press&PSP_CTRL_RIGHT)video_jump(10000);
 if(press&PSP_CTRL_LTRIGGER)video_jump(-60000);
 if(press&PSP_CTRL_RTRIGGER)video_jump(60000);
 if(press&PSP_CTRL_SQUARE){subs_on^=1;show_toast(-2,!sub_n?"No subtitles (.srt) for this video":subs_on?"Subtitles on":"Subtitles off");}
 if(press&PSP_CTRL_TRIANGLE){vid.zoom^=1;show_toast(-2,vid.zoom?"Zoom: fill the screen":"Zoom: whole picture");}
 video_overlay();
}

/* The picture, letterboxed (whole picture) or cropped to fill the screen. Textures are at most
   512 wide, so a wider video is drawn as two strips. */
static void video_picture(void) {
 int w=vid.info.width,h=vid.info.height;
 float sx=480.0f/w,sy=272.0f/h,s=vid.zoom?(sx>sy?sx:sy):(sx<sy?sx:sy);
 float dw=w*s,dh=h*s,x0=(480-dw)/2,y0=(272-dh)/2;
 /* 64-pixel strips: one wide textured sprite thrashes the GPU's small texture cache (on a real PSP a
    480x272 picture took ~84 ms to draw that way). No smoothing when the picture isn't scaled. */
 sceGuDisable(GU_BLEND);linear(s!=1.0f);sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGB);
 for(int left=0;left<w;left+=64) {
  int sw=w-left<64?w-left:64;
  sceGuTexImage(0,64,512,video_stride,video_pixels+left);sceGuTexFlush();
  sprite(0,0,sw,h,x0+left*s,y0,sw*s,dh,0xffffffff);
 }
 sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);linear(0);sceGuEnable(GU_BLEND);sceGuTexImage(0,512,512,512,atlas);sceGuTexFlush();
}

/* Control hints on the video overlay: the real face-button shapes, arrows for Left/Right and
   square key caps for L and R, in white over the dark band. Each hint is measured first and the
   spare width is shared out evenly, so nothing crowds or overlaps. */
enum {VH_ICON,VH_ARROWS,VH_KEYS};
typedef struct {int kind,ic;const char *label;} VHint;
static const VHint vhints[]={{VH_ICON,IC_BTN_CROSS,"Pause"},{VH_ARROWS,0,"10 s"},{VH_KEYS,0,"1 min"},
 {VH_ICON,IC_BTN_TRIANGLE,"Zoom"},{VH_ICON,IC_BTN_SQUARE,"Subtitles"},{VH_ICON,IC_BTN_CIRCLE,"Back"}};
static float vhint_glyphs(const VHint *h){return h->kind==VH_ICON?14:h->kind==VH_ARROWS?26:text_w("L",F_SMALL)+text_w("R",F_SMALL)+8+8+4;}
static void draw_video_hints(float y) {
 int n=sizeof(vhints)/sizeof(vhints[0]);float used=0;
 for(int i=0;i<n;i++)used+=vhint_glyphs(&vhints[i])+5+text_w(vhints[i].label,F_SMALL);
 float gap=(456-used)/(n-1);if(gap<8)gap=8;
 float x=12;unsigned int ink=C(0xffffff,230);
 for(int i=0;i<n;i++) {
  const VHint *h=&vhints[i];
  if(h->kind==VH_ICON)icon(h->ic,x+7,y,14,ink);
  else if(h->kind==VH_ARROWS){icon(IC_CHEV_L,x+6,y,12,ink);icon(IC_CHEV_R,x+20,y,12,ink);}
  else {float kx=x;const char *keys[2]={"L","R"};
   for(int k=0;k<2;k++){int w=text_w(keys[k],F_SMALL)+8;rect(kx,y-8,w,16,C(0xffffff,60));text_vc(kx+4,y,keys[k],ink,F_SMALL);kx+=w+4;}}
  x+=vhint_glyphs(h)+5;
  x+=text_vc(x,y,h->label,C(0xffffff,170),F_SMALL)+gap;
 }
}
static void draw_video_overlay(void) {
 int pos=(vid.jump_pending?vid.jump_target:video_clock())/1000,dur=vid.info.duration_ms/1000;
 grad(0,0,480,40,C(0,200),C(0,0));grad(0,212,480,60,C(0,0),C(0,220));
 text_fit(14,8,vid.title,C(0xffffff,255),F_MEDIUM,380);
 if(video_paused())text_right(466,10,"PAUSED",C(T->accent,255),F_SMALL);
 char a[16],b[16];time_text(a,sizeof(a),pos);time_text(b,sizeof(b),dur);
 float bx=60,bw=360,frac=dur>0?(float)pos/dur:0;if(frac>1)frac=1;
 text_right(bx-10,236,a,C(0xffffff,255),F_SMALL);text(bx+bw+10,236,b,C(0xffffff,255),F_SMALL);
 rect(bx,242,bw,5,C(0xffffff,70));rect(bx,242,bw*frac,5,C(T->accent,255));
 draw_video_hints(262);
}

/* One pass of the main loop while a video is on screen. */
static void video_frame(unsigned int press) {
 vid.loops++;
 /* for video.log: power events (sleep, HOLD) and any long freeze of the loop */
 {static int events_seen;unsigned long long t=sceKernelGetSystemTimeWide(),last_pass=vid.last_pass;vid.last_pass=t;
  if(power_events!=events_seen){events_seen=power_events;char b[64];snprintf(b,sizeof(b),"power event: flags %08X",(unsigned int)power_flags);video_log_line(b);}
  if(last_pass && t-last_pass>400000){char b[80];snprintf(b,sizeof(b),"loop froze for %u ms (audio %d)",(unsigned int)((t-last_pass)/1000),state);video_log_line(b);}}
 video_key(press);
 if(screen!=SCR_VIDEO)return;
 {static int notes_seen;while(notes_seen<audio_notes){video_log_line(audio_notes_buf[notes_seen&7]);notes_seen++;}}
 if(vid.jump_pending && now_ms>=vid.jump_at)video_jump_now();
 if(vid.has_audio && !state && error_code && !vid.ended)video_sound_lost();
 int now=video_clock();
 /* no automatic jumps: decoding stays in order, at most 2 frames a refresh; if it falls far behind
    (the screen was off), it skips ahead to a key frame near the sound */
 if(now-video_next_ms()>5000 && !vid.ended){video_seek(now);if(video_log){char b[64];snprintf(b,sizeof(b),"skipped ahead to %d ms",now);video_log_line(b);}}
 {unsigned long long t0=sceKernelGetSystemTimeWide();
 for(int i=0;i<2 && !vid.ended && video_next_ms()<=now;i++) {
  int r=video_decode();
  if(r==VIDEO_END)vid.ended=1;
  else if(r==VIDEO_ERR_DECODE){if(++vid.bad_frames>=8){video_stop();show_toast(-2,video_error_text(VIDEO_ERR_DECODE));toast_until=now_ms+5000;return;}}
  else{vid.bad_frames=0;if(r==1){vid.new_pic=1;vid.pics_shown++;}}
 }
 vid.decode_us+=(unsigned int)(sceKernelGetSystemTimeWide()-t0);}
 /* once a second: where the time goes (decode, drawing, the rest of the loop), sound level, memory */
 {static unsigned int err_seen;if(error_code!=err_seen){err_seen=error_code;if(error_code){char b[64];snprintf(b,sizeof(b),"sound error: stage %d code %08X",stage,(unsigned int)error_code);video_log_line(b);}}}
 /* a line a second for the first 200 seconds watched (jumps included), saved every 10 */
 if(now/1000!=vid.heartbeat && vid.beats<200) {
  vid.beats++;
  static int loops_seen;static unsigned int wait_seen;int lp=vid.loops-loops_seen;loops_seen=vid.loops;unsigned int wt=me_wait_us-wait_seen;wait_seen=me_wait_us;
  vid.heartbeat=now/1000;
  char b[220];
  snprintf(b,sizeof(b),"alive %d s: frame %d, %d pictures, loop %d/s, decode %u ms, draw %u ms, ME wait %u ms, sound %d AAC frames peak %d, audio %d",
   vid.heartbeat,video_frame_index(),vid.pics_shown,lp,vid.decode_us/1000,vid.draw_us/1000,wt/1000,aac_frames,aac_peak,state);
  video_log_line(b);aac_frames=aac_peak=0;vid.decode_us=vid.draw_us=0;
  if(vid.beats%10==0){snprintf(b,sizeof(b),"  heap %u KB, system %u KB, largest frame %d, slowest decode %d us",(unsigned int)(mallinfo().fordblks/1024),(unsigned int)(sceKernelTotalFreeMemSize()/1024),video_largest_frame,video_slowest_us);video_log_line(b);video_log_flush();}
 }
 if(!vid.logged && now>3000){vid.logged=1;char b[120];unsigned int px=video_pixels?video_pixels[(vid.info.height/2)*video_stride+vid.info.width/2]:0;
  snprintf(b,sizeof(b),"after 3 s: %d pictures, clock %d ms, next %d ms, audio state %d code %08X, centre pixel %08X",vid.pics_shown,now,video_next_ms(),state,(unsigned int)error_code,px);video_log_line(b);
  if(!vid.pics_shown)show_toast(-2,"No picture from the video decoder - see video.log");}
 if(vid.ended && (!vid.has_audio || video_audio_ended || state==0 || error_code)){video_stop();return;}
 int overlay=now_ms<vid.overlay_until || video_paused();
 int cue=subs_on?subs_at(now):-1;if(cue!=vid.sub_shown){vid.sub_shown=cue;vid.new_pic=1;}
 if(overlay && now/1000!=vid.last_second){vid.last_second=now/1000;vid.new_pic=1;}
 if(overlay!=vid.overlay_drawn)vid.new_pic=1;
 if(volume_showing()!=vid.vol_drawn)vid.new_pic=1;
 int toast_on=toast_panel==-2 && now_ms<toast_until;
 if(!vid.new_pic && !toast_on){sceDisplayWaitVblankStartCB();return;}
 vid.new_pic=0;vid.overlay_drawn=overlay;vid.vol_drawn=volume_showing();
 unsigned long long td=sceKernelGetSystemTimeWide();
 frame_begin(C(0,255));
 video_picture();
 draw_subtitle(vid.sub_shown,overlay?222:264);
 if(overlay)draw_video_overlay();
 if(toast_on)draw_notice();
 draw_volume();
 frame_end();
 vid.draw_us+=(unsigned int)(sceKernelGetSystemTimeWide()-td);
}
