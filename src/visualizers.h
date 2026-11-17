/* Visualizers, drawn with the GPU. Ids 0-9 keep their old meaning so saved
   settings still work; 10+ are ports of desktop ZOID's rack visualizers
   (SimplePlayer/Visualizers.cs, MoreVisualizers.cs). The spectrum comes from a
   512-point FFT of the newest decoded PCM in 32 log-spaced bands. */
#include "sine.h"
static int sn(int a){return sintab[a&255];}
static unsigned int random_state=0x137;
static int rnd(int n){random_state=random_state*1664525+1013904223;return (random_state>>8)%n;}
static float frand(void){return rnd(10000)/10000.0f;}
#define VIS_COUNT 25
static const char *visual_names[VIS_COUNT]={"Oscilloscope","Ridgelines","Starfield","Tunnel","Spectrum Bars","Wave Ribbons","Orbit","Rain","VU Meters","Plasma Grid",
 "Mirror Bars","Spectrum Line","Radial","Spectrogram","Level Meter","Phase Scope","Ring Scope","Fountain","LED Matrix",
 "Circle Scope","Pulse Rings","Synth Grid","Bubbles","Sunburst","Heartbeat"};
/* menu order: desktop's rack order first, then the PSP extras, then the newest */
static const int visual_order[VIS_COUNT]={4,10,11,0,12,13,14,2,1,15,16,17,3,18,8,5,6,7,9,19,20,21,22,23,24};
#define BANDS 32
static float spec[BANDS],spec_peak[BANDS];
static float vis_bass,vis_energy,vis_rms,vis_peak;
static int band_edge[BANDS+1];static float hann[512];
static unsigned int visual_tick;

static void spectrum_init(void) {
 for(int i=0;i<512;i++)hann[i]=0.5f-0.5f*cosf(6.2831853f*i/511);
 for(int b=0;b<=BANDS;b++){int e=(int)(2*powf(100.0f,(float)b/BANDS));if(b && e<=band_edge[b-1])e=band_edge[b-1]+1;if(e>255)e=255;band_edge[b]=e;}
}
static void spectrum_update(void) {
 static float re[512],im[512];
 int live=state==2 && !paused;
 if(live) {
  for(int i=0;i<512;i++){re[i]=wave[i]*hann[i]/32768.0f;im[i]=0;}
  for(int i=1,j=0;i<512;i++){int bit=256;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j){float t=re[i];re[i]=re[j];re[j]=t;}}
  for(int len=2;len<=512;len<<=1){float ang=-6.2831853f/len,wr=cosf(ang),wi=sinf(ang);
   for(int i=0;i<512;i+=len){float cr=1,ci=0;for(int k=0;k<len/2;k++){int a=i+k,b=a+len/2;float tr=re[b]*cr-im[b]*ci,ti=re[b]*ci+im[b]*cr;re[b]=re[a]-tr;im[b]=im[a]-ti;re[a]+=tr;im[a]+=ti;float n=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=n;}}}
 }
 /* Auto-gain: the loudest band tracks a reference level, and bars show
    each band's distance below it, so loud mastered songs never pin at max. */
 static float ref=-30;float db[BANDS],loudest=-120;
 for(int b=0;b<BANDS;b++){db[b]=-120;
  if(live){float m=0;for(int k=band_edge[b];k<band_edge[b+1];k++){float v=re[k]*re[k]+im[k]*im[k];if(v>m)m=v;}
   /* 16384 = power of a full-scale sine in this window, so 0 dB is maximum. */
   db[b]=10*log10f(m/16384.0f+1e-12f)+b*0.3f;}
  if(db[b]>loudest)loudest=db[b];}
 if(live && loudest>-75)ref+=(loudest-ref)*(loudest>ref?0.25f:0.01f);
 for(int b=0;b<BANDS;b++) {
  float t=live?clampf((db[b]-ref+32)/32,0,1):0,target=t*t*0.95f;
  spec[b]+=(target-spec[b])*(target>spec[b]?0.55f:0.14f);
  if(spec[b]>spec_peak[b])spec_peak[b]=spec[b];else spec_peak[b]-=0.008f;if(spec_peak[b]<0)spec_peak[b]=0;
 }
 float bass=0,e=0;for(int b=0;b<4;b++)bass+=spec[b];for(int b=0;b<BANDS;b++)e+=spec[b];
 vis_bass=bass/4;vis_energy=e/BANDS;
 float sq=0,pk=0;if(live)for(int i=0;i<512;i++){float v=wave[i]/32768.0f;sq+=v*v;if(fabsf(v)>pk)pk=fabsf(v);}
 vis_rms=sqrtf(sq/512);vis_peak=pk;
}
/* Band(i,count) as on the desktop: the loudest band in that slice of the spectrum. */
static float band(int i,int count) {
 if(count<=BANDS){int a=i*BANDS/count,b=(i+1)*BANDS/count;if(b<=a)b=a+1;float m=0;for(int k=a;k<b && k<BANDS;k++)if(spec[k]>m)m=spec[k];return m;}
 float p=(float)i*(BANDS-1)/(count-1);int k=(int)p;float f=p-k;return k>=BANDS-1?spec[BANDS-1]:spec[k]*(1-f)+spec[k+1]*f;
}
static float wave_at(int i){return wave[i&511]/32768.0f;}
/* the second accent: desktop's DisplayAccentB */
static unsigned int accent_b(void){return theme==1?0x8a9cff:theme==2?0x9cffb4:theme==3?0xa8ecff:theme==4?0xffb020:theme==5?0xB83A2E:T->fg;}
static unsigned int vis_dim(void){return theme==2?C(T->second,112):C(T->accent,85);}
static unsigned int vis_peak_color(void){return theme==1?0xfcee0a:theme==2?T->hot:theme==3?0xb4f05a:theme==5?0xB83A2E:T->fg;}
static int playing_now(void){return state==2 && !paused;}

/* A thick segment as two triangles, for batches of many strokes in one draw. */
static int quad_seg(Vtx *v,float x0,float y0,float x1,float y1,float w,unsigned int c) {
 float dx=x1-x0,dy=y1-y0,len=sqrtf(dx*dx+dy*dy);if(len<0.01f){dx=0.01f;len=0.01f;}
 float nx=-dy/len*w/2,ny=dx/len*w/2;
 vput(v,WHITE_UV,WHITE_UV,c,x0+nx,y0+ny);vput(v+1,WHITE_UV,WHITE_UV,c,x1+nx,y1+ny);vput(v+2,WHITE_UV,WHITE_UV,c,x0-nx,y0-ny);
 vput(v+3,WHITE_UV,WHITE_UV,c,x1+nx,y1+ny);vput(v+4,WHITE_UV,WHITE_UV,c,x1-nx,y1-ny);vput(v+5,WHITE_UV,WHITE_UV,c,x0-nx,y0-ny);return 6;
}
static int disk_sprite(Vtx *v,float cx,float cy,float r,unsigned int c) {
 vput(v,DISK16_X,DISK16_Y,c,cx-r,cy-r);vput(v+1,DISK16_X+16,DISK16_Y+16,c,cx+r,cy+r);return 2;
}
static void circle_line(float cx,float cy,float rx,float ry,float width,unsigned int c) {
 float xs[49],ys[49];for(int i=0;i<=48;i++){float a=i*6.2831853f/48;xs[i]=cx+cosf(a)*rx;ys[i]=cy+sinf(a)*ry;}polyline(xs,ys,49,width,c);
}

/* ---- per-panel state ---- */
typedef struct {float x,y,z,pz;} Star;
typedef struct {float x,y,vx,vy,life;int hot;} Spark;
typedef struct {
 float level[BANDS],peak[BANDS];int hold[BANDS];
 float mirror[24],line_v[24],radial[72],radial_bass,turn;
 float ridge_now[56],ridge_hist[26][56];int ridge_head,frame;
 Star stars[320];float star_speed;int stars_ready;
 float phase[4][480][2];int phase_n;float phase_scale;
 float ring[3][128];int ring_n;float ring_bass;
 Spark sparks[300];int spark_n;
 float tunnel_z[24];int tunnel_hot[24],tunnel_n;float tunnel_since;
 float led_v[28],led_peak[28];int led_hold[28];
 float lvl_rms,lvl_peak,lvl_hold;int lvl_hold_frames;
 float vu[2],vu_v[2];
 float kick_avg;int kick_wait;
 int old_stars[48][3];int tunnel_phase,tunnel_speed;unsigned long long tunnel_clock;
 float osc[128],calm_energy,ribbon_phase,tunnel_boost;
 /* newer visualizers */
 float cscope[96],cscope_bass;
 float pring_r[12],pring_life[12];int pring_hot[12];
 float grid[14][24],grid_scroll;int grid_head;
 float bub_x[40],bub_y[40],bub_r[40],bub_v[40];int bub_n;
 float sun[36],sun_turn;
 float ecg[200];int ecg_head;float ecg_beat,ecg_phase;
} VisSlot;
static VisSlot vslots[3];
static unsigned int spectro_tex[3][256*64] __attribute__((aligned(64)));
static int kick_hit(VisSlot *s) {
 float b=playing_now()?vis_bass:0;int hit=s->kick_wait<=0 && b>0.2f && b>s->kick_avg+0.18f;
 s->kick_avg+=(b-s->kick_avg)*0.08f;if(hit)s->kick_wait=6;else s->kick_wait--;return hit;
}

/* Spectrum Bars: the display's segmented analyzer with falling peak caps. */
static void vis_bars(VisSlot *s,float x,float y,float w,float h) {
 int bars=w<200?16:BANDS,segs=h<50?8:12;float gx=w/bars*0.25f;if(gx<1)gx=1;float cw=(w-gx*(bars-1))/bars;
 float gy=h/segs*0.3f;if(gy<1)gy=1;float sh=(h-gy*(segs-1))/segs;
 Vtx *v=vb(bars*segs*2);int n=0;unsigned int lit=C(T->accent,255),pk=C(vis_peak_color(),255),dim=C(T->accent,theme==2?40:20);
 for(int b=0;b<bars;b++) {
  float t=playing_now()?band(b,bars):0;
  s->level[b]=t>s->level[b]?t:fmaxf(t,s->level[b]-0.06f);
  if(s->level[b]>=s->peak[b]){s->peak[b]=s->level[b];s->hold[b]=14;}else if(s->hold[b]>0)s->hold[b]--;else s->peak[b]=fmaxf(0,s->peak[b]-0.025f);
  int on=(int)(s->level[b]*segs+0.5f),p=(int)ceilf(s->peak[b]*segs)-1;
  for(int k=0;k<segs;k++){float sx=x+b*(cw+gx),sy=y+h-(k+1)*sh-k*gy;unsigned int c=k<on?lit:(k==p && p>=on)?pk:dim;
   vput(v+n++,WHITE_UV,WHITE_UV,c,sx,sy);vput(v+n++,WHITE_UV,WHITE_UV,c,sx+cw,sy+sh);}
 }
 sceGuDrawArray(GU_SPRITES,VFMT,n,0,v);
}
static void vis_mirror(VisSlot *s,float x,float y,float w,float h) {
 for(int i=0;i<24;i++){float t=playing_now()?band(i,24):0;s->mirror[i]+=(t-s->mirror[i])*(t>s->mirror[i]?0.6f:0.18f);}
 float step=w/48,bw=fmaxf(1,step*0.62f),r=fminf(bw/2,3);
 for(int k=0;k<48;k++){int i=k<24?23-k:k-24;float v=fmaxf(0.02f,s->mirror[i]),bh=v*(h-4);
  rrect(x+k*step+(step-bw)/2,y+(h-bh)/2,bw,bh,r,C(v>0.82f?accent_b():T->accent,255));}
}
static void vis_line(VisSlot *s,float x,float y,float w,float h) {
 float xs[24],ys[24];
 for(int p=0;p<24;p++){float t=playing_now()?band(p,24):0;s->line_v[p]+=(t-s->line_v[p])*(t>s->line_v[p]?0.55f:0.12f);
  float taper=sinf(3.1415926f*p/23);xs[p]=x+p*w/23;ys[p]=y+h-1.5f-s->line_v[p]*taper*(h-4);}
 Vtx *v=vb(48);for(int p=0;p<24;p++){vput(v+p*2,WHITE_UV,WHITE_UV,C(T->accent,51),xs[p],ys[p]);vput(v+p*2+1,WHITE_UV,WHITE_UV,C(T->accent,8),xs[p],y+h);}
 sceGuDrawArray(GU_TRIANGLE_STRIP,VFMT,48,0,v);
 polyline(xs,ys,24,1.6f,C(T->accent,255));
}
static void vis_radial(VisSlot *s,float x,float y,float w,float h) {
 for(int i=0;i<72;i++){int j=i<36?i:71-i;float t=playing_now()?band(j,36):0;s->radial[i]+=(t-s->radial[i])*(t>s->radial[i]?0.6f:0.15f);}
 s->radial_bass+=((playing_now()?vis_bass:0)-s->radial_bass)*0.35f;if(playing_now())s->turn+=0.15f+vis_energy*0.6f;
 float cx=x+w/2,cy=y+h/2,mx=fminf(w,h)/2-2,r0=mx*(0.32f+0.1f*s->radial_bass),pw=fmaxf(1.5f,6.2831853f*r0/72*0.55f);
 Vtx *v=vb(72*6);int n=0;
 for(int i=0;i<72;i++){float a=(i/72.0f*360+s->turn-90)*3.1415926f/180,len=fmaxf(1,s->radial[i]*(mx-r0)),c=cosf(a),sy=sinf(a);
  n+=quad_seg(v+n,cx+c*r0,cy+sy*r0,cx+c*(r0+len),cy+sy*(r0+len),pw,C(s->radial[i]>0.82f?accent_b():T->accent,255));}
 sceGuDrawArray(GU_TRIANGLES,VFMT,n,0,v);
 circle_line(cx,cy,r0-4,r0-4,1,vis_dim());
 float cr=fmaxf(2,(r0-8)*s->radial_bass*0.6f);disk(cx,cy,cr,C(T->accent,255));
}
static void vis_spectrogram(int slot,float x,float y,float w,float h) {
 unsigned int *px=spectro_tex[slot],lut[64];unsigned int a=T->accent,b=accent_b();
 for(int i=0;i<64;i++){float t=i/63.0f;unsigned int c;int al;
  if(t<0.6f){c=a;al=(int)(t/0.6f*255);}else if(t<0.88f){c=mix(a,b,(int)((t-0.6f)/0.28f*255));al=255;}else{c=mix(b,0xffffff,(int)((t-0.88f)/0.12f*180));al=255;}
  lut[i]=C(c,al);}
 for(int r=0;r<64;r++){unsigned int *row=px+r*256;memmove(row,row+1,239*4);
  float v=playing_now()?band(63-r,64):0;int k=(int)(clampf(v*v*1.15f,0,1)*63);row[239]=lut[k];}
 sceKernelDcacheWritebackRange(px,256*64*4);
 sceGuTexImage(0,256,64,256,px);sceGuTexFlush();linear(1);
 sprite(0,0,240,64,x,y,w,h,0xffffffff);
 linear(0);sceGuTexImage(0,512,512,512,atlas);sceGuTexFlush();
}
static void vis_level(VisSlot *s,float x,float y,float w,float h) {
 #define DB48(v) clampf((20*log10f((v)+1e-9f)+48)/48,0,1)
 float r=playing_now()?DB48(vis_rms*1.41f):0,p=playing_now()?DB48(vis_peak):0;
 s->lvl_rms=r>s->lvl_rms?r:fmaxf(r,s->lvl_rms-0.025f);s->lvl_peak=p>s->lvl_peak?p:fmaxf(p,s->lvl_peak-0.03f);
 if(s->lvl_peak>=s->lvl_hold){s->lvl_hold=s->lvl_peak;s->lvl_hold_frames=30;}else if(s->lvl_hold_frames>0)s->lvl_hold_frames--;else s->lvl_hold=fmaxf(0,s->lvl_hold-0.01f);
 /* Few wide blocks: two rows (average, peak) of chunky square segments. */
 int segs=w<200?10:14;float gap=fmaxf(3,w/60),sw=(w-gap*(segs-1))/segs,rg=fmaxf(4,h*0.12f),rh=(h-rg)/2;
 for(int row=0;row<2;row++){float val=row?s->lvl_peak:s->lvl_rms;int lit=(int)(val*segs+0.5f),held=row?(int)(s->lvl_hold*segs+0.5f)-1:-1;
  for(int i=0;i<segs;i++){int hot=i>=segs-3;unsigned int c=(i<lit || i==held)?C(hot?accent_b():T->accent,255):vis_dim();
   rect(x+i*(sw+gap),y+row*(rh+rg),sw,rh,c);}}
}
static void star_reset(Star *s,float z){s->x=frand()*2-1;s->y=frand()*2-1;s->z=s->pz=fmaxf(0.02f,z);}
static void vis_starfield(VisSlot *s,float x,float y,float w,float h) {
 int count=(int)(w*h/60);if(count<120)count=120;if(count>320)count=320;
 if(!s->stars_ready){for(int i=0;i<320;i++)star_reset(&s->stars[i],frand());s->stars_ready=1;s->star_speed=0.004f;}
 float target=playing_now()?0.004f+vis_bass*vis_bass*0.05f+vis_energy*0.01f:0.0015f;s->star_speed+=(target-s->star_speed)*0.3f;
 float cx=x+w/2,cy=y+h/2,scale=fmaxf(w,h)*0.5f;Vtx *v=vb(count*6);int n=0;
 for(int i=0;i<count;i++){Star *st=&s->stars[i];st->pz=st->z;st->z-=s->star_speed;if(st->z<=0.02f)star_reset(st,1);
  float sx=cx+st->x/st->z*scale,sy=cy+st->y/st->z*scale;if(sx<x || sx>x+w || sy<y || sy>y+h)continue;
  float px=cx+st->x/st->pz*scale,py=cy+st->y/st->pz*scale;int k=(int)((1-st->z)*6);if(k<0)k=0;if(k>5)k=5;float near=(k+0.5f)/6;
  n+=quad_seg(v+n,px,py,sx+0.3f,sy+0.3f,0.9f+near*2.2f,C(T->accent,(int)((0.2f+0.8f*near)*255)));}
 if(n)sceGuDrawArray(GU_TRIANGLES,VFMT,n,0,v);
}
static void vis_ridges(VisSlot *s,float x,float y,float w,float h) {
 for(int i=0;i<56;i++){float t=playing_now()?band(i,56):0,shape=sinf(3.1415926f*(i+0.5f)/56);s->ridge_now[i]+=(t*(0.35f+0.65f*shape)-s->ridge_now[i])*0.5f;}
 if(++s->frame%3==0){s->ridge_head=(s->ridge_head+1)%26;memcpy(s->ridge_hist[s->ridge_head],s->ridge_now,sizeof(s->ridge_now));}
 float rise=h*0.42f,depth=(h-rise*0.35f)/26,xs[58],ys[58];unsigned int back=C(theme==1?0x07070e:theme==2?T->bg:T->panel,255);
 for(int k=25;k>=0;k--){float *line=k==0?s->ridge_now:s->ridge_hist[(s->ridge_head-k+1+26)%26];float near=1-k/26.0f,inset=(1-near)*w*0.18f,base=y+h-2-k*depth;
  for(int i=0;i<56;i++){xs[i]=x+inset+(w-2*inset)*i/55;ys[i]=base-line[i]*rise*(0.55f+0.45f*near);}
  Vtx *v=vb(112);for(int i=0;i<56;i++){vput(v+i*2,WHITE_UV,WHITE_UV,back,xs[i],ys[i]);vput(v+i*2+1,WHITE_UV,WHITE_UV,back,xs[i],base);}
  sceGuDrawArray(GU_TRIANGLE_STRIP,VFMT,112,0,v);
  polyline(xs,ys,56,k==0?1.8f:1.2f,C(T->accent,(int)((0.25f+0.75f*near)*255)));}
}
static void vis_phase(VisSlot *s,float x,float y,float w,float h) {
 /* Calm version: fewer points, each frame blended into the last, slow auto-gain, one faint trail. */
 memmove(s->phase[1],s->phase[0],sizeof(s->phase[0]));float peak=0.05f;
 for(int i=0;i<240;i++){float a=playing_now()?wave_at(i*2):0,b=playing_now()?wave_at(i*2+14):0;
  s->phase[0][i][0]=s->phase[1][i][0]*0.6f+(a-b)*0.7071f*0.4f;s->phase[0][i][1]=s->phase[1][i][1]*0.6f+(a+b)*0.7071f*0.4f;peak=fmaxf(peak,fmaxf(fabsf(a),fabsf(b)));}
 if(s->phase_n<2)s->phase_n++;if(s->phase_scale<=0)s->phase_scale=1;
 s->phase_scale+=(1/peak-s->phase_scale)*0.03f;
 float cx=x+w/2,cy=y+h/2,r=fminf(w,h)/2-3;
 seg(cx-r,cy,cx+r,cy,1,vis_dim());seg(cx,cy-r,cx,cy+r,1,vis_dim());
 static float xs[240],ys[240];float k=r*fminf(s->phase_scale,6)*0.55f;
 for(int t=s->phase_n-1;t>=0;t--){for(int i=0;i<240;i++){xs[i]=cx+s->phase[t][i][0]*k;ys[i]=cy-s->phase[t][i][1]*k;}
  polyline(xs,ys,240,t==0?1.4f:1,C(t==0?T->accent:accent_b(),t==0?200:50));}
}
static void vis_ringscope(VisSlot *s,float x,float y,float w,float h) {
 memmove(s->ring[1],s->ring[0],2*sizeof(s->ring[0]));
 /* each frame blends into the last so the ring breathes instead of jittering */
 for(int i=0;i<128;i++){float v=0;if(playing_now())for(int j=0;j<4;j++)v+=wave_at(i*4+j);s->ring[0][i]=s->ring[1][i]*0.65f+v/4*0.35f;}
 for(int i=0;i<16;i++){float k=i/16.0f;s->ring[0][112+i]=s->ring[0][112+i]*(1-k)+s->ring[0][i]*k;}
 if(s->ring_n<2)s->ring_n++;
 s->ring_bass+=((playing_now()?vis_bass:0)-s->ring_bass)*0.08f;if(playing_now())s->turn+=0.003f+vis_energy*0.005f;
 float cx=x+w/2,cy=y+h/2,mx=fminf(w,h)/2-2,r0=mx*(0.55f+0.12f*s->ring_bass),xs[129],ys[129];
 circle_line(cx,cy,r0,r0,1,vis_dim());
 for(int t=s->ring_n-1;t>=0;t--){float rr=r0*(1+t*0.06f);
  for(int i=0;i<=128;i++){int j=i%128;float a=j/128.0f*6.2831853f+s->turn-t*0.04f,rad=rr+s->ring[t][j]*mx*0.4f;xs[i]=cx+cosf(a)*rad;ys[i]=cy+sinf(a)*rad;}
  polyline(xs,ys,129,t==0?1.6f:1.1f,C(t==0?T->accent:accent_b(),t==0?230:50));}
}
static void vis_fountain(VisSlot *s,float x,float y,float w,float h) {
 float highs=0;if(playing_now())for(int b=20;b<BANDS;b++)highs=fmaxf(highs,spec[b]);
 int spray=playing_now()?(int)(vis_energy*14):0,kick=kick_hit(s);
 for(int i=0;i<spray+(kick?40:0) && s->spark_n<300;i++){int hot=kick && i>=spray;float spread=0.25f+highs*0.9f+(hot?0.5f:0);Spark *p=&s->sparks[s->spark_n++];
  p->x=0.5f+(frand()-0.5f)*0.06f;p->y=1;p->vx=(frand()-0.5f)*0.02f*spread;p->vy=-(0.018f+frand()*0.02f+(hot?0.012f:0)+vis_energy*0.02f);p->life=1;p->hot=hot;}
 for(int i=s->spark_n-1;i>=0;i--){Spark *p=&s->sparks[i];p->x+=p->vx;p->y+=p->vy;p->vy+=0.0011f;p->life-=0.012f;
  if(p->life<=0 || p->y>1.02f || p->x<-0.05f || p->x>1.05f)s->sparks[i]=s->sparks[--s->spark_n];}
 if(!s->spark_n)return;
 float size=fmaxf(1.2f,fminf(w,h)/70);Vtx *v=vb(s->spark_n*2);int n=0;
 for(int i=0;i<s->spark_n;i++){Spark *p=&s->sparks[i];int b=(int)(p->life*5);if(b>4)b=4;if(b<0)b=0;
  n+=disk_sprite(v+n,x+p->x*w,y+p->y*h,size*(0.6f+p->life*0.6f),C(p->hot?accent_b():T->accent,(b+1)*51));}
 linear(1);sceGuDrawArray(GU_SPRITES,VFMT,n,0,v);linear(0);
}
static void rounded_outline(float cx,float cy,float rx,float ry,float width,unsigned int c) {
 /* four quarter arcs; each arc's center sits in the quadrant it bends around */
 float xs[41],ys[41];int n=0;float kx=rx*0.35f,ky=ry*0.35f;const int start[4]={0,90,180,270};
 for(int q=0;q<4;q++)for(int i=0;i<=9;i++){float a=(start[q]+i*10)*3.1415926f/180,ca=cosf(a),sa=sinf(a);
  xs[n]=cx+(ca>=0?rx-kx:-(rx-kx))+ca*kx;ys[n]=cy+(sa>=0?ry-ky:-(ry-ky))+sa*ky;n++;}
 xs[n]=xs[0];ys[n]=ys[0];n++;polyline(xs,ys,n,width,c);
}
static void vis_tunnel(VisSlot *s,float x,float y,float w,float h) {
 /* Follows the music without strobing: loudness sets the cruising speed, and each beat
    gives a short surge that eases off over about half a second. Beats never add extra rings. */
 s->calm_energy+=((playing_now()?vis_energy:0)-s->calm_energy)*0.1f;
 int kick=kick_hit(s);if(kick)s->tunnel_boost=fmaxf(s->tunnel_boost,0.5f+vis_bass*0.5f);s->tunnel_boost*=0.92f;
 float speed=playing_now()?0.004f+s->calm_energy*0.016f+s->tunnel_boost*0.008f:0.0015f;s->tunnel_since+=speed;
 if(s->tunnel_since>0.16f && s->tunnel_n<24){s->tunnel_z[s->tunnel_n]=0.02f;s->tunnel_hot[s->tunnel_n]=kick;s->tunnel_n++;s->tunnel_since=0;}
 for(int i=s->tunnel_n-1;i>=0;i--){s->tunnel_z[i]+=speed*(0.5f+s->tunnel_z[i]*2.5f);if(s->tunnel_z[i]>1.3f){s->tunnel_z[i]=s->tunnel_z[--s->tunnel_n];s->tunnel_hot[i]=s->tunnel_hot[s->tunnel_n];}}
 if(playing_now())s->turn+=0.08f+s->calm_energy*0.3f+s->tunnel_boost*0.3f;
 float cx=x+w/2,cy=y+h/2,reach=sqrtf(w*w+h*h)/2;
 for(int i=0;i<8;i++){float a=(i*45+s->turn*0.2f)*3.1415926f/180;seg(cx,cy,cx+cosf(a)*reach,cy+sinf(a)*reach,1,vis_dim());}
 for(int i=0;i<s->tunnel_n;i++){float z=s->tunnel_z[i],k=z*z,rx=w/2*k*1.1f,ry=h/2*k*1.1f;if(rx<2 || ry<2)continue;
  float al=clampf(z*1.6f,0.1f,1)*(z>1?fmaxf(0,(1.3f-z)/0.3f):1);
  rounded_outline(cx,cy,rx,ry,fmaxf(1,k*4),C(s->tunnel_hot[i]?accent_b():T->accent,(int)(al*255)));}
}
static void vis_leds(VisSlot *s,float x,float y,float w,float h) {
 float cw=w/28,rh=h/14,r=fmaxf(1,fminf(cw,rh)*0.36f);Vtx *v=vb(28*14*2);int n=0;unsigned int off=theme==2?C(T->second,67):C(T->accent,51);
 for(int i=0;i<28;i++){float t=playing_now()?band(i,28):0;s->led_v[i]+=(t-s->led_v[i])*(t>s->led_v[i]?0.7f:0.2f);
  if(s->led_v[i]>=s->led_peak[i]){s->led_peak[i]=s->led_v[i];s->led_hold[i]=18;}else if(s->led_hold[i]>0)s->led_hold[i]--;else s->led_peak[i]=fmaxf(0,s->led_peak[i]-0.025f);
  int lit=(int)(s->led_v[i]*14+0.5f),pk=(int)(s->led_peak[i]*14+0.5f)-1;
  for(int j=0;j<14;j++){int hot=j>=11;unsigned int c=(j<lit || j==pk)?C((hot || (j==pk && pk>0))?accent_b():T->accent,255):off;
   n+=disk_sprite(v+n,x+(i+0.5f)*cw,y+h-(j+0.5f)*rh,r,c);}}
 linear(1);sceGuDrawArray(GU_SPRITES,VFMT,n,0,v);linear(0);
}
/* Twin VU needles, LOW and HIGH, like the desktop meter pair. */
static void vis_vu(VisSlot *s,float x,float y,float w,float h) {
 float lo=0,hi=0;if(playing_now())for(int b=0;b<BANDS;b++){if(b<16)lo=fmaxf(lo,spec[b]);else hi=fmaxf(hi,spec[b]);}
 float target[2]={fminf(1,lo*1.05f),fminf(1,hi*1.35f)};
 for(int m=0;m<2;m++) {
  /* needle ballistics: spring toward the target with a little overshoot */
  s->vu_v[m]=s->vu_v[m]*0.72f+(target[m]-s->vu[m])*0.22f;s->vu[m]=clampf(s->vu[m]+s->vu_v[m],0,1.05f);
  float mw=(w-6)/2,mx=x+m*(mw+6),my=y;
  rect(mx,my,mw,h,C(T->accent,theme==2?10:12));frame_rect(mx,my,mw,h,1,vis_dim());
  float px=mx+mw/2,py=my+h*1.18f,rad=fminf(mw*0.62f,h*0.95f),a0=-2.35f,a1=-0.79f,xs[33],ys[33];
  for(int i=0;i<=32;i++){float a=a0+(a1-a0)*i/32;xs[i]=px+cosf(a)*rad;ys[i]=py+sinf(a)*rad;}
  polyline(xs,ys,25,1.2f,C(theme==1?0x6e86a8:T->dim,255));polyline(xs+24,ys+24,9,2,C(accent_b(),255));
  for(int i=0;i<=10;i++){float a=a0+(a1-a0)*i/10,c=cosf(a),sy=sinf(a),len=i%5==0?6:3;seg(px+c*rad,py+sy*rad,px+c*(rad-len),py+sy*(rad-len),1,C(i>=8?accent_b():(theme==1?0x6e86a8:T->dim),255));}
  float a=a0+(a1-a0)*s->vu[m];seg(px,py,px+cosf(a)*(rad+2),py+sinf(a)*(rad+2),1.5f,C(T->accent,255));
  text_mid(mx+mw/2,my+h-9,m?"HIGH":"LOW",C(theme==1?0x6e86a8:T->dim,255),F_SMALL);
 }
}
/* ---- PSP extras (kept from 2.0) ---- */
static void vis_extra(VisSlot *sl,int id,float x,float y,float w,float h) {
 unsigned int a=T->accent,s2=T->second;float cx=x+w/2,cy=y+h/2,energy=vis_energy*28,xs[160],ys[160];
 if(id==0) {
  /* smoothed between frames and drawn at a gentler height, so it sways instead of flickering */
  for(int i=0;i<128;i++){float t=playing_now()?scope[i]/32768.0f:0;sl->osc[i]+=(t-sl->osc[i])*0.3f;xs[i]=x+i*w/127;ys[i]=cy+sl->osc[i]*(h/2-3)*0.6f;}
  polyline(xs,ys,128,1.8f,C(a,230));
 }else if(id==5) {
  /* slow drift; the height follows a heavily smoothed energy, not each frame's level */
  sl->calm_energy+=((playing_now()?vis_energy:0)-sl->calm_energy)*0.03f;
  if(playing_now())sl->ribbon_phase+=0.5f+sl->calm_energy*1.5f;
  float amp=(sl->calm_energy*28*0.6f+5)*h/60000.0f;int n=(int)(w/4)+1;if(n>160)n=160;
  for(int r=0;r<3;r++){for(int i=0;i<n;i++){float px=x+i*w/(n-1);int k=(int)((px-x)*127/w);
   xs[i]=px;ys[i]=cy+sn((int)sl->ribbon_phase+i*6+r*50)*amp+sl->osc[k]*h*0.08f;}
   polyline(xs,ys,n,r==1?2.2f:1.4f,C(r==1?s2:a,r==1?200:120));}
  for(int i=0;i<128;i++){float t=playing_now()?scope[i]/32768.0f:0;sl->osc[i]+=(t-sl->osc[i])*0.15f;}
 }else if(id==6) {
  for(int r=0;r<3;r++) {
   float rx=w/5+r*w/14,ry=h/4+r*h/12;
   for(int k=0;k<=32;k++){xs[k]=cx+sn(k*8+64)*rx/1024;ys[k]=cy+sn(k*8)*ry/1024;}
   polyline(xs,ys,33,1.2f,C(a,90+r*50));
   int ang=visual_tick*(r+1)*(1+(int)energy/10);disk(cx+sn(ang+64)*rx/1024,cy+sn(ang)*ry/1024,3+spec[r*8]*4,C(r==1?s2:T->fg,255));
  }
 }else if(id==7) {
  Vtx *v=vb(64);
  for(int i=0;i<32;i++){float xx=x+(i*37)%(int)w,yy=y+(visual_tick*(1+i%3)+i*17)%(int)h;unsigned int c=C(a,70+i*5);
   vput(v+i*2,WHITE_UV,WHITE_UV,c,xx,yy);vput(v+i*2+1,WHITE_UV,WHITE_UV,c,xx,yy+4+energy/3);}
  sceGuDrawArray(GU_LINES,VFMT,64,0,v);
 }else {
  int cols=(int)(w/8),rows=(int)(h/6);if(cols*rows>900)rows=900/cols;
  Vtx *v=vb(cols*rows*2);int n=0;
  for(int j=0;j<rows;j++)for(int i=0;i<cols;i++) {
   int t=(sn(i*9+visual_tick)+sn(j*17-visual_tick)+sn((i+j)*7+visual_tick/2)+3072)/24;if(t>255)t=255;if(t<0)t=0;
   unsigned int c=C(mix(s2,a,t),20+t*(int)(energy+8)/40);float px=x+i*8,py=y+j*6;
   vput(v+n++,WHITE_UV,WHITE_UV,c,px,py);vput(v+n++,WHITE_UV,WHITE_UV,c,px+6,py+4);
  }
  sceGuDrawArray(GU_SPRITES,VFMT,n,0,v);
 }
}
/* ---- newer visualizers ---- */
/* Circle Scope: the waveform wrapped around a ring that swells with the bass. */
static void vis_circle_scope(VisSlot *s,float x,float y,float w,float h) {
 float cx=x+w/2,cy=y+h/2,base=fminf(w,h)*0.28f,xs[97],ys[97];
 s->cscope_bass+=((playing_now()?vis_bass:0)-s->cscope_bass)*0.2f;
 float r0=base*(1+s->cscope_bass*0.35f);
 for(int i=0;i<96;i++){float t=playing_now()?scope[(i*128/96)&127]/32768.0f:0;s->cscope[i]+=(t-s->cscope[i])*0.35f;}
 for(int i=0;i<=96;i++){int k=i%96;float a=k*6.2831853f/96-1.5707963f,r=r0+s->cscope[k]*base*0.8f;xs[i]=cx+cosf(a)*r*w/fmaxf(w,h)*1.4f;ys[i]=cy+sinf(a)*r;}
 circle_line(cx,cy,r0*w/fmaxf(w,h)*1.4f,r0,1,vis_dim());
 polyline(xs,ys,97,1.8f,C(T->accent,240));
 disk(cx,cy,2+s->cscope_bass*5,C(accent_b(),200));
}
/* Pulse Rings: every beat sends a ring out from the center; loud beats glow in the second color. */
static void vis_pulse_rings(VisSlot *s,float x,float y,float w,float h) {
 int kick=kick_hit(s);
 if(kick){int slot=0;for(int i=1;i<12;i++)if(s->pring_life[i]<s->pring_life[slot])slot=i;s->pring_r[slot]=0.04f;s->pring_life[slot]=1;s->pring_hot[slot]=vis_bass>0.55f;}
 float cx=x+w/2,cy=y+h/2,reach=fmaxf(w,h)*0.6f,e=playing_now()?vis_energy:0;
 for(int i=0;i<12;i++)if(s->pring_life[i]>0){s->pring_r[i]+=0.018f+s->pring_r[i]*0.03f;s->pring_life[i]-=0.022f;
  float r=s->pring_r[i]*reach;if(s->pring_life[i]>0)circle_line(cx,cy,r,r*h/w*1.2f,1+s->pring_life[i]*2.5f,C(s->pring_hot[i]?accent_b():T->accent,(int)(s->pring_life[i]*230)));}
 float core=4+e*fminf(w,h)*0.5f;glow(cx,cy,core*2.2f,C(T->accent,90));disk(cx,cy,core*0.5f,C(T->accent,230));
}
/* Synth Grid: a perspective floor scrolling toward you, its hills shaped by the spectrum. */
static void vis_synth_grid(VisSlot *s,float x,float y,float w,float h) {
 const int rows=14,cols=24;
 if(playing_now())s->grid_scroll+=0.08f+vis_energy*0.25f;
 while(s->grid_scroll>=1){s->grid_scroll-=1;s->grid_head=(s->grid_head+1)%rows;
  for(int c=0;c<cols;c++){float mid=fabsf(c-(cols-1)/2.0f)/((cols-1)/2.0f);s->grid[s->grid_head][c]=playing_now()?band((int)(mid*15),16)*(0.3f+mid*0.9f):0;}}
 float hz=y+h*0.32f,xs[24],ys[24];
 glow(x+w/2,hz,fminf(w,h)*0.35f,C(accent_b(),70));
 for(int r=rows-1;r>=0;r--) { /* far rows first */
  float d=(r+1-s->grid_scroll)/rows;if(d<=0.02f)continue;float p=1/(d*3+0.35f)*0.35f,yy=hz+(y+h-hz)*p;
  int idx=(s->grid_head-r+rows*4)%rows;
  for(int c=0;c<cols;c++){float u=(c/(float)(cols-1)-0.5f)*(0.3f+p*1.9f);xs[c]=x+w/2+u*w;ys[c]=yy-s->grid[idx][c]*h*0.45f*p;}
  polyline(xs,ys,cols,1,C(T->accent,(int)(60+p*190)));
 }
 for(int c=0;c<cols;c+=3){float u0=(c/(float)(cols-1)-0.5f);seg(x+w/2+u0*0.3f*w*0.4f,hz,x+w/2+u0*2.2f*w,y+h,1,vis_dim());}
}
/* Bubbles: rising circles, blown bigger by the band each one belongs to. */
static void vis_bubbles(VisSlot *s,float x,float y,float w,float h) {
 int spawn=playing_now()?(int)(vis_energy*3+(kick_hit(s)?4:0)):0;if(!spawn && playing_now() && rnd(6)==0)spawn=1;
 for(int i=0;i<spawn && s->bub_n<40;i++){int k=s->bub_n++;s->bub_x[k]=frand();s->bub_y[k]=1.05f;s->bub_r[k]=0.3f+band((int)(s->bub_x[k]*15),16)*1.2f;s->bub_v[k]=0.006f+frand()*0.01f;}
 for(int i=s->bub_n-1;i>=0;i--){s->bub_y[i]-=s->bub_v[i];s->bub_x[i]+=sinf((s->bub_y[i]+i)*9)*0.002f;
  if(s->bub_y[i]<-0.1f){int l=--s->bub_n;s->bub_x[i]=s->bub_x[l];s->bub_y[i]=s->bub_y[l];s->bub_r[i]=s->bub_r[l];s->bub_v[i]=s->bub_v[l];}}
 float unit=fminf(w,h)*0.09f;
 for(int i=0;i<s->bub_n;i++){float r=unit*s->bub_r[i],bx=x+s->bub_x[i]*w,by=y+s->bub_y[i]*h;int al=(int)(clampf(s->bub_y[i]*1.4f,0,1)*230);
  circle_line(bx,by,r,r,1.3f,C(s->bub_r[i]>1?accent_b():T->accent,al));disk(bx-r*0.35f,by-r*0.35f,fmaxf(1,r*0.2f),C(T->fg,al/2));}
}
/* Sunburst: rays from the center, the bands going round twice so opposite rays match, turning slowly. */
static void vis_sunburst(VisSlot *s,float x,float y,float w,float h) {
 float cx=x+w/2,cy=y+h/2,inner=fminf(w,h)*0.12f,outer=fminf(w,h)*0.48f,sx=w/fmaxf(w,h)*1.5f;if(sx<1)sx=1;
 for(int i=0;i<18;i++){float t=playing_now()?band(i,18):0;s->sun[i]+=(t-s->sun[i])*(t>s->sun[i]?0.5f:0.1f);}
 if(playing_now())s->sun_turn+=0.004f+vis_energy*0.02f;
 Vtx *v=vb(36*6);int n=0;
 for(int k=0;k<36;k++){int i=k%18;float a=k*6.2831853f/36+s->sun_turn,len=inner+(outer-inner)*fmaxf(0.05f,s->sun[i]);
  float ca=cosf(a)*sx,sa=sinf(a);n+=quad_seg(v+n,cx+ca*inner,cy+sa*inner,cx+ca*len,cy+sa*len,fmaxf(1.5f,outer*0.07f),C(s->sun[i]>0.75f?accent_b():T->accent,200));}
 sceGuDrawArray(GU_TRIANGLES,VFMT,n,0,v);
 circle_line(cx,cy,inner*sx,inner,1.5f,C(T->accent,255));
}
/* Heartbeat: a monitor trace that spikes on each beat and scrolls left. */
static void vis_heartbeat(VisSlot *s,float x,float y,float w,float h) {
 /* beats on the kick drum, or a resting pulse when the music has none */
 s->ecg_phase+=1;if(kick_hit(s) || (playing_now() && s->ecg_phase>55)){if(s->ecg_beat<=0)s->ecg_beat=1;s->ecg_phase=0;}
 for(int step=0;step<2;step++) {
  float v=0;
  if(s->ecg_beat>0){float p=1-s->ecg_beat; /* P wave, the sharp QRS spike, then the T wave */
   v=p<0.15f?sinf(p/0.15f*3.14159f)*0.12f:p<0.22f?-(p-0.15f)/0.07f*0.25f:p<0.3f?-0.25f+(p-0.22f)/0.08f*1.25f*(0.6f+vis_bass*0.6f):p<0.38f?(1-(p-0.3f)/0.08f)*0.9f:p<0.7f?sinf((p-0.38f)/0.32f*3.14159f)*0.18f:0;
   s->ecg_beat-=0.045f;}
  v+=(playing_now()?wave_at(s->ecg_head*7)*0.05f:0);
  s->ecg[s->ecg_head]=v;s->ecg_head=(s->ecg_head+1)%200;
 }
 for(int g=1;g<4;g++)rect(x,y+h*g/4,w,1,vis_dim());
 for(int g=1;g<8;g++)rect(x+w*g/8,y,1,h,vis_dim());
 float xs[200],ys[200];
 for(int i=0;i<200;i++){int k=(s->ecg_head+i)%200;xs[i]=x+i*w/199;ys[i]=y+h*0.6f-s->ecg[k]*h*0.5f;}
 polyline(xs,ys,200,1.6f,C(T->accent,240));
 glow(xs[199],ys[199],8,C(accent_b(),160));disk(xs[199],ys[199],2,C(accent_b(),255));
}
static void visualizer(int id,int slot,float x,float y,float w,float h) {
 VisSlot *s=&vslots[slot];
 switch(id) {
 case 19:vis_circle_scope(s,x,y,w,h);break;case 20:vis_pulse_rings(s,x,y,w,h);break;case 21:vis_synth_grid(s,x,y,w,h);break;
 case 22:vis_bubbles(s,x,y,w,h);break;case 23:vis_sunburst(s,x,y,w,h);break;case 24:vis_heartbeat(s,x,y,w,h);break;
 case 1:vis_ridges(s,x,y,w,h);break;case 2:vis_starfield(s,x,y,w,h);break;case 3:vis_tunnel(s,x,y,w,h);break;
 case 4:vis_bars(s,x,y,w,h);break;case 8:vis_vu(s,x,y,w,h);break;case 10:vis_mirror(s,x,y,w,h);break;
 case 11:vis_line(s,x,y,w,h);break;case 12:vis_radial(s,x,y,w,h);break;case 13:vis_spectrogram(slot,x,y,w,h);break;
 case 14:vis_level(s,x,y,w,h);break;case 15:vis_phase(s,x,y,w,h);break;case 16:vis_ringscope(s,x,y,w,h);break;
 case 17:vis_fountain(s,x,y,w,h);break;case 18:vis_leds(s,x,y,w,h);break;
 default:vis_extra(s,id,x,y,w,h);
 }
}
