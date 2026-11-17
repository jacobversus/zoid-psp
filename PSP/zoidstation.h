/* The Zoidstation skins: three themes after the game consoles' own menus, each drawn from scratch
   (not the Zoid layout recolored). Included from hud.h; the Home screen half is ms_home.h.

   Zoidstation 1 (theme 6), after the first console's BIOS, CD player and memory card screens:
   the console's own light gray plastic, matte with plain bevels (no shine and no glow, as that era drew),
   black type in a plain rounded sans, the logo's red / yellow / teal / blue exactly, the four logo colors as the accent, each with its own job,
   a blue selection bar, round buttons like POWER and OPEN, ridges like the console's sides, the
   lid's ring around a spinning disc, a dark readout window and the CD player's grid of track numbers.

   Zoidstation 2 (theme 7), after the second console's system browser: a deep blue night with
   slowly rising points of light and faint light towers, nothing boxed in hard lines, thin
   Century Gothic type that glows, translucent floating cubes for keys and tiles, and glowing bars
   for the selection.

   Zoidstation Portable (theme 8), after the handheld's cross media bar: the blue gradient with
   flowing translucent waves, white Segoe UI type, white icons, glass panels with a bright top
   edge, a row of category icons on Home, and a glass bar on the selected row.

   Animation stays light for the real PSP: a few dozen sprites and two short triangle strips a frame. */
#define MS (theme>=6)
#define MS1 (theme==6)
#define MS2 (theme==7)
#define MSP (theme==8)

/* Zoidstation 1: the console's light gray plastic (sampled from the console) and the logo's four colors,
   exactly: red DE0029, yellow F3C202, teal 00AA9E, blue 326DB3. All four are the accent, each with its own job (below). */
#define M1_FACE0 0xc4c8cc  /* the lit upper half of a bevel */
#define M1_FACE1 0xa9afb4
#define M1_HI 0xdfe2e5     /* bevel light edge */
#define M1_SH 0x575a60     /* bevel shadow: the gray of the lettering on the lid */
#define M1_INK 0x1c1d21
#define M1_DIM 0x4b4e54
#define M1_FAINT 0x80848c
#define M1_DARK 0x3c3f45   /* charcoal: toasts, the scroll thumb */
#define M1_WELL 0xc9cdd1   /* a sunk list */
#define M1_LCD 0x16181c
#define M1_LCDTXT 0x00aa9e
#define M1_LCDDIM 0x3f7f7a
#define M1_RED 0xde0029
#define M1_YEL 0xf3c202
#define M1_TEAL 0x00aa9e
#define M1_BLUE 0x326db3
/* The accent is all four logo colors, each with its own job: red marks focus (outlines, the focused
   key, the current track, the round buttons), blue is the selection bar and values and folders,
   yellow fills progress, teal is the readout and the battery. */
#define M1_ACC M1_RED    /* focus */
#define M1_SEL M1_BLUE   /* the selection bar, values, folders */
#define M1_FILL M1_YEL   /* progress */
/* Zoidstation 2 */
#define M2_GLOW 0x4f9bff
#define M2_TEXT 0xd6e6ff
#define M2_DIM 0x7f9cd0
#define M2_FAINT 0x40598a
/* Zoidstation Portable */
#define MP_DIM (T->dim)  /* Portable takes its colors from its Theme: the color picked (Theme color) changes them */

static float ms_sec(void){return (now_ms%3600000)/1000.0f;}
static const unsigned int ms1_logo[4]={M1_RED,M1_YEL,M1_TEAL,M1_BLUE}; /* the logo, P to base */

/* a filled quad, corners in order around it */
static void ms_quad(float x0,float y0,float x1,float y1,float x2,float y2,float x3,float y3,unsigned int c) {
 Vtx *v=vb(4);vput(v,WHITE_UV,WHITE_UV,c,x0,y0);vput(v+1,WHITE_UV,WHITE_UV,c,x1,y1);vput(v+2,WHITE_UV,WHITE_UV,c,x3,y3);vput(v+3,WHITE_UV,WHITE_UV,c,x2,y2);
 sceGuDrawArray(GU_TRIANGLE_STRIP,VFMT,4,0,v);
}
/* a ring segment from angle a0 to a1 (radians) */
static void ms_arc(float cx,float cy,float r0,float r1,float a0,float a1,unsigned int c) {
 const int n=8;Vtx *v=vb((n+1)*2);
 for(int i=0;i<=n;i++){float a=a0+(a1-a0)*i/n,cs=cosf(a),sn=sinf(a);vput(v+i*2,WHITE_UV,WHITE_UV,c,cx+cs*r1,cy+sn*r1);vput(v+i*2+1,WHITE_UV,WHITE_UV,c,cx+cs*r0,cy+sn*r0);}
 sceGuDrawArray(GU_TRIANGLE_STRIP,VFMT,(n+1)*2,0,v);
}
/* "BACK" -> "Back", "SELECT  SETTINGS" -> "Select  Settings": these skins write in ordinary case */
static void ms_case(char *dst,const char *src,int cap) {
 int i=0,start=1;
 for(;src[i] && i<cap-1;i++){unsigned char ch=src[i];dst[i]=ch<128?(start?toupper(ch):tolower(ch)):ch;start=ch==' ' || ch=='/';}
 dst[i]=0;
}

/* ---- backgrounds ---- */
static void ms_wave(float yc,float amp,float freq,float ph,float thick,int alpha,int edge) {
 enum{N=25};float xs[N],ys[N];Vtx *v=vb(N*2);
 for(int i=0;i<N;i++) {
  float x=i*20.0f,t=x*freq+ph;
  float y=yc+amp*sinf(t)+amp*0.35f*sinf(t*2.3f+ph*0.6f);
  float th=thick*(0.55f+0.45f*sinf(t*0.7f+ph*0.4f));
  xs[i]=x;ys[i]=y;
  vput(v+i*2,WHITE_UV,WHITE_UV,C(0xffffff,alpha),x,y);vput(v+i*2+1,WHITE_UV,WHITE_UV,C(0xffffff,0),x,y+th);
 }
 sceGuDrawArray(GU_TRIANGLE_STRIP,VFMT,N*2,0,v);
 if(edge)polyline(xs,ys,N,1.2f,C(0xffffff,edge));
}
static void ms_background(void) {
 float t=ms_sec();
 if(MS1) {
  /* the BIOS gray, lighter at the top, with the faint lines of a TV of the time */
  grad(0,0,480,272,C(0xa7aab2,255),C(0x80848c,255));
  scanlines(0,1,480,271,4,C(0xffffff,14));
 }else if(MS2) {
  /* deep blue night: a glow on the horizon, light towers, and points of light rising */
  grad(0,0,480,150,C(0x000003,255),C(0x000614,255));grad(0,150,480,122,C(0x000614,255),C(0x000106,255)); /* near black, a little blue at the middle */
  glow(240,236,300,C(0x1450c8,38));
  for(int k=0;k<7;k++) {
   float x=40+k*67+12*sinf(k*1.7f),h=70+60*(0.5f+0.5f*sinf(t*0.35f+k*1.3f));int a=(int)(26+22*sinf(t*0.5f+k*2.1f));
   grad(x,272-h,3+(k%3),h,C(0x6fb4ff,0),C(0x6fb4ff,a));
  }
  for(int i=0;i<26;i++) {
   float sp=6+(i*37%19),x=fmodf(i*73.3f+18*sinf(t*0.3f+i),480),y=272-fmodf(t*sp+i*97.0f,300);
   float tw=0.5f+0.5f*sinf(t*1.7f+i*2.3f);float r=1+(i%3)*0.6f;
   glow(x,y,r*5,C(0x6fb4ff,(int)(40+50*tw)));disk(x,y,r*0.7f,C(0xdcecff,(int)(120+120*tw)));
  }
 }else {
  /* the handheld's blue, the waves drifting across it */
  grad(0,0,480,272,C(T->bg2,255),C(T->bg,255));
  /* (no darkening at the foot: everything on this skin is glass over the blue) */
  ms_wave(150,16,0.011f,t*0.45f,46,40,90);
  ms_wave(166,12,0.008f,-t*0.32f+2,38,30,60);
  ms_wave(140,9,0.015f,t*0.6f+4,24,18,0);
 }
}

/* ---- panels ---- */
/* Zoidstation 1: a flat, matte bevel: one even face, a light edge top-left and a dark edge bottom-right
   (swapped when pressed). No shine: f0 and f1 are mixed into the one face color. */
static void ms1_bevel(float x,float y,float w,float h,float r,unsigned int f0,unsigned int f1,int pressed) {
 rrect(x,y,w,h,r,C(pressed?M1_HI:M1_SH,255));
 rrect(x,y,w-1,h-1,r,C(pressed?M1_SH:M1_HI,255));
 rrect(x+1,y+1,w-2,h-2,r-1,C(mix(f0,f1,110),255));
}
/* Zoidstation 1: a window sunk into the panel */
static void ms1_well(float x,float y,float w,float h,unsigned int fill) {
 rrect(x,y,w,h,3,C(0xffffff,220));rrect(x,y,w-1,h-1,3,C(M1_SH,255));rrect(x+1,y+1,w-2,h-2,2,C(fill,255));
}
/* a round console button (POWER, OPEN): a sunk ring, a domed face, its mark in red */
static void ms1_round(float cx,float cy,float r,int ic,float isz) {
 disk(cx,cy,r+2,C(M1_SH,255));disk(cx-0.5f,cy-0.5f,r+1,C(M1_HI,255));disk(cx,cy,r,C(mix(M1_FACE0,M1_FACE1,110),255));
 circle_line(cx,cy,r*0.78f,r*0.78f,1,C(M1_FACE0,255));
 if(ic>=0)icon(ic,cx,cy,isz,C(M1_ACC,255));
}
/* the ridges down the console's sides: n raised grooves */
static void ms1_ridges(float x,float y,float h,int n){for(int i=0;i<n;i++){rect(x+i*4,y,1,h,C(M1_SH,150));rect(x+i*4+1,y,1,h,C(M1_HI,200));}}
/* the four logo colors in a row */
static void ms1_stripe(float x,float y,float w,float h){float q=w/4;for(int i=0;i<4;i++)rect(x+i*q,y,q+0.5f,h,C(ms1_logo[i],255));}
/* Zoidstation 2: a translucent pane, its top edge lit in the middle */
static void ms2_pane(float x,float y,float w,float h,int lit) {
 rrect(x,y,w,h,6,C(0x0a2c70,lit?110:70));
 hgrad(x+6,y,w/2-6,1,C(0x8cc4ff,0),C(0x8cc4ff,lit?230:120));hgrad(x+w/2,y,w/2-6,1,C(0x8cc4ff,lit?230:120),C(0x8cc4ff,0));
 hgrad(x+6,y+h-1,w/2-6,1,C(0x4f9bff,0),C(0x4f9bff,lit?120:50));hgrad(x+w/2,y+h-1,w/2-6,1,C(0x4f9bff,lit?120:50),C(0x4f9bff,0));
 if(lit)glow(x+w/2,y,w*0.35f,C(M2_GLOW,40));
}
/* Zoidstation Portable: glass, brighter at the top, with a bright edge along it */
static void msp_glass(float x,float y,float w,float h,float r,int strength) {
 rrect(x,y,w,h,r,C(0xffffff,16*strength/4));
 clip((int)x,(int)y,(int)w+1,(int)(h*0.46f)+1);rrect(x,y,w,h,r,C(0xffffff,22*strength/4));unclip();
 hgrad(x+r,y,w/2-r,1,C(0xffffff,30),C(0xffffff,60+30*strength));hgrad(x+w/2,y,w/2-r,1,C(0xffffff,60+30*strength),C(0xffffff,30));
 rect(x+r,y+h-1,w-2*r,1,C(0xffffff,20+8*strength));
}
static void ms_frame(float x,float y,float w,float h,int rack) {
 if(MS1){ms1_bevel(x,y,w,h,5,M1_FACE0,M1_FACE1,0);if(rack)ms1_well(x+5,y+5,w-10,h-10,0x9ea3a9);}
 else if(MS2)ms2_pane(x,y,w,h,0);
 else msp_glass(x,y,w,h,8,rack?2:3);
}
/* a dialog over the screen: menus, the visualizer list, notices */
static void ms_sheet(float x,float y,float w,float h) {
 if(MS1){ms1_bevel(x,y,w,h,6,M1_FACE0,M1_FACE1,0);rect(x+6,y+30,w-12,2,C(M1_DARK,255));ms1_stripe(x+6,y+32,w-12,2);}
 else if(MS2){rrect(x,y,w,h,8,C(0x000a22,235));ms2_pane(x,y,w,h,1);}
 else{rrect(x,y,w,h,10,C(0x1f74c4,200));msp_glass(x,y,w,h,10,4);} /* blue glass, tinted enough to read over the screen behind */
}
static void ms_row_sel(float x,float y,float w,float h) {
 if(MS1){int p=(int)(40*(0.5f+0.5f*sinf(ms_sec()*4)));rect(x,y,w,h,C(mix(M1_SEL,0x24528a,p),255));rect(x,y,3,h,C(M1_RED,255));}
 else if(MS2){glow(x+w/2,y+h/2,w*0.45f,C(M2_GLOW,60));hgrad(x,y,w/2,h,C(0x2a6fd0,0),C(0x2a6fd0,150));hgrad(x+w/2,y,w/2,h,C(0x2a6fd0,150),C(0x2a6fd0,0));
  hgrad(x,y+h-1,w/2,1,C(0xbfe0ff,0),C(0xbfe0ff,220));hgrad(x+w/2,y+h-1,w/2,1,C(0xbfe0ff,220),C(0xbfe0ff,0));}
 else{glow(x+w/2,y+h/2,w*0.4f,C(0xffffff,26));msp_glass(x,y,w,h,h/2>8?8:h/2,4);}
}
/* colors for text on each skin's selection and page */
static unsigned int ms_ink(void){return MS1?M1_INK:MS2?M2_TEXT:0xffffff;}
static unsigned int ms_dim(void){return MS1?M1_DIM:MS2?M2_DIM:MP_DIM;}
static unsigned int ms_sel_ink(void){return MS1?0xffffff:0xffffff;}
static void ms_text(float x,float y,const char *s,unsigned int c,int f,int glowing) {
 if(MS2 && glowing)text_glow(x,y,s,c,f,50);else text(x,y,s,C(c,255),f);
}

/* ---- keys ---- */
static void ms_key(float x,float y,float w,float h,int ic,float isz,int on,int focused) {
 float cx=x+w/2,cy=y+h/2;
 if(MS1) {
  if(focused){rrect(x-3,y-3,w+6,h+6,7,C(M1_ACC,(int)(160+90*sinf(ms_sec()*5))));}
  ms1_bevel(x,y,w,h,4,on?0xfaeec0:M1_FACE0,on?0xf0d77a:M1_FACE1,on);
  if(ic>=0)icon(ic,cx,cy,isz,C(M1_INK,255));
  return;
 }
 if(MS2) {
  /* a translucent cube floating a little */
  float s=(w<h?w:h)*0.78f,d=s*0.22f,bob=focused?2*sinf(ms_sec()*2.2f):0;cy+=bob;
  if(focused || on)glow(cx,cy,s*1.3f,C(M2_GLOW,focused?110:60));
  float l=cx-s/2-d/2,t=cy-s/2+d/2;
  ms_quad(l,t,l+s,t,l+s+d,t-d,l+d,t-d,C(0x9cc8ff,focused?170:110));          /* top */
  ms_quad(l+s,t,l+s+d,t-d,l+s+d,t+s-d,l+s,t+s,C(0x0a2c70,focused?220:170)); /* side */
  grad(l,t,s,s,C(focused?0x4f9bff:0x2a6fd0,focused?220:150),C(0x0c2c70,focused?230:170));
  frame_rect(l,t,s,s,1,C(0xbfe0ff,focused?230:90));
  if(ic>=0)icon(ic,l+s/2,t+s/2,isz*0.85f,C(on || focused?0xffffff:M2_TEXT,255));
  return;
 }
 if(focused){glow(cx,cy,w*0.9f,C(0xffffff,40));msp_glass(x,y,w,h,h/2>9?9:h/2,4);}
 if(ic>=0)icon(ic,cx,cy,isz,C(0xffffff,focused || on?255:200));
 if(on)disk(cx,y+h-3,1.8f,C(0xffffff,230));
}
static void ms_chip(float x,float y,int ic,const char *label) {
 char l[48];ms_case(l,label,sizeof(l));int w=text_w(l,F_SMALL)+(ic>=0?18:8);
 if(MS1){ms1_bevel(x,y,w,16,4,M1_FACE0,M1_FACE1,0);if(ic>=0)icon(ic,x+8,y+8,11,C(M1_INK,255));text_vc(x+(ic>=0?15:4),y+8,l,C(M1_INK,255),F_SMALL);return;}
 if(MS2){if(ic>=0){glow(x+8,y+8,9,C(M2_GLOW,70));icon(ic,x+8,y+8,11,C(0xffffff,255));}text_vc(x+(ic>=0?16:4),y+8,l,C(M2_TEXT,255),F_SMALL);return;}
 if(ic>=0)icon(ic,x+8,y+8,12,C(0xffffff,255));text_vc(x+(ic>=0?16:4),y+8,l,C(0xffffff,255),F_SMALL);
}
static float ms_chip_w(int ic,const char *label){char l[48];ms_case(l,label,sizeof(l));return text_w(l,F_SMALL)+(ic>=0?18:8);}

/* what the transport keys show: icon (or a short label), and whether each is on */
static void ms_key_info(int id,int *ic,int *on,char *label,int cap) {
 *on=0;label[0]=0;
 if(id==0){*ic=IC_SHUFFLE;*on=shuffle;}else if(id==1)*ic=IC_PREV;else if(id==2){*ic=is_playing()?IC_PAUSE:IC_PLAY;*on=is_playing();}
 else if(id==3)*ic=IC_NEXT;else if(id==4){*ic=repeat_mode==1?IC_REPEAT1:IC_REPEAT;*on=repeat_mode!=0;}
 else{*ic=-1;*on=mix_mode!=3;if(mix_mode==2)snprintf(label,cap,"Fade %ds",fade_seconds);else snprintf(label,cap,"%s",mix_names[mix_mode]);}
 if((id==1 || id==3) && skipping()){*ic=-1;snprintf(label,cap,"%s",id==1?"-15 sec":"+30 sec");}
}
static void ms_controls(float y) {
 static const int row[6]={0,1,2,3,4,9};
 if(MS1) {
  ms_frame(4,y,472,36,0);
  const float gap=6,kw=(452-gap*5)/6;
  for(int k=0;k<6;k++) {
   int id=row[k],ic,on;char ml[24];ms_key_info(id,&ic,&on,ml,sizeof(ml));float x=14+k*(kw+gap);
   ms_key(x,y+6,kw,24,ic,16,on,np_focus==id);
   if(ic<0)text_mid(x+kw/2,y+18,ml,C(M1_INK,255),F_SMALL);
   /* an LED in the corner of a key that is on, in one of the logo colors */
   if(on){unsigned int led=id==0?M1_TEAL:id==2?M1_BLUE:id==4?M1_YEL:M1_RED;disk(x+kw-7,y+11,2.6f,C(led,255));}
  }
  return;
 }
 if(MS2) {
  const float step=74,x0=240-step*2.5f;
  for(int k=0;k<6;k++) {
   int id=row[k],ic,on;char ml[24];ms_key_info(id,&ic,&on,ml,sizeof(ml));float cx=x0+k*step;int f=np_focus==id;
   if(ic>=0)ms_key(cx-17,y+3,34,32,ic,16,on,f);
   else{if(f)ms_row_sel(cx-34,y+8,68,22);ms_text(cx-text_w(ml,F_SMALL)/2,y+10,ml,on || f?0xffffff:M2_DIM,F_SMALL,f);}
  }
  return;
 }
 msp_glass(4,y+2,472,32,16,2);
 const float step=76,x0=240-step*2.5f;
 for(int k=0;k<6;k++) {
  int id=row[k],ic,on;char ml[24];ms_key_info(id,&ic,&on,ml,sizeof(ml));float cx=x0+k*step;int f=np_focus==id;
  if(ic>=0)ms_key(cx-22,y+4,44,28,ic,id==2?20:16,on,f);
  else{if(f)ms_key(cx-34,y+4,68,28,-1,0,0,1);text_mid(cx,y+18,ml,C(0xffffff,on || f?255:190),F_SMALL);}
 }
}

/* ---- the seek bar ---- */
static void ms_seek_at(float wx,float ww,float wy) {
 char buf[16];int focus=np_focus==5,pos=seek_preview>=0?seek_preview:elapsed;
 float frac=duration>0?(float)pos/duration:row_live() && state==2?1:0;if(frac>1)frac=1;float fx=wx+ww*frac;
 unsigned int tc=focus?(MS1?M1_ACC:0xffffff):ms_dim();
 time_text(buf,sizeof(buf),pos);text_right(wx-8,wy-text_height(F_SMALL)/2.0f,buf,C(tc,255),F_SMALL);
 end_text(buf,sizeof(buf),pos);text(wx+ww+8,wy-text_height(F_SMALL)/2.0f,buf,C(ms_dim(),255),F_SMALL);
 if(MS1) {
  /* a groove of blocks that fill blue, like the memory card's, and a beveled knob */
  ms1_well(wx,wy-5,ww,10,M1_LCD);
  int n=(int)(ww/8);for(int i=0;i<n;i++){float bx=wx+2+i*(ww-4)/n;int lit=bx<fx;rect(bx,wy-3,(ww-4)/n-1.5f,6,C(lit?M1_FILL:0x2a2a2e,255));}
  ms1_bevel(fx-5,wy-8,10,16,3,M1_FACE0,M1_FACE1,0);
  if(focus)frame_rect(wx-4,wy-11,ww+8,22,1.5f,C(M1_ACC,255));
 }else if(MS2) {
  hgrad(wx,wy-0.5f,ww/2,1,C(M2_GLOW,10),C(M2_GLOW,90));hgrad(wx+ww/2,wy-0.5f,ww/2,1,C(M2_GLOW,90),C(M2_GLOW,10));
  if(fx>wx){hgrad(wx,wy-1,fx-wx,2,C(0x9fd0ff,40),C(0xffffff,255));glow((wx+fx)/2,wy,(fx-wx)*0.5f+6,C(M2_GLOW,30));}
  glow(fx,wy,focus?18:12,C(M2_GLOW,focus?200:140));disk(fx,wy,focus?3.5f:2.5f,C(0xffffff,255));
  if(focus){hgrad(wx,wy+8,ww/2,1,C(0xbfe0ff,0),C(0xbfe0ff,160));hgrad(wx+ww/2,wy+8,ww/2,1,C(0xbfe0ff,160),C(0xbfe0ff,0));}
 }else {
  rrect(wx,wy-2.5f,ww,5,2.5f,C(0xffffff,50));
  if(fx>wx+2)rrect(wx,wy-2.5f,fx-wx,5,2.5f,C(0xffffff,230));
  if(focus){glow(fx,wy,16,C(0xffffff,90));disk(fx,wy,5,C(0xffffff,255));}
 }
}

/* "Now Playing" and the details after it, in ordinary case */
static void ms_status(char *what,int wcap,char *more,int mcap) {
 AudioTrack *t=&tracks[current_slot];more[0]=0;
 if(row_live()){const char *ph=radio_phase_text();snprintf(what,wcap,"%s",ph?ph:paused?"Paused":"On the air");}
 else snprintf(what,wcap,"%s",error_code?"Could not play":state==1?"Reading":!state?"Stopped":paused?"Paused":"Now Playing");
 if(row_live()) {
  if(error_code)snprintf(more,mcap,"%s",radio_error_text((int)error_code));
  else if(radio_status.kbps>0)snprintf(more,mcap,"Station %d  -  %s  %d kbps",queue_count?queue_pos+1:0,radio_status.codec==2?"AAC":"MP3",radio_status.kbps);
 }else if(error_code && radio_is_url(active) && (int)error_code<=-1001 && (int)error_code>=-1199)snprintf(more,mcap,"%s",radio_error_text((int)error_code));
 else if(error_code)snprintf(more,mcap,"Error %d / %08X",stage,error_code);
 else if(state==2) {
  int kbps=duration>0 && t->index.frames?(int)((long long)(t->index.end-t->index.offsets[0])/125/duration):0;
  int n=snprintf(more,mcap,"Track %d of %d",queue_count?queue_pos+1:0,queue_count);
  if(kbps>0 && n<mcap)n+=snprintf(more+n,mcap-n,"  -  %d kbps",kbps);
  if(t->rate && n<mcap)snprintf(more+n,mcap-n,"  -  %d.%d kHz",t->rate/1000,(t->rate%1000)/100);
 }
}

/* ---- the spinning disc (Zoidstation 1) ---- */
static float ms1_spin;static unsigned long long ms1_spin_at;
static void ms1_disc(float cx,float cy,float r) {
 if(ms1_spin_at && is_playing())ms1_spin+=(now_ms-ms1_spin_at)*0.0042f;
 ms1_spin_at=now_ms;if(ms1_spin>6283.2f)ms1_spin-=6283.2f;
 disk(cx+2,cy+3,r,C(0x000000,60)); /* its shadow */
 /* like the console's own discs: a black hub and a thin black stroke at the edge, chrome between */
 disk(cx,cy,r,C(0x0c0c0e,255));
 disk(cx,cy,r-1.5f,C(0xd4d7da,255));
 /* the printed label: the four colors in quarters, turning */
 for(int q=0;q<4;q++){float a=ms1_spin+q*1.5708f;ms_arc(cx,cy,r*0.70f,r*0.84f,a+0.08f,a+1.49f,C(ms1_logo[q],170));}
 /* the rainbow sheen stays where the light is */
 static const unsigned int sheen[6]={0xff8fa8,0xffd27a,0xb6f08a,0x7ae0ff,0x9aa8ff,0xe09aff};
 for(int k=0;k<6;k++){float a=-2.4f+k*0.14f;ms_arc(cx,cy,r*0.36f,r-1.5f,a,a+0.14f,C(sheen[k],40));ms_arc(cx,cy,r*0.36f,r-1.5f,a+3.1416f,a+3.2816f,C(sheen[k],40));}
 disk(cx,cy,r*0.34f,C(0x0c0c0e,255));
 disk(cx,cy,r*0.14f,C(mix(M1_FACE0,M1_FACE1,140),255)); /* the hole shows the lid beneath */
}

/* ---- Now Playing ---- */
static void ms_rack(float top) {
 char nm[64];int vc=visual_count;
 if(!vc)return;
 float gap=6,ph=268-top-(MS1?10:4),pw=((MS1?452:472)-gap*(vc-1))/vc,py=top+(MS1?6:0),x0=MS1?14:4;
 for(int i=0;i<vc;i++) {
  float px=x0+i*(pw+gap);int focused=np_focus==6+i;
  if(MS1){ms1_well(px,py,pw,ph,M1_LCD);if(focused)frame_rect(px-2,py-2,pw+4,ph+4,1.5f,C(M1_ACC,255));}
  else if(MS2){rrect(px,py,pw,ph,6,C(0x000818,150));ms2_pane(px,py,pw,ph,focused);}
  else msp_glass(px,py,pw,ph,8,focused?4:2);
  clip((int)px+2,(int)py+14,(int)pw-4,(int)ph-16);visualizer(visuals[i],i,px+5,py+15,pw-10,ph-19);unclip();
  snprintf(nm,sizeof(nm),"%s",visual_names[visuals[i]]);
  unsigned int lc=MS1?(focused?0xffffff:0x8f969c):focused?0xffffff:ms_dim();
  float w=text(px+7,py+1,nm,C(lc,255),F_SMALL);
  if(focused)text_fit(px+7+w+8,py+1,"X next, Triangle list",C(MS1?0x8f969c:ms_dim(),200),F_SMALL,(int)(pw-w-24));
  if(toast_panel==i && now_ms<toast_until){int tw=text_w(toast,F_MEDIUM)+20;float cx=px+pw/2;ms_sheet(cx-tw/2,py+ph/2-13,tw,26);text_mid(cx,py+ph/2,toast,C(MS1?M1_INK:0xffffff,255),F_MEDIUM);}
 }
}
static void ms1_now(void) {
 char buf[64],what[48],more[96];
 ms_frame(4,4,472,108,0);
 /* the lid: a raised ring around the disc, as on the console's top */
 disk(56,58,48,C(M1_SH,255));disk(55.5f,57.5f,47,C(M1_HI,255));disk(56,58,46,C(mix(M1_FACE0,M1_FACE1,140),255)); /* centered in the panel (4..112) */
 ms1_disc(56,58,42);
 /* the readout: track and time, teal on black, each centered on the window's middle line */
 ms1_well(110,10,190,40,M1_LCD);
 int pos=seek_preview>=0?seek_preview:elapsed;
 {int cs=clock_secs(pos);snprintf(buf,sizeof(buf),"%d:%02d",cs/60%100,cs%60);}
 {const float mid=30;float lw=text_vc(118,mid,row_live()?"STATION":"TRACK",C(M1_LCDDIM,255),F_SMALL);
  snprintf(what,sizeof(what),"%02d",queue_count?queue_pos+1:0);text_vc(118+lw+6,mid,what,C(M1_LCDTXT,255),F_LARGE);
  text_vc(292-text_w(buf,F_CLOCK),mid,buf,C(M1_LCDTXT,255),F_CLOCK);}
 ms_status(what,sizeof(what),more,sizeof(more));
 marquee(&mq_title,np_key,110,54,np_title[0]?np_title:"Ready",C(M1_INK,255),F_MEDIUM,190);
 marquee(&mq_artist,np_key,110,72,np_artist[0]?np_artist:what,C(M1_DIM,255),F_SMALL,190);
 /* the CD player's grid of track numbers: ten around the one playing */
 int n=queue_count,cur=queue_count?queue_pos:-1,first=cur>=0?cur/10*10:0;
 text(310,10,what,C(M1_DARK,255),F_SMALL);
 for(int k=0;k<10;k++) {
  int tn=first+k;float bx=310+(k%5)*32,by=26+(k/5)*28;
  if(tn<n){int on=tn==cur;ms1_bevel(bx,by,28,24,3,on?M1_ACC:M1_FACE0,on?M1_ACC:M1_FACE1,on);snprintf(buf,sizeof(buf),"%d",tn+1);text_mid(bx+14,by+12,buf,C(on?0xffffff:M1_INK,255),F_SMALL);}
  else ms1_well(bx,by,28,24,0x9ea3a9);
 }
 ms_seek_at(146,280,97);
 ms_controls(116);
 if(visual_count){ms_frame(4,156,472,112,0);ms_rack(156);}
 else {
  ms_frame(4,156,472,112,0);
  if(art_ready){ms1_well(12,164,96,96,0);art_draw(13,165,94,0xffffffff);}else{ms1_well(12,164,96,96,0x9ea3a9);icon(row_live()?IC_RADIO:IC_NOTE,60,212,40,C(M1_DIM,255));}
  text_fit(122,170,np_title,C(M1_INK,255),F_LARGE,340);text_fit(122,200,np_artist,C(M1_DIM,255),F_MEDIUM,340);text_fit(122,224,np_album,C(M1_FAINT,255),F_MEDIUM,340);
  ms1_stripe(122,250,120,3);
 }
}
static void ms2_now(void) {
 char buf[64],what[48],more[96];
 int pos=seek_preview>=0?seek_preview:elapsed;
 ms_status(what,sizeof(what),more,sizeof(more));
 /* the kicker, letter-spaced, then the title and the rest, all lit from within */
 {float x=20;for(const char *p=what;*p;p++){char c[2]={(char)toupper((unsigned char)*p),0};x+=text(x,10,c,C(0x8cc4ff,255),F_SMALL)+2;}
  if(more[0])text_fit(x+12,10,more,C(M2_FAINT,255),F_SMALL,(int)(330-x));}
 glow(170,44,150,C(M2_GLOW,28));
 clip(20,24,320,34);marquee(&mq_title,np_key,20,28,np_title[0]?np_title:"Ready",C(0xffffff,255),F_LARGE,320);unclip();
 marquee(&mq_artist,np_key,20,58,np_artist,C(M2_TEXT,255),F_MEDIUM,320);
 marquee(&mq_album,np_key,20,76,np_album,C(M2_DIM,255),F_SMALL,320);
 {int cs=clock_secs(pos);snprintf(buf,sizeof(buf),"%d:%02d",cs/60%100,cs%60);}
 float cw=text_w(buf,F_CLOCK);glow(462-cw/2,46,cw*0.8f,C(M2_GLOW,60));text_glow(462-cw,46-fonts[F_CLOCK].ch/2.0f,buf,0xe6f2ff,F_CLOCK,60);
 text_right(462,70,time_left?"remaining":"elapsed",C(M2_DIM,255),F_SMALL);
 ms_seek_at(50,380,97);
 ms_controls(116);
 if(visual_count)ms_rack(158);
 else {
  if(art_ready){glow(70,212,70,C(M2_GLOW,70));art_draw(22,164,96,0xffffffff);}else{ms_key(22,164,96,96,row_live()?IC_RADIO:IC_NOTE,40,0,0);}
  text_fit(134,172,np_title,C(0xffffff,255),F_LARGE,330);text_fit(134,202,np_artist,C(M2_TEXT,255),F_MEDIUM,330);text_fit(134,224,np_album,C(M2_DIM,255),F_MEDIUM,330);
 }
}
/* Zoidstation Portable: the cover in a glass frame with its reflection, the details beside it */
static void msp_cover(float x,float y,float s) {
 msp_glass(x-3,y-3,s+6,s+6,4,3);
 if(art_ready) {
  art_draw(x,y,s,0xffffffff);
  /* the reflection: the cover upside down, fading */
  sceGuTexImage(0,ART_N,ART_N,ART_N,art_tex);sceGuTexFlush();linear(1);
  float rh=s*0.22f;sprite(0,ART_N,ART_N,ART_N*(1-rh/s),x,y+s+4,s,rh,C(0xffffff,70));
  linear(0);sceGuTexImage(0,512,512,512,atlas);sceGuTexFlush();
  grad(x,y+s+4,s,rh,C(mix(T->bg2,T->bg,110),0),C(mix(T->bg2,T->bg,110),210)); /* fades into the blue behind it */
 }else{rrect(x,y,s,s,4,C(0xffffff,30));icon(row_live()?IC_RADIO:IC_NOTE,x+s/2,y+s/2,s*0.45f,C(0xffffff,220));}
}
static void msp_now(void) {
 char buf[64],what[48],more[96];
 ms_status(what,sizeof(what),more,sizeof(more));
 msp_cover(14,10,72);
 float tx=100,tw=360;
 text(tx,8,what,C(0xffffff,255),F_SMALL);if(more[0])text_fit(tx+text_w(what,F_SMALL)+10,8,more,C(MP_DIM,255),F_SMALL,(int)(tw-text_w(what,F_SMALL)-10));
 marquee(&mq_title,np_key,tx,24,np_title[0]?np_title:"Ready",C(0xffffff,255),F_LARGE,(int)tw);
 marquee(&mq_artist,np_key,tx,52,np_artist,C(MP_DIM,255),F_MEDIUM,(int)tw);
 marquee(&mq_album,np_key,tx,70,np_album,C(mix(T->dim,T->bg2,60),255),F_SMALL,(int)tw);
 ms_seek_at(140,290,97);
 (void)buf;
 ms_controls(116);
 if(visual_count)ms_rack(158);
 else {
  msp_glass(4,158,472,110,8,2);
  text_fit(20,172,np_title,C(0xffffff,255),F_LARGE,440);text_fit(20,202,np_artist,C(MP_DIM,255),F_MEDIUM,440);text_fit(20,224,np_album,C(mix(T->dim,T->bg2,60),255),F_MEDIUM,440);
 }
}
static void ms_now(void){if(MS1)ms1_now();else if(MS2)ms2_now();else msp_now();}

/* ---- the mini player along the foot of the lists and Home ---- */
static void ms_mini(void) {
 char t1[16],t2[16],buf[48];float y=230;
 if(MS1)ms1_bevel(4,y,472,38,5,M1_FACE0,M1_FACE1,0);
 else if(MS2){rrect(4,y,472,38,8,C(0x000818,170));ms2_pane(4,y,472,38,0);}
 else msp_glass(4,y,472,38,10,4);
 if(!active[0]) {
  text(16,y+10,"Nothing playing",C(ms_dim(),255),F_MEDIUM);
  ms_chip(468-ms_chip_w(IC_BTN_CROSS,"Play"),y+11,IC_BTN_CROSS,"Play");return;
 }
 if(MS1)ms1_disc(23,y+19,14);
 else if(MS2)ms_key(10,y+4,28,30,is_playing()?IC_PAUSE:IC_PLAY,14,is_playing(),0);
 else{if(art_ready)art_draw(10,y+5,28,0xffffffff);else icon(is_playing()?IC_PAUSE:IC_PLAY,24,y+19,18,C(0xffffff,255));}
 text(44,y+1,row_live()?"On the air":is_playing()?"Now Playing":"Paused",C(MS1?M1_ACC:MS2?0x8cc4ff:MP_DIM,255),F_SMALL);
 marquee(&mq_mini,np_key,44,y+15,np_title,C(ms_ink(),255),F_MEDIUM,244);
 int pos=seek_preview>=0?seek_preview:elapsed;
 time_text(t1,sizeof(t1),pos);if(row_live())snprintf(t2,sizeof(t2),"live");else time_text(t2,sizeof(t2),duration);snprintf(buf,sizeof(buf),"%s / %s",t1,t2);
 text_right(372,y+3,buf,C(ms_dim(),255),F_SMALL);
 if(duration>0){float f=(float)pos/duration;if(f>1)f=1;
  if(MS1){ms1_well(300,y+22,72,6,M1_LCD);rect(301,y+23,70*f,4,C(M1_FILL,255));}
  else if(MS2){rect(300,y+25,72,1,C(M2_GLOW,60));rect(300,y+24.5f,72*f,2,C(0xffffff,255));glow(300+72*f,y+25,6,C(M2_GLOW,160));}
  else{rrect(300,y+23,72,4,2,C(0xffffff,50));if(f>0.04f)rrect(300,y+23,72*f,4,2,C(0xffffff,230));}}
 ms_chip(382,y+11,IC_BTN_TRIANGLE,"Playing");
}

/* ---- lists: the music library, radio and podcasts ---- */
static int ms_row_h(void){return MS1?22:MS2?24:26;}
static int ms_list_icon(void){return in_radio?IC_RADIO:in_pod?IC_MIC:in_video_folder()?IC_FILM:IC_NOTE;}
/* the page heading: where you are, and a line about it */
static void ms_heading(const char *title,const char *info,int ic) {
 if(MS1) {
  ms1_bevel(4,4,472,36,5,M1_FACE0,M1_FACE1,0);
  ms1_round(22,22,11,ic,13);ms1_ridges(366,10,24,8); /* left of the battery */
  text_fit(40,5,title,C(M1_INK,255),F_MEDIUM,250);text_fit(40,22,info,C(M1_DIM,255),F_SMALL,318);
  ms1_stripe(4,40,472,3);
 }else if(MS2) {
  glow(26,22,20,C(M2_GLOW,90));icon(ic,26,22,20,C(0xffffff,255));
  text_glow(44,4,title,0xffffff,F_MEDIUM,40);text_fit(44,23,info,C(M2_DIM,255),F_SMALL,340);
  hgrad(12,42,228,1,C(M2_GLOW,0),C(M2_GLOW,200));hgrad(240,42,228,1,C(M2_GLOW,200),C(M2_GLOW,0));
 }else {
  /* the column's icon at top left, as on the handheld */
  glow(30,22,22,C(0xffffff,40));icon(ic,30,22,26,C(0xffffff,255));
  text_fit(52,4,title,C(0xffffff,255),F_MEDIUM,320);text_fit(52,23,info,C(MP_DIM,255),F_SMALL,330);
 }
}
static void ms_library(void) {
 char buf[160],label[256],path[PATHLEN];
 const char *name=strrchr(folder,'/');name=(strlen(folder)<=strlen(root) || !name)?(in_video_folder()?"Video":"Music"):name+1;
 char pln[128];if(in_playlist){const char *b=strrchr(playlist_file,'/');snprintf(pln,sizeof(pln),"%s",b?b+1:playlist_file);char *d=strrchr(pln,'.');if(d)*d=0;name=pln;}
 if(in_radio)name=radio_title;
 if(in_pod)name=pod_title;
 int songs=0;for(int i=0;i<count;i++)if(!entries[i].folder)songs++;
 if(in_radio || in_pod){if(in_pod)pod_info(buf,sizeof(buf));else radio_info(buf,sizeof(buf));}
 else if(entries_hidden())snprintf(buf,sizeof(buf),"Showing %d of %d  -  hold X for more",count,entries_total);
 else if(songs)snprintf(buf,sizeof(buf),"%d songs  -  hold X for more",songs);else snprintf(buf,sizeof(buf),"%d %s  -  hold X for more",count,count==1?"item":"items");
 ms_heading(name,buf,ms_list_icon());
 const float top=MS1?48:48;const int rh=ms_row_h(),rows=(int)((226-top)/rh);
 if(cursor<scroll)scroll=cursor;if(cursor>=scroll+rows)scroll=cursor-rows+1;
 lib_scroll_f+=(scroll-lib_scroll_f)*0.4f;lib_sel_f+=(cursor-lib_sel_f)*0.5f;
 if(fabsf(scroll-lib_scroll_f)<0.01f)lib_scroll_f=scroll;if(fabsf(cursor-lib_sel_f)<0.01f)lib_sel_f=cursor;
 if(MS1)ms1_well(4,top-2,472,rows*rh+4,M1_WELL);
 clip(0,(int)top,480,rows*rh);
 if(count){float sy=top+(lib_sel_f-lib_scroll_f)*rh;ms_row_sel(MS1?6:8,sy,MS1?468:464,rh-(MS1?0:2));}
 for(int i=(int)lib_scroll_f;i<count && i<=(int)lib_scroll_f+rows;i++) {
  Entry *e=&entries[i];float y=top+(i-lib_scroll_f)*rh,cy=y+rh/2.0f-(MS1?0:1);int sel=i==cursor;
  entry_label(e,label,sizeof(label));
  int playing=0;if(!e->folder && active[0] && entry_path(i,path,sizeof(path)))playing=!strcmp(path,active);
  unsigned int tc=sel?ms_sel_ink():playing?(MS1?M1_RED:0xffffff):ms_ink(),ac=sel?(MS1?0xd6e2f4:0xffffff):ms_dim();
  if(MS1 && !sel && (i&1))rect(6,y,468,rh,C(0xbfc3c8,255));
  int ic=e->folder?row_icon(e):playing?IC_WAVE:in_radio?IC_RADIO:in_pod?pod_ep_icon(i):-1;
  unsigned int icc=MS1?(sel?0xffffff:e->folder?M1_SEL:playing?M1_RED:M1_DIM):(sel || playing?0xffffff:ms_dim());
  if(ic>=0)icon(ic,22,cy,MSP && sel?18:15,C(icc,255));
  if(MS2 && playing && !sel)glow(22,cy,10,C(M2_GLOW,90));
  int tw=e->folder && !e->artist[0]?420:262;float ty=cy-fonts[F_MEDIUM].ch/2.0f;
  if(sel){marquee(&mq_row,i*7919+count,36,ty,label,C(tc,255),F_MEDIUM,tw);}
  else text_fit(36,ty,label,C(tc,MSP?200:255),F_MEDIUM,tw);
  if(e->folder && e->folder!=4 && !e->artist[0] && !row_is_action(e->folder))icon(IC_CHEV_R,462,cy,12,C(ac,255));
  else if(e->artist[0])text_fit(316,cy-fonts[F_SMALL].ch/2.0f,e->artist,C(ac,255),F_SMALL,148);
 }
 unclip();
 if(count>rows){float track=rows*rh,thumb=track*rows/count;if(thumb<12)thumb=12;float ty=top+(track-thumb)*lib_scroll_f/(count-rows);
  if(MS1){rect(472,top,3,track,C(0x9ea3a9,255));rect(472,ty,3,thumb,C(M1_DARK,255));}
  else{rect(475,top,1,track,C(0xffffff,40));rrect(474,ty,3,thumb,1.5f,C(0xffffff,MS2?200:220));if(MS2)glow(475,ty+thumb/2,8,C(M2_GLOW,90));}}
 {char l1[96],l2[96];
  if(radio_empty(l1,sizeof(l1),l2,sizeof(l2)) || pod_empty(l1,sizeof(l1),l2,sizeof(l2))){text_center(240,110,l1,C(ms_ink(),radio_waiting() || pod_waiting()?np_pulse():255),F_MEDIUM);text_center(240,134,l2,C(ms_dim(),255),F_SMALL);}
  else if(!count){text_center(240,110,"No files here",C(ms_ink(),255),F_MEDIUM);
   text_center(240,134,in_video_folder()?"Put .mp4 videos in the VIDEO folder on your Memory Stick":"Put MP3s in the MUSIC folder on your Memory Stick",C(ms_dim(),255),F_SMALL);}}
 ms_mini();
}

/* ---- Settings ---- */
static void ms_settings(void) {
 char v[48],b[110];
 ms_heading("Settings","ZOID 1.0",IC_GEAR);
 const float top=48;const int rh=ms_row_h(),rows=MS1?7:MSP?6:7;
 if(set_item<set_scroll)set_scroll=set_item;if(set_item>=set_scroll+rows)set_scroll=set_item-rows+1;
 set_scroll_f+=(set_scroll-set_scroll_f)*0.4f;set_sel_f+=(set_item-set_sel_f)*0.5f;
 if(fabsf(set_scroll-set_scroll_f)<0.01f)set_scroll_f=set_scroll;if(fabsf(set_item-set_sel_f)<0.01f)set_sel_f=set_item;
 if(MS1)ms1_well(4,top-2,472,rows*rh+4,M1_WELL);
 clip(0,(int)top,480,rows*rh);
 ms_row_sel(MS1?6:8,top+(set_sel_f-set_scroll_f)*rh,MS1?468:464,rh-(MS1?0:2));
 for(int i=(int)set_scroll_f;i<SET_N && i<=(int)set_scroll_f+rows;i++) {
  int id=SID(i);float y=top+(i-set_scroll_f)*rh,cy=y+rh/2.0f-(MS1?0:1);int sel=i==set_item,off=set_off(id);
  if(i==0 || strcmp(set_section_of(i),set_section_of(i-1))) {
   unsigned int sc=MS1?(sel?M1_YEL:ms1_logo[(i/5)%4]):sel?0xffffff:MS2?0x8cc4ff:MP_DIM;
   text(12,cy-fonts[F_SMALL].ch/2.0f,set_section_of(i),C(sc,255),F_SMALL);
  }
  unsigned int lc=sel?ms_sel_ink():id==SET_EXIT?(MS1?M1_RED:0xffb0b0):off?(MS1?M1_FAINT:MS2?M2_FAINT:T->faint):ms_ink();
  text(84,cy-fonts[F_MEDIUM].ch/2.0f,set_labels[id],C(lc,MSP && !sel?210:255),F_MEDIUM);
  setting_value(id,v,sizeof(v));
  if(v[0]){unsigned int vc=sel?(MS1?0xd6e2f4:0xffffff):off?lc:(MS1?M1_SEL:MS2?0x9fd0ff:MP_DIM);
   if(sel){snprintf(b,sizeof(b),"< %s >",v);text_fit(250,cy-fonts[F_MEDIUM].ch/2.0f,b,C(vc,255),F_MEDIUM,222);}
   else text_fit(250+text_w("< ",F_MEDIUM),cy-fonts[F_MEDIUM].ch/2.0f,v,C(vc,255),F_MEDIUM,210);}
 }
 unclip();
 /* what the selected option does */
 float dy=top+rows*rh+6;
 if(MS1)ms1_stripe(8,dy-3,464,2);else{hgrad(8,dy-3,232,1,C(0xffffff,0),C(0xffffff,MS2?90:110));hgrad(240,dy-3,232,1,C(0xffffff,MS2?90:110),C(0xffffff,0));}
 wrap2(10,dy,460,set_desc[SID(set_item)],C(ms_dim(),255),F_SMALL,13);
 int id=SID(set_item);const char *act=id==SET_EXIT?"Exit":(id==17 || id==18)?"Open":"Change";
 float x=10;ms_chip(x,240,IC_BTN_CROSS,act);x+=6+ms_chip_w(IC_BTN_CROSS,act);
 ms_chip(x,240,IC_BTN_CIRCLE,"Back");x+=6+ms_chip_w(IC_BTN_CIRCLE,"Back");
 text_vc(x+4,248,"Left / Right to adjust",C(ms_dim(),255),F_SMALL);
}
