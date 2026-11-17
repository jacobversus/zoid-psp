/* GPU drawing. One 512x512 texture (the atlas) holds the fonts, icons,
   disks and a white texel, so every shape uses the same vertex format. */
#include <pspgu.h>

static unsigned int gu_list[0x40000] __attribute__((aligned(64)));
static unsigned int atlas[512*512] __attribute__((aligned(64)));
#define ART_N 256 /* cover art texture size; big enough for the 200 px Zoid cover */
static unsigned int art_tex[ART_N*ART_N] __attribute__((aligned(64)));
static int art_ready;static unsigned int art_tone;
static void *draw_buf;
static char capture_path[64];
static unsigned long long frame_start_us;static float cpu_ms;

typedef struct {float u,v;unsigned int c;float x,y,z;} Vtx;
#define VFMT (GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D)
#define WHITE_UV 508.0f

/* Colors are written as 0xRRGGBB; the GPU wants 0xAABBGGRR. color_remap, when set, recolors a whole
   skin as it draws (Y2K's Theme color: y2k_remap in main.c). */
static unsigned int (*color_remap)(unsigned int);
static unsigned int C(unsigned int rgb,int a){if(a<0)a=0;if(a>255)a=255;if(color_remap)rgb=color_remap(rgb);return ((unsigned int)a<<24)|((rgb&255)<<16)|(rgb&0xff00)|((rgb>>16)&255);}
static unsigned int mix(unsigned int a,unsigned int b,int t) {
 unsigned int out=0;for(int s=0;s<24;s+=8){int x=(a>>s)&255,y=(b>>s)&255;out|=(unsigned int)(x+(y-x)*t/255)<<s;}return out;
}

/* ---- atlas layout ---- */
typedef struct {const unsigned char *data;const unsigned char *widths;int cw,ch,ax,ay,per,first,count;} FontInfo;
/* ui_paper: the Paper skin (and Books) is matte ink on paper: no glow, and words keep their own case.
   text_italic: Paper sets titles in italic by slanting the letters (the atlas holds upright Cambria). */
static int ui_paper,ui_keepcase;static float text_italic; /* ui_keepcase: Paper, Books and the Zoidstation skins write in ordinary case */
static FontInfo fonts[4];static float ink_mid[4],ink_cap[4],ink_base[4]; /* ink_cap: height of H; ink_base: its baseline from the cell top */
static int glyph_index_raw(int f,unsigned char c){int g=c-fonts[f].first;return g<0 || g>=fonts[f].count?0:g;}
#define ICON_CELL 32
#define ICON_Y 362
#define ICONS_PER_ROW 14
#define DISK64_X 448
#define DISK64_Y 362
#define DISK16_X 448
#define DISK16_Y 426
#define GLOW_X 464
#define GLOW_Y 426
enum {IC_PLAY,IC_PAUSE,IC_NEXT,IC_PREV,IC_SHUFFLE,IC_REPEAT,IC_REPEAT1,IC_STOP,IC_FOLDER,IC_NOTE,IC_CHEV_R,IC_CHEV_L,
      IC_SEEK_F,IC_SEEK_B,IC_DOTS,IC_BTN_CIRCLE,IC_BTN_TRIANGLE,IC_BTN_CROSS,IC_BTN_SQUARE,IC_WAVE,IC_RADIO,IC_SEARCH,
      IC_FILM,IC_MIC,IC_GEAR,IC_DOWNLOAD,IC_CHECK,IC_PLUS,IC_BOOK,IC_BOOKMARK,IC_COUNT}; /* two rows of 14, then spare cells */
/* Where an icon sits in the atlas. Past the two rows, ids 28 and 29 take the two free cells right of the clock digits. */
static void icon_cell(int id,int *x,int *y) {
 if(id>=2*ICONS_PER_ROW){*x=384+(id-2*ICONS_PER_ROW)*ICON_CELL;*y=426;}
 else{*x=(id%ICONS_PER_ROW)*ICON_CELL;*y=ICON_Y+(id/ICONS_PER_ROW)*ICON_CELL;}
}

/* style 0 Zoid (Bahnschrift Bold), 1 Cyberpunk (Bahnschrift SemiBold Condensed), 2 Phosphor (Lucida Console) */
#define FONT_SET(n) fonts[0]=(FontInfo){&font_##n##_small[0][0],widths_##n##_small,20,18,0,0,24,32,95};\
 fonts[1]=(FontInfo){&font_##n##_medium[0][0],widths_##n##_medium,24,22,0,72,21,32,95};\
 fonts[2]=(FontInfo){&font_##n##_large[0][0],widths_##n##_large,28,30,0,182,18,32,95};\
 fonts[3]=(FontInfo){&font_##n##_clock[0][0],widths_##n##_clock,24,44,0,426,16,43,16};
static void fonts_build(int style) {
 ui_paper=style==5;ui_keepcase=style>=5;
 if(style==5){FONT_SET(paper)} /* Paper and Books: Cambria (font_paper.h) */
 else if(style==6){FONT_SET(ms1)}else if(style==7){FONT_SET(ms2)}else if(style==8){FONT_SET(msp)} /* font_ms.h */
 else if(style==1 || style==4){FONT_SET(cyber)}else if(style==2){FONT_SET(ph)}else{FONT_SET(zoid)}
 /* ink_mid: the line's optical middle in the cell, which text() puts on the middle of its box. Centering
    the capitals alone left mixed-case text looking low (the lowercase and the tails of g, p, y all sit
    below the capitals' middle), so it is the middle of the capital's top to the tail's bottom.
    The clock (digits only) centers its digits. */
 for(int f=0;f<4;f++){int g=glyph_index_raw(f,f==3?(unsigned char)'0':(unsigned char)'H');const unsigned char *src=fonts[f].data+g*fonts[f].cw*fonts[f].ch;int top=-1,bot=0;
  for(int y=0;y<fonts[f].ch;y++)for(int x=0;x<fonts[f].cw;x++)if(src[y*fonts[f].cw+x]>100){if(top<0)top=y;bot=y;}
  int desc=bot;
  if(f!=3 && top>=0){const unsigned char *p=fonts[f].data+glyph_index_raw(f,'p')*fonts[f].cw*fonts[f].ch;
   for(int y=0;y<fonts[f].ch;y++)for(int x=0;x<fonts[f].cw;x++)if(p[y*fonts[f].cw+x]>100 && y>desc)desc=y;}
  ink_mid[f]=top<0?fonts[f].ch/2.0f:(top+desc+1)/2.0f;ink_cap[f]=top<0?fonts[f].ch*0.6f:bot-top+1;ink_base[f]=top<0?fonts[f].ch*0.8f:bot+1;}
 for(int f=0;f<4;f++){FontInfo *fi=&fonts[f];
  for(int g=0;g<fi->count;g++){int gx=fi->ax+(g%fi->per)*fi->cw,gy=fi->ay+(g/fi->per)*fi->ch;const unsigned char *src=fi->data+g*fi->cw*fi->ch;
   for(int y=0;y<fi->ch;y++)for(int x=0;x<fi->cw;x++)atlas[(gy+y)*512+gx+x]=((unsigned int)src[y*fi->cw+x]<<24)|0xffffff;}
 }
 sceKernelDcacheWritebackAll();
}

/* Signed distance shapes: negative inside. Coverage comes from distance,
   which gives smooth antialiased edges without supersampling. */
static float clampf(float v,float a,float b){return v<a?a:v>b?b:v;}
static float dmin(float a,float b){return a<b?a:b;}
static float sd_circle(float px,float py,float cx,float cy,float r){float dx=px-cx,dy=py-cy;return sqrtf(dx*dx+dy*dy)-r;}
static float sd_seg(float px,float py,float ax,float ay,float bx,float by,float r) {
 float pax=px-ax,pay=py-ay,bax=bx-ax,bay=by-ay,h=clampf((pax*bax+pay*bay)/(bax*bax+bay*bay),0,1);
 float dx=pax-bax*h,dy=pay-bay*h;return sqrtf(dx*dx+dy*dy)-r;
}
static float sd_box(float px,float py,float cx,float cy,float hw,float hh,float r) {
 float qx=fabsf(px-cx)-hw+r,qy=fabsf(py-cy)-hh+r,ox=qx>0?qx:0,oy=qy>0?qy:0;
 float in=qx>qy?qx:qy;if(in>0)in=0;return sqrtf(ox*ox+oy*oy)+in-r;
}
static float sd_tri(float px,float py,float x0,float y0,float x1,float y1,float x2,float y2) {
 float e0x=x1-x0,e0y=y1-y0,e1x=x2-x1,e1y=y2-y1,e2x=x0-x2,e2y=y0-y2;
 float v0x=px-x0,v0y=py-y0,v1x=px-x1,v1y=py-y1,v2x=px-x2,v2y=py-y2;
 float h0=clampf((v0x*e0x+v0y*e0y)/(e0x*e0x+e0y*e0y),0,1),h1=clampf((v1x*e1x+v1y*e1y)/(e1x*e1x+e1y*e1y),0,1),h2=clampf((v2x*e2x+v2y*e2y)/(e2x*e2x+e2y*e2y),0,1);
 float q0x=v0x-e0x*h0,q0y=v0y-e0y*h0,q1x=v1x-e1x*h1,q1y=v1y-e1y*h1,q2x=v2x-e2x*h2,q2y=v2y-e2y*h2;
 float s=e0x*e2y-e0y*e2x>0?1:-1;
 float dx=q0x*q0x+q0y*q0y,dy=s*(v0x*e0y-v0y*e0x);
 float d1=q1x*q1x+q1y*q1y,s1=s*(v1x*e1y-v1y*e1x),d2=q2x*q2x+q2y*q2y,s2=s*(v2x*e2y-v2y*e2x);
 if(d1<dx)dx=d1;if(s1<dy)dy=s1;if(d2<dx)dx=d2;if(s2<dy)dy=s2;
 return -sqrtf(dx)*(dy>0?1:-1);
}
static float icon_sd(int id,float x,float y) {
 switch(id) {
 case IC_PLAY:return sd_tri(x,y,11,8.5f,24,16,11,23.5f)-1.6f;
 case IC_PAUSE:return dmin(sd_box(x,y,11.5f,16,3,8.5f,1.4f),sd_box(x,y,20.5f,16,3,8.5f,1.4f));
 case IC_NEXT:return dmin(sd_tri(x,y,8.5f,9,20,16,8.5f,23)-1.3f,sd_box(x,y,23.5f,16,1.8f,8,1.2f));
 case IC_PREV:return icon_sd(IC_NEXT,32-x,y);
 case IC_SHUFFLE:{float r=1.15f,d=sd_seg(x,y,5,10,10,10,r);
  d=dmin(d,sd_seg(x,y,10,10,20,22,r));d=dmin(d,sd_seg(x,y,20,22,24,22,r));
  d=dmin(d,sd_seg(x,y,5,22,10,22,r));d=dmin(d,sd_seg(x,y,10,22,20,10,r));d=dmin(d,sd_seg(x,y,20,10,24,10,r));
  d=dmin(d,sd_tri(x,y,23,6,28.5f,10,23,14)-0.4f);d=dmin(d,sd_tri(x,y,23,18,28.5f,22,23,26)-0.4f);return d;}
 case IC_REPEAT:case IC_REPEAT1:{float d=fabsf(sd_box(x,y,16,16,10,6.5f,4.5f))-1.15f;
  d=dmin(d,sd_tri(x,y,17,5.5f,22,9.5f,17,13.5f)-0.3f);d=dmin(d,sd_tri(x,y,15,18.5f,10,22.5f,15,26.5f)-0.3f);
  if(id==IC_REPEAT1)d=dmin(d,sd_box(x,y,16,16,1.1f,3.4f,0.4f));return d;}
 case IC_STOP:return sd_box(x,y,16,16,7.5f,7.5f,2);
 case IC_FOLDER:return dmin(sd_box(x,y,16,18,12,8.5f,2.2f),sd_box(x,y,10.5f,9.5f,5.5f,3,1.6f));
 case IC_NOTE:{float d=sd_circle(x,y,11.5f,22.5f,4.6f);d=dmin(d,sd_seg(x,y,15,22,15,7.5f,1.2f));return dmin(d,sd_seg(x,y,15,8,22.5f,11,1.7f));}
 case IC_CHEV_R:return dmin(sd_seg(x,y,12.5f,8,20,16,1.7f),sd_seg(x,y,20,16,12.5f,24,1.7f));
 case IC_CHEV_L:return icon_sd(IC_CHEV_R,32-x,y);
 case IC_SEEK_F:return dmin(sd_tri(x,y,6,9.5f,15.5f,16,6,22.5f)-1.2f,sd_tri(x,y,16,9.5f,25.5f,16,16,22.5f)-1.2f);
 case IC_SEEK_B:return icon_sd(IC_SEEK_F,32-x,y);
 case IC_DOTS:return dmin(dmin(sd_circle(x,y,8,16,2.4f),sd_circle(x,y,16,16,2.4f)),sd_circle(x,y,24,16,2.4f));
 case IC_BTN_CIRCLE:return fabsf(sd_circle(x,y,16,16,9.5f))-1.7f;
 case IC_BTN_TRIANGLE:return fabsf(sd_tri(x,y,16,6.5f,26.5f,24,5.5f,24))-1.6f;
 case IC_BTN_CROSS:return dmin(sd_seg(x,y,9,9,23,23,1.7f),sd_seg(x,y,23,9,9,23,1.7f));
 case IC_BTN_SQUARE:return fabsf(sd_box(x,y,16,16,8.5f,8.5f,1.2f))-1.6f;
 case IC_WAVE:{float d=sd_box(x,y,7,18,1.6f,4,1.6f);d=dmin(d,sd_box(x,y,12.5f,16,1.6f,8,1.6f));d=dmin(d,sd_box(x,y,18,14,1.6f,10,1.6f));return dmin(d,sd_box(x,y,23.5f,18,1.6f,5,1.6f));}
 case IC_RADIO:{float d=sd_box(x,y,16,20.5f,12,7.5f,2.6f),cut=dmin(sd_circle(x,y,10.5f,20.5f,3.7f),dmin(sd_box(x,y,22,18,3.6f,0.9f,0.4f),sd_box(x,y,22,22.5f,3.6f,0.9f,0.4f)));
  if(-cut>d)d=-cut; /* the speaker and dial are cut out of the body */
  d=dmin(d,sd_seg(x,y,8.5f,12.5f,21.5f,5.5f,1.2f));return dmin(d,sd_circle(x,y,21.5f,5.5f,1.9f));}
 case IC_SEARCH:return dmin(fabsf(sd_circle(x,y,13.5f,13.5f,7.2f))-1.9f,sd_seg(x,y,19,19,25.5f,25.5f,2.3f));
 case IC_FILM:{ /* a screen with a play mark, on a stand */
  float d=fabsf(sd_box(x,y,16,14.5f,12.5f,9,2.6f))-1.5f;d=dmin(d,sd_tri(x,y,13.2f,10.5f,20.5f,14.5f,13.2f,18.5f)-0.6f);
  d=dmin(d,sd_seg(x,y,16,24,16,26.5f,1.3f));return dmin(d,sd_seg(x,y,10.5f,27,21.5f,27,1.4f));}
 case IC_MIC:{ /* a capsule in a cradle, on a stand */
  float d=sd_box(x,y,16,11,4.6f,7.6f,4.6f),cradle=fabsf(sd_circle(x,y,16,13,8.2f))-1.35f;
  if(y<13.5f)cradle=99;d=dmin(d,cradle);
  d=dmin(d,sd_seg(x,y,16,21.5f,16,26,1.35f));return dmin(d,sd_seg(x,y,11,26.5f,21,26.5f,1.4f));}
 case IC_GEAR:{ /* eight teeth round a ring */
  float d=sd_circle(x,y,16,16,8.6f);
  for(int k=0;k<8;k++){float a=k*0.785398f,c=cosf(a),s=sinf(a),px=(x-16)*c+(y-16)*s,py=-(x-16)*s+(y-16)*c;d=dmin(d,sd_box(px,py,9.6f,0,2.6f,2.3f,0.7f));}
  float hole=sd_circle(x,y,16,16,3.6f);return d>-hole?d:-hole;}
 case IC_DOWNLOAD:{float d=sd_seg(x,y,16,5,16,16.5f,1.8f);d=dmin(d,sd_tri(x,y,9.5f,13.5f,22.5f,13.5f,16,21)-0.6f);
  d=dmin(d,sd_seg(x,y,6.5f,26,25.5f,26,1.6f));d=dmin(d,sd_seg(x,y,6.5f,21.5f,6.5f,26,1.6f));return dmin(d,sd_seg(x,y,25.5f,21.5f,25.5f,26,1.6f));}
 case IC_CHECK:return dmin(sd_seg(x,y,7.5f,16.5f,13,22.5f,2),sd_seg(x,y,13,22.5f,24.5f,9.5f,2));
 case IC_PLUS:return dmin(sd_seg(x,y,16,7.5f,16,24.5f,2),sd_seg(x,y,7.5f,16,24.5f,16,2));
 case IC_BOOK:{ /* an open book: two page outlines, each with lines of text */
  float d=dmin(fabsf(sd_box(x,y,9.8f,16,6,9.5f,1.2f))-1.25f,fabsf(sd_box(x,y,22.2f,16,6,9.5f,1.2f))-1.25f);
  for(int k=0;k<3;k++){float ly=11.5f+k*4.5f;d=dmin(d,sd_seg(x,y,6.8f,ly,12.8f,ly,0.75f));d=dmin(d,sd_seg(x,y,19.2f,ly,25.2f,ly,0.75f));}
  return d;}
 case IC_BOOKMARK:{ /* a ribbon with a notch cut into its foot */
  float d=sd_box(x,y,16,15.5f,6.5f,11,1);float n=sd_tri(x,y,9,27.5f,23,27.5f,16,20.5f);
  return d>-n?d:-n;}
 }
 return 99;
}
static void atlas_build(int mono) {
 memset(atlas,0,sizeof(atlas));
 fonts_build(mono);
 for(int id=0;id<IC_COUNT;id++){int ox,oy;icon_cell(id,&ox,&oy);
  for(int y=0;y<ICON_CELL;y++)for(int x=0;x<ICON_CELL;x++){float cov=clampf(0.5f-icon_sd(id,x+0.5f,y+0.5f),0,1);atlas[(oy+y)*512+ox+x]=((unsigned int)(cov*255)<<24)|0xffffff;}}
 for(int y=0;y<64;y++)for(int x=0;x<64;x++){float cov=clampf(0.5f-sd_circle(x+0.5f,y+0.5f,32,32,31.5f),0,1);atlas[(DISK64_Y+y)*512+DISK64_X+x]=((unsigned int)(cov*255)<<24)|0xffffff;}
 for(int y=0;y<16;y++)for(int x=0;x<16;x++){float cov=clampf(0.5f-sd_circle(x+0.5f,y+0.5f,8,8,7.6f),0,1);atlas[(DISK16_Y+y)*512+DISK16_X+x]=((unsigned int)(cov*255)<<24)|0xffffff;}
 for(int y=0;y<48;y++)for(int x=0;x<48;x++){float d=sqrtf((x-23.5f)*(x-23.5f)+(y-23.5f)*(y-23.5f))/24.0f,a=clampf(1-d,0,1);atlas[(GLOW_Y+y)*512+GLOW_X+x]=((unsigned int)(a*a*255)<<24)|0xffffff;}
 for(int y=504;y<512;y++)for(int x=504;x<512;x++)atlas[y*512+x]=0xffffffff;
 sceKernelDcacheWritebackAll();
}

/* ---- primitives ---- */
static Vtx *vb(int n){return (Vtx *)sceGuGetMemory(n*sizeof(Vtx));}
static void vput(Vtx *v,float u,float t,unsigned int c,float x,float y){v->u=u;v->v=t;v->c=c;v->x=x;v->y=y;v->z=0;}
static void rect(float x,float y,float w,float h,unsigned int c) {
 if(w<=0 || h<=0)return;Vtx *v=vb(2);vput(v,WHITE_UV,WHITE_UV,c,x,y);vput(v+1,WHITE_UV,WHITE_UV,c,x+w,y+h);sceGuDrawArray(GU_SPRITES,VFMT,2,0,v);
}
static void grad(float x,float y,float w,float h,unsigned int top,unsigned int bottom) {
 Vtx *v=vb(4);vput(v,WHITE_UV,WHITE_UV,top,x,y);vput(v+1,WHITE_UV,WHITE_UV,top,x+w,y);vput(v+2,WHITE_UV,WHITE_UV,bottom,x,y+h);vput(v+3,WHITE_UV,WHITE_UV,bottom,x+w,y+h);
 sceGuDrawArray(GU_TRIANGLE_STRIP,VFMT,4,0,v);
}
static void sprite(float u0,float v0,float u1,float v1,float x,float y,float w,float h,unsigned int c) {
 Vtx *v=vb(2);vput(v,u0,v0,c,x,y);vput(v+1,u1,v1,c,x+w,y+h);sceGuDrawArray(GU_SPRITES,VFMT,2,0,v);
}
static void linear(int on){sceGuTexFilter(on?GU_LINEAR:GU_NEAREST,on?GU_LINEAR:GU_NEAREST);}
/* Rounded rectangle: four disk quadrants plus three white strips, one draw. */
static void rrect(float x,float y,float w,float h,float r,unsigned int c) {
 if(w<=0 || h<=0)return;
 if(theme==0){rect(x,y,w,h,c);return;} /* the Zoid theme has square corners throughout (circles use disk) */
 if(r>w/2)r=w/2;if(r>h/2)r=h/2;if(r<1){rect(x,y,w,h,c);return;}
 float dx=r<=8?DISK16_X:DISK64_X,dy=r<=8?DISK16_Y:DISK64_Y,half=r<=8?8:32;
 Vtx *v=vb(14);
 vput(v,dx,dy,c,x,y);vput(v+1,dx+half,dy+half,c,x+r,y+r);
 vput(v+2,dx+half,dy,c,x+w-r,y);vput(v+3,dx+half*2,dy+half,c,x+w,y+r);
 vput(v+4,dx,dy+half,c,x,y+h-r);vput(v+5,dx+half,dy+half*2,c,x+r,y+h);
 vput(v+6,dx+half,dy+half,c,x+w-r,y+h-r);vput(v+7,dx+half*2,dy+half*2,c,x+w,y+h);
 vput(v+8,WHITE_UV,WHITE_UV,c,x+r,y);vput(v+9,WHITE_UV,WHITE_UV,c,x+w-r,y+h);
 vput(v+10,WHITE_UV,WHITE_UV,c,x,y+r);vput(v+11,WHITE_UV,WHITE_UV,c,x+r,y+h-r);
 vput(v+12,WHITE_UV,WHITE_UV,c,x+w-r,y+r);vput(v+13,WHITE_UV,WHITE_UV,c,x+w,y+h-r);
 linear(1);sceGuDrawArray(GU_SPRITES,VFMT,14,0,v);linear(0);
}
static void disk(float cx,float cy,float r,unsigned int c) {
 linear(1);if(r<=8)sprite(DISK16_X,DISK16_Y,DISK16_X+16,DISK16_Y+16,cx-r,cy-r,r*2,r*2,c);
 else sprite(DISK64_X,DISK64_Y,DISK64_X+64,DISK64_Y+64,cx-r,cy-r,r*2,r*2,c);linear(0);
}
static void glow(float cx,float cy,float r,unsigned int c){if(ui_paper)return;linear(1);sprite(GLOW_X,GLOW_Y,GLOW_X+48,GLOW_Y+48,cx-r,cy-r,r*2,r*2,c);linear(0);}
static void icon(int id,float cx,float cy,float size,unsigned int c) {
 int iu,iv;icon_cell(id,&iu,&iv);float u=iu,v=iv;
 linear(1);sprite(u,v,u+ICON_CELL,v+ICON_CELL,cx-size/2,cy-size/2,size,size,c);linear(0);
}
/* A polyline drawn as a triangle strip, so it can be wider than one pixel. */
static void polyline(const float *xs,const float *ys,int n,float width,unsigned int c) {
 if(n<2)return;Vtx *v=vb(n*2);float hw=width/2;
 for(int i=0;i<n;i++){int a=i?i-1:0,b=i<n-1?i+1:n-1;float dx=xs[b]-xs[a],dy=ys[b]-ys[a],len=sqrtf(dx*dx+dy*dy);if(len<0.001f)len=1;
  float nx=-dy/len*hw,ny=dx/len*hw;vput(v+i*2,WHITE_UV,WHITE_UV,c,xs[i]+nx,ys[i]+ny);vput(v+i*2+1,WHITE_UV,WHITE_UV,c,xs[i]-nx,ys[i]-ny);}
 sceGuDrawArray(GU_TRIANGLE_STRIP,VFMT,n*2,0,v);
}

static void hgrad(float x,float y,float w,float h,unsigned int left,unsigned int right) {
 Vtx *v=vb(4);vput(v,WHITE_UV,WHITE_UV,left,x,y);vput(v+1,WHITE_UV,WHITE_UV,right,x+w,y);vput(v+2,WHITE_UV,WHITE_UV,left,x,y+h);vput(v+3,WHITE_UV,WHITE_UV,right,x+w,y+h);
 sceGuDrawArray(GU_TRIANGLE_STRIP,VFMT,4,0,v);
}
static void seg(float x0,float y0,float x1,float y1,float width,unsigned int c){float xs[2]={x0,x1},ys[2]={y0,y1};polyline(xs,ys,2,width,c);}
static void frame_rect(float x,float y,float w,float h,float t,unsigned int c){rect(x,y,w,t,c);rect(x,y+h-t,w,t,c);rect(x,y+t,t,h-2*t,c);rect(x+w-t,y+t,t,h-2*t,c);}
/* Horizontal lines every `step` pixels, drawn as one batch. */
static void scanlines(float x,float y,float w,float h,int step,unsigned int c) {
 int n=(int)(h/step);if(n<=0)return;Vtx *v=vb(n*2);
 for(int i=0;i<n;i++){vput(v+i*2,WHITE_UV,WHITE_UV,c,x,y+i*step);vput(v+i*2+1,WHITE_UV,WHITE_UV,c,x+w,y+i*step+1);}
 sceGuDrawArray(GU_SPRITES,VFMT,n*2,0,v);
}
/* Box with the top-left and bottom-right corners cut, as on the desktop Cyberpunk clock. */
static void chamfer_box(float x,float y,float w,float h,float cut,float t,unsigned int c) {
 /* straight edges as rectangles, diagonals as short strokes: corners meet cleanly */
 rect(x+cut,y,w-cut,t,c);rect(x+w-t,y,t,h-cut,c);rect(x,y+h-t,w-cut,t,c);rect(x,y+cut,t,h-cut,c);
 seg(x+w-t/2,y+h-cut,x+w-cut,y+h-t/2,t,c);seg(x+t/2,y+cut,x+cut,y+t/2,t,c);
}

/* ---- clipping ---- */
static int clip_stack[8][4],clip_depth;
static int scr_w=480,scr_h=272; /* the picture being drawn: the screen, or Books' upright page (portrait_begin) */
static void clip_set(int x0,int y0,int x1,int y1){if(x0<0)x0=0;if(y0<0)y0=0;if(x1>scr_w)x1=scr_w;if(y1>scr_h)y1=scr_h;if(x1<=x0)x1=x0+1;if(y1<=y0)y1=y0+1;sceGuScissor(x0,y0,x1-x0,y1-y0);}
static void clip(int x,int y,int w,int h) {
 int x0=x,y0=y,x1=x+w,y1=y+h;
 if(clip_depth){int *p=clip_stack[clip_depth-1];if(x0<p[0])x0=p[0];if(y0<p[1])y0=p[1];if(x1>p[2])x1=p[2];if(y1>p[3])y1=p[3];}
 if(clip_depth<8){clip_stack[clip_depth][0]=x0;clip_stack[clip_depth][1]=y0;clip_stack[clip_depth][2]=x1;clip_stack[clip_depth][3]=y1;clip_depth++;}
 clip_set(x0,y0,x1,y1);
}
static void unclip(void){if(clip_depth)clip_depth--;if(clip_depth){int *p=clip_stack[clip_depth-1];clip_set(p[0],p[1],p[2],p[3]);}else clip_set(0,0,scr_w,scr_h);}

/* ---- text ---- */
enum {F_SMALL,F_MEDIUM,F_LARGE,F_CLOCK};
/* Index into a font, or -1 when the font lacks the character (the clock font only has '+' to ':'). */
static int glyph_of(int f,unsigned char c) {
 int g=c-fonts[f].first;
 if(g>=0 && g<fonts[f].count)return g;
 return f==F_CLOCK?-1:'?'-32;
}
static int advance(int f,unsigned char c){int g=glyph_of(f,c);return g>=0?fonts[f].widths[g]:fonts[f].widths['0'-43]/2;}
/* ---- letters outside ASCII: the PSP's own fonts (sysfont.h) ---- */
#include "sysfont.h"
static void gc_unbind(void){sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexImage(0,512,512,512,atlas);sceGuTexFlush();}
/* PSP-font letters are scaled so their capitals match the theme font's; CJK and Hangul a little larger */
static float sys_scale(int f,unsigned int cp) {
 if(!sys_cap_done)sys_cap_measure();
 float s=ink_cap[f]/sys_cap;if(cp>=0x2E80)s*=1.12f;
 if(s<0.62f)s=0.62f;if(s>0.95f && s<1.05f)s=1;return s;
}
/* when no PSP font has a letter (or the clock font is asked): a plain letter, or '?' */
static const char latin_fold[]="AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
/* U+0100-017F (Polish, Czech, Turkish, Baltic...) */
static const char latin_ext_fold[]="AaAaAa" "CcCcCcCc" "DdDd" "EeEeEeEeEe" "GgGgGgGg" "HhHh" "IiIiIiIiIi" "Ii" "Jj" "Kkk" "LlLlLlLlLl"
 "NnNnNn" "n" "Nn" "OoOoOo" "Oo" "RrRrRr" "SsSsSsSs" "TtTtTt" "UuUuUuUuUuUu" "Ww" "Yy" "Y" "ZzZzZz" "s";
static unsigned char fold_char(unsigned int cp){return cp>=0xC0 && cp<=0xFF?latin_fold[cp-0xC0]:cp>=0x100 && cp<=0x17F?latin_ext_fold[cp-0x100]:'?';}
/* UTF-8 cut short at the end of a buffer reads as 0 and takes no room */
static unsigned int text_cp(const char *s,int *i) {
 const unsigned char *p=(const unsigned char *)s+*i;
 if(p[0]>=0xC2 && p[0]<0xF5){int len=p[0]>=0xF0?4:p[0]>=0xE0?3:2;for(int k=1;k<len;k++)if(!p[k]){*i+=k;return 0;}}
 return utf8_at(s,i);
}
/* A letter no font has and no plain letter stands in for (a heart, say) shows as a white square,
   U+25A1 from the Japanese PSP font, rather than '?'. *cp becomes the square's when it is used. */
static GCell *sys_glyph_or_box(unsigned int *cp) {
 GCell *g=sys_glyph(*cp);
 if(!g && fold_char(*cp)=='?'){g=sys_glyph(0x25A1);if(g)*cp=0x25A1;}
 return g;
}
static int cp_advance(int f,unsigned int cp) {
 if(!cp)return 0;
 if(cp<128)return advance(f,cp);
 if(f!=F_CLOCK){GCell *g=sys_glyph_or_box(&cp);if(g)return (int)(g->adv*sys_scale(f,cp)/64+0.5f);}
 return advance(f,fold_char(cp));
}
/* width of the letter at s[*i]; *i moves past it */
static int adv_at(int f,const char *s,int *i){return cp_advance(f,text_cp(s,i));}
static int text_w(const char *s,int f){int w=0;for(int i=0;s[i];)w+=adv_at(f,s,&i);return w;}
static int text_height(int f){return fonts[f].ch;}
static int text(float x,float y,const char *s,unsigned int c,int f) {
 int n=strlen(s);if(!n)return 0;FontInfo *fi=&fonts[f];const float sk=text_italic;Vtx *v=vb(n*(sk?6:2)),*sv=NULL;int pen=(int)(x+0.5f),iy=(int)(y+fi->ch/2.0f-ink_mid[f]+0.5f),start=pen,k=0,ks=0; /* letters sit centered in their line, not at its top */
 for(int i=0;i<n;) {
  unsigned int cp=text_cp(s,&i);if(!cp)continue;
  GCell *sg=cp>=128 && f!=F_CLOCK?sys_glyph_or_box(&cp):NULL;
  if(sg) { /* from the PSP font: on the theme font's baseline, drawn after the theme letters */
   float sc=sys_scale(f,cp),base=iy+ink_base[f];
   if(sg->w && sg->h) {
    if(!sv)sv=vb(n*2);
    int cell=sg-gcells;float u=(cell%GC_PER)*GC_CELL+1,t=(cell/GC_PER)*GC_CELL+1,dx=pen+sg->left*sc,dy=base-sg->top*sc;
    if(sc==1){dx=(int)(dx+0.5f);dy=(int)(dy+0.5f);}
    vput(sv+ks*2,u,t,c,dx,dy);vput(sv+ks*2+1,u+sg->w,t+sg->h,c,dx+sg->w*sc,dy+sg->h*sc);ks++;
   }
   pen+=(int)(sg->adv*sc/64+0.5f);continue;
  }
  int g=glyph_of(f,cp<128?(unsigned char)cp:fold_char(cp));if(g<0){pen+=advance(f,' ');continue;}
  float u=fi->ax+(g%fi->per)*fi->cw,t=fi->ay+(g/fi->per)*fi->ch;
  if(sk) { /* italic: the cell slanted about the baseline, as two triangles */
   float base=iy+ink_base[f],top=sk*(base-iy),bot=sk*(base-iy-fi->ch);Vtx *q=v+k*6;
   vput(q,u,t,c,pen+top,iy);vput(q+1,u+fi->cw,t,c,pen+fi->cw+top,iy);vput(q+2,u,t+fi->ch,c,pen+bot,iy+fi->ch);
   q[3]=q[1];vput(q+4,u+fi->cw,t+fi->ch,c,pen+fi->cw+bot,iy+fi->ch);q[5]=q[2];
  }else{vput(v+k*2,u,t,c,pen,iy);vput(v+k*2+1,u+fi->cw,t+fi->ch,c,pen+fi->cw,iy+fi->ch);}
  pen+=fi->widths[g];k++;
 }
 if(k && sk){linear(1);sceGuDrawArray(GU_TRIANGLES,VFMT,k*6,0,v);linear(0);}
 else if(k)sceGuDrawArray(GU_SPRITES,VFMT,k*2,0,v);
 if(ks){gc_bind();linear(1);sceGuDrawArray(GU_SPRITES,VFMT,ks*2,0,sv);linear(0);gc_unbind();}
 return pen-start;
}
/* Lit text: a soft halo of offset copies under the text, like phosphor bloom or neon. */
static int text_glow(float x,float y,const char *s,unsigned int rgb,int f,int halo) {
 if(halo>0 && !ui_paper){unsigned int h=C(rgb,halo);text(x-1,y,s,h,f);text(x+1,y,s,h,f);text(x,y-1,s,h,f);text(x,y+1,s,h,f);}
 return text(x,y,s,C(rgb,255),f);
}
/* small capitals (Paper's kickers and headings) always; elsewhere the HUD skins write in capitals and Paper keeps the case */
static void upper_sc(char *dst,const char *src,int cap){int i=0;for(;src[i] && i<cap-1;i++){unsigned char ch=src[i];dst[i]=ch<128?toupper(ch):ch;}dst[i]=0;}
static void upper(char *dst,const char *src,int cap){if(ui_keepcase){if(dst!=src)snprintf(dst,cap,"%s",src);return;}upper_sc(dst,src,cap);}
static void text_right(float right,float y,const char *s,unsigned int c,int f){text(right-text_w(s,f),y,s,c,f);}
static void text_center(float cx,float y,const char *s,unsigned int c,int f){text(cx-text_w(s,f)/2,y,s,c,f);}
/* Text whose capitals are centered on cy (for buttons, chips and badges). */
static int text_vc(float x,float cy,const char *s,unsigned int c,int f){return text(x,cy-fonts[f].ch/2.0f,s,c,f);}
static void text_mid(float cx,float cy,const char *s,unsigned int c,int f){text(cx-text_w(s,f)/2,cy-fonts[f].ch/2.0f,s,c,f);}
/* Shortens with "..." when the text does not fit. */
static void text_fit(float x,float y,const char *s,unsigned int c,int f,int maxw) {
 if(text_w(s,f)<=maxw){text(x,y,s,c,f);return;}
 char buf[260];int dots=text_w("...",f),w=0,n=0;
 while(s[n] && n<250){int next=n,a=adv_at(f,s,&next);if(w+a+dots>maxw)break;w+=a;n=next;} /* whole letters only */
 while(n>0 && s[n-1]==' ')n--;
 memcpy(buf,s,n);strcpy(buf+n,"...");text(x,y,buf,c,f);
}
typedef struct {unsigned int key;unsigned long long start;} Marquee;
static unsigned long long now_ms;
/* Scrolls text that is too wide: pause, glide, repeat. */
static void marquee(Marquee *m,unsigned int key,float x,float y,const char *s,unsigned int c,int f,int width) {
 int total=text_w(s,f);
 if(total<=width){text(x,y,s,c,f);return;}
 if(m->key!=key || !m->start){m->key=key;m->start=now_ms;}
 int gap=40;unsigned long long cycle=(total+gap)*marquee_ms+2000,phase=(now_ms-m->start)%cycle;
 float offset=phase<2000?0:(phase-2000)/(float)marquee_ms;
 clip((int)x,(int)y-2,width,text_height(f)+4);
 text(x-offset,y,s,c,f);text(x-offset+total+gap,y,s,c,f);
 unclip();
}

/* ---- frame ---- */
static void gfx_init(void) {
 sceGuInit();sceGuStart(GU_DIRECT,gu_list);
 sceGuDrawBuffer(GU_PSM_8888,(void *)0,512);
 sceGuDispBuffer(480,272,(void *)0x88000,512);
 sceGuDepthBuffer((void *)0x110000,512);
 sceGuOffset(2048-240,2048-136);sceGuViewport(2048,2048,480,272);
 sceGuDepthRange(65535,0);sceGuDisable(GU_DEPTH_TEST);
 sceGuScissor(0,0,480,272);sceGuEnable(GU_SCISSOR_TEST);
 sceGuDisable(GU_CULL_FACE);sceGuShadeModel(GU_SMOOTH);
 sceGuEnable(GU_TEXTURE_2D);sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexImage(0,512,512,512,atlas);
 sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuTexFilter(GU_NEAREST,GU_NEAREST);sceGuTexWrap(GU_CLAMP,GU_CLAMP);
 sceGuEnable(GU_BLEND);sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
 sceGuFinish();sceGuSync(0,0);
 sceDisplayWaitVblankStart();sceGuDisplay(GU_TRUE);
}
static void frame_begin(unsigned int clear) {
 gfx_frame++;
 sceGuStart(GU_DIRECT,gu_list);sceGuTexImage(0,512,512,512,atlas);sceGuTexFlush();
 sceGuClearColor(clear);sceGuClear(GU_COLOR_BUFFER_BIT);clip_depth=0;sceGuScissor(0,0,480,272);
}
static void art_draw(float x,float y,float size,unsigned int c) {
 sceGuTexImage(0,ART_N,ART_N,ART_N,art_tex);sceGuTexFlush();linear(1);
 sprite(0,0,ART_N,ART_N,x,y,size,size,c);
 linear(0);sceGuTexImage(0,512,512,512,atlas);sceGuTexFlush();
}
/* Upright drawing (Books held vertically, D-pad at the bottom): draw into a 272 x 480 picture in the
   VRAM the depth buffer would use (depth is never tested), then portrait_end turns it onto the screen.
   The picture's top is the screen's right edge and its left the screen's top: the PSP is held
   turned a quarter to the left. */
#define PORTRAIT_VRAM 0x110000 /* 512 x 480 x 4 bytes, up to the end of the 2 MB */
static void portrait_begin(unsigned int clear) {
 sceGuDrawBufferList(GU_PSM_8888,(void *)PORTRAIT_VRAM,512);
 scr_w=272;scr_h=480;clip_depth=0;sceGuScissor(0,0,272,480);
 sceGuClearColor(clear);sceGuClear(GU_COLOR_BUFFER_BIT);
}
static void portrait_end(void) {
 sceGuDrawBufferList(GU_PSM_8888,draw_buf,512);
 scr_w=480;scr_h=272;clip_depth=0;sceGuScissor(0,0,480,272);
 sceGuDisable(GU_BLEND);sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGB);sceGuTexMode(GU_PSM_8888,0,0,0);
 sceGuTexImage(0,512,512,512,(void *)((unsigned int)sceGeEdramGetAddr()+PORTRAIT_VRAM));sceGuTexFlush();linear(0);
 /* in 32 x 32 tiles, so the turned reads stay in the texture cache: screen (x,y) shows picture (y, 480-x) */
 for(int y0=0;y0<272;y0+=32)for(int x0=0;x0<480;x0+=32) {
  int x1=x0+32,y1=y0+32>272?272:y0+32;Vtx *v=vb(6);
  vput(v,y0,480-x0,0xffffffff,x0,y0);vput(v+1,y0,480-x1,0xffffffff,x1,y0);vput(v+2,y1,480-x0,0xffffffff,x0,y1);
  vput(v+3,y0,480-x1,0xffffffff,x1,y0);vput(v+4,y1,480-x1,0xffffffff,x1,y1);vput(v+5,y1,480-x0,0xffffffff,x0,y1);
  sceGuDrawArray(GU_TRIANGLES,VFMT,6,0,v);
 }
 sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);sceGuEnable(GU_BLEND);
 sceGuTexImage(0,512,512,512,atlas);sceGuTexFlush();
}
static void (*frame_hook)(void); /* runs after the frame is drawn, before it is shown */
static void frame_end(void) {
 sceGuFinish();sceGuSync(0,0);
 cpu_ms=(sceKernelGetSystemTimeWide()-frame_start_us)/1000.0f; /* logic + drawing, before the vblank wait */
 if(capture_path[0]) {
  FILE *f=fopen(capture_path,"wb");
  if(f){unsigned int *frame=(unsigned int *)(((unsigned int)sceGeEdramGetAddr()|0x40000000)+(unsigned int)draw_buf);for(int y=0;y<272;y++)fwrite(frame+y*512,4,480,f);fclose(f);}
  capture_path[0]=0;
 }
 if(frame_hook)frame_hook();
 sceDisplayWaitVblankStartCB();draw_buf=sceGuSwapBuffers();
}
