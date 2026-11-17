/* 10-band graphic equalizer, after desktop ZOID's EQ: bands at 31 Hz to 16 kHz, -12 to +12 dB,
   a preamp, presets and auto preamp. Each band is a peaking biquad (RBJ cookbook, Q 1.41 so
   neighboring octaves blend). The audio thread runs it on each 1024-frame output block, right
   before it goes to the hardware; bands at 0 dB are skipped, so a flat EQ costs nothing.
   The page changes gains and bumps eq_version; the audio thread rebuilds its filters from that. */
#define EQ_BANDS 10
static const float eq_freq[EQ_BANDS]={31,62,125,250,500,1000,2000,4000,8000,16000};
static const char *eq_band_names[EQ_BANDS]={"31","62","125","250","500","1K","2K","4K","8K","16K"};
static int eq_on=1,eq_gain[EQ_BANDS],eq_pre,eq_auto_pre,eq_preset; /* gains and preamp in dB */
static volatile unsigned int eq_version=1;
#define EQ_PRESET_N 10
static const char *eq_preset_names[EQ_PRESET_N]={"Flat","Bass Boost","Treble Boost","Vocal","Rock","Pop","Jazz","Classical","Electronic","Loudness"};
static const signed char eq_presets[EQ_PRESET_N][EQ_BANDS]={
 {0,0,0,0,0,0,0,0,0,0},
 {7,6,5,3,1,0,0,0,0,0},
 {0,0,0,0,0,1,3,5,6,7},
 {-2,-2,-1,1,3,4,3,2,0,-1},
 {5,4,2,-1,-2,-1,1,3,4,5},
 {-1,1,3,4,3,0,-1,-1,1,2},
 {3,2,1,2,-1,-1,0,1,2,3},
 {4,3,2,1,-1,-1,0,2,3,4},
 {6,5,2,0,-2,1,0,2,4,5},
 {6,4,1,0,-1,0,0,1,4,6},
};
/* eq_preset: 0..EQ_PRESET_N-1 a preset, -1 Custom (the sliders were moved) */
static void eq_changed(void) {
 if(eq_auto_pre){int top=0;for(int i=0;i<EQ_BANDS;i++)if(eq_gain[i]>top)top=eq_gain[i];eq_pre=-top;}
 eq_version++;
}
static void eq_apply_preset(int p) {
 eq_preset=(p%EQ_PRESET_N+EQ_PRESET_N)%EQ_PRESET_N;
 for(int i=0;i<EQ_BANDS;i++)eq_gain[i]=eq_presets[eq_preset][i];
 eq_changed();
}
/* User presets: saved in zoid-eq-presets.txt beside the settings. eq_preset 10.. means user preset (eq_preset-10). */
#define EQ_USER_MAX 8
typedef struct {char name[40];int pre;signed char gain[EQ_BANDS];} EqUser;
static EqUser eq_user[EQ_USER_MAX];static int eq_user_n;static char eq_user_path[PATHLEN];
static const char *eq_preset_name(void) {
 if(eq_preset>=0 && eq_preset<EQ_PRESET_N)return eq_preset_names[eq_preset];
 if(eq_preset>=EQ_PRESET_N && eq_preset-EQ_PRESET_N<eq_user_n)return eq_user[eq_preset-EQ_PRESET_N].name;
 return "Custom";
}
static void eq_user_write(void) {
 if(!eq_user_path[0])return;
 char tmp[PATHLEN+8];snprintf(tmp,sizeof(tmp),"%s.tmp",eq_user_path);
 FILE *f=fopen(tmp,"w");if(!f)return;
 int ok=fprintf(f,"ZOID_EQP_1\n%d\n",eq_user_n)>0;
 for(int i=0;i<eq_user_n && ok;i++){EqUser *u=&eq_user[i];ok=fprintf(f,"%d",u->pre)>0;for(int b=0;b<EQ_BANDS;b++)fprintf(f," %d",u->gain[b]);fprintf(f," %s\n",u->name);}
 if(fclose(f)!=0)ok=0;
 if(!ok){sceIoRemove(tmp);return;}
 sceIoRemove(eq_user_path);sceIoRename(tmp,eq_user_path);
}
static void eq_user_load(void) {
 eq_user_n=0;FILE *f=fopen(eq_user_path,"r");if(!f)return;
 char magic[16]={0};int n=0;
 if(fscanf(f,"%15s %d",magic,&n)==2 && !strcmp(magic,"ZOID_EQP_1") && n>=0 && n<=EQ_USER_MAX) {
  for(int i=0;i<n;i++) {
   EqUser u;int g,ok=fscanf(f,"%d",&u.pre)==1 && u.pre>=-12 && u.pre<=12;
   for(int b=0;b<EQ_BANDS && ok;b++){ok=fscanf(f,"%d",&g)==1 && g>=-12 && g<=12;u.gain[b]=g;}
   if(!ok || !fgets(u.name,sizeof(u.name),f))break;
   char *s=u.name;while(*s==' ')s++;memmove(u.name,s,strlen(s)+1);
   int len=strlen(u.name);while(len && (u.name[len-1]=='\n' || u.name[len-1]=='\r'))u.name[--len]=0;
   if(!len)break;
   eq_user[eq_user_n++]=u;
  }
 }
 fclose(f);
}
/* Saves the current curve under a name (replacing one with the same name). 0 when full. */
static int eq_user_save(const char *name) {
 int k=-1;for(int i=0;i<eq_user_n;i++)if(!strcasecmp(eq_user[i].name,name))k=i;
 if(k<0){if(eq_user_n>=EQ_USER_MAX)return 0;k=eq_user_n++;}
 EqUser *u=&eq_user[k];snprintf(u->name,sizeof(u->name),"%s",name);u->pre=eq_pre;for(int b=0;b<EQ_BANDS;b++)u->gain[b]=eq_gain[b];
 eq_user_write();eq_preset=EQ_PRESET_N+k;eq_changed();return 1;
}
static void eq_user_apply(int k) {
 if(k<0 || k>=eq_user_n)return;
 for(int b=0;b<EQ_BANDS;b++)eq_gain[b]=eq_user[k].gain[b];
 if(!eq_auto_pre)eq_pre=eq_user[k].pre;
 eq_preset=EQ_PRESET_N+k;eq_changed();
}
static void eq_user_delete(int k) {
 if(k<0 || k>=eq_user_n)return;
 memmove(&eq_user[k],&eq_user[k+1],sizeof(EqUser)*(eq_user_n-k-1));eq_user_n--;
 int cur=eq_preset-EQ_PRESET_N;if(eq_preset>=EQ_PRESET_N){if(cur==k)eq_preset=-1;else if(cur>k)eq_preset--;}
 eq_user_write();
}

typedef struct {float b0,b1,b2,a1,a2;} Biquad;
static void eq_design(Biquad *q,float f,float db,float rate) {
 float A=powf(10,db/40),w=2*3.14159265f*f/rate,s=sinf(w),c=cosf(w),al=s/(2*1.41f);
 float a0=1+al/A;
 q->b0=(1+al*A)/a0;q->b1=-2*c/a0;q->b2=(1-al*A)/a0;q->a1=-2*c/a0;q->a2=(1-al/A)/a0;
}
/* Response of the current settings in dB at frequency f (for the page's curve). */
static float eq_response_db(float f) {
 float total=eq_pre;
 for(int i=0;i<EQ_BANDS;i++) {
  if(!eq_gain[i])continue;
  Biquad q;eq_design(&q,eq_freq[i],eq_gain[i],44100);
  float w=2*3.14159265f*f/44100,c1=cosf(w),s1=sinf(w),c2=cosf(2*w),s2=sinf(2*w);
  float nr=q.b0+q.b1*c1+q.b2*c2,ni=-(q.b1*s1+q.b2*s2),dr=1+q.a1*c1+q.a2*c2,di=-(q.a1*s1+q.a2*s2);
  total+=10*log10f((nr*nr+ni*ni)/(dr*dr+di*di));
 }
 return total;
}

/* ---- audio thread side ---- */
static Biquad eq_q[EQ_BANDS];static float eq_z[EQ_BANDS][2][2]; /* band, channel, state */
static int eq_active[EQ_BANDS],eq_used;static float eq_gain_lin=1;
static unsigned int eq_built;static int eq_built_rate;
static void eq_process(short *pcm,int frames,int rate) {
 if(eq_built!=eq_version || eq_built_rate!=rate) {
  if(eq_built_rate!=rate)memset(eq_z,0,sizeof(eq_z));
  eq_built=eq_version;eq_built_rate=rate;eq_used=0;
  for(int i=0;i<EQ_BANDS;i++) {
   int was=eq_active[i];eq_active[i]=eq_gain[i] && eq_freq[i]<rate*0.45f;
   if(eq_active[i]){eq_design(&eq_q[i],eq_freq[i],eq_gain[i],rate);eq_used=1;}
   if(!was)memset(eq_z[i],0,sizeof(eq_z[i]));
  }
  eq_gain_lin=powf(10,eq_pre/20.0f);
 }
 if(!eq_on || (!eq_used && eq_pre==0))return;
 for(int ch=0;ch<2;ch++) {
  for(int n=0;n<frames;n++) {
   float x=pcm[n*2+ch]*eq_gain_lin;
   for(int i=0;i<EQ_BANDS;i++) {
    if(!eq_active[i])continue;
    const Biquad *q=&eq_q[i];float *z=eq_z[i][ch];
    float y=q->b0*x+z[0];z[0]=q->b1*x-q->a1*y+z[1];z[1]=q->b2*x-q->a2*y;x=y;
   }
   /* soft limiter: peaks above about -2.7 dB bend smoothly toward full scale instead of
      clipping, so boosted presets stay clean even with Auto preamp off */
   float a=x<0?-x:x;
   if(a>24000){a=24000+8767*tanhf((a-24000)/8767);x=x<0?-a:a;}
   int v=(int)x;if(v>32767)v=32767;if(v<-32768)v=-32768;pcm[n*2+ch]=v;
  }
 }
}

/* ---- volume boost: an automatic leveler for movies, podcasts and radio ----
   Films keep dialogue far below their peaks (their AAC peaks near full scale while speech sits
   20-30 dB down), and podcasts and stations are mastered much quieter than music, so at the PSP's
   full volume they are hard to hear. A fixed gain would clip the loud parts, so the gain follows
   the sound: the block level is tracked (fast to fall back when it gets loud, slow to rise again),
   quiet passages are raised toward a target up to a ceiling, loud ones are left alone, near-silence
   is not pumped up, and a peak limiter holds whatever goes over just under full scale.
   Music is mastered loud (RMS around -8 dBFS) while speech has 12-15 dB between its average and
   its peaks, so matching music needs the speech peaks pushed well past full scale: only a real
   limiter (it looks 1.5 ms ahead and turns the gain down before a peak arrives, then lets it back
   up over about 80 ms) makes that clean. A soft clip after it catches what the limiter misses.
   Settings > Volume boost: Off, Low, Medium, High (the default) or Max, each about 3 dB louder
   than the one before. Music is left as mastered. */
#define BOOST_LEVELS 5
static int volume_boost=3;          /* 0 Off, 1 Low, 2 Medium, 3 High, 4 Max */
static volatile int boost_src;      /* the audio playing is a film soundtrack, a station or an episode */
static float boost_env=-1,boost_gain=1;
#define LIM_AHEAD 64                /* frames: 1.45 ms at 44.1 kHz */
#define LIM_CEIL 31000.0f           /* about -0.5 dBFS */
static float lim_line[LIM_AHEAD][2],lim_g=1,lim_step;static int lim_pos;
/* the smallest gain any sample still in the line needs: a running minimum (values rise front to back) */
static float lim_qv[128];static unsigned int lim_qt[128],lim_qh,lim_qn,lim_t;
static void boost_reset(void){boost_env=-1;boost_gain=1;memset(lim_line,0,sizeof(lim_line));lim_pos=0;lim_g=1;lim_step=0;lim_qh=lim_qn=lim_t=0;}
/* each level's RMS target (dBFS: -14, -11, -9, -7, -5) and the most it may raise (dB: +12, +18, +22, +26, +30) */
static const float boost_target[BOOST_LEVELS]={0,6500,9200,11600,14600},boost_ceiling[BOOST_LEVELS]={1,4.0f,7.9f,12.6f,20.0f};
static void boost_process(short *pcm,int frames) {
 if(!volume_boost || !boost_src){if(boost_env>=0 || lim_t)boost_reset();return;}
 int lv=volume_boost<BOOST_LEVELS?volume_boost:BOOST_LEVELS-1;
 const float target=boost_target[lv],ceiling=boost_ceiling[lv];
 float sum=0;for(int i=0;i<frames*2;i++){float v=pcm[i];sum+=v*v;}
 float rms=sqrtf(sum/(frames*2));
 if(rms>60) { /* above about -55 dBFS: real sound, not silence between sentences */
  if(boost_env<0)boost_env=rms;
  else if(rms>boost_env)boost_env+=(rms-boost_env)*0.5f;  /* loud: back off within about 50 ms */
  else boost_env+=(rms-boost_env)*0.015f;                 /* quieter: rise over about 1.5 s */
 }
 float want=boost_env>0?target/boost_env:1;if(want>ceiling)want=ceiling;if(want<1)want=1;
 float from=boost_gain,to=boost_gain+(want-boost_gain)*(want<boost_gain?0.6f:0.08f);boost_gain=to;
 const float release=0.0003f; /* the limiter lets the gain back up over about 80 ms */
 for(int n=0;n<frames;n++) {
  float g=from+(to-from)*n/frames,l=pcm[n*2]*g,r=pcm[n*2+1]*g;
  float pk=l<0?-l:l,pr=r<0?-r:r;if(pr>pk)pk=pr;
  float need=pk>LIM_CEIL?LIM_CEIL/pk:1;  /* both channels share one gain, so the stereo image holds */
  while(lim_qn && lim_qv[(lim_qh+lim_qn-1)&127]>=need)lim_qn--;
  lim_qv[(lim_qh+lim_qn)&127]=need;lim_qt[(lim_qh+lim_qn)&127]=lim_t;lim_qn++;
  while(lim_t-lim_qt[lim_qh]>LIM_AHEAD){lim_qh=(lim_qh+1)&127;lim_qn--;}
  float wmin=lim_qv[lim_qh];lim_t++;
  /* a straight ramp that is all the way down when this sample comes out of the line */
  if(need<lim_g){float s=(lim_g-need)/LIM_AHEAD;if(s>lim_step)lim_step=s;}
  if(lim_step>0){lim_g-=lim_step;if(lim_g<=wmin){lim_g=wmin;lim_step=0;}}
  else if(wmin>lim_g){lim_g+=(1-lim_g)*release;if(lim_g>wmin)lim_g=wmin;}
  else lim_g=wmin;
  float *d=lim_line[lim_pos];float out[2]={d[0]*lim_g,d[1]*lim_g};d[0]=l;d[1]=r;if(++lim_pos==LIM_AHEAD)lim_pos=0;
  for(int ch=0;ch<2;ch++) {
   float x=out[ch],a=x<0?-x:x;
   if(a>30000){a=30000+2767*tanhf((a-30000)/2767);x=x<0?-a:a;}
   int v=(int)x;if(v>32767)v=32767;if(v<-32768)v=-32768;pcm[n*2+ch]=v;
  }
 }
}
