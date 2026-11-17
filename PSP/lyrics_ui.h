/* Full-screen lyrics, drawn in the current skin.
   Synced lyrics follow the song: the line being sung is centered, large and bright; sung lines
   fade above it. Plain lyrics are a page to scroll. Up/Down scroll (synced lyrics drift back
   to the song after 4 s), Left/Right seek, X play/pause, Circle or Triangle back. */
typedef struct {short line,start,len;} LyrRow;
#define LYR_ROWS 900
#define LYR_RH 27
static LyrRow lyr_rows[LYR_ROWS];static int lyr_nrows,lyr_first_row[LYR_MAX];
static unsigned int lyr_wrapped_gen=~0u;static int lyr_wrapped_theme=-1;
static int lyr_return=SCR_NOW,lyr_top,lyr_manual;static unsigned long long lyr_manual_until;static float lyr_scroll_f;
static void open_lyrics(void){if(lyr_state==6){lyr_state=3;lj_tried=~0u;} /* the network failed last time: ask again */
 if(screen!=SCR_LYRICS){lyr_return=screen;screen=SCR_LYRICS;lyr_manual=0;lyr_top=0;lyr_scroll_f=0;}}
/* Breaks each line into rows that fit the screen in the large font. */
static void lyrics_wrap(void) {
 lyr_wrapped_gen=lyr_gen;lyr_wrapped_theme=theme;lyr_nrows=0;
 const int maxw=440;
 for(int i=0;i<lyr.n;i++) {
  const char *s=lyr.line[i].text;int n=strlen(s),start=0;
  lyr_first_row[i]=lyr_nrows;
  if(!n){if(lyr_nrows<LYR_ROWS){lyr_rows[lyr_nrows].line=i;lyr_rows[lyr_nrows].start=0;lyr_rows[lyr_nrows].len=0;lyr_nrows++;}continue;}
  while(start<n && lyr_nrows<LYR_ROWS) {
   int w=0,end=start,brk=-1;
   while(end<n){int next=end;w+=adv_at(F_LARGE,s,&next);if(w>maxw)break;if(s[end]==' ')brk=end;end=next;}
   if(end<n && brk>start)end=brk;
   lyr_rows[lyr_nrows].line=i;lyr_rows[lyr_nrows].start=start;lyr_rows[lyr_nrows].len=end-start;lyr_nrows++;
   start=end;while(start<n && s[start]==' ')start++;
  }
 }
}
/* Playback position in ms, from the decoder, less a little for the output buffers. */
static int play_ms(void) {
 AudioTrack *t=&tracks[current_slot];
 if(row_live())return radio_song_ms(); /* a station: time since the song's title came */
 if(state==2 && t->rate){long long ms=(long long)(t->position-t->index.trim_start)*1000/t->rate-180;return ms<0?0:(int)ms;}
 return elapsed*1000;
}
static int lyrics_current(void) {
 if(!lyr.synced)return -1;
 int ms=play_ms(),cur=-1;for(int i=0;i<lyr.n && lyr.line[i].ms<=ms;i++)cur=i;return cur;
}
static void lyrics_input(unsigned int press) {
 if(press&(PSP_CTRL_CIRCLE|PSP_CTRL_TRIANGLE)){screen=lyr_return==SCR_LYRICS?SCR_NOW:lyr_return;return;}
 if(press&PSP_CTRL_CROSS)toggle_play();
 if(press&PSP_CTRL_LEFT)seek_by(-1);
 if(press&PSP_CTRL_RIGHT)seek_by(1);
 int d=(press&PSP_CTRL_DOWN?1:0)-(press&PSP_CTRL_UP?1:0);
 if(d) {
  if(lyr.synced){lyr_manual+=d;lyr_manual_until=now_ms+4000;}
  else{lyr_top+=d*2;if(lyr_top>lyr_nrows-4)lyr_top=lyr_nrows-4;if(lyr_top<0)lyr_top=0;}
 }
}
/* ---- skin colors ---- */
static unsigned int lyr_hot(void){return MODERN?T->fg:MS1?M1_DARK:MS?0xffffff:STUDIO?0xff3b2a:Y2K?0xffffff:T->hot;}
static unsigned int lyr_cold(void){return MODERN || PAPER?T->dim:MS?ms_dim():STUDIO?0x9a9a9a:Y2K?0xa8d8ea:CYBER?T->accent:T->accent;}
static void lyr_draw_row(int r,float y,int hot,int alpha) {
 const LyrRow *row=&lyr_rows[r];char s[LYR_LEN];
 int n=row->len<LYR_LEN-1?row->len:LYR_LEN-1;memcpy(s,lyr.line[row->line].text+row->start,n);s[n]=0;
 if(!MODERN && !Y2K){char up[LYR_LEN];upper(up,s,sizeof(up));memcpy(s,up,sizeof(up));}
 int f=hot?F_LARGE:F_MEDIUM;float x=240-text_w(s,f)/2.0f,ty=y-text_height(f)/2.0f;
 if(!hot){text(x,ty,s,C(lyr_cold(),alpha),f);return;}
 if(CYBER){text(x-1.5f,ty,s,C(T->second,150),f);text(x+1.5f,ty,s,C(T->accent,150),f);text(x,ty,s,C(T->hot,255),f);}
 else if(PHOS || Y2K)text_glow(x,ty,s,lyr_hot(),f,45);
 else if(STUDIO){glow(240,y,text_w(s,f)*0.6f,C(0xff2a1a,40));text(x,ty,s,C(0xff3b2a,255),f);}
 else if(PAPER)ITALIC(text(x,ty,s,C(PP_INK,255),f));
 else text(x,ty,s,C(T->fg,255),f);
}
static void draw_lyrics(void) {
 char buf[160];
 if(lyr_wrapped_gen!=lyr_gen || lyr_wrapped_theme!=theme)lyrics_wrap();
 /* backdrop and header */
 float top,bottom;
 if(MODERN) {
  if(art_ready){art_draw(0,-104,480,C(0xffffff,70));grad(0,0,480,272,C(T->bg,170),C(T->bg,235));}
  if(art_ready)art_draw(12,8,32,0xffffffff);else{rrect(12,8,32,32,6,C(T->raised,255));icon(row_live()?IC_RADIO:IC_NOTE,28,24,18,C(T->faint,255));}
  text_fit(52,6,np_title[0]?np_title:"Nothing playing",C(T->fg,255),F_MEDIUM,300);
  text_fit(52,26,np_artist,C(T->accent,255),F_SMALL,300);
  float x=hint(400-text_w("Back",F_SMALL)-34,8,IC_BTN_CIRCLE,"Back");(void)x;
  rect(0,46,480,1,C(T->line,180));
  top=50;bottom=238;
 }else {
  hud_frame(4,4,472,264,0);
  char up[128];upper(up,np_title[0]?np_title:"NO SIGNAL",sizeof(up));
  unsigned int tc=MS?ms_ink():CYBER?T->label:STUDIO?0xff3b2a:Y2K?0xffffff:T->accent;
  text_fit(14,9,CYBER?"// LYRICS":Y2K || MS?"Lyrics":STUDIO || PAPER?"LYRICS":"> LYRICS",C(MS?ms_dim():CYBER?T->accent:Y2K?0xb4f05a:STUDIO?0xbdbdbd:T->second,255),F_SMALL,120);
  if(PAPER)ITALIC(text_fit(14,22,up,C(PP_INK,255),F_MEDIUM,300));else text_fit(14,22,up,C(tc,255),F_MEDIUM,300);
  hud_chip(402-chip_w(IC_BTN_CIRCLE,"BACK"),9,IC_BTN_CIRCLE,"BACK");
  rect(12,46,456,1,C(STUDIO?0x2c2c2c:Y2K?0xffffff:T->line,Y2K?60:255));
  top=50;bottom=230;
 }
 /* the lyrics */
 float mid=(top+bottom)/2;
 if(lyr_state!=2) {
  /* 4: asking LRCLIB, 5: it has none, 6: the network failed (online lyrics, radio_ui.h) */
  int busy=lyr_state==1 || lyr_state==4;
  const char *msg=lyr_state==1?"Loading lyrics...":lyr_state==4?(net_status==1?"Connecting to Wi-Fi...":"Looking up lyrics online..."):lyr_state>=3?"No lyrics for this song":"Nothing playing";
  if(!MODERN && !Y2K){upper(buf,msg,sizeof(buf));msg=buf;}
  text_mid(240,mid-8,msg,C(busy?lyr_cold():lyr_hot(),busy?np_pulse():255),F_MEDIUM);
  if(lyr_state==5 || lyr_state==6){const char *h=lj_msg;char hb[100];if(!MODERN && !Y2K){upper(hb,h,sizeof(hb));h=hb;}text_mid(240,mid+16,h,C(lyr_cold(),170),F_SMALL);}
  if(lyr_state==3){const char *h=online_lyrics?"Put a .lrc file next to the song, or connect Wi-Fi":"Put a .lrc file with the same name next to the song";char hb[80];if(!MODERN && !Y2K){upper(hb,h,sizeof(hb));h=hb;}text_mid(240,mid+16,h,C(lyr_cold(),170),F_SMALL);}
 }else {
  int cur=lyrics_current();float target;
  if(lyr.synced) {
   if(lyr_manual && now_ms>lyr_manual_until)lyr_manual=0;
   int base=cur>=0?lyr_first_row[cur]:0;
   target=base+lyr_manual;if(target<0)target=0;if(target>lyr_nrows-1)target=lyr_nrows-1;
  }else target=lyr_top;
  lyr_scroll_f+=(target-lyr_scroll_f)*0.18f;if(fabsf(target-lyr_scroll_f)<0.01f)lyr_scroll_f=target;
  /* synced: the current row sits in the middle; plain: the page starts at the top */
  float anchor=lyr.synced?mid:top+LYR_RH/2.0f;
  clip(0,(int)top,480,(int)(bottom-top));
  int first=(int)(lyr_scroll_f-(anchor-top)/LYR_RH)-1;if(first<0)first=0;
  for(int r=first;r<lyr_nrows;r++) {
   float y=anchor+(r-lyr_scroll_f)*LYR_RH;if(y>bottom+LYR_RH)break;
   int line=lyr_rows[r].line,hot=lyr.synced && line==cur;
   int alpha=255;
   if(lyr.synced && !hot){float d=fabsf(y-mid)/(mid-top);alpha=(int)(230-150*d);if(cur>=0 && line<cur)alpha=alpha*6/10;if(alpha<30)alpha=30;}
   lyr_draw_row(r,y,hot,alpha);
  }
  unclip();
  if(!lyr.synced && lyr_nrows>7){float track=bottom-top-8,th=track*7/lyr_nrows;if(th<12)th=12;float ty=top+4+(track-th)*lyr_scroll_f/(lyr_nrows>4?lyr_nrows-4:1);
   rect(470,top+4,2,track,C(MODERN?T->line:STUDIO?0x2c2c2c:T->line,255));rect(470,ty,2,th,C(lyr_hot(),200));}
 }
 /* footer: where the song is */
 if(MODERN) {
  int pos=seek_preview>=0?seek_preview:elapsed;float sx=54,sw=372,by=254,frac=duration>0?(float)pos/duration:row_live() && state==2?1:0;if(frac>1)frac=1;
  rrect(sx,by-2,sw,4,2,C(T->line,255));rrect(sx,by-2,sw*frac,4,2,C(T->accent,255));disk(sx+sw*frac,by,4,C(T->fg,255));
  time_text(buf,sizeof(buf),pos);text_right(sx-10,by-7,buf,C(T->dim,255),F_SMALL);
  end_text(buf,sizeof(buf),pos);text(sx+sw+10,by-7,buf,C(T->dim,255),F_SMALL);
 }else hud_seek(248);
}
