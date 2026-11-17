/* The Home screen: where ZOID starts, and where Circle ends up. Five big tiles (Music, Videos,
   Podcasts, Internet Radio, Books) and a Settings bar, drawn in each skin's own style:
   Zoid rounded tiles with an accent fill, Cyberpunk chamfered neon frames with a chromatic icon,
   Phosphor tube outlines that invert when picked, Y2K glass bubbles that turn to lime jelly,
   Studio Rack units with a red readout and a status LED.
   Left/Right move along the tiles, Down goes to Settings, X opens, Triangle goes to Now Playing.
   Circle at the top of any section (MUSIC, VIDEO, Internet Radio, Podcasts) comes back here. */
#include <psprtc.h>
static int home_sel,home_col;static float home_lift[HOME_TILES+1];
static int home_has_video; /* checked when Home opens, not every frame */
static const char *home_names[HOME_TILES+1]={"Music","Videos","Podcasts","Radio","Books","Settings"};
static const int home_icons[HOME_TILES+1]={IC_NOTE,IC_FILM,IC_MIC,IC_RADIO,IC_BOOK,IC_GEAR};
static void pod_enter(void); /* podcast_ui.h */
static int pod_sub_count(void);
static void books_enter(void); /* books_ui.h */
static void books_home_sub(char *out,int cap);
static void home_info(void){home_has_video=dir_exists(video_root);}
static void go_home(int sel){screen=SCR_HOME;chooser=0;if(sel>=0 && sel<=HOME_SETTINGS){home_sel=sel;if(sel<HOME_TILES)home_col=sel;}home_info();}
static void home_activate(int i) {
 lib_scroll_f=lib_sel_f=0;
 if(i==HOME_MUSIC){strcpy(folder,root);browse();screen=SCR_LIBRARY;}
 else if(i==HOME_VIDEOS){strcpy(folder,video_root);browse();screen=SCR_LIBRARY;}
 /* Podcasts and Radio leave VIDEO behind: a row there is never sent to the video player */
 else if(i==HOME_PODCASTS){strcpy(folder,root);pod_enter();screen=SCR_LIBRARY;}
 else if(i==HOME_RADIO){strcpy(folder,root);radio_enter();screen=SCR_LIBRARY;}
 else if(i==HOME_BOOKS)books_enter();
 else open_settings();
}
static int ms_home_input(unsigned int press); /* ms_home.h */
static void home_input(unsigned int press) {
 if(ms_home_input(press))return;
 if(press&PSP_CTRL_LEFT && home_sel>0 && home_sel<HOME_TILES)home_sel--;
 if(press&PSP_CTRL_RIGHT && home_sel<HOME_TILES-1)home_sel++;
 if(press&PSP_CTRL_DOWN && home_sel<HOME_TILES){home_col=home_sel;home_sel=HOME_SETTINGS;}
 if(press&PSP_CTRL_UP && home_sel==HOME_SETTINGS)home_sel=home_col;
 if(press&PSP_CTRL_CROSS)home_activate(home_sel);
 else if(press&PSP_CTRL_TRIANGLE && active[0]){np_return=SCR_HOME;screen=SCR_NOW;np_focus=2;}
}
/* the line under each tile's name */
static void home_sub(int i,char *out,int cap) {
 switch(i) {
 case HOME_MUSIC:snprintf(out,cap,"%s",active[0] && !radio_is_url(active) && strncmp(active,pod_root,strlen(pod_root))?"Playing":"Library");break;
 case HOME_VIDEOS:snprintf(out,cap,"%s",home_has_video?"Movies":"No videos");break;
 case HOME_PODCASTS:{int n=pod_sub_count();if(n)snprintf(out,cap,"%d show%s",n,n==1?"":"s");else snprintf(out,cap,"Browse");break;}
 case HOME_RADIO:if(row_live())snprintf(out,cap,"On the air");else snprintf(out,cap,"%d station%s",saved_n,saved_n==1?"":"s");break;
 case HOME_BOOKS:books_home_sub(out,cap);break;
 default:snprintf(out,cap,"Theme, sound, Wi-Fi, buttons");
 }
}
static void center_fit(float cx,float y,const char *s,unsigned int c,int f,int maxw) {
 int w=text_w(s,f);if(w<=maxw){text(cx-w/2.0f,y,s,c,f);return;}
 text_fit(cx-maxw/2.0f,y,s,c,f,maxw);
}
/* "Wed, Nov 11  10:44 AM" */
static void home_clock(char *out,int cap) {
 ScePspDateTime t;memset(&t,0,sizeof(t));
 if(sceRtcGetCurrentClockLocalTime(&t)<0 || t.month<1 || t.month>12){out[0]=0;return;}
 static const char *days[7]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"},*mons[12]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
 int dow=sceRtcGetDayOfWeek(t.year,t.month,t.day),h=t.hour%12;if(!h)h=12;if(dow<0 || dow>6)dow=0;
 snprintf(out,cap,"%s, %s %d  %d:%02d %s",days[dow],mons[t.month-1],t.day,h,t.minute,t.hour<12?"AM":"PM");
}

/* ---- tiles, one style per skin ---- */
#define HOME_TY 42
#define HOME_TH 128
static void tile_zoid(int i,float x,float y,float w,float h,int sel,const char *sub) {
 float cx=x+w/2;
 if(sel){glow(cx,y+h/2,w*0.95f,C(T->accent,70));rrect(x-2,y-2,w+4,h+4,16,C(mix(T->accent,0xffffff,60),255));}
 rrect(x,y,w,h,14,C(sel?T->accent:T->raised,255));
 if(!sel)grad(x+6,y+2,w-12,h/2,C(0xffffff,10),C(0xffffff,0));
 disk(cx,y+48,30,C(sel?mix(T->accent,T->on_accent,46):mix(T->raised,T->accent,34),255));
 icon(home_icons[i],cx,y+48,40,C(sel?T->on_accent:T->accent,255));
 center_fit(cx,y+86,home_names[i],C(sel?T->on_accent:T->fg,255),F_MEDIUM,(int)w-10);
 center_fit(cx,y+107,sub,C(sel?mix(T->on_accent,T->accent,110):T->dim,255),F_SMALL,(int)w-12);
}
static void tile_cyber(int i,float x,float y,float w,float h,int sel,const char *sub) {
 float cx=x+w/2;char up[48],num[8];
 if(sel)glow(cx,y+h/2,w*0.9f,C(T->accent,55));
 rect(x,y,w,h,C(0x07070e,255));scanlines(x+2,y+2,w-4,h-4,3,C(0x0f1020,255));
 if(sel)rect(x+2,y+2,w-4,h-4,C(T->accent,26));
 chamfer_box(x,y,w,h,12,1.5f,sel?C(T->label,255):C(T->accent,140));
 if(sel)chamfer_box(x-3,y-3,w+6,h+6,14,1,C(T->second,150));
 snprintf(num,sizeof(num),"0%d",i+1);text(x+14,y+3,num,C(T->second,255),F_SMALL);
 rect(x+w-22,y+8,12,2,C(sel?T->label:T->line,255));
 float iy=y+50;int glitch=sel && (now_ms%2300)<120;
 if(sel){icon(home_icons[i],cx-2-(glitch?3:0),iy,42,C(T->second,170));icon(home_icons[i],cx+2+(glitch?3:0),iy,42,C(T->accent,170));}
 icon(home_icons[i],cx,iy,42,C(sel?T->hot:T->accent,255));
 upper(up,home_names[i],sizeof(up));

 center_fit(cx,y+86,up,C(sel?T->label:T->hot,255),F_MEDIUM,(int)w-10);
 upper(up,sub,sizeof(up));center_fit(cx,y+107,up,C(sel?T->accent:T->dim,255),F_SMALL,(int)w-12);
}
static void tile_phosphor(int i,float x,float y,float w,float h,int sel,const char *sub) {
 float cx=x+w/2;char up[48];
 unsigned int ink=sel?T->bg:T->accent;
 if(sel){glow(cx,y+h/2,w*0.85f,C(T->accent,60));rrect(x,y,w,h,5,C(T->accent,255));}
 else{rrect(x,y,w,h,5,C(T->faint,255));rrect(x+1,y+1,w-2,h-2,4,C(T->bg,255));rect(x+2,y+2,w-4,h-4,C(T->accent,10));}
 if(!sel)glow(cx,y+48,30,C(T->accent,26));
 icon(home_icons[i],cx,y+48,42,C(ink,255));
 upper(up,home_names[i],sizeof(up));
 char lab[56];snprintf(lab,sizeof(lab),"%s%s",sel?"> ":"",up);
 int lw=text_w(lab,F_MEDIUM);if(lw>w-14){snprintf(lab,sizeof(lab),"%s",up);lw=text_w(lab,F_MEDIUM);} /* narrow tile: no prompt mark */float lx=cx-lw/2.0f;
 if(sel){text(lx,y+86,lab,C(ink,255),F_MEDIUM);if((now_ms/530)&1)rect(lx+lw+2,y+90,8,14,C(ink,255));}
 else text_glow(lx,y+86,lab,T->accent,F_MEDIUM,40);
 upper(up,sub,sizeof(up));center_fit(cx,y+107,up,C(sel?T->bg:T->dim,255),F_SMALL,(int)w-12);
}
static void tile_y2k(int i,float x,float y,float w,float h,int sel,const char *sub) {
 float cx=x+w/2,cy=y+48,r=28;
 if(sel)glow(cx,y+h/2,w,C(0xffffff,70));
 rrect(x-1,y-1,w+2,h+2,19,C(sel?0xffffff:0x8fd3ee,sel?255:200));
 if(sel)glass_rrect(x,y,w,h,18,0xd8ffa0,0x9ce050,0x6cc02a,0x94dc4c);
 else glass_rrect(x,y,w,h,18,0x2a94c0,0x13698f,0x0a557a,0x06405e);
 /* the icon in a round glass bubble, like the transport keys */
 disk(cx,cy,r,C(0xffffff,sel?220:120));disk(cx,cy,r-1.2f,C(sel?0x3f8a1c:0x0f5f8a,255));
 clip((int)(cx-r),(int)(cy-r),(int)(2*r)+1,(int)r);disk(cx,cy,r-1.2f,C(sel?0x7cc23c:0x3f93c0,255));unclip();
 disk(cx,cy-r*0.5f,r*0.5f,C(0xffffff,50));
 icon(home_icons[i],cx,cy,36,C(0xffffff,255));
 center_fit(cx,y+86,home_names[i],C(sel?0x0b3a10:0xffffff,255),F_MEDIUM,(int)w-10);
 center_fit(cx,y+107,sub,C(sel?0x1f5a10:0xcff1fa,255),F_SMALL,(int)w-14);
}
static void tile_studio(int i,float x,float y,float w,float h,int sel,const char *sub) {
 float cx=x+w/2;char up[48];
 if(sel)glow(cx,y+h/2,w*0.8f,C(0xff2a1a,45));
 grad(x,y,w,h,C(0x1c1c1c,255),C(0x141414,255));rect(x,y,w,1,C(0x343434,255));rect(x,y+h-1,w,1,C(0x050505,255));
 rack_screw(x+6,y+6);rack_screw(x+w-6,y+h-6);rack_screw(x+6,y+h-6);
 /* status LED: green when picked */
 unsigned int led=sel?0x3bff6a:0x0c2a12;disk(x+w-9,y+8,3,C(led,255));if(sel)glow(x+w-9,y+8,9,C(led,90));
 /* the recessed readout with the icon lit red */
 float wx=x+10,wy=y+16,ww=w-20,wh=56;
 rect(wx,wy,ww,wh,C(0x070606,255));frame_rect(wx,wy,ww,wh,1,C(sel?0x5a1a12:0x2c2c2c,255));
 for(int k=0;k<wh;k+=2)rect(wx+1,wy+k,ww-2,1,C(0x000000,60));
 if(sel)glow(cx,wy+wh/2,30,C(0xff2a1a,90));
 icon(home_icons[i],cx,wy+wh/2,36,C(sel?0xff3b2a:0x6a1a10,255));
 upper(up,home_names[i],sizeof(up));
 center_fit(cx,y+86,up,C(sel?0xffffff:0xd0d0d0,255),F_MEDIUM,(int)w-10);
 upper(up,sub,sizeof(up));center_fit(cx,y+107,up,C(sel?0xff3b2a:0x808080,255),F_SMALL,(int)w-12);
 if(sel)frame_rect(x-2,y-2,w+4,h+4,1,C(0xff3b2a,255));
}
/* Paper: a cream card with a hairline, the icon in ink, the name in Cambria and the line under it in italic.
   The picked card is pale red with an ink rule and the icon in red pen. */
static void tile_paper(int i,float x,float y,float w,float h,int sel,const char *sub) {
 float cx=x+w/2;
 if(sel)rect(x+2,y+3,w,h,C(PP_INK,18));
 rrect(x,y,w,h,2,C(sel?PP_INK:PP_RIM,255));rrect(x+1,y+1,w-2,h-2,1.5f,C(sel?PP_SELECT:PP_LIST,255));
 char num[8];snprintf(num,sizeof(num),"No. %d",i+1);ITALIC(text(x+8,y+3,num,C(sel?PP_RED:PP_FAINT,255),F_SMALL));
 icon(home_icons[i],cx,y+52,40,C(sel?PP_RED:PP_INK,255));
 rect(x+14,y+78,w-28,1,C(sel?PP_RED:PP_LINE,255));
 center_fit(cx,y+84,home_names[i],C(PP_INK,255),F_MEDIUM,(int)w-10);
 ITALIC(center_fit(cx,y+106,sub,C(PP_MUTED,255),F_SMALL,(int)w-12));
}
/* The Settings bar under the tiles */
static void home_settings_bar(float x,float y,float w,float h,int sel) {
 char sub[48],up[48];home_sub(HOME_SETTINGS,sub,sizeof(sub));
 float cy=y+h/2;unsigned int ic,lc,sc;
 if(MODERN){if(sel)glow(x+w/2,cy,w*0.4f,C(T->accent,50));rrect(x,y,w,h,12,C(sel?T->accent:T->raised,255));ic=sel?T->on_accent:T->accent;lc=sel?T->on_accent:T->fg;sc=sel?mix(T->on_accent,T->accent,110):T->dim;}
 else if(PAPER){if(sel)rect(x+2,y+3,w,h,C(PP_INK,18));rrect(x,y,w,h,2,C(sel?PP_INK:PP_RIM,255));rrect(x+1,y+1,w-2,h-2,1.5f,C(sel?PP_SELECT:PP_LIST,255));ic=sel?PP_RED:PP_INK;lc=PP_INK;sc=PP_MUTED;}
 else if(CYBER){rect(x,y,w,h,C(0x07070e,255));scanlines(x+2,y+2,w-4,h-4,3,C(0x0f1020,255));if(sel)rect(x,y,w,h,C(T->accent,26));
  hgrad(x,y,w/2,1.5f,C(T->second,255),C(T->rim_mid,255));hgrad(x+w/2,y,w/2,1.5f,C(T->rim_mid,255),C(T->accent,255));rect(x,y+h-1.5f,w,1.5f,C(sel?T->label:T->line,255));
  if(sel)frame_rect(x-2,y-2,w+4,h+4,1,C(T->label,200));ic=sel?T->label:T->accent;lc=sel?T->label:T->hot;sc=T->dim;}
 else if(PHOS){if(sel){glow(x+w/2,cy,w*0.4f,C(T->accent,50));rrect(x,y,w,h,5,C(T->accent,255));}else{rrect(x,y,w,h,5,C(T->faint,255));rrect(x+1,y+1,w-2,h-2,4,C(T->bg,255));}
  ic=lc=sel?T->bg:T->accent;sc=sel?T->bg:T->dim;}
 else if(Y2K){if(sel)glow(x+w/2,cy,w*0.4f,C(0xffffff,60));rrect(x-1,y-1,w+2,h+2,h/2+1,C(sel?0xffffff:0x8fd3ee,255));
  if(sel)glass_rrect(x,y,w,h,h/2,0xd8ffa0,0x9ce050,0x6cc02a,0x94dc4c);else glass_rrect(x,y,w,h,h/2,0x7ccbf0,0x3a9fd2,0x2187bf,0x48b0e2);
  ic=lc=sel?0x0b3a10:0xffffff;sc=sel?0x1f5a10:0xe6f6fc;}
 else {grad(x,y,w,h,C(0x1c1c1c,255),C(0x141414,255));rect(x,y,w,1,C(0x343434,255));rack_screw(x+6,cy);rack_screw(x+w-6,cy);
  disk(x+w-20,cy,3,C(sel?0x3bff6a:0x0c2a12,255));if(sel){glow(x+w-20,cy,9,C(0x3bff6a,90));frame_rect(x-2,y-2,w+4,h+4,1,C(0xff3b2a,255));}
  ic=sel?0xff3b2a:0x8a8a8a;lc=sel?0xffffff:0xd0d0d0;sc=sel?0xff3b2a:0x808080;}
 icon(IC_GEAR,x+26,cy,24,C(ic,255));
 const char *label="Settings";if(!MODERN && !Y2K){upper(up,label,sizeof(up));label=up;}
 float lw=text_vc(x+46,cy,label,C(lc,255),F_MEDIUM);
 char su[48];if(!MODERN && !Y2K){upper(su,sub,sizeof(su));}else snprintf(su,sizeof(su),"%s",sub);
 if(PAPER)ITALIC(text_vc(x+46+lw+14,cy,su,C(sc,255),F_SMALL));else text_vc(x+46+lw+14,cy,su,C(sc,255),F_SMALL);
 if(!STUDIO)icon(IC_CHEV_R,x+w-18,cy,14,C(sc,255));
}
static void home_header(void) {
 char clk[48];home_clock(clk,sizeof(clk));
 if(MODERN){float w=text(16,6,"ZOID",C(T->fg,255),F_LARGE);disk(16+w+5,27,3,C(T->accent,255));text_right(398,11,clk,C(T->dim,255),F_SMALL);return;}
 if(CYBER){int g=(now_ms%3900)<130;if(g){text(14,6,"ZOID",C(T->second,200),F_LARGE);text(18,6,"ZOID",C(T->accent,200),F_LARGE);}
  float w=text(16,6,"ZOID",C(T->hot,255),F_LARGE);rect(16+w+8,15,6,6,C(T->label,255));text(16+w+18,10,"// MAIN MENU",C(T->label,255),F_SMALL);
  upper(clk,clk,sizeof(clk));text_right(398,11,clk,C(T->dim,255),F_SMALL);return;}
 if(PAPER){float w=text(16,6,"ZOID",C(PP_INK,255),F_LARGE);disk(16+w+4,27,2.6f,C(PP_RED,255));
  ITALIC(text_right(398,11,clk,C(PP_MUTED,255),F_SMALL));rect(12,36,456,1,C(PP_LINE,255));return;}
 if(PHOS){float w=text_glow(14,6,"> ZOID",T->accent,F_LARGE,45);if((now_ms/530)&1)rect(14+w+4,12,10,18,C(T->accent,255));
  upper(clk,clk,sizeof(clk));text_right(398,11,clk,C(T->dim,255),F_SMALL);return;}
 if(Y2K){float w=text_glow(16,6,"ZOID",0xffffff,F_LARGE,50);text(16+w+8,12,"Home",C(0xb4f05a,255),F_MEDIUM);text_right(398,11,clk,C(0xcff1fa,255),F_SMALL);return;}
 /* Studio: the time in red seven-segment digits */
 text(16,6,"ZOID",C(0xe2e2e2,255),F_LARGE);text(78,14,"MAIN",C(0x8a8a8a,255),F_SMALL);
 {ScePspDateTime t;memset(&t,0,sizeof(t));if(sceRtcGetCurrentClockLocalTime(&t)>=0){char b[8];int hh=t.hour%12;if(!hh)hh=12;snprintf(b,sizeof(b),"%d:%02d",hh,t.minute);
  float sw=0;for(const char *s=b;*s;s++)sw+=*s==':'?18*0.11f*2.4f:18*0.5f+18*0.17f;
  rect(392-sw-8,7,sw+28,24,C(0x070606,255));frame_rect(392-sw-8,7,sw+28,24,1,C(0x2c2c2c,255));
  seg7(392-sw-2,10,18,b,0xff2a1a,0x1c0705);text(392-sw+sw-1,15,t.hour<12?"AM":"PM",C(0x8a8a8a,255),F_SMALL);}}
}
/* the foot: what is playing (the usual mini player), or the buttons */
static void home_foot(void) {
 if(active[0]){if(MODERN)draw_mini();else hud_mini();return;}
 if(MODERN){float y=234;rect(0,y,480,38,C(T->panel,235));rect(0,y,480,1,C(T->line,255));float x=16;x=hint(x,y+10,IC_BTN_CROSS,"Open");x=hint_key(x,y+10,"SELECT","Settings");(void)x;return;}
 hud_frame(4,230,472,38,0);
 float x=14;hud_chip(x,241,IC_BTN_CROSS,"OPEN");x+=6+chip_w(IC_BTN_CROSS,"OPEN");hud_chip(x,241,-1,"SELECT  SETTINGS");
 text_right(466,244,Y2K || PAPER?"Pick a tile to begin":"PICK A TILE TO BEGIN",C(Y2K?0xcff1fa:STUDIO?0x8a8a8a:T->dim,255),F_SMALL);
}
#include "ms_home.h"
static void draw_home(void) {
 if(MS){ms_home();return;}
 if(CYBER){for(int gx=0;gx<480;gx+=24)rect(gx,36,1,190,C(T->line,40));for(int gy=36;gy<226;gy+=24)rect(0,gy,480,1,C(T->line,40));}
 if(STUDIO){rect(0,36,6,190,C(0x101010,255));rect(474,36,6,190,C(0x101010,255));for(int ry=44;ry<226;ry+=16){disk(3,ry,1.4f,C(0x050505,255));disk(477,ry,1.4f,C(0x050505,255));}}
 home_header();
 const float x0=12,gap=8,w=(456-gap*(HOME_TILES-1))/HOME_TILES;
 for(int i=0;i<=HOME_TILES;i++){float want=i==home_sel?1:0;home_lift[i]+=(want-home_lift[i])*0.3f;if(fabsf(want-home_lift[i])<0.01f)home_lift[i]=want;}
 for(int i=0;i<HOME_TILES;i++) {
  char sub[48];home_sub(i,sub,sizeof(sub));
  float x=x0+i*(w+gap),y=HOME_TY-4*home_lift[i];int sel=home_sel==i;
  if(MODERN)tile_zoid(i,x,y,w,HOME_TH,sel,sub);else if(CYBER)tile_cyber(i,x,y,w,HOME_TH,sel,sub);else if(PHOS)tile_phosphor(i,x,y,w,HOME_TH,sel,sub);
  else if(Y2K)tile_y2k(i,x,y,w,HOME_TH,sel,sub);else if(PAPER)tile_paper(i,x,y,w,HOME_TH,sel,sub);else tile_studio(i,x,y,w,HOME_TH,sel,sub);
 }
 home_settings_bar(12,HOME_TY+HOME_TH+10-2*home_lift[HOME_SETTINGS],456,36,home_sel==HOME_SETTINGS);
 home_foot();
}
