/* Home in the Zoidstation skins (see zoidstation.h).
   Zoidstation 1: the BIOS main menu: big beveled buttons, each icon on a disc in one of the logo
   colors, a red outline that pulses on the one picked, and a round console button for Settings.
   Zoidstation 2: the system browser: translucent cubes floating in the dark, the picked one
   larger and glowing, its name lit beneath it; Settings is a glowing line of text below.
   Zoidstation Portable: the cross media bar: a row of white category icons (Settings first, as on
   the handheld) that slides so the picked one stays put, its name under it, and its item below in
   a glass bar. Left / Right move along the bar there; Up and Down are not needed. */
static const int msp_order[HOME_TILES+1]={HOME_SETTINGS,HOME_MUSIC,HOME_VIDEOS,HOME_PODCASTS,HOME_RADIO,HOME_BOOKS};
static int msp_pos(int sel){for(int i=0;i<=HOME_TILES;i++)if(msp_order[i]==sel)return i;return 1;}
/* 1 when the skin took the buttons */
static int ms_home_input(unsigned int press) {
 if(!MSP)return 0;
 int p=msp_pos(home_sel);
 if(press&PSP_CTRL_LEFT && p>0)home_sel=msp_order[p-1];
 if(press&PSP_CTRL_RIGHT && p<HOME_TILES)home_sel=msp_order[p+1];
 if(home_sel<HOME_TILES)home_col=home_sel;
 if(press&PSP_CTRL_CROSS)home_activate(home_sel);
 else if(press&PSP_CTRL_TRIANGLE && active[0]){np_return=SCR_HOME;screen=SCR_NOW;np_focus=2;}
 return 1;
}
static void ms_home_foot(void) {
 if(active[0]){ms_mini();return;}
 if(MSP) { /* the bar has no foot: just the buttons, quietly */
  float x=14;ms_chip(x,246,IC_BTN_CROSS,"Open");x+=10+ms_chip_w(IC_BTN_CROSS,"Open");ms_chip(x,246,IC_BTN_TRIANGLE,"Now Playing");return;
 }
 ms_frame(4,230,472,38,0);
 float x=14;ms_chip(x,241,IC_BTN_CROSS,"Open");x+=6+ms_chip_w(IC_BTN_CROSS,"Open");ms_chip(x,241,-1,"Select  Settings");
 text_right(466,244,"Pick a tile to begin",C(ms_dim(),255),F_SMALL);
}
static void ms1_home(void) {
 char clk[48],sub[48];home_clock(clk,sizeof(clk));float t=ms_sec();
 float w=text(16,6,"ZOID",C(M1_INK,255),F_LARGE);
 for(int k=0;k<4;k++)rrect(16+w+8+(k%2)*8,12+(k/2)*8,7,7,1.5f,C(ms1_logo[k],255));
 text(16+w+30,13,"Main Menu",C(M1_DIM,255),F_SMALL);
 text_right(398,11,clk,C(M1_DIM,255),F_SMALL);
 ms1_stripe(12,33,456,2);
 const float ty=HOME_TY+6; /* clear of the stripe even when a tile lifts */
 const float x0=12,gap=8,tw=(456-gap*(HOME_TILES-1))/HOME_TILES;
 for(int i=0;i<HOME_TILES;i++) {
  home_sub(i,sub,sizeof(sub));
  float x=x0+i*(tw+gap),y=ty-4*home_lift[i],cx=x+tw/2;int sel=home_sel==i;
  if(sel)rrect(x-3,y-3,tw+6,HOME_TH+6,9,C(M1_ACC,(int)(150+100*sinf(t*4))));
  ms1_bevel(x,y,tw,HOME_TH,7,sel?0xd6d9dc:M1_FACE0,sel?0xbcc1c6:M1_FACE1,0);
  unsigned int lc=ms1_logo[i%4];
  disk(cx+1,y+44,24,C(M1_SH,120));disk(cx,y+42,24,C(lc,255));
  icon(home_icons[i],cx,y+42,28,C(0xffffff,255));
  center_fit(cx,y+76,home_names[i],C(M1_INK,255),F_MEDIUM,(int)tw-8);
  center_fit(cx,y+98,sub,C(sel?M1_SEL:M1_DIM,255),F_SMALL,(int)tw-10);
 }
 {float y=ty+HOME_TH+8-2*home_lift[HOME_SETTINGS],cy=y+18;int sel=home_sel==HOME_SETTINGS;
  if(sel)rrect(9,y-3,462,42,9,C(M1_ACC,(int)(150+100*sinf(t*4))));
  ms1_bevel(12,y,456,36,7,sel?0xd6d9dc:M1_FACE0,sel?0xbcc1c6:M1_FACE1,0);
  ms1_round(32,cy,12,IC_GEAR,16);
  float lw=text_vc(52,cy,"Settings",C(M1_INK,255),F_MEDIUM);home_sub(HOME_SETTINGS,sub,sizeof(sub));text_vc(52+lw+14,cy,sub,C(M1_DIM,255),F_SMALL);
  icon(IC_CHEV_R,452,cy,14,C(M1_DIM,255));}
 ms_home_foot();
}
static void ms2_home(void) {
 char clk[48],sub[48];home_clock(clk,sizeof(clk));float t=ms_sec();
 float w=text_glow(18,6,"ZOID",0xffffff,F_LARGE,45);text(18+w+12,14,"Browser",C(M2_DIM,255),F_SMALL);
 text_right(398,11,clk,C(M2_DIM,255),F_SMALL);
 const float step=92,x0=240-step*2;
 for(int i=0;i<HOME_TILES;i++) {
  int sel=home_sel==i;float cx=x0+i*step,cy=100+3*sinf(t*1.3f+i*1.1f)-6*home_lift[i],s=sel?54:38;
  ms_key(cx-s/2,cy-s/2,s,s,home_icons[i],s*0.55f,0,sel);
  if(sel){float nw=text_w(home_names[i],F_MEDIUM);text_glow(cx-nw/2,cy+s/2+6,home_names[i],0xffffff,F_MEDIUM,55);
   home_sub(i,sub,sizeof(sub));text_center(cx,cy+s/2+28,sub,C(0x8cc4ff,255),F_SMALL);}
  else center_fit(cx,cy+s/2+8,home_names[i],C(M2_DIM,255),F_SMALL,84);
 }
 {int sel=home_sel==HOME_SETTINGS;float y=178-2*home_lift[HOME_SETTINGS];
  if(sel)ms_row_sel(120,y,240,26);
  icon(IC_GEAR,172,y+13,18,C(sel?0xffffff:M2_DIM,255));
  if(sel)text_glow(188,y+2,"Settings",0xffffff,F_MEDIUM,50);else text(188,y+2,"Settings",C(M2_DIM,255),F_MEDIUM);
  home_sub(HOME_SETTINGS,sub,sizeof(sub));if(sel)text_center(240,y+28,sub,C(0x8cc4ff,255),F_SMALL);}
 ms_home_foot();
}
static float msp_col_f=1;
static void msp_home(void) {
 char clk[48],sub[48];home_clock(clk,sizeof(clk));
 text_right(398,11,clk,C(0xffffff,255),F_SMALL);
 int p=msp_pos(home_sel);msp_col_f+=(p-msp_col_f)*0.25f;if(fabsf(p-msp_col_f)<0.01f)msp_col_f=p;
 const float ax=132,ay=78,step=74;
 for(int i=0;i<=HOME_TILES;i++) {
  int id=msp_order[i],sel=i==p;float x=ax+(i-msp_col_f)*step;if(x<-30 || x>510)continue;
  float d=fabsf(i-msp_col_f);if(d>1)d=1;float s=46-14*d;int a=(int)(255-110*d);
  if(sel)glow(x,ay,36,C(0xffffff,50));
  icon(home_icons[id],x,ay,s,C(0xffffff,a));
  if(sel)text_center(x,ay+28,home_names[id],C(0xffffff,255),F_SMALL);
 }
 /* the picked category's item, in a glass bar under it */
 int id=msp_order[p];home_sub(id,sub,sizeof(sub));
 float by=128;ms_row_sel(ax-26,by,290,44);
 icon(id==HOME_SETTINGS?IC_GEAR:home_icons[id],ax-2,by+22,26,C(0xffffff,255));
 text(ax+22,by+3,sub,C(0xffffff,255),F_MEDIUM);
 text(ax+22,by+23,"Press X to open",C(MP_DIM,255),F_SMALL);
 ms_home_foot();
}
static void ms_home(void){if(MS1)ms1_home();else if(MS2)ms2_home();else msp_home();}
