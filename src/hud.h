/* Cyberpunk and Phosphor screens, laid out after desktop ZOID: a now-playing
   HUD (transport keys, clock, title block, waveform seek bar) over a rack of
   stacked visualizers, and a song table with an inverse-video header.
   T->name decides the details: Cyberpunk has square neon rims, scan-lined
   panels, hazard-yellow labels and a chromatic title; Phosphor is one green
   at several brightnesses with rounded tube edges, bloom and CRT lines. */
#define CYBER (theme==1)
#define PHOS (theme==2)
/* Y2K Translucent (desktop Y2K.xaml): deep water-blue glass, glossy upper halves over a
   split line, round bubble keys, white legends, lime as the second color.
   Studio Rack (desktop Studio.xaml / RackStudio.xaml): matte black rack units with screws,
   square keys, red seven-segment and dot-matrix readouts, green/amber/red LED meters. */
#define Y2K (theme==3)
#define STUDIO (theme==4)
#include "paper.h"
#include "zoidstation.h"

/* Glass with a bright upper half: the desktop's four-stop gradients split at 48/50%. */
static void glass_rrect(float x,float y,float w,float h,float r,unsigned int top0,unsigned int top1,unsigned int bot0,unsigned int bot1) {
 /* The whole rounded shape is drawn in thin horizontal bands, each in its own gradient
    color, so the gradient reaches the curved edges with no flat strips at the sides. */
 int y0=(int)y,y1=(int)(y+h+0.999f),split=(int)(y+h*0.49f+0.5f),step=h>40?3:2;
 for(int row=y0;row<y1;row+=step) {
  int band=row+step>y1?y1-row:step;if(row<split && row+band>split)band=split-row;
  float mid=row+band*0.5f;unsigned int c;
  if(row<split){float t=clampf((mid-y)/(split-y),0,1);c=mix(top0,top1,(int)(t*255));}
  else{float t=clampf((mid-split)/(y+h-split),0,1);c=mix(bot0,bot1,(int)(t*255));}
  clip((int)x,row,(int)(w+1.999f),band);rrect(x,y,w,h,r,C(c,255));unclip();
  if(row+band<y1 && band<step)row-=step-band;
 }
}
/* rack screw: a dark disk with a slot */
static void rack_screw(float cx,float cy){disk(cx,cy,2.6f,C(0x2e2e2e,255));disk(cx,cy,1.8f,C(0x1a1a1a,255));rect(cx-1.6f,cy-0.4f,3.2f,0.8f,C(0x050505,255));}
/* Seven-segment readout like the desktop's SegmentDisplay, slanted 6 degrees. Returns its width. */
/* One LED segment: a six-sided bar with pointed ends, slanted like the digits.
   grow widens it (positive for bloom and soft edges, negative for the bright core). */
static void seg7_bar(float ox,float oy,float h,float sk,float u0,float v0,float u1,float v1,float t,float gap,float grow,unsigned int c) {
 float du=u1-u0,dv=v1-v0,len=sqrtf(du*du+dv*dv);if(len<0.01f)return;
 du/=len;dv/=len;float nu=-dv,nv=du,hw=t/2+grow,tip=gap-grow,sh=gap+t/2;
 if(sh*2>len){sh=len/2;}
 float pu[6]={u0+du*tip,u0+du*sh+nu*hw,u1-du*sh+nu*hw,u1-du*tip,u1-du*sh-nu*hw,u0+du*sh-nu*hw};
 float pv[6]={v0+dv*tip,v0+dv*sh+nv*hw,v1-dv*sh+nv*hw,v1-dv*tip,v1-dv*sh-nv*hw,v0+dv*sh-nv*hw};
 Vtx *v=vb(6);for(int i=0;i<6;i++)vput(v+i,WHITE_UV,WHITE_UV,c,ox+pu[i]+sk*(h-pv[i]),oy+pv[i]);
 sceGuDrawArray(GU_TRIANGLE_FAN,VFMT,6,0,v);
}
/* Seven-segment readout like the desktop's SegmentDisplay: slanted 6 degrees, unlit segments
   faintly visible, lit ones with a red bloom, softened edges and a hot core. Colors are plain RGB.
   Returns its width. */
static float seg7(float x,float y,float h,const char *s,unsigned int on,unsigned int off) {
 static const unsigned char mask[10]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
 float dw=h*0.5f,t=h*0.11f,gap=t*0.3f,sk=0.105f,cx=x,a=t/2;
 /* side segments run the full digit height, so a 1 stands as tall as an 8 */
 const float sx[7][4]={{a,a,dw-a,a},{dw-a,0,dw-a,h/2},{dw-a,h/2,dw-a,h},{a,h-a,dw-a,h-a},{a,h/2,a,h},{a,0,a,h/2},{a,h/2,dw-a,h/2}};
 unsigned int core=mix(on,0xffffff,120);
 for(int pass=0;pass<5;pass++) {
  float px=x;
  for(const char *p=s;*p;p++) {
   if(*p==':') {
    if(pass>=1)for(int k=0;k<2;k++){float v=h*(k?0.69f:0.31f),u=t*0.55f,d=pass==1?t*1.6f:pass==2?t*0.62f:pass==3?t*0.5f:t*0.22f;
     unsigned int c=pass==1?C(on,0):pass==2?C(on,90):pass==3?C(on,255):C(core,70);
     disk(px+u+sk*(h-v),y+v,d,c);}
    px+=t*2.4f;continue;
   }
   if(*p<'0' || *p>'9'){px+=dw*0.6f;continue;}
   int m=mask[*p-'0'];
   for(int k=0;k<7;k++) {
    int lit=(m>>k)&1;const float *q=sx[k];
    if(pass==0 && !lit)seg7_bar(px,y,h,sk,q[0],q[1],q[2],q[3],t,gap,0,C(off,255));
    if(!lit || pass==0)continue;
    if(pass==1)continue;
    else if(pass==2)seg7_bar(px,y,h,sk,q[0],q[1],q[2],q[3],t,gap,0.6f,C(on,90));
    else if(pass==3)seg7_bar(px,y,h,sk,q[0],q[1],q[2],q[3],t,gap,0,C(on,255));
    else continue;
   }
   px+=dw+h*0.17f;
  }
  cx=px;
 }
 return cx-x;
}

static void hud_frame(float x,float y,float w,float h,int rack) {
 if(PAPER){(void)rack;paper_display(x,y,w,h);return;}
 if(MS){ms_frame(x,y,w,h,rack);return;}
 if(Y2K) {
  /* glossy aqua glass window with a light-to-dark rim */
  rrect(x,y,w,h,14,C(0x8fd3ee,255));rrect(x+1,y+1.5f,w-2,h-2.5f,13,C(0x032e44,255));
  if(rack)glass_rrect(x+2,y+2,w-4,h-4,12,0x1c7db0,0x0f5f8a,0x0a557a,0x06405e);
  else glass_rrect(x+2,y+2,w-4,h-4,12,0x2a94c0,0x13698f,0x0a557a,0x06405e);
  return;
 }
 if(STUDIO) {
  /* a rack unit: matte chassis, recessed black readout, a screw in each corner */
  grad(x,y,w,h,C(0x1c1c1c,255),C(0x151515,255));rect(x,y,w,1,C(0x2e2e2e,255));rect(x,y+h-1,w,1,C(0x050505,255));
  rect(x+7,y+4,w-14,h-8,C(0x070606,255));frame_rect(x+7,y+4,w-14,h-8,1,C(0x2c2c2c,255));
  rack_screw(x+3.5f,y+7);rack_screw(x+w-3.5f,y+7);rack_screw(x+3.5f,y+h-7);rack_screw(x+w-3.5f,y+h-7);
  return;
 }
 if(CYBER) {
  rect(x,y,w,h,C(0x07070e,255));scanlines(x+2,y+2,w-4,h-4,3,C(0x0f1020,255));
  if(rack){frame_rect(x,y,w,h,1.5f,C(T->second,200));return;}
  float half=w/2;
  hgrad(x,y,half,1.5f,C(T->second,255),C(T->rim_mid,255));hgrad(x+half,y,half,1.5f,C(T->rim_mid,255),C(T->accent,255));
  hgrad(x,y+h-1.5f,half,1.5f,C(T->second,255),C(T->rim_mid,255));hgrad(x+half,y+h-1.5f,half,1.5f,C(T->rim_mid,255),C(T->accent,255));
  rect(x,y,1.5f,h,C(T->second,255));rect(x+w-1.5f,y,1.5f,h,C(T->accent,255));
 }else {
  rrect(x,y,w,h,6,C(T->rim_mid,255));rrect(x+1,y+1,w-2,h-2,5,C(T->panel,255));
 }
}
/* Display keys: thin outlines, tinted fill, glow when on. Phosphor inverts when on. */
static void hud_key(float x,float y,float w,float h,int ic,float isz,int on,int focused) {
 if(MS){ms_key(x,y,w,h,ic,isz,on,focused);return;}
 if(PAPER){(void)isz;if(w>=26)paper_play(x+w/2,y+h/2,w/2-1,ic,focused);else paper_key(x,y,w,h,ic,"",on,focused);return;}
 float cx=x+w/2,cy=y+h/2;
 if(Y2K) {
  /* round glass bubble: translucent white, glossy upper half; lime jelly when on */
  float r=w/2;
  if(focused || on)glow(cx,cy,r*1.6f,C(on?0xb4f05a:0xffffff,on?90:70));
  disk(cx,cy,r,C(0xffffff,focused?230:119));
  disk(cx,cy,r-1,C(on?0x5aa82a:0x0f5f8a,255));
  clip((int)(cx-r),(int)(cy-r),(int)(2*r)+1,(int)r);disk(cx,cy,r-1,C(on?0x9ce050:0x3f93c0,255));unclip();
  disk(cx,cy-r*0.45f,r*0.5f,C(0xffffff,on?60:40));
  icon(ic,cx,cy,isz,C(on?0x0b3a10:0xffffff,255));
  return;
 }
 if(STUDIO) {
  /* square rack key with a white silkscreen legend; the play key is a red ring; red when on */
  if(w>=30) {
   if(on || focused)glow(cx,cy,w*0.85f,C(0xff2a1a,on?90:60));
   disk(cx,cy,w/2,C(0xff3b2a,255));disk(cx,cy,w/2-1.6f,C(on?0x5a120c:0x2a0a08,255));
   icon(ic,cx,cy,isz,C(0xff3b2a,255));if(focused)circle_line(cx,cy,w/2+2.5f,w/2+2.5f,1,C(0xffffff,200));return;
  }
  if(on)glow(cx,cy,w*0.8f,C(0xff2a1a,60));
  rect(x,y,w,h,C(on?0x3a100c:0x1f1f1f,255));rect(x,y,w,1,C(0x3a3a3a,255));
  frame_rect(x,y,w,h,1,focused?C(0xffffff,220):on?C(0xff3b2a,255):C(0x333333,255));
  icon(ic,cx,cy,isz,C(on?0xff3b2a:0xe2e2e2,255));
  return;
 }
 if(CYBER) {
  if(on)glow(cx,cy,w*0.9f,C(T->accent,70));
  rect(x,y,w,h,C(T->accent,on?51:12));
  frame_rect(x,y,w,h,1,focused?C(T->label,255):C(T->accent,on?255:85));
  icon(ic,cx,cy,isz,C(on?T->hot:T->accent,255));
 }else {
  if(on){glow(cx,cy,w*0.8f,C(T->accent,60));rect(x,y,w,h,C(T->accent,255));icon(ic,cx,cy,isz,C(T->bg,255));}
  else{rect(x,y,w,h,C(T->accent,10));icon(ic,cx,cy,isz,C(T->accent,255));}
  frame_rect(x,y,w,h,1,focused?C(T->hot,255):C(T->second,on?255:112));
 }
 if(focused && CYBER)frame_rect(x-2,y-2,w+4,h+4,1,C(T->label,110));
}
static void hud_chip(float x,float y,int ic,const char *label) {
 if(PAPER){paper_chip(x,y,ic,label);return;}
 if(MS){ms_chip(x,y,ic,label);return;}
 int w=text_w(label,F_SMALL)+(ic>=0?18:8);
 if(Y2K) { /* glossy blue pill button */
  rrect(x,y,w,16,8,C(0x0b4f72,255));glass_rrect(x+1,y+1,w-2,14,7,0x7ccbf0,0x3a9fd2,0x2187bf,0x48b0e2);
  if(ic>=0)icon(ic,x+8,y+8,11,C(0xffffff,255));
  text_vc(x+(ic>=0?15:4),y+8,label,C(0xffffff,255),F_SMALL);return;
 }
 if(STUDIO) { /* square dark rack button */
  rect(x,y,w,16,C(0x1f1f1f,255));rect(x,y,w,1,C(0x3a3a3a,255));frame_rect(x,y,w,16,1,C(0x333333,255));
  if(ic>=0)icon(ic,x+8,y+8,11,C(0xbdbdbd,255));
  text_vc(x+(ic>=0?15:4),y+8,label,C(0xe2e2e2,255),F_SMALL);return;
 }
 rect(x,y,w,16,C(T->bg,255));frame_rect(x,y,w,16,1,C(T->accent,CYBER?150:110));
 if(ic>=0)icon(ic,x+8,y+8,11,C(T->accent,255));
 text_vc(x+(ic>=0?15:4),y+8,label,C(T->accent,255),F_SMALL);
}
static float chip_w(int ic,const char *label){if(MS)return ms_chip_w(ic,label);return text_w(label,F_SMALL)+(ic>=0?18:8);}
static void hud_status(char *buf,int cap) {
 AudioTrack *t=&tracks[current_slot];
 const char *what=error_code?"AUDIO ERROR":state==1?"READING":!state?"STANDBY":paused?"PAUSED":"NOW PLAYING";
 int kbps=duration>0 && t->index.frames?(int)((long long)(t->index.end-t->index.offsets[0])/125/duration):0;
 char rate[24]="";if(state==2 && t->rate)snprintf(rate,sizeof(rate)," %s %d.%d KHZ",CYBER?"//":"/",t->rate/1000,(t->rate%1000)/100);
 char bits[24]="";if(kbps>0)snprintf(bits,sizeof(bits)," %s %d KBPS",CYBER?"//":"/",kbps);
 if(row_live()) { /* a station: on air (or connecting / buffering), codec and bit rate */
  const char *ph=radio_phase_text(),*sep=CYBER?" // ":Y2K || STUDIO?"  -  ":" / ";char up2[48],k[24]="";
  upper(up2,ph?ph:paused?"Paused":"On air",sizeof(up2));
  if(radio_status.kbps>0)snprintf(k,sizeof(k),"%s%d KBPS",sep,radio_status.kbps);
  if(error_code)snprintf(buf,cap,"%s",radio_error_text((int)error_code));
  else snprintf(buf,cap,"%s%sSTN %02d%s%s%s",up2,sep,queue_count?queue_pos+1:0,sep,radio_status.codec==2?"AAC":"MP3",k);
  return;
 }
 if(error_code && radio_is_url(active) && (int)error_code<=-1001 && (int)error_code>=-1199){char up[96];upper(up,radio_error_text((int)error_code),sizeof(up));snprintf(buf,cap,"%s",up);} /* an episode: why, in words */
 else if(error_code)snprintf(buf,cap,"AUDIO ERROR %d / %08X",stage,error_code);
 else if(Y2K || STUDIO) {
  char r2[24]="",b2[24]="";if(state==2 && t->rate)snprintf(r2,sizeof(r2),"  -  %d.%d KHZ",t->rate/1000,(t->rate%1000)/100);if(kbps>0)snprintf(b2,sizeof(b2),"  -  %d KBPS",kbps);
  snprintf(buf,cap,Y2K?"Track %d  -  MP3%s%s  -  %s":"TRK %02d  -  MP3%s%s  -  %s",queue_count?queue_pos+1:0,b2,r2,t->channels==1?"MONO":"STEREO");
 }
 else if(CYBER)snprintf(buf,cap,"%s // TRK %02d // MP3%s%s",what,queue_count?queue_pos+1:0,rate,bits);
 else snprintf(buf,cap,"TRK %02d / MP3%s%s%s",queue_count?queue_pos+1:0,rate,bits,t->channels==1?" / MONO":" / STEREO");
}
/* A wide key for the controls row, in each skin's style: an icon, or a short label (ic<0). */
static void hud_wide_key(float x,float y,float w,float h,int ic,const char *label,int on,int focused) {
 if(PAPER){paper_key(x,y,w,h,ic,label,on,focused);return;}
 if(MS){ms_key(x,y,w,h,ic,16,on,focused);if(ic<0)text_mid(x+w/2,y+h/2,label,C(ms_ink(),255),F_SMALL);return;}
 float cx=x+w/2,cy=y+h/2;unsigned int c;
 if(Y2K) { /* glossy capsule; lime jelly when on */
  if(focused)glow(cx,cy,w*0.6f,C(0xffffff,60));
  rrect(x,y,w,h,h/2,C(focused?0xffffff:0x0b4f72,255));
  if(on)glass_rrect(x+1,y+1,w-2,h-2,h/2-1,0xd8ffa0,0x9ce050,0x6cc02a,0x94dc4c);else glass_rrect(x+1,y+1,w-2,h-2,h/2-1,0x7ccbf0,0x3a9fd2,0x2187bf,0x48b0e2);
  c=on?0x0b3a10:0xffffff;
 }else if(STUDIO) { /* square rack key, red when on */
  if(on)glow(cx,cy,w*0.5f,C(0xff2a1a,50));
  rect(x,y,w,h,C(on?0x3a100c:0x1f1f1f,255));rect(x,y,w,1,C(0x3a3a3a,255));
  frame_rect(x,y,w,h,1,focused?C(0xffffff,220):on?C(0xff3b2a,255):C(0x333333,255));
  c=on?0xff3b2a:0xe2e2e2;
 }else if(PHOS) { /* inverse video when on */
  if(on){glow(cx,cy,w*0.5f,C(T->accent,50));rect(x,y,w,h,C(T->accent,255));c=T->bg;}
  else{rect(x,y,w,h,C(T->accent,12));frame_rect(x,y,w,h,1,C(T->accent,90));c=T->accent;}
  if(focused)frame_rect(x-2,y-2,w+4,h+4,1,C(T->hot,255));
 }else { /* Cyberpunk: neon outline, tinted fill */
  if(on)glow(cx,cy,w*0.5f,C(T->accent,60));
  rect(x,y,w,h,C(T->accent,on?51:12));frame_rect(x,y,w,h,1,focused?C(T->label,255):C(T->accent,on?255:85));
  c=focused?T->label:on?T->hot:T->accent;
  if(focused)frame_rect(x-2,y-2,w+4,h+4,1,C(T->label,110));
 }
 if(ic>=0)icon(ic,cx,cy,16,C(c,255));
 else{char up[32];upper(up,label,sizeof(up));text_mid(cx,cy,up,C(c,255),F_SMALL);}
}
/* The controls row under the seek bar: shuffle, previous, play, next, repeat, mixing. */
static void hud_controls(float y) {
 if(PAPER){paper_controls(y);return;}
 if(MS){ms_controls(y);return;}
 hud_frame(4,y,472,36,0);
 static const int row[6]={0,1,2,3,4,9};
 const float x0=STUDIO?14:12,total=STUDIO?452:456,gap=6,kw=(total-gap*5)/6,ky=y+5,kh=26;
 for(int k=0;k<6;k++) {
  int id=row[k],ic,on=0;float x=x0+k*(kw+gap);
  if(id==0){ic=IC_SHUFFLE;on=shuffle;}else if(id==1)ic=IC_PREV;else if(id==2){ic=is_playing()?IC_PAUSE:IC_PLAY;on=is_playing();}
  else if(id==3)ic=IC_NEXT;else if(id==4){ic=repeat_mode==1?IC_REPEAT1:IC_REPEAT;on=repeat_mode!=0;}else{ic=-1;on=mix_mode!=3;}
  char ml[24]="";if(id==9){if(mix_mode==2)snprintf(ml,sizeof(ml),"Fade %ds",fade_seconds);else snprintf(ml,sizeof(ml),"%s",mix_names[mix_mode]);}
  if((id==1 || id==3) && skipping()){ic=-1;snprintf(ml,sizeof(ml),"%s",id==1?"-15 sec":"+30 sec");} /* skip keys on a podcast episode */
  hud_wide_key(x,ky,kw,kh,ic,ml,on,np_focus==id);
 }
}
/* Seek bar along the foot of the display: a smooth bar in each skin's style.
   Cyberpunk: a neon gradient line with a bright head. Phosphor: a dotted tube line filling solid.
   Y2K: a glass capsule filling with lime. Studio: a recessed slot with a red fill and a fader cap. */
static void hud_seek(float wy) {
 if(PAPER){paper_seek(wy);return;}
 if(MS){ms_seek_at(50,380,wy);return;}
 char buf[16];int focus=np_focus==5,pos=seek_preview>=0?seek_preview:elapsed;const float wx=50,ww=380;
 unsigned int lab=Y2K?0xcff1fa:STUDIO?0x8a8a8a:T->dim,hi=focus?(CYBER?T->label:(Y2K || STUDIO)?0xffffff:T->hot):lab;
 time_text(buf,sizeof(buf),pos);text_right(wx-8,wy-text_height(F_SMALL)/2.0f,buf,C(hi,255),F_SMALL);
 end_text(buf,sizeof(buf),pos);text(wx+ww+8,wy-text_height(F_SMALL)/2.0f,buf,C(lab,255),F_SMALL);
 float frac=duration>0?(float)pos/duration:row_live() && state==2?1:0;if(frac>1)frac=1;float fx=wx+ww*frac;
 if(Y2K) {
  rrect(wx-1,wy-6,ww+2,12,6,C(focus?0xffffff:0x8fd3ee,focus?255:160));rrect(wx,wy-5,ww,10,5,C(0x052f44,255));
  if(fx-wx>=8)glass_rrect(wx+1,wy-4,fx-wx-1,8,4,0xd8ffa0,0x9ce050,0x6cc02a,0x94dc4c);
  glow(fx,wy,14,C(0xffffff,focus?110:60));disk(fx,wy,6,C(0xffffff,255));disk(fx,wy-1.5f,3,C(0xffffff,255));
 }else if(STUDIO) {
  rect(wx,wy-3,ww,6,C(0x050505,255));frame_rect(wx-1,wy-4,ww+2,8,1,C(focus?0x8a8a8a:0x2c2c2c,255));
  if(fx>wx){glow((wx+fx)/2,wy,(fx-wx)*0.6f+8,C(0xff2a1a,25));rect(wx,wy-2,fx-wx,4,C(0xff3b2a,255));}
  /* fader cap */
  grad(fx-4,wy-7,8,14,C(0x9a9a9a,255),C(0x3a3a3a,255));frame_rect(fx-4,wy-7,8,14,1,C(focus?0xffffff:0x111111,255));rect(fx-3,wy-0.5f,6,1,C(0xff3b2a,255));
 }else if(CYBER) {
  rect(wx,wy-0.5f,ww,1,C(T->accent,60));
  for(int k=0;k<=10;k++)rect(wx+ww*k/10.0f-0.5f,wy-3,1,6,C(T->accent,k==0 || k==10?160:70));
  if(fx>wx){hgrad(wx,wy-1.5f,fx-wx,3,C(T->second,255),C(T->accent,255));glow(fx,wy,12,C(T->accent,120));}
  rect(fx-1,wy-7,2,14,C(T->hot,255));
 }else { /* Phosphor */
  for(float x=wx;x<wx+ww;x+=4)rect(x,wy-1,2,2,C(T->accent,x<fx?0:70));
  if(fx>wx){glow((wx+fx)/2,wy,(fx-wx)*0.55f+6,C(T->accent,30));rect(wx,wy-1.5f,fx-wx,3,C(T->accent,255));}
  rect(fx-2,wy-6,4,12,C(T->hot,(now_ms/530)&1?255:170));
 }
 if(focus) {
  if(Y2K)rrect(wx-8,wy-11,ww+16,22,10,C(0xffffff,30));
  else if(STUDIO)frame_rect(wx-7,wy-10,ww+14,20,1,C(0xffffff,140));
  else{unsigned int fc=C(CYBER?T->label:T->hot,255);float l=wx-6,r=wx+ww+4,t=wy-11,b=wy+11;
   rect(l,t,8,1.5f,fc);rect(l,t,1.5f,8,fc);rect(r-8,t,8,1.5f,fc);rect(r-1.5f,t,1.5f,8,fc);
   rect(l,b-1.5f,8,1.5f,fc);rect(l,b-8,1.5f,8,fc);rect(r-8,b-1.5f,8,1.5f,fc);rect(r-1.5f,b-8,1.5f,8,fc);}
 }
}
/* The display: clock at the left, song details across the rest, the seek bar along its foot.
   The LIBRARY / SETTINGS chips sit in its top-right corner, left of the battery. */
#define HUD_TX 114
#define HUD_TW 354
static float hud_corner_chips(void) {
 return 410; /* the LIBRARY / SETTINGS chips were dropped: Circle and SELECT still work */
}
static void draw_skin_display(void) {
 char buf[160],up[160];
 int pos=seek_preview>=0?seek_preview:elapsed;
 {int cs=clock_secs(pos);snprintf(buf,sizeof(buf),"%d:%02d",cs/60%100,cs%60);}
 float tx=HUD_TX,tw=HUD_TW,chips=hud_corner_chips();
 if(Y2K) {
  /* the time in a frosted capsule */
  rrect(12,12,92,62,16,C(0xffffff,0x66));rrect(13,13,90,60,15,C(0x2f86ad,255));
  clip(13,13,90,28);rrect(13,13,90,60,15,C(0x4a9fc6,255));unclip();
  text_mid(58,23,time_left?"LEFT":"TIME",C(0xcff1fa,255),F_SMALL);
  float cw=text_w(buf,F_CLOCK);text_glow(58-cw/2,50-fonts[F_CLOCK].ch/2.0f,buf,0xffffff,F_CLOCK,45);
  hud_status(up,sizeof(up));text_fit(tx,10,up,C(0xb4f05a,255),F_SMALL,(int)(chips-tx-8));
  marquee(&mq_title,np_key,tx,26,np_title[0]?np_title:"ZOID",C(0xffffff,255),F_LARGE,tw);
  marquee(&mq_artist,np_key,tx,54,np_artist[0]?np_artist:"Ready to play",C(0xcff1fa,255),F_MEDIUM,tw);
  marquee(&mq_album,np_key,tx,72,np_album,C(0xa8d8ea,255),F_SMALL,tw);
 }else {
  /* ELAPSED in slanted red seven-segment digits, glowing */
  text(24,12,time_left?"REMAINING":"ELAPSED",C(0xbdbdbd,255),F_SMALL);
  float sh=strlen(buf)>4?23:29,dw=0;{const char *s=buf;float h=sh;for(;*s;s++)dw+=*s==':'?h*0.11f*2.4f:h*0.5f+h*0.17f;}
  glow(24+dw/2,47,dw*0.7f,C(0xff2a1a,50));
  seg7(24,32+(29-sh)/2,sh,buf,0xff2a1a,0x1c0705);
  hud_status(up,sizeof(up));text_fit(tx,10,up,C(0xbdbdbd,255),F_SMALL,(int)(chips-tx-8));
  upper(up,np_title[0]?np_title:"READY",sizeof(up));
  glow(tx+tw/2,40,tw*0.5f,C(0xff2a1a,26));
  marquee(&mq_title,np_key,tx,26,up,C(0xff3b2a,255),F_LARGE,tw);
  for(int i=0;i<tw;i+=2)rect(tx+i+1,28,1,30,C(0x070606,120));scanlines(tx,28,tw,30,2,C(0x070606,120));
  marquee(&mq_artist,np_key,tx,56,np_artist,C(0xe2e2e2,255),F_MEDIUM,tw-150);
  /* status LEDs beside the artist */
  const char *names[3]={"PLAY","SHUF","RPT"};int lit[3]={is_playing(),shuffle,repeat_mode!=0};unsigned int col[3]={0x3bff6a,0xff2a1a,0xff2a1a};
  float lx=tx+tw-138;for(int i=0;i<3;i++){disk(lx+3,66,3,C(lit[i]?col[i]:mix(col[i],0x070606,215),255));if(lit[i])glow(lx+3,66,7,C(col[i],70));lx+=10+text_vc(lx+9,66,names[i],C(0xbdbdbd,255),F_SMALL)+8;}
  marquee(&mq_album,np_key,tx,76,np_album,C(0x8a8a8a,255),F_SMALL,tw);
 }
}
static void draw_hud_now(void) {
 if(PAPER){paper_now();return;}
 if(MS){ms_now();return;}
 char buf[160],up[160];
 /* display (clock, details, seek bar), then the controls row, then the visualizer rack */
 hud_frame(4,4,472,108,0);
 if(Y2K || STUDIO)draw_skin_display();else {
  float chips=hud_corner_chips();
  int pos=seek_preview>=0?seek_preview:elapsed;
  {int cs=clock_secs(pos);snprintf(buf,sizeof(buf),"%02d:%02d",cs/60%100,cs%60);}
  float tx=HUD_TX,tw=HUD_TW;
  if(CYBER) {
   chamfer_box(12,10,92,64,9,1.5f,C(T->accent,255));
   text(22,9,time_left?"// T-":"// T+",C(T->label,255),F_SMALL);
   float cw=text_w(buf,F_CLOCK),cx=58-cw/2,cy=45-fonts[F_CLOCK].ch/2.0f;
   text(cx-1,cy,buf,C(T->second,130),F_CLOCK);text(cx+1,cy,buf,C(T->accent,130),F_CLOCK);text(cx,cy,buf,C(T->hot,255),F_CLOCK);
   hud_status(buf,sizeof(buf));rect(tx,17,6,6,C(T->label,255));text_fit(tx+9,11,buf,C(T->label,255),F_SMALL,(int)(chips-tx-17));
   upper(up,np_title[0]?np_title:"NO SIGNAL",sizeof(up));
   int glitch=(now_ms%4700)<140;
   if(glitch){marquee(&mq_title,np_key,tx-2,28,up,C(T->second,200),F_LARGE,tw);marquee(&mq_title,np_key,tx+2,28,up,C(T->accent,200),F_LARGE,tw);}
   marquee(&mq_title,np_key,tx,28,up,C(T->hot,255),F_LARGE,tw);
   marquee(&mq_artist,np_key,tx,56,np_artist,C(T->accent,255),F_MEDIUM,tw);
   marquee(&mq_album,np_key,tx,74,np_album,C(T->dim,255),F_SMALL,tw);
  }else {
   text(14,12,paused?"|| PAUSE":state==2?">> PLAY":state==1?".. READ":"-- STOP",C(T->second,255),F_SMALL);
   text_glow(12,48-fonts[F_CLOCK].ch/2.0f,buf,T->hot,F_CLOCK,40);
   hud_status(buf,sizeof(buf));text_fit(tx,10,buf,C(T->dim,255),F_SMALL,(int)(chips-tx-8));
   upper(up,np_title[0]?np_title:"NO CARRIER",sizeof(up));
   text(tx,26,">",C(T->accent,255),F_LARGE);
   int titlew=text_w(up,F_LARGE),room=tw-18;
   clip((int)tx+16,0,room,80);
   if(titlew<=room-12){text_glow(tx+16,26,up,T->accent,F_LARGE,45);if((now_ms/530)&1)rect(tx+17+titlew,33,9,16,C(T->accent,255));}
   else marquee(&mq_title,np_key,tx+16,26,up,C(T->accent,255),F_LARGE,room);
   unclip();
   marquee(&mq_artist,np_key,tx,56,np_artist,C(T->second,255),F_MEDIUM,tw);
   marquee(&mq_album,np_key,tx,74,np_album,C(T->dim,255),F_SMALL,tw);
  }
 }
 hud_seek(97);
 hud_controls(116);
 /* ---- rack ---- */
 hud_frame(4,156,472,112,1);
 int vc=visual_count;
 if(vc) {
  /* panels side by side, like the desktop's rack row */
  float gap=6,ph=96,pw=(460-gap*(vc-1))/vc,py=164;
  for(int i=0;i<vc;i++) {
   float px=10+i*(pw+gap);int focused=np_focus==6+i;
   if(Y2K){rrect(px,py,pw,ph,9,C(0xffffff,focused?230:0x55));glass_rrect(px+1,py+1,pw-2,ph-2,8,0x2384b6,0x13699a,0x0a557a,0x06405e);}
   else if(STUDIO){rect(px,py,pw,ph,C(0x070606,255));frame_rect(px,py,pw,ph,1,focused?C(0xff3b2a,255):C(0x262626,255));}
   else if(CYBER){rect(px,py,pw,ph,C(0x0a0a12,255));frame_rect(px,py,pw,ph,1,focused?C(T->label,255):C(T->line,255));}
   else{rrect(px,py,pw,ph,4,focused?C(T->hot,255):C(T->faint,255));rrect(px+1,py+1,pw-2,ph-2,3,C(T->bg,255));}
   clip((int)px+2,(int)py+2,(int)pw-4,(int)ph-4);visualizer(visuals[i],i,px+4,py+3,pw-8,ph-6);unclip();
   {char nm[40];upper(nm,visual_names[visuals[i]],sizeof(nm));if(focused)snprintf(up,sizeof(up),"%s   X NEXT  /  TRIANGLE LIST",nm);else snprintf(up,sizeof(up),"%s",nm);
    text_fit(px+5,py+1,up,C(focused?(CYBER?T->label:T->hot):T->dim,focused?255:150),F_SMALL,(int)pw-10);}
   if(toast_panel==i && now_ms<toast_until){upper(up,toast,sizeof(up));int w=text_w(up,F_MEDIUM)+16;float cx=px+pw/2;
    rect(cx-w/2,py+ph/2-12,w,24,C(T->bg,230));frame_rect(cx-w/2,py+ph/2-12,w,24,1,C(T->label,255));text_mid(cx,py+ph/2,up,C(T->label,255),F_MEDIUM);}
  }
 }else {
  if(art_ready)art_draw(16,164,96,0xffffffff);else{frame_rect(16,164,96,96,1,C(T->line,255));icon(row_live()?IC_RADIO:IC_NOTE,64,212,40,C(T->faint,255));}
  upper(up,np_title,sizeof(up));text_fit(128,170,up,C(CYBER?T->hot:T->accent,255),F_LARGE,336);
  text_fit(128,200,np_artist,C(T->accent,255),F_MEDIUM,336);text_fit(128,224,np_album,C(T->dim,255),F_MEDIUM,336);
 }
}

/* ---- song table ---- */
#define HUD_ROW 22
#define HUD_ROWS 8
#define HUD_TOP 50
static void hud_row_bg(float y,int alt) {
 if(PAPER){rect(0,y,480,HUD_ROW,C(alt?PP_LIST_ALT:PP_LIST,255));return;}
 if(Y2K){if(alt)rect(0,y,480,HUD_ROW,C(0xffffff,0x0c));}
 else if(STUDIO){/* a 1U rack panel: machined top edge, dark groove under it */
  grad(0,y,480,HUD_ROW,C(alt?0x151515:0x181818,255),C(alt?0x111111:0x141414,255));rect(0,y,480,1,C(alt?0x272727:0x2a2a2a,255));rect(0,y+HUD_ROW-2,480,2,C(0x050505,255));}
 else if(CYBER){rect(0,y,480,HUD_ROW,C(alt?0x0e0e1a:T->list_a,255));scanlines(0,y+1,480,HUD_ROW,2,C(alt?0x08080f:T->list_b,255));}
 else if(alt)rect(0,y,480,HUD_ROW,C(0x020b05,255));
}
/* the selection bar across a table row (Music, Settings, Books) */
static void hud_row_sel(float sy) {
 if(Y2K){rect(0,sy,480,HUD_ROW,C(0x2a86b4,200));rect(0,sy,480,1,C(0xffffff,90));}
 else if(STUDIO){rect(0,sy,480,HUD_ROW-2,C(0x3a100c,255));rect(0,sy,3,HUD_ROW-2,C(0xff3b2a,255));}
 else if(CYBER){rect(0,sy,480,HUD_ROW,C(T->accent,26));frame_rect(0,sy,480,HUD_ROW,1,C(T->accent,200));}
 else{rect(0,sy,480,HUD_ROW,C(T->select,255));rect(0,sy,3,HUD_ROW,C(T->hot,255));}
}
static void hud_header(const char *left,const char *right) {
 if(Y2K){grad(0,30,480,9,C(0xf4fbfe,255),C(0xd8eef7,255));grad(0,39,480,9,C(0xb8ddee,255),C(0xcfe9f5,255));}
 else if(STUDIO){grad(0,30,480,18,C(0x2e2e2e,255),C(0x1c1c1c,255));rect(0,47,480,1,C(0x050505,255));}
 else if(PAPER){rect(0,30,480,18,C(PP_HEADER,255));rect(0,30,480,1,C(PP_LINE,255));rect(0,47,480,1,C(PP_LINE,255));}
 else rect(0,30,480,18,C(T->header_bg,255));
 text(30,29,left,C(T->header_fg,255),F_SMALL);if(right)text(316,29,right,C(T->header_fg,255),F_SMALL);
 if(right)rect(306,30,1,18,C(PAPER?PP_LINE:T->bg,PAPER?255:150));
}
static void hud_mini(void) {
 if(PAPER){paper_mini();return;}
 if(MS){ms_mini();return;}
 char buf[128],up[128];float y=230;
 hud_frame(4,y,472,38,0);
 if(!active[0]) {
  text(14,y+9,CYBER?"// NO SIGNAL":Y2K?"Ready to play":STUDIO?"READY":"> NO CARRIER",C(T->dim,255),F_MEDIUM);
  hud_chip(468-chip_w(IC_BTN_CROSS,"PLAY"),y+12,IC_BTN_CROSS,"PLAY");return;
 }
 hud_key(10,y+6,26,26,is_playing()?IC_PAUSE:IC_PLAY,16,is_playing(),0);
 int pos=seek_preview>=0?seek_preview:elapsed;
 upper(up,np_title,sizeof(up));
 if(Y2K){text(44,y+2,is_playing()?"Now Playing":"Paused",C(0xcff1fa,255),F_SMALL);marquee(&mq_mini,np_key,44,y+14,np_title,C(0xffffff,255),F_MEDIUM,250);}
 else if(STUDIO){text(44,y+2,is_playing()?"PLAY":"PAUSE",C(0xbdbdbd,255),F_SMALL);marquee(&mq_mini,np_key,44,y+14,up,C(0xff3b2a,255),F_MEDIUM,250);}
 else if(CYBER){text(44,y+2,is_playing()?"NOW PLAYING":"PAUSED",C(T->label,255),F_SMALL);marquee(&mq_mini,np_key,44,y+14,up,C(T->hot,255),F_MEDIUM,250);}
 else{text(44,y+2,is_playing()?">> PLAY":"|| PAUSE",C(T->second,255),F_SMALL);snprintf(buf,sizeof(buf),"> %s",up);marquee(&mq_mini,np_key,44,y+14,buf,C(T->accent,255),F_MEDIUM,250);}
 char t1[16],t2[16];time_text(t1,sizeof(t1),pos);if(row_live())snprintf(t2,sizeof(t2),"LIVE");else time_text(t2,sizeof(t2),duration);snprintf(buf,sizeof(buf),"%s / %s",t1,t2);
 text_right(370,y+3,buf,C(T->accent,255),F_SMALL);
 if(duration>0){float f=(float)pos/duration;if(f>1)f=1;rect(300,y+22,70,2,C(T->line,255));rect(300,y+22,70*f,2,C(T->accent,255));}
 hud_chip(380,y+12,IC_BTN_TRIANGLE,"PLAYING");
}
static void draw_hud_library(void) {
 if(MS){ms_library();return;}
 char buf[160],up[160];
 int back=1;const char *name=strrchr(folder,'/');name=(strlen(folder)<=strlen(root) || !name)?(PAPER?"Music":"MUSIC"):name+1; /* Circle always goes somewhere: up, or Home */
 char pln[128];if(in_playlist){const char *b=strrchr(playlist_file,'/');snprintf(pln,sizeof(pln),"%s",b?b+1:playlist_file);char *d=strrchr(pln,'.');if(d)*d=0;name=pln;}
 if(in_radio)name=radio_title;
 if(in_pod)name=pod_title;
 if(back && PAPER){paper_button(6,5,20,20,0,0);icon(IC_CHEV_L,16,15,14,C(PP_INK,255));}
 else if(back){rect(6,5,20,20,C(T->accent,12));frame_rect(6,5,20,20,1,C(T->accent,CYBER?200:120));icon(IC_CHEV_L,16,15,14,C(T->accent,255));}
 upper(up,name,sizeof(up));
 int nw=text_w(up,F_MEDIUM);if(nw>230)nw=230;
 if(PAPER)ITALIC(text_fit(back?32:10,4,up,C(PP_INK,255),F_MEDIUM,230));
 else if(CYBER)text_fit(back?32:10,4,up,C(T->dim,255),F_MEDIUM,230);else text_fit(back?32:10,4,up,C(STUDIO?0xe2e2e2:T->accent,255),F_MEDIUM,230);
 int songs=0;for(int i=0;i<count;i++)if(!entries[i].folder)songs++;
 if(in_radio || in_pod){char ri[64];if(in_pod)pod_info(ri,sizeof(ri));else radio_info(ri,sizeof(ri));upper(up,ri,sizeof(up));snprintf(buf,sizeof(buf),"%s %s",CYBER?"//":"-",up);}
 else if(entries_hidden())snprintf(buf,sizeof(buf),"%s SHOWING %d OF %d %s HOLD X FOR MORE",CYBER?"//":"-",count,entries_total,CYBER?"//":"-");
 else if(songs)snprintf(buf,sizeof(buf),"%s %d SONGS %s HOLD X FOR MORE",CYBER?"//":"-",songs,CYBER?"//":"-");else snprintf(buf,sizeof(buf),"%s %d %s %s HOLD X FOR MORE",CYBER?"//":"-",count,count==1?"ITEM":"ITEMS",CYBER?"//":"-");
 if(PAPER){char ri[64]="";if(in_radio || in_pod){if(in_pod)pod_info(ri,sizeof(ri));else radio_info(ri,sizeof(ri));snprintf(buf,sizeof(buf),"%s",ri);}
  else if(entries_hidden())snprintf(buf,sizeof(buf),"Showing %d of %d, hold X for more",count,entries_total);
  else if(songs)snprintf(buf,sizeof(buf),"%d songs, hold X for more",songs);else snprintf(buf,sizeof(buf),"%d %s, hold X for more",count,count==1?"item":"items");}
 {float bx=(back?32:10)+nw+8;if(PAPER)ITALIC(text_fit(bx,8,buf,C(PP_MUTED,255),F_SMALL,(int)(400-bx)));else text_fit(bx,8,buf,C(T->dim,180),F_SMALL,(int)(400-bx));}
 hud_header("TITLE",in_pod?"STATUS":"ARTIST");
 if(cursor<scroll)scroll=cursor;if(cursor>=scroll+HUD_ROWS)scroll=cursor-HUD_ROWS+1;
 lib_scroll_f+=(scroll-lib_scroll_f)*0.4f;lib_sel_f+=(cursor-lib_sel_f)*0.5f;
 if(fabsf(scroll-lib_scroll_f)<0.01f)lib_scroll_f=scroll;if(fabsf(cursor-lib_sel_f)<0.01f)lib_sel_f=cursor;
 clip(0,HUD_TOP,480,HUD_ROWS*HUD_ROW);
 for(int i=(int)lib_scroll_f;i<count && i<=(int)lib_scroll_f+HUD_ROWS;i++) {
  Entry *e=&entries[i];float y=HUD_TOP+(i-lib_scroll_f)*HUD_ROW;
  char label[256],path[PATHLEN];entry_label(e,label,sizeof(label));
  int playing=0;if(!e->folder && active[0] && entry_path(i,path,sizeof(path)))playing=!strcmp(path,active);
  hud_row_bg(y,i&1);
  if(playing && Y2K){grad(0,y,480,HUD_ROW/2,C(0x9be6fa,255),C(0x3db8e0,255));grad(0,y+HUD_ROW/2,480,HUD_ROW/2,C(0x1290c0,255),C(0x38bce6,255));}
  else if(playing && STUDIO){grad(0,y+1,480,HUD_ROW/2,C(0x240806,255),C(0x3a0d08,255));grad(0,y+1+HUD_ROW/2,480,HUD_ROW/2-3,C(0x3a0d08,255),C(0x1e0604,255));}
 else if(playing && PAPER){}
  else if(playing){hgrad(0,y,290,HUD_ROW,C(T->play_row,CYBER?0x77:0x55),C(T->play_row,CYBER?0x22:0x1a));hgrad(290,y,190,HUD_ROW,C(T->play_row,CYBER?0x22:0x1a),C(T->play_row,0));}
 }
 if(count)hud_row_sel(HUD_TOP+(lib_sel_f-lib_scroll_f)*HUD_ROW);
 for(int i=(int)lib_scroll_f;i<count && i<=(int)lib_scroll_f+HUD_ROWS;i++) {
  Entry *e=&entries[i];float y=HUD_TOP+(i-lib_scroll_f)*HUD_ROW;int sel=i==cursor;
  char label[256],path[PATHLEN];entry_label(e,label,sizeof(label));
  int playing=0;if(!e->folder && active[0] && entry_path(i,path,sizeof(path)))playing=!strcmp(path,active);
  unsigned int tc=playing?T->playing:sel && !PAPER?T->hot:T->fg;
  if(e->folder)icon(row_icon(e),16,y+11,15,C(T->folder,255));else if(playing)icon(IC_WAVE,16,y+11,15,C(T->playing,255));else if(in_radio)icon(IC_RADIO,16,y+11,15,C(T->dim,255));else if(in_pod)icon(pod_ep_icon(i),16,y+11,15,C(T->dim,255));
  int tw=e->folder && !e->artist[0]?420:262;
  if(sel)marquee(&mq_row,i*7919+count,30,y,label,C(tc,255),F_MEDIUM,tw);
  else if(PHOS && playing)text_fit(30,y,label,C(tc,255),F_MEDIUM,tw);else text_fit(30,y,label,C(tc,255),F_MEDIUM,tw);
  if(e->folder && e->folder!=4 && !e->artist[0] && !row_is_action(e->folder))icon(IC_CHEV_R,466,y+11,12,C(T->dim,255));
  else if(e->artist[0])text_fit(316,y+2,e->artist,C(playing?T->playing:sel?T->fg:T->dim,255),F_SMALL,156);
 }
 unclip();
 if(count>HUD_ROWS){float track=HUD_ROWS*HUD_ROW,thumb=track*HUD_ROWS/count;if(thumb<12)thumb=12;
  if(!PAPER)rect(476,HUD_TOP,3,track,C(T->bg2,255));rect(476,HUD_TOP+(track-thumb)*lib_scroll_f/(count-HUD_ROWS),3,thumb,C(CYBER?0x2a3a55:PAPER?PP_RIM:T->select,255));}
 {char l1[96],l2[96],u1[96],u2[96];
  if(radio_empty(l1,sizeof(l1),l2,sizeof(l2)) || pod_empty(l1,sizeof(l1),l2,sizeof(l2))){upper(u1,l1,sizeof(u1));upper(u2,l2,sizeof(u2));text_center(240,110,u1,C(T->dim,radio_waiting() || pod_waiting()?np_pulse():255),F_MEDIUM);text_center(240,134,u2,C(T->faint,255),F_SMALL);}
  else if(!count){text_center(240,110,CYBER?"// NO FILES IN THIS FOLDER":PAPER?"No files here":"> NO FILES",C(T->dim,255),F_MEDIUM);
  text_center(240,134,PAPER?(in_video_folder()?"Put .mp4 videos in the VIDEO folder on your Memory Stick":"Put MP3s in the MUSIC folder on your Memory Stick"):in_video_folder()?"PUT .MP4 VIDEOS IN THE VIDEO FOLDER ON YOUR MEMORY STICK":"PUT MP3S IN THE MUSIC FOLDER ON YOUR MEMORY STICK",C(T->faint,255),F_SMALL);}}
 hud_mini();
}
static void draw_hud_settings(void) {
 if(MS){ms_settings();return;}
 char up[96];const int rows=7;
 if(PAPER)ITALIC(text(10,4,"Settings",C(PP_INK,255),F_MEDIUM));else text(10,4,CYBER?"// SETTINGS":(Y2K || STUDIO)?"SETTINGS":"> SETTINGS",C(CYBER?T->label:STUDIO?0xe2e2e2:T->accent,255),F_MEDIUM);
 text_right(400,8,"ZOID 1.0",C(T->dim,255),F_SMALL);
 hud_header("OPTION","VALUE");
 if(set_item<set_scroll)set_scroll=set_item;if(set_item>=set_scroll+rows)set_scroll=set_item-rows+1;
 set_scroll_f+=(set_scroll-set_scroll_f)*0.4f;set_sel_f+=(set_item-set_sel_f)*0.5f;
 if(fabsf(set_scroll-set_scroll_f)<0.01f)set_scroll_f=set_scroll;if(fabsf(set_item-set_sel_f)<0.01f)set_sel_f=set_item;
 clip(0,HUD_TOP,480,rows*HUD_ROW);
 for(int i=(int)set_scroll_f;i<SET_N && i<=(int)set_scroll_f+rows;i++)hud_row_bg(HUD_TOP+(i-set_scroll_f)*HUD_ROW,i&1);
 hud_row_sel(HUD_TOP+(set_sel_f-set_scroll_f)*HUD_ROW);
 for(int i=(int)set_scroll_f;i<SET_N && i<=(int)set_scroll_f+rows;i++) {
  int id=SID(i);float y=HUD_TOP+(i-set_scroll_f)*HUD_ROW;int sel=i==set_item,off=set_off(id);
  if(i==0 || strcmp(set_section_of(i),set_section_of(i-1))){upper_sc(up,set_section_of(i),sizeof(up));text(10,y+4,up,C(Y2K?0xb4f05a:STUDIO?0x8a8a8a:CYBER?T->second:PAPER?PP_RED:T->dim,255),F_SMALL);}
  unsigned int lc=id==SET_EXIT?T->second:off?T->faint:sel && !PAPER?T->hot:T->fg;
  upper(up,set_labels[id],sizeof(up));text(84,y,up,C(lc,255),F_MEDIUM);
  char v[48];setting_value(id,v,sizeof(v));upper(up,v,sizeof(up));
  if(up[0]){unsigned int vc=off?T->faint:sel?(CYBER?T->label:T->hot):T->accent;
   if(sel){char b[110];snprintf(b,sizeof(b),"< %s >",up);text_fit(300,y,b,C(vc,255),F_MEDIUM,172);}else text_fit(300+text_w("< ",F_MEDIUM),y,up,C(vc,255),F_MEDIUM,160);}
 }
 unclip();
 /* what the selected option does */
 float dy=HUD_TOP+rows*HUD_ROW+4;
 rect(8,dy-3,464,1,C(Y2K?0xffffff:STUDIO?0x2c2c2c:T->line,Y2K?60:255));
 upper(up,set_desc[SID(set_item)],sizeof(up));
 wrap2(10,dy,460,Y2K?set_desc[SID(set_item)]:up,C(Y2K?0xcff1fa:STUDIO?0xbdbdbd:T->dim,255),F_SMALL,13);
 int id=SID(set_item);const char *act=id==SET_EXIT?"EXIT":(id==17 || id==18)?"OPEN":"CHANGE";
 float x=10;hud_chip(x,240,IC_BTN_CROSS,act);x+=6+chip_w(IC_BTN_CROSS,act);
 hud_chip(x,240,IC_BTN_CIRCLE,"BACK");x+=6+chip_w(IC_BTN_CIRCLE,"BACK");
 if(PAPER)ITALIC(text_vc(x+4,248,"Left / Right to adjust",C(PP_MUTED,255),F_SMALL));else text_vc(x+4,248,"LEFT / RIGHT TO ADJUST",C(T->dim,255),F_SMALL);
}
/* Phosphor's tube: every third line darker, edges falling off into black. */
static void phosphor_glass(void) {
 scanlines(0,2,480,270,3,C(0,70));
 grad(0,0,480,40,C(0,90),C(0,0));grad(0,232,480,40,C(0,0),C(0,90));
 hgrad(0,0,50,272,C(0,90),C(0,0));hgrad(430,0,50,272,C(0,0),C(0,90));
}
/* The visualizer chooser, over Now Playing: Up/Down pick, X sets, Left/Right switch panel. */
static void draw_chooser(void) {
 const int rows=9;float bx=110,by=26,bw=260,rh=20,bh=44+rows*rh+22;char buf[64],up[64];
 if(chooser_item<chooser_scroll)chooser_scroll=chooser_item;if(chooser_item>=chooser_scroll+rows)chooser_scroll=chooser_item-rows+1;
 rect(0,0,480,272,PAPER?C(PP_INK,60):MSP?C(T->bg,70):C(0,120));
 if(MODERN){rrect(bx,by,bw,bh,12,C(T->panel,250));}
 else if(PAPER)paper_sheet(bx,by,bw,bh);
 else if(MS)ms_sheet(bx,by,bw,bh);
 else{rect(bx,by,bw,bh,C(T->bg,245));if(CYBER){scanlines(bx,by,bw,bh,3,C(0x0f1020,255));frame_rect(bx,by,bw,bh,1.5f,C(T->accent,255));}else{frame_rect(bx,by,bw,bh,1,C(T->rim_mid,255));}}
 snprintf(buf,sizeof(buf),MODERN?"Panel %d visualizer":"PANEL %d // VISUALIZER",chooser_slot+1);if(PHOS)snprintf(buf,sizeof(buf),"> PANEL %d VISUALIZER",chooser_slot+1);if(PAPER)snprintf(buf,sizeof(buf),"Panel %d visualizer",chooser_slot+1);
 text_vc(bx+14,by+18,buf,C(MODERN?T->fg:CYBER?T->label:T->accent,255),F_MEDIUM);
 for(int r=0;r<rows;r++) {
  int i=chooser_scroll+r;if(i>=VIS_COUNT)break;float y=by+36+r*rh;int sel=i==chooser_item,cur=visual_order[i]==visuals[chooser_slot];
  if(sel){if(MODERN)rrect(bx+8,y,bw-16,rh,6,C(T->raised,255));else if(MS)ms_row_sel(bx+8,y,bw-16,rh);else if(CYBER){rect(bx+6,y,bw-12,rh,C(T->accent,30));frame_rect(bx+6,y,bw-12,rh,1,C(T->accent,220));}else rect(bx+6,y,bw-12,rh,C(T->select,255));}
  const char *name=visual_names[visual_order[i]];if(!MODERN){upper(up,name,sizeof(up));name=up;}
  text_vc(bx+20,y+rh/2,name,C(sel?(MODERN?T->fg:T->hot):cur?T->accent:MODERN?T->dim:T->fg,255),F_MEDIUM);
  if(cur)icon(IC_WAVE,bx+bw-22,y+rh/2,14,C(T->accent,255));
 }
 if(VIS_COUNT>rows){float track=rows*rh,th=track*rows/VIS_COUNT;rect(bx+bw-6,by+36,2,track,C(T->line,255));rect(bx+bw-6,by+36+(track-th)*chooser_scroll/(VIS_COUNT-rows),2,th,C(T->accent,255));}
 const char *help=visual_count>1?(!MODERN && !PAPER && !MS?"X SET   O CANCEL   LEFT/RIGHT PANEL":"X set   O cancel   Left/Right panel"):(!MODERN && !PAPER && !MS?"X SET   O CANCEL":"X set   O cancel");
 text_mid(bx+bw/2,by+bh-12,help,C(T->dim,255),F_SMALL);
}
/* The hold-X menu: a short list over the screen, drawn in the theme's style. */
static void menu_panel(float x,float y,float w,float h) {
 if(MODERN)rrect(x,y,w,h,12,C(T->panel,250));
 else if(PAPER)paper_sheet(x,y,w,h);
 else if(MS)ms_sheet(x,y,w,h);
 else if(Y2K){rrect(x-1,y-1,w+2,h+2,13,C(0x8fd3ee,255));glass_rrect(x,y,w,h,12,0x2a94c0,0x13698f,0x0a557a,0x06405e);}
 else if(STUDIO){grad(x,y,w,h,C(0x1c1c1c,255),C(0x151515,255));frame_rect(x,y,w,h,1,C(0x2e2e2e,255));rack_screw(x+5,y+6);rack_screw(x+w-5,y+6);}
 else{rect(x,y,w,h,C(T->bg,245));if(CYBER){scanlines(x,y,w,h,3,C(0x0f1020,255));frame_rect(x,y,w,h,1.5f,C(T->accent,255));}else frame_rect(x,y,w,h,1,C(T->rim_mid,255));}
}
static void menu_row_sel(float x,float y,float w,float h) {
 if(MODERN)rrect(x,y,w,h,6,C(T->raised,255));
 else if(PAPER){rect(x,y,w,h,C(PP_SELECT,255));rect(x,y,2,h,C(PP_RED,255));}
 else if(MS)ms_row_sel(x,y,w,h);
 else if(Y2K){rrect(x,y,w,h,h/2,C(0xffffff,0x77));glass_rrect(x+1,y+1,w-2,h-2,h/2-1,0x7ccbf0,0x3a9fd2,0x2187bf,0x48b0e2);}
 else if(STUDIO){rect(x,y,w,h,C(0x3a100c,255));rect(x,y,3,h,C(0xff3b2a,255));}
 else if(CYBER){rect(x,y,w,h,C(T->accent,30));frame_rect(x,y,w,h,1,C(T->accent,220));}
 else rect(x,y,w,h,C(T->select,255));
}
static void draw_menu(void) {
 const int rows=8;float bw=280,rh=22,x=100;
 int shown=menu_n<rows?menu_n:rows;float extra=menu_warn?20:0,bh=44+extra+shown*rh+22,y=(272-bh)/2;
 if(menu_sel<menu_scroll)menu_scroll=menu_sel;if(menu_sel>=menu_scroll+rows)menu_scroll=menu_sel-rows+1;
 rect(0,0,480,272,PAPER?C(PP_INK,60):MSP?C(T->bg,70):C(0,130));
 menu_panel(x,y,bw,bh);
 char up[96];if(MODERN)snprintf(up,sizeof(up),"%s",menu_title);else upper(up,menu_title,sizeof(up));
 text_fit(x+16,y+9,up,C(MODERN?T->fg:CYBER?T->label:STUDIO?0xe2e2e2:Y2K?0xffffff:T->accent,255),F_MEDIUM,(int)bw-32);
 rect(x+12,y+34,bw-24,1,C(Y2K?0xffffff:T->line,Y2K?70:255));
 if(menu_warn){unsigned int wc=MODERN?0xff6a5a:MS1?M1_RED:MS?0xffb0b0:STUDIO?0xffb020:CYBER?T->second:Y2K?0xffd27a:T->hot;
  text_vc(x+16,y+48,MODERN || PAPER?"This cannot be undone.":"THIS CANNOT BE UNDONE.",C(wc,255),F_SMALL);}
 for(int r=0;r<shown;r++) {
  int i=menu_scroll+r;float ry=y+40+extra+r*rh;int sel=i==menu_sel;
  if(sel)menu_row_sel(x+8,ry,bw-16,rh-2);
  const char *s=menu_label[i];if(!MODERN){upper(up,s,sizeof(up));s=up;}
  unsigned int c=sel?(MODERN || PAPER?T->fg:STUDIO?0xffffff:T->hot):(menu_act[i]==A_CANCEL || menu_act[i]==A_PL_DELETE_YES?T->dim:(MODERN?mix(T->dim,T->fg,170):STUDIO?0xdadada:T->fg));
  if((menu_act[i]==A_PL_DELETE_YES || menu_act[i]==A_DEL_CONFIRM || menu_act[i]==A_EQ_DEL_YES) && sel)c=MODERN?0xff6a5a:T->hot;
  text_vc(x+20,ry+(rh-2)/2,s,C(c,255),F_MEDIUM);
 }
 if(menu_n>rows){float track=rows*rh,th=track*rows/menu_n;rect(x+bw-7,y+40+extra,2,track,C(T->line,255));rect(x+bw-7,y+40+extra+(track-th)*menu_scroll/(menu_n-rows),2,th,C(T->accent,255));}
 text_mid(x+bw/2,y+bh-13,MODERN || PAPER || MS?"X choose   O back":"X CHOOSE   O BACK",C(MODERN?T->dim:STUDIO?0x8a8a8a:Y2K?0xcff1fa:T->dim,255),F_SMALL);
}
/* A short message along the bottom: "Added 12 songs to Road Trip". */
static void draw_notice(void) {
 char up[96];const char *s=toast;if(!MODERN){upper(up,toast,sizeof(up));s=up;}
 int w=text_w(s,F_SMALL)+24;float x=240-w/2.0f,y=200;
 int a=toast_until-now_ms>300?255:(int)(toast_until-now_ms)*255/300;
 if(MODERN)rrect(x,y,w,22,11,C(T->raised,a*240/255));
 else if(Y2K){rrect(x,y,w,22,11,C(0x8fd3ee,a));rrect(x+1,y+1,w-2,20,10,C(0x0f5f8a,a));}
 else if(STUDIO){rect(x,y,w,22,C(0x070606,a));frame_rect(x,y,w,22,1,C(0xff3b2a,a));}
 else if(PAPER){rrect(x,y,w,22,2,C(PP_INK,a));rrect(x+1,y+1,w-2,20,1.5f,C(PP_LIST,a));}
 else if(MS){if(MS1){rrect(x,y,w,22,6,C(M1_SH,a));rrect(x+1,y+1,w-2,20,5,C(M1_DARK,a));}else{rrect(x,y,w,22,11,MS2?C(0x06204a,a*220/255):C(0xffffff,a*40/255));if(MS2)glow(240,y+11,w*0.4f,C(M2_GLOW,a/4));else rect(x+11,y,w-22,1,C(0xffffff,a/2));}}
 else{rect(x,y,w,22,C(T->bg,a));frame_rect(x,y,w,22,1,C(CYBER?T->label:T->accent,a));}
 text_mid(240,y+11,s,C(MODERN?T->fg:MS?0xffffff:STUDIO?0xff3b2a:CYBER?T->label:Y2K?0xffffff:T->accent,a),F_SMALL);
}
