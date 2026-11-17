/* Paper, after desktop ZOID's Paper theme (SimplePlayer/Themes/Paper.xaml and RackPaper.xaml):
   warm off-white stock, black ink set in Cambria, and a red-pen accent. The displays are matte
   e-ink panels with no glow anywhere. The now-playing display is the desktop's e-ink page: the
   time set large with an italic "elapsed" under it, a hairline rule, a red small-caps kicker over
   an italic title, and a hairline meter that redraws in slow steps the way e-ink does.
   Transport keys are bare ink icons, the play key is a black ink circle, and text buttons are cream
   with a hairline border. The seek bar is a waveform of 1 px ink strokes with a 4 x 14 ink thumb.
   Lists are cream with alternate rows, a hairline header, and the selection in pale red. */
#define PP_STOCK 0xEFEBE3     /* WindowBg */
#define PP_PANEL 0xF6F3EC     /* PanelBg */
#define PP_LINE 0xD3CCBE      /* LineBrush */
#define PP_HOVER 0xE6E0D3     /* HoverBrush */
#define PP_INK 0x1F1D1A
#define PP_MUTED 0x6E685E
#define PP_FAINT 0x9A9284
#define PP_RED 0xB83A2E
#define PP_SELECT 0xF0DCD3
#define PP_LIST 0xFAF8F3
#define PP_LIST_ALT 0xF5F2EB
#define PP_HEADER 0xF2EEE6
#define PP_DISPLAY 0xE4E0D6   /* the e-ink display */
#define PP_DISPLAY_RIM 0xBDB5A4
#define PP_RIM 0xCFC7B6       /* text buttons, slider track, scroll thumb */
#define PP_METER_DIM 0xD3CEC2 /* unlit meter segments */
#define PP_INK2 0x1F3A5F      /* blue-black ink: the second visualizer color on the desktop's deck B */

/* an e-ink display: hairline rim, radius 4 */
static void paper_display(float x,float y,float w,float h){rrect(x,y,w,h,4,C(PP_DISPLAY_RIM,255));rrect(x+1,y+1,w-2,h-2,3,C(PP_DISPLAY,255));}
/* a sheet laid over the screen (menus, the visualizer list): cream with a hairline */
static void paper_sheet(float x,float y,float w,float h){rect(x+2,y+3,w,h,C(PP_INK,18));rrect(x,y,w,h,2,C(PP_RIM,255));rrect(x+1,y+1,w-2,h-2,1.5f,C(PP_PANEL,255));}
/* a text button: cream, hairline border, radius 2; ink border when it has the focus */
static void paper_button(float x,float y,float w,float h,int focused,int on) {
 rrect(x,y,w,h,2,C(focused?PP_INK:PP_RIM,255));rrect(x+1,y+1,w-2,h-2,1.5f,C(on?PP_SELECT:PP_LIST,255));
 if(focused)rrect(x+1,y+h-2.5f,w-2,1.5f,0.5f,C(PP_RED,255)); /* a red-pen underline */
}
/* the round play key: a black ink circle with a cream legend */
static void paper_play(float cx,float cy,float r,int ic,int focused) {
 if(focused)circle_line(cx,cy,r+3,r+3,1.4f,C(PP_RED,255));
 disk(cx,cy,r,C(PP_INK,255));icon(ic,cx,cy,r*1.15f,C(PP_LIST,255));
}
/* a bare key: an ink icon; red when on; the focus is a hover-colored square with a hairline */
static void paper_key(float x,float y,float w,float h,int ic,const char *label,int on,int focused) {
 float cx=x+w/2,cy=y+h/2;
 if(focused){rrect(x,y,w,h,2,C(PP_INK,150));rrect(x+1,y+1,w-2,h-2,1.5f,C(PP_HOVER,255));}
 else if(on)rrect(x,y,w,h,2,C(PP_RED,0x22));
 unsigned int c=on?PP_RED:PP_INK;
 if(ic>=0)icon(ic,cx,cy,16,C(c,255));
 else{char up[32];upper_sc(up,label,sizeof(up));text_mid(cx,cy,up,C(c,255),F_SMALL);}
}
static void paper_chip(float x,float y,int ic,const char *label) {
 int w=text_w(label,F_SMALL)+(ic>=0?18:8);
 paper_button(x,y,w,16,0,0);
 if(ic>=0)icon(ic,x+8,y+8,11,C(PP_INK,255));
 text_vc(x+(ic>=0?15:4),y+8,label,C(PP_INK,255),F_SMALL);
}

/* "Now Playing", the kicker over the title, and the details after it in muted small capitals */
static void paper_kicker(float x,float y,float maxw) {
 char what[48],more[96]="",up[96];AudioTrack *t=&tracks[current_slot];
 if(row_live()){const char *ph=radio_phase_text();snprintf(what,sizeof(what),"%s",ph?ph:paused?"Paused":"On the air");}
 else snprintf(what,sizeof(what),"%s",error_code?"Could not play":state==1?"Reading":!state?"Stopped":paused?"Paused":"Now Playing");
 if(row_live()) {
  if(error_code)snprintf(more,sizeof(more),"%s",radio_error_text((int)error_code));
  else if(radio_status.kbps>0)snprintf(more,sizeof(more),"%s  %d kbps",radio_status.codec==2?"AAC":"MP3",radio_status.kbps);
 }else if(error_code && radio_is_url(active) && (int)error_code<=-1001 && (int)error_code>=-1199)snprintf(more,sizeof(more),"%s",radio_error_text((int)error_code));
 else if(error_code)snprintf(more,sizeof(more),"Error %d / %08X",stage,error_code);
 else if(state==2) {
  int kbps=duration>0 && t->index.frames?(int)((long long)(t->index.end-t->index.offsets[0])/125/duration):0;
  int n=snprintf(more,sizeof(more),"Track %d of %d",queue_count?queue_pos+1:0,queue_count);
  if(kbps>0 && n<(int)sizeof(more))n+=snprintf(more+n,sizeof(more)-n,"  %d kbps",kbps);
  if(t->rate && n<(int)sizeof(more))snprintf(more+n,sizeof(more)-n,"  %d.%d kHz",t->rate/1000,(t->rate%1000)/100);
 }
 upper_sc(up,what,sizeof(up));float w=text(x,y,up,C(PP_RED,255),F_SMALL);
 if(more[0] && w+20<maxw){upper_sc(up,more,sizeof(up));text_fit(x+w+10,y,up,C(PP_MUTED,255),F_SMALL,(int)(maxw-w-10));}
}

/* The e-ink meter: thin ink bars, unlit segments faintly printed, the peak in red pen.
   Like e-ink it redraws only every sixth frame. */
static void paper_meter(float x,float y,float w,float h,int bars,int segs) {
 static float shown[32],peak[32];static unsigned int last;
 if(bars>32)bars=32;
 if(visual_tick-last>=6 || visual_tick<last) {
  last=visual_tick;
  for(int b=0;b<bars;b++) {
   float p=b*23.0f/(bars-1);int i=(int)p;float f=p-i;
   float v=is_playing()?(levels[i]*(1-f)+levels[i<23?i+1:23]*f)/28.0f:0;v=sqrtf(clampf(v,0,1));
   shown[b]=v;if(v>peak[b])peak[b]=v;else peak[b]-=0.06f;if(peak[b]<0)peak[b]=0;
  }
 }
 float bw=w/bars,sg=1,sh=(h-(segs-1)*sg)/segs,lw=bw*0.62f;if(lw<1)lw=1;
 for(int b=0;b<bars;b++) {
  int lit=(int)(shown[b]*segs+0.5f),pk=(int)(peak[b]*segs+0.5f)-1;float bx=x+b*bw+(bw-lw)/2;
  for(int s=0;s<segs;s++) {
   float sy=y+h-(s+1)*sh-s*sg;unsigned int c=s<lit?PP_INK:PP_METER_DIM;
   if(s==pk && pk>0)c=PP_RED;
   rect(bx,sy,lw,sh,C(c,255));
  }
 }
}

/* The seek bar: a waveform of 1 px strokes, 2 px apart, ink where it has played; the thumb is a 4 x 14 ink bar */
static void paper_seek(float wy) {
 char buf[16];int focus=np_focus==5,pos=seek_preview>=0?seek_preview:elapsed;const float wx=50,ww=380;
 time_text(buf,sizeof(buf),pos);text_right(wx-8,wy-text_height(F_SMALL)/2.0f,buf,C(focus?PP_RED:PP_INK,255),F_SMALL);
 end_text(buf,sizeof(buf),pos);text(wx+ww+8,wy-text_height(F_SMALL)/2.0f,buf,C(PP_MUTED,255),F_SMALL);
 float frac=duration>0?(float)pos/duration:row_live() && state==2?1:0;if(frac>1)frac=1;float fx=wx+ww*frac;
 for(float x=wx;x<wx+ww;x+=3) {
  int b=(int)((x-wx)/ww*98);if(b>97)b=97;float a=wave_hist[b]*7;if(a>7)a=7;if(a<0.5f)a=0.5f;
  rect(x,wy-a,1,a*2,C(x<fx?PP_INK:PP_RIM,255));
 }
 rect(fx-2,wy-7,4,14,C(focus?PP_RED:PP_INK,255));
 if(focus){rect(wx-6,wy+10,ww+12,1,C(PP_RED,255));}
}

/* The controls under the display: shuffle, previous, play, next, repeat, mixing. Bare ink keys on the page. */
static void paper_controls(float y) {
 static const int row[6]={0,1,2,3,4,9};
 const float x0=12,total=456,gap=6,kw=(total-gap*5)/6,ky=y+4,kh=28;
 rect(12,y+36,456,1,C(PP_LINE,255));
 for(int k=0;k<6;k++) {
  int id=row[k],ic,on=0;float x=x0+k*(kw+gap);
  if(id==0){ic=IC_SHUFFLE;on=shuffle;}else if(id==1)ic=IC_PREV;else if(id==2)ic=is_playing()?IC_PAUSE:IC_PLAY;
  else if(id==3)ic=IC_NEXT;else if(id==4){ic=repeat_mode==1?IC_REPEAT1:IC_REPEAT;on=repeat_mode!=0;}else{ic=-1;on=mix_mode!=3;}
  char ml[24]="";if(id==9){if(mix_mode==2)snprintf(ml,sizeof(ml),"Fade %ds",fade_seconds);else snprintf(ml,sizeof(ml),"%s",mix_names[mix_mode]);}
  if((id==1 || id==3) && skipping()){ic=-1;snprintf(ml,sizeof(ml),"%s",id==1?"-15 sec":"+30 sec");}
  if(id==2)paper_play(x+kw/2,ky+kh/2,14,ic,np_focus==id);
  else if(id==9){paper_button(x+4,ky+3,kw-8,kh-6,np_focus==id,0);text_mid(x+kw/2,ky+kh/2-0.5f,ml,C(on?PP_INK:PP_FAINT,255),F_SMALL);}
  else paper_key(x+(kw-40)/2,ky,40,kh,ic,ml,on,np_focus==id);
 }
}

static void paper_now(void) {
 char buf[64],up[96];
 /* the e-ink display */
 paper_display(4,4,472,108);
 int pos=seek_preview>=0?seek_preview:elapsed;
 {int cs=clock_secs(pos);snprintf(buf,sizeof(buf),"%d:%02d",cs/60%100,cs%60);}
 float cw=text_w(buf,F_CLOCK);if(cw<72)cw=72;
 text(16,8,buf,C(PP_INK,255),F_CLOCK);
 ITALIC(text(18,56,time_left?"remaining":"elapsed",C(PP_MUTED,255),F_SMALL));
 float rx=16+cw+12,tx=rx+13,mx=372,tw=mx-12-tx;
 rect(rx,14,1,64,C(PP_INK,77));
 paper_kicker(tx,9,mx-8-tx);
 ITALIC(marquee(&mq_title,np_key,tx,22,np_title[0]?np_title:"Nothing yet",C(PP_INK,255),F_LARGE,(int)tw));
 marquee(&mq_artist,np_key,tx,50,np_artist,C(PP_MUTED,255),F_MEDIUM,(int)tw);
 ITALIC(marquee(&mq_album,np_key,tx,69,np_album,C(PP_FAINT,255),F_SMALL,(int)tw));
 paper_meter(mx,30,92,40,28,10);
 {const char *m=row_live()?"live":shuffle?"shuffle":repeat_mode?"repeat":"";if(m[0])ITALIC(text_right(mx+92,74,m,C(PP_MUTED,255),F_SMALL));}
 paper_seek(97);
 paper_controls(116);
 /* the rack: e-ink panels side by side */
 int vc=visual_count;
 if(vc) {
  float gap=6,ph=104,pw=(472-gap*(vc-1))/vc,py=160;
  for(int i=0;i<vc;i++) {
   float px=4+i*(pw+gap);int focused=np_focus==6+i;
   paper_display(px,py,pw,ph);if(focused)frame_rect(px,py,pw,ph,1,C(PP_INK,255));
   clip((int)px+2,(int)py+14,(int)pw-4,(int)ph-16);visualizer(visuals[i],i,px+5,py+15,pw-10,ph-19);unclip();
   upper_sc(up,visual_names[visuals[i]],sizeof(up));
   float w=text(px+7,py+1,up,C(focused?PP_RED:PP_MUTED,255),F_SMALL);
   if(focused)ITALIC(text_fit(px+7+w+8,py+1,"X next, Triangle list",C(PP_MUTED,255),F_SMALL,(int)(pw-w-24)));
   if(toast_panel==i && now_ms<toast_until){int tw2=text_w(toast,F_MEDIUM)+16;float cx=px+pw/2;
    paper_button(cx-tw2/2,py+ph/2-12,tw2,24,0,0);text_mid(cx,py+ph/2,toast,C(PP_INK,255),F_MEDIUM);}
  }
 }else {
  paper_display(4,160,472,104);
  if(art_ready)art_draw(14,168,88,0xffffffff);else{rrect(14,168,88,88,2,C(PP_RIM,255));icon(row_live()?IC_RADIO:IC_NOTE,58,212,36,C(PP_FAINT,255));}
  ITALIC(text_fit(116,170,np_title,C(PP_INK,255),F_LARGE,348));
  text_fit(116,200,np_artist,C(PP_MUTED,255),F_MEDIUM,348);ITALIC(text_fit(116,222,np_album,C(PP_FAINT,255),F_MEDIUM,348));
 }
}

/* the mini player along the foot of the lists and Home */
static void paper_mini(void) {
 char t1[16],t2[16],buf[40],up[32];float y=230;
 paper_display(4,y,472,38);
 if(!active[0]) {
  ITALIC(text(16,y+10,"Nothing playing",C(PP_MUTED,255),F_MEDIUM));
  paper_chip(468-18-text_w("Play",F_SMALL),y+11,IC_BTN_CROSS,"Play");return;
 }
 paper_play(23,y+19,12,is_playing()?IC_PAUSE:IC_PLAY,0);
 upper_sc(up,row_live()?"On the air":is_playing()?"Now Playing":"Paused",sizeof(up));text(44,y+1,up,C(PP_RED,255),F_SMALL);
 ITALIC(marquee(&mq_mini,np_key,44,y+15,np_title,C(PP_INK,255),F_MEDIUM,244));
 int pos=seek_preview>=0?seek_preview:elapsed;
 time_text(t1,sizeof(t1),pos);if(row_live())snprintf(t2,sizeof(t2),"live");else time_text(t2,sizeof(t2),duration);snprintf(buf,sizeof(buf),"%s / %s",t1,t2);
 text_right(372,y+3,buf,C(PP_MUTED,255),F_SMALL);
 if(duration>0){float f=(float)pos/duration;if(f>1)f=1;rect(300,y+25,72,1,C(PP_RIM,255));rect(300,y+24.5f,72*f,2,C(PP_INK,255));}
 paper_chip(382,y+11,IC_BTN_TRIANGLE,"Playing");
}
