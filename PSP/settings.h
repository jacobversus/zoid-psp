/* Validated, versioned preferences. Playback data is never stored here.
   v13 adds the sort orders (MUSIC, VIDEO). Older files load with shuffle off: Shuffle all used to
   leave it saved on, which played every album out of order afterwards.
   v12 widens Volume boost to five levels (Off, Low, Medium, High, Max; v9-v11 Normal and Strong become High and Max);
   v11 adds Y2K's Theme color; v10 adds Theme color (Zoid, Phosphor, Portable); v9 adds Volume boost; v8 the Wi-Fi network and Online lyrics;
   v7 adds auto screen off, time display, hold-X time and scrolling speed; v6 the equalizer;
   v5 adds mixing mode, crossfade length, CPU speed and resume. v4 and older files
   keep what they had (v2/v3 switch to the new look). */
static void save_settings(void) {
 if(!settings_path[0])return;
 char tmp[PATHLEN+8],bak[PATHLEN+8];snprintf(tmp,sizeof(tmp),"%s.tmp",settings_path);snprintf(bak,sizeof(bak),"%s.bak",settings_path);
 FILE *f=fopen(tmp,"w");if(!f)return;
 int ok=fprintf(f,"ZOID_UI_13\n%d %d %d\n%d %d %d\n%d %d %d %d\n%d %d %d %d %d %d\n",theme,visual_count,seek_step_index,visuals[0],visuals[1],visuals[2],bindings[0],bindings[1],bindings[2],bindings[3],shuffle && !shuffle_oneoff,repeat_mode,mix_mode,fade_seconds,cpu_fast,resume_on)>0;
 if(ok){ok=fprintf(f,"%d %d %d %d",eq_on,eq_pre,eq_auto_pre,eq_preset)>0;for(int i=0;i<EQ_BANDS;i++)fprintf(f," %d",eq_gain[i]);fprintf(f,"\n");}
 if(ok)ok=fprintf(f,"%d %d %d %d\n",auto_off_idx,time_left,hold_idx,scroll_idx)>0;
 if(ok)ok=fprintf(f,"%d %d\n",wifi_config,online_lyrics)>0; /* v8: Wi-Fi network, online lyrics */
 if(ok)ok=fprintf(f,"%d\n",volume_boost)>0; /* v9: volume boost */
 if(ok)ok=fprintf(f,"%d %d %d\n",theme_color[0],theme_color[2],theme_color[8])>0; /* v10: theme colors */
 if(ok)ok=fprintf(f,"%d\n",theme_color[3])>0; /* v11: Y2K color */
 if(ok)ok=fprintf(f,"%d %d\n",music_sort,video_sort)>0; /* v13: sort orders */
 if(fclose(f)!=0)ok=0;
 if(!ok){sceIoRemove(tmp);return;}
 sceIoRemove(bak);sceIoRename(settings_path,bak);
 if(sceIoRename(tmp,settings_path)<0)sceIoRename(bak,settings_path);
}
static int read_settings(const char *path) {
 FILE *f=fopen(path,"r");if(!f)return 0;
 char magic[24]={0};if(fscanf(f,"%23s",magic)!=1){fclose(f);return 0;}
 int t,n,step,vis[3]={4,0,2},bind[4],sh,rp,mm=0,fs=6,cpu=1,res=0,ok;
 int eon=1,epre=0,eauto=0,epreset=0,eg[EQ_BANDS]={0},aoff=0,tleft=0,hold=1,scr=1;
 int v13=!strcmp(magic,"ZOID_UI_13"),v12=!strcmp(magic,"ZOID_UI_12") || v13,ms=SORT_ALBUM,vs=SORT_FILE,v11=!strcmp(magic,"ZOID_UI_11") || v12,v10=!strcmp(magic,"ZOID_UI_10") || v11,v9=!strcmp(magic,"ZOID_UI_9") || v10,v8=!strcmp(magic,"ZOID_UI_8") || v9,v7=!strcmp(magic,"ZOID_UI_7") || v8,wcfg=0,olyr=1,vb=3,tc[3]={0,0,0},ty=0;
 if(!strcmp(magic,"ZOID_UI_5") || !strcmp(magic,"ZOID_UI_6") || v7) {
  ok=fscanf(f,"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",&t,&n,&step,&vis[0],&vis[1],&vis[2],&bind[0],&bind[1],&bind[2],&bind[3],&sh,&rp,&mm,&fs,&cpu,&res)==16;
  /* v6 adds the equalizer: on, preamp, auto preamp, preset, ten band gains */
  if(ok && (v7 || !strcmp(magic,"ZOID_UI_6"))) {
   ok=fscanf(f,"%d %d %d %d",&eon,&epre,&eauto,&epreset)==4;
   for(int i=0;i<EQ_BANDS && ok;i++)ok=fscanf(f,"%d",&eg[i])==1 && eg[i]>=-12 && eg[i]<=12;
   if(ok)ok=eon>=0 && eon<=1 && epre>=-12 && epre<=12 && eauto>=0 && eauto<=1 && epreset>=-1 && epreset<EQ_PRESET_N+EQ_USER_MAX;
  }
  if(ok && v7) {
   ok=fscanf(f,"%d %d %d %d",&aoff,&tleft,&hold,&scr)==4 && aoff>=0 && aoff<5 && tleft>=0 && tleft<=1 && hold>=0 && hold<4 && scr>=0 && scr<3;
  }
  if(ok && v8)ok=fscanf(f,"%d %d",&wcfg,&olyr)==2 && wcfg>=0 && wcfg<=10 && olyr>=0 && olyr<=2;
  if(ok && v9){ok=fscanf(f,"%d",&vb)==1 && vb>=0 && vb<=(v12?BOOST_LEVELS-1:2);if(ok && !v12 && vb)vb+=2;} /* v9-v11: Off, Normal, Strong */
  if(ok && v10)ok=fscanf(f,"%d %d %d",&tc[0],&tc[1],&tc[2])==3 && tc[0]>=0 && tc[0]<7 && tc[1]>=0 && tc[1]<4 && tc[2]>=0 && tc[2]<13;
  if(ok && v11)ok=fscanf(f,"%d",&ty)==1 && ty>=0 && ty<7;
  if(ok && v13)ok=fscanf(f,"%d %d",&ms,&vs)==2 && ms>=0 && ms<SORT_N && vs>=0 && vs<SORT_N && sort_for_video(vs);
 }else if(!strcmp(magic,"ZOID_UI_4")) {
  int gp;ok=fscanf(f,"%d %d %d %d %d %d %d %d %d %d %d %d %d",&t,&n,&step,&vis[0],&vis[1],&vis[2],&bind[0],&bind[1],&bind[2],&bind[3],&sh,&rp,&gp)==13;mm=gp?0:3;
 }else if(!strcmp(magic,"ZOID_UI_3") || !strcmp(magic,"ZOID_UI_2")) {
  int row,mask,old[3],order[6],gp=1;
  int want=!strcmp(magic,"ZOID_UI_3")?21:20;
  ok=fscanf(f,"%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",&t,&n,&row,&mask,&step,&old[0],&old[1],&old[2],&order[0],&order[1],&order[2],&order[3],&order[4],&order[5],&bind[0],&bind[1],&bind[2],&bind[3],&sh,&rp,&gp)==want;
  t=0;n=1;mm=gp?0:3;
 }else ok=0;
 fclose(f);
 if(!v13)sh=0; /* see v13 above */
 if(!ok || t<0 || t>8 || n<0 || n>3 || step<0 || step>3 || sh<0 || sh>1 || rp<0 || rp>2 || mm<0 || mm>3 || cpu<0 || cpu>2 || res<0 || res>1)return 0; /* 9 themes (THEME_N in main.c) */
 int fade_ok=0;for(int i=0;i<6;i++)if(fs==fade_choices[i])fade_ok=1;if(!fade_ok)fs=6;
 for(int i=0;i<3;i++)if(vis[i]<0 || vis[i]>=25)return 0; /* 25 = VIS_COUNT (visualizers.h is included later) */
 for(int i=0;i<4;i++)if(bind[i]<0 || bind[i]>13)return 0;
 theme=t;visual_count=n;seek_step_index=step;shuffle=sh;repeat_mode=rp;set_mix(mm);fade_seconds=fs;cpu_fast=cpu;resume_on=res;
 memcpy(visuals,vis,sizeof(vis));memcpy(bindings,bind,sizeof(bind));
 eq_on=eon;eq_pre=epre;eq_auto_pre=eauto;eq_preset=epreset;memcpy(eq_gain,eg,sizeof(eg));eq_changed();
 auto_off_idx=aoff;time_left=tleft;hold_idx=hold;scroll_idx=scr;marquee_ms=scroll_ms[scr];
 wifi_config=wcfg;online_lyrics=olyr;volume_boost=vb;music_sort=ms;video_sort=vs;shuffle_oneoff=0;
 theme_color[0]=tc[0];theme_color[2]=tc[1];theme_color[8]=tc[2];theme_color[3]=ty;themes_apply();
 return 1;
}
static void load_settings(void) {
 if(!read_settings(settings_path)){char bak[PATHLEN+8];snprintf(bak,sizeof(bak),"%s.bak",settings_path);read_settings(bak);}
}
/* Resume: the song and position, saved while playing and on exit. */
static char resume_file[PATHLEN];
static void save_resume(void) {
 if(!resume_file[0] || !active[0] || radio_is_url(active))return;
 char tmp[PATHLEN+8];snprintf(tmp,sizeof(tmp),"%s.tmp",resume_file);
 FILE *f=fopen(tmp,"w");if(!f)return;
 int ok=fprintf(f,"ZOID_RESUME_1\n%d\n%s\n",elapsed,active)>0;if(fclose(f)!=0)ok=0;
 if(!ok){sceIoRemove(tmp);return;}
 sceIoRemove(resume_file);sceIoRename(tmp,resume_file);
}
static int load_resume(char *path,int cap,int *seconds) {
 FILE *f=fopen(resume_file,"r");if(!f)return 0;
 char magic[24]={0};int s=-1;path[0]=0;
 int ok=fscanf(f,"%23s %d ",magic,&s)==2 && !strcmp(magic,"ZOID_RESUME_1") && s>=0 && fgets(path,cap,f);
 fclose(f);if(!ok)return 0;
 int n=strlen(path);while(n && (path[n-1]=='\n' || path[n-1]=='\r'))path[--n]=0;
 *seconds=s;return n>0;
}
