/* Equalizer page, after desktop ZOID's EQ rack: a response curve across the top, the preamp
   and ten band faders below it, and ON / PRESETS / FLAT / AUTO PRE keys at the right.
   Drawn in the current skin. Focus: 0 preamp, 1-10 bands, 11-15 keys (On, Presets, Save, Flat, Auto pre).
   Left/Right pick a fader, Up/Down move it 1 dB, X centers it; on the keys Up/Down pick, X presses. */
static int eq_focus=1,eq_return=SCR_SETTINGS;static unsigned long long eq_dirty_at;
static float eq_curve[96];static unsigned int eq_curve_ver;
static void open_eq(void){if(screen!=SCR_EQ){eq_return=screen;screen=SCR_EQ;}}
static void eq_touch(void){eq_changed();eq_dirty_at=now_ms?now_ms:1;}
static void eq_save_now(void){if(eq_dirty_at){eq_dirty_at=0;save_settings();}}
static void eq_close(void){eq_save_now();screen=eq_return;}
static void eq_nudge(int band,int d) {
 if(band==0){eq_auto_pre=0;eq_pre+=d;if(eq_pre>12)eq_pre=12;if(eq_pre<-12)eq_pre=-12;}
 else{int *g=&eq_gain[band-1];int v=d?*g+d:0;if(v>12)v=12;if(v<-12)v=-12;if(v==*g)return;*g=v;eq_preset=-1;}
 eq_touch();
}
/* ---- presets list, saving and deleting (the theme's menu popup) ---- */
static int eq_del_k;
static void eq_presets_menu(void) {
 menu_begin("Presets");
 for(int i=0;i<EQ_PRESET_N;i++)menu_add(eq_preset_names[i],A_EQ_PICK);
 for(int i=0;i<eq_user_n;i++){char s[64];snprintf(s,sizeof(s),"* %s",eq_user[i].name);menu_add(s,A_EQ_PICK);}
 menu_add("Save current as...",A_EQ_SAVE);
 if(eq_user_n)menu_add("Delete a saved preset...",A_EQ_DELMENU);
 menu_add("Cancel",A_CANCEL);
 if(eq_preset>=0 && eq_preset<EQ_PRESET_N+eq_user_n)menu_sel=eq_preset;
}
static void eq_named(const char *text) {
 if(!text){notice("Cancelled");return;}
 char name[40];const char *s=text;while(*s==' ')s++;snprintf(name,sizeof(name),"%s",s);
 int n=strlen(name);while(n && name[n-1]==' ')name[--n]=0;
 if(!n)snprintf(name,sizeof(name),"My EQ %d",eq_user_n+1);
 if(!eq_user_save(name)){notice("8 saved presets max - delete one first");return;}
 char msg[64];snprintf(msg,sizeof(msg),"Saved %s",name);notice(msg);eq_touch();
}
static void eq_save_prompt(void) {
 char def[40];
 if(eq_preset>=EQ_PRESET_N && eq_preset-EQ_PRESET_N<eq_user_n)snprintf(def,sizeof(def),"%s",eq_user[eq_preset-EQ_PRESET_N].name);
 else snprintf(def,sizeof(def),"My EQ %d",eq_user_n+1);
 if(!osk_open("Preset name",def,eq_named))eq_named(def);
}
static void eq_menu_run(int act,int sel) {
 switch(act) {
 case A_EQ_PICK:if(sel<EQ_PRESET_N)eq_apply_preset(sel);else eq_user_apply(sel-EQ_PRESET_N);eq_touch();notice(eq_preset_name());break;
 case A_EQ_SAVE:eq_save_prompt();break;
 case A_EQ_DELMENU:menu_begin("Delete which preset?");for(int i=0;i<eq_user_n;i++)menu_add(eq_user[i].name,A_EQ_DEL);menu_add("Cancel",A_CANCEL);break;
 case A_EQ_DEL: {
  eq_del_k=sel;char t[64];snprintf(t,sizeof(t),"Delete \"%.40s\"?",eq_user[sel].name);
  menu_begin(t);menu_warn=1;menu_add("Cancel",A_CANCEL);menu_add("Delete permanently",A_EQ_DEL_YES);break;
 }
 case A_EQ_DEL_YES:eq_user_delete(eq_del_k);eq_touch();notice("Preset deleted");break;
 }
}
static void eq_key(int k) {
 if(k==0){eq_on^=1;show_toast(-2,eq_on?"Equalizer on":"Equalizer off");}
 else if(k==1){eq_presets_menu();return;}
 else if(k==2){eq_save_prompt();return;}
 else if(k==3){eq_apply_preset(0);if(!eq_auto_pre)eq_pre=0;show_toast(-2,"Flat");}
 else{eq_auto_pre^=1;show_toast(-2,eq_auto_pre?"Auto preamp on":"Auto preamp off");}
 eq_touch();
}
static void eq_input(unsigned int press) {
 if(press&PSP_CTRL_CIRCLE){eq_close();return;}
 if(eq_focus<=10) {
  if(press&PSP_CTRL_LEFT && eq_focus>0)eq_focus--;
  else if(press&PSP_CTRL_RIGHT)eq_focus++;
  else if(press&PSP_CTRL_UP)eq_nudge(eq_focus,1);
  else if(press&PSP_CTRL_DOWN)eq_nudge(eq_focus,-1);
  else if(press&PSP_CTRL_CROSS){if(eq_focus==0){eq_auto_pre=0;eq_pre=0;eq_touch();}else eq_nudge(eq_focus,0);}
 }else {
  if(press&PSP_CTRL_LEFT)eq_focus=10;
  else if(press&PSP_CTRL_UP && eq_focus>11)eq_focus--;
  else if(press&PSP_CTRL_DOWN && eq_focus<15)eq_focus++;
  else if(press&PSP_CTRL_CROSS)eq_key(eq_focus-11);
 }
}

/* ---- skin colors ---- */
static unsigned int eq_hi(void){return MS1?M1_SEL:MS?0xffffff:Y2K?0xb4f05a:STUDIO?0xff3b2a:T->accent;}
static unsigned int eq_track(void){return STUDIO?0x2a2a2a:Y2K?0x052f44:PAPER?PP_RIM:T->line;}
static unsigned int eq_text(void){return MODERN?T->dim:MS?ms_dim():STUDIO?0xbdbdbd:Y2K?0xa8d8ea:CYBER?T->dim:T->dim;}
static unsigned int eq_focus_c(void){return MODERN?T->fg:MS1?M1_DARK:MS?0xffffff:CYBER?T->label:STUDIO?0xffffff:Y2K?0xffffff:T->hot;}
static void eq_panel(float x,float y,float w,float h) {
 if(MODERN){rrect(x,y,w,h,10,C(T->panel,255));return;}
 hud_frame(x,y,w,h,0);
}
static void eq_fader(float cx,float top,float h,int db,int focused,int dimmed) {
 float zero=top+h/2,ky=zero-db*(h/2)/12.0f;unsigned int hi=dimmed?eq_text():eq_hi();
 /* track, tick marks at +12 / 0 / -12, and the fill from 0 dB to the knob */
 rect(cx-1.5f,top,3,h,C(eq_track(),255));
 for(int k=0;k<3;k++)rect(cx-6,top+k*h/2-0.5f,12,1,C(eq_track(),255));
 if(db)rect(cx-1.5f,db>0?ky:zero,3,fabsf(ky-zero),C(hi,MODERN?255:200));
 if(focused && !STUDIO)glow(cx,ky,16,C(hi,MODERN?90:110));
 float kw=18,kh=8;
 if(PAPER){rect(cx-kw/2,ky-2,kw,4,C(focused?PP_RED:dimmed?PP_FAINT:PP_INK,255));}
 else if(MS1)ms1_bevel(cx-kw/2,ky-kh/2-1,kw,kh+2,3,focused?0xd6d9dc:M1_FACE0,focused?0xbcc1c6:M1_FACE1,0);
 else if(MS2){glow(cx,ky,12,C(M2_GLOW,focused?160:70));rrect(cx-kw/2,ky-kh/2,kw,kh,3,C(focused?0xffffff:0x9fd0ff,focused?255:200));}
 else if(MSP){rrect(cx-kw/2,ky-kh/2,kw,kh,4,C(0xffffff,focused?255:170));}
 else if(MODERN){rrect(cx-kw/2,ky-kh/2,kw,kh,3,C(focused?T->fg:mix(T->dim,T->fg,140),255));if(focused)rect(cx-5,ky-0.5f,10,1,C(T->bg,255));}
 else if(Y2K){rrect(cx-kw/2-1,ky-kh/2-1,kw+2,kh+2,5,C(focused?0xffffff:0x8fd3ee,255));glass_rrect(cx-kw/2,ky-kh/2,kw,kh,4,dimmed?0xd8eef7:0xd8ffa0,dimmed?0x8fc4dc:0x9ce050,dimmed?0x5f9db8:0x6cc02a,dimmed?0x8fc4dc:0x94dc4c);}
 else if(STUDIO) {
  /* a metal fader cap with a red index line */
  kh=10;grad(cx-kw/2,ky-kh/2,kw,kh,C(0x8a8a8a,255),C(0x3a3a3a,255));frame_rect(cx-kw/2,ky-kh/2,kw,kh,1,C(focused?0xffffff:0x1a1a1a,255));
  rect(cx-kw/2+2,ky-0.75f,kw-4,1.5f,C(dimmed?0x808080:0xff3b2a,255));
 }else {
  /* Cyberpunk / Phosphor: an outlined block with a lit bar, like desktop ZOID */
  rect(cx-kw/2,ky-kh/2,kw,kh,C(T->bg,255));
  frame_rect(cx-kw/2,ky-kh/2,kw,kh,1,C(focused?eq_focus_c():hi,255));rect(cx-5,ky-0.75f,10,1.5f,C(focused?eq_focus_c():hi,255));
 }
}
static void eq_button(float x,float y,float w,float h,const char *label,int on,int focused) {
 char up[24];if(MODERN)snprintf(up,sizeof(up),"%s",label);else upper(up,label,sizeof(up));
 float cx=x+w/2,cy=y+h/2;unsigned int tc;
 if(PAPER){paper_button(x,y,w,h,focused,on);tc=on?PP_RED:PP_INK;}
 else if(MS){char l[24];snprintf(l,sizeof(l),"%s",label);
  if(MS1){if(focused)rrect(x-2,y-2,w+4,h+4,6,C(M1_ACC,255));ms1_bevel(x,y,w,h,4,on?0xfaeec0:M1_FACE0,on?0xf0d77a:M1_FACE1,on);tc=M1_INK;}
  else if(MS2){if(focused || on)ms_row_sel(x,y,w,h);else ms2_pane(x,y,w,h,0);tc=focused || on?0xffffff:M2_DIM;}
  else{msp_glass(x,y,w,h,h/2,focused?4:on?3:1);if(focused)glow(cx,cy,w*0.5f,C(0xffffff,40));tc=0xffffff;}
  text_mid(cx,cy,l,C(tc,255),F_SMALL);return;}
 else if(MODERN) {
  if(focused)rrect(x-2,y-2,w+4,h+4,9,C(T->accent,255));
  rrect(x,y,w,h,7,C(on?T->accent:T->raised,255));tc=on?T->on_accent:T->fg;
 }else if(Y2K) {
  if(focused)glow(cx,cy,w*0.6f,C(0xffffff,60));
  rrect(x,y,w,h,h/2,C(focused?0xffffff:0x0b4f72,255));
  if(on)glass_rrect(x+1,y+1,w-2,h-2,h/2-1,0xd8ffa0,0x9ce050,0x6cc02a,0x94dc4c);else glass_rrect(x+1,y+1,w-2,h-2,h/2-1,0x7ccbf0,0x3a9fd2,0x2187bf,0x48b0e2);
  tc=on?0x0b3a10:0xffffff;
 }else if(STUDIO) {
  if(on)glow(cx,cy,w*0.5f,C(0xff2a1a,50));
  rect(x,y,w,h,C(on?0x3a100c:0x1f1f1f,255));rect(x,y,w,1,C(0x3a3a3a,255));
  frame_rect(x,y,w,h,1,focused?C(0xffffff,220):on?C(0xff3b2a,255):C(0x333333,255));
  tc=on?0xff3b2a:0xe2e2e2;
 }else if(PHOS) {
  if(on){glow(cx,cy,w*0.5f,C(T->accent,50));rect(x,y,w,h,C(T->accent,255));tc=T->bg;}
  else{rect(x,y,w,h,C(T->accent,12));tc=T->accent;}
  if(focused)frame_rect(x-2,y-2,w+4,h+4,1,C(T->hot,255));
 }else {
  if(on)glow(cx,cy,w*0.5f,C(T->accent,60));
  rect(x,y,w,h,C(T->accent,on?51:12));frame_rect(x,y,w,h,1,focused?C(T->label,255):C(T->accent,on?255:85));
  tc=focused?T->label:on?T->hot:T->accent;
 }
 text_mid(cx,cy,up,C(tc,255),F_SMALL);
}
static void eq_draw_curve(float x,float y,float w,float h) {
 if(eq_curve_ver!=eq_version) {
  eq_curve_ver=eq_version;
  for(int i=0;i<96;i++){float f=20*powf(1000.0f,i/95.0f);eq_curve[i]=eq_response_db(f);}
 }
 unsigned int line=eq_track(),hi=eq_on?eq_hi():eq_text();
 float zero=y+h/2,sc=(h/2-3)/12.0f;
 /* grid: +12 / 0 / -12 and a line at each band */
 rect(x,y+3,w,1,C(line,160));rect(x,y+h-4,w,1,C(line,160));
 for(float gx=x;gx<x+w;gx+=6)rect(gx,zero,3,1,C(line,255));
 for(int b=0;b<EQ_BANDS;b++){float bx=x+w*log10f(eq_freq[b]/20)/3;rect(bx,y+3,1,h-6,C(line,90));}
 float xs[96],ys[96];
 for(int i=0;i<96;i++){float d=eq_curve[i];if(d>12)d=12;if(d<-12)d=-12;xs[i]=x+w*i/95.0f;ys[i]=zero-d*sc;}
 for(int i=0;i<95;i++){float t=ys[i]<zero?ys[i]:zero,b=ys[i]<zero?zero:ys[i];rect(xs[i],t,xs[i+1]-xs[i]+1,b-t,C(hi,MODERN?30:STUDIO?40:35));}
 polyline(xs,ys,96,STUDIO?1.2f:1.6f,C(hi,255));
 text(x+3,y+2,"+12",C(eq_text(),200),F_SMALL);text(x+3,y+h-text_height(F_SMALL)-1,"-12",C(eq_text(),200),F_SMALL);
}
static void draw_eq(void) {
 if(eq_dirty_at && now_ms-eq_dirty_at>1500)eq_save_now();
 char buf[64],up[64];
 /* header */
 if(MODERN){icon(IC_BTN_CIRCLE,22,13,15,C(T->dim,255));text(33,4,"Back",C(T->dim,255),F_SMALL);text(16,18,"Equalizer",C(T->fg,255),F_MEDIUM);}
 else if(PAPER)ITALIC(text(10,4,"Equalizer",C(PP_INK,255),F_MEDIUM));
 else if(MS)text(10,4,"Equalizer",C(ms_ink(),255),F_MEDIUM);
 else text(10,4,CYBER?"// EQUALIZER":(Y2K || STUDIO)?"EQUALIZER":"> EQUALIZER",C(CYBER?T->label:STUDIO?0xe2e2e2:T->accent,255),F_MEDIUM);
 snprintf(buf,sizeof(buf),"%s  -  %s  -  Preamp %+d dB",eq_on?"On":"Off",eq_preset_name(),eq_pre);
 if(!MODERN){upper(up,buf,sizeof(up));snprintf(buf,sizeof(buf),"%s",up);}
 text_right(MODERN?464:395,MODERN?22:8,buf,C(eq_on?eq_hi():eq_text(),255),F_SMALL);
 float top=MODERN?40:30;
 /* response curve */
 eq_panel(10,top,460,66);
 {float inset=STUDIO?10:6;clip(10+inset,(int)top+(STUDIO?6:4),460-2*inset,66-(STUDIO?12:8));eq_draw_curve(10+inset,top+(STUDIO?6:4),460-2*inset,66-(STUDIO?12:8));unclip();}
 /* faders: preamp, a divider, then the ten bands */
 float py=top+72,ph=MODERN?122:130;
 eq_panel(10,py,330,ph);
 float ft=py+14,fh=ph-42;
 for(int i=0;i<=EQ_BANDS;i++) {
  float cx=i==0?32:62+(i-1)*29.2f;int focused=eq_focus==i,db=i==0?eq_pre:eq_gain[i-1];
  eq_fader(cx,ft,fh,db,focused,!eq_on || (i==0 && eq_auto_pre));
  const char *nm=i==0?"PRE":eq_band_names[i-1];
  text_center(cx,ft+fh+8,nm,C(focused?eq_focus_c():eq_text(),255),F_SMALL);
  if(focused){snprintf(buf,sizeof(buf),"%+d",db);text_center(cx,ft-13,buf,C(eq_focus_c(),255),F_SMALL);}
 }
 rect(48,ft-4,1,fh+8,C(eq_track(),255));
 /* keys */
 eq_panel(346,py,124,ph);
 const char *names[5]={"On","Presets","Save","Flat","Auto pre"};int ons[5]={eq_on,0,0,eq_preset==0,eq_auto_pre};
 float bh=19,gap=(ph-12-5*bh)/4;
 for(int k=0;k<5;k++)eq_button(358,py+6+k*(bh+gap),100,bh,k==0?(eq_on?"On":"Off"):names[k],ons[k],eq_focus==11+k);
 /* footer */
 float fy=MODERN?244:248;
 if(MODERN){float x=16;x=hint(x,fy,IC_BTN_CROSS,eq_focus<=10?"Center":"Press");x=hint(x,fy,IC_BTN_CIRCLE,"Back");text_vc(x,fy+8.5f,"Up / Down to adjust",C(T->faint,255),F_SMALL);}
 else{float x=10;hud_chip(x,fy-8,IC_BTN_CROSS,eq_focus<=10?"CENTER":"PRESS");x+=6+chip_w(IC_BTN_CROSS,eq_focus<=10?"CENTER":"PRESS");
  hud_chip(x,fy-8,IC_BTN_CIRCLE,"BACK");x+=6+chip_w(IC_BTN_CIRCLE,"BACK");if(PAPER)ITALIC(text_vc(x+4,fy,"Up / Down to adjust",C(PP_MUTED,255),F_SMALL));else if(MS)text_vc(x+4,fy,"Up / Down to adjust",C(ms_dim(),255),F_SMALL);else text_vc(x+4,fy,"UP / DOWN TO ADJUST",C(T->dim,255),F_SMALL);}
}
