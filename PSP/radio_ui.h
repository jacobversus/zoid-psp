/* Internet Radio in the Library, and synced lyrics from LRCLIB: the screen side.
   Internet Radio (an entry at the top of MUSIC) opens a list: Search stations (the PSP keyboard),
   Popular stations, Genres, then your saved stations (radio.txt beside the EBOOT, one
   "Name|URL" per line; edit it on a PC or hold X on a station to save it).
   The directory is Radio Browser (radio-browser.info, free, no account); only MP3 and AAC
   stations are listed, as those are what the PSP decodes. iHeartRadio's directory (api.iheart.com,
   the one its website uses, no account) adds the big commercial stations Radio Browser lacks: pick
   a state and city once (radio-iheart.txt), then its whole lineup, or search it. A station plays like a song: it
   becomes the queue (Previous / Next step through the list), the title and artist follow the
   station's song titles, and the EQ and visualizers work as usual.
   Network work runs on the zoid_net thread; Wi-Fi turns off after 3 minutes unused.
   Online lyrics: when a song has no lyrics of its own, LRCLIB (lrclib.net, free) is asked for
   them, over HTTPS. Synced lyrics are preferred; what is found is saved as a .lrc beside the song,
   so it works offline next time. For a station the lyrics follow the song from the moment its
   title changed. Settings > Online lyrics: Off, When Lyrics is open (default), or For every song. */
#define RADIO_NAME "Internet Radio"
static int pod_is_episode(const char *path); /* podcast_ui.h */
enum {RV_HOME=1,RV_LIST,RV_GENRES,RV_COUNTRIES,RV_REGIONS,RV_IHSTATES,RV_IHCITIES};
/* Radio rows (Entry.folder): 5 Internet Radio (in MUSIC), 6 Search, 7 Popular, 8 Genres, 9 a genre,
   10 Local stations, 11 a country, 12 a place in it, 13 Change place, 14 Anywhere (no place),
   30 iHeartRadio, 31 Search iHeartRadio, 32 Change city, 33 a state, 34 a city in it */
typedef struct {char name[80],url[400],info[48];} Station;
static int radio_view,radio_from,radio_first,radio_home_cursor;
static char radio_title[96];
static Station *saved_st;static int saved_n;      /* radio.txt */
static Station *list_st;static int list_n;        /* the list on screen (RV_LIST) */
static char radio_file[PATHLEN];
static int row_live(void){return radio_is_url(active) && !radio_is_episode(active);} /* a station (a streamed podcast episode is not live) */

/* ---- saved stations (radio.txt) ---- */
static const char *radio_defaults[]={
 "Groove Salad (SomaFM)|http://ice1.somafm.com/groovesalad-128-mp3",
 "Drone Zone (SomaFM)|http://ice1.somafm.com/dronezone-128-mp3",
 "Indie Pop Rocks! (SomaFM)|http://ice1.somafm.com/indiepop-128-mp3",
 "Secret Agent (SomaFM)|http://ice1.somafm.com/secretagent-128-mp3",
 "Lush (SomaFM)|http://ice1.somafm.com/lush-128-mp3",
 "Radio Paradise|http://stream.radioparadise.com/mp3-128",
 NULL};
static void station_set(Station *s,const char *name,const char *url,const char *info) {
 snprintf(s->name,sizeof(s->name),"%s",name);snprintf(s->url,sizeof(s->url),"%s",url);snprintf(s->info,sizeof(s->info),"%s",info?info:"");
}
static int radio_write_saved(void) {
 if(!radio_file[0])return 0;
 char tmp[PATHLEN+8];snprintf(tmp,sizeof(tmp),"%s.tmp",radio_file);
 FILE *f=fopen(tmp,"w");if(!f)return 0;
 int ok=fprintf(f,"# ZOID internet radio stations: one per line, Name|URL (MP3 or AAC streams, http or https).\n# .pls and .m3u links work too. Lines starting with # are ignored.\n")>0;
 for(int i=0;i<saved_n && ok;i++)ok=fprintf(f,"%s|%s\n",saved_st[i].name,saved_st[i].url)>0;
 if(fclose(f)!=0)ok=0;
 if(!ok){sceIoRemove(tmp);return 0;}
 sceIoRemove(radio_file);return sceIoRename(tmp,radio_file)>=0;
}
static void radio_load_saved(void) {
 free(saved_st);saved_st=NULL;saved_n=0;int cap=0;
 FILE *f=fopen(radio_file,"r");
 if(!f) { /* first run: a few stations to start with */
  for(int i=0;radio_defaults[i];i++) {
   if(saved_n>=cap){cap=cap?cap*2:16;saved_st=realloc(saved_st,cap*sizeof(Station));}
   char line[512];snprintf(line,sizeof(line),"%s",radio_defaults[i]);char *bar=strchr(line,'|');*bar=0;station_set(&saved_st[saved_n++],line,bar+1,"Saved");
  }
  radio_write_saved();return;
 }
 char line[600];
 while(fgets(line,sizeof(line),f) && saved_n<200) {
  int n=strlen(line);while(n && (line[n-1]=='\n' || line[n-1]=='\r' || line[n-1]==' '))line[--n]=0;
  char *p=line;while(*p==' ' || *p=='\t')p++;
  if(!*p || *p=='#')continue;
  if(n>=3 && (unsigned char)p[0]==0xEF && (unsigned char)p[1]==0xBB && (unsigned char)p[2]==0xBF)p+=3;
  char *bar=strchr(p,'|'),*url=bar?bar+1:p,name[80];
  while(*url==' ')url++;
  if(!radio_is_url(url))continue;
  if(bar){*bar=0;int k=strlen(p);while(k && p[k-1]==' ')p[--k]=0;snprintf(name,sizeof(name),"%s",p);}
  else{int tls,port;char host[96],path[8];if(!net_parse_url(url,&tls,host,sizeof(host),&port,path,sizeof(path)))snprintf(host,sizeof(host),"%s",url);snprintf(name,sizeof(name),"%s",host);}
  if(saved_n>=cap){cap=cap?cap*2:16;Station *s=realloc(saved_st,cap*sizeof(Station));if(!s)break;saved_st=s;}
  station_set(&saved_st[saved_n++],name[0]?name:"Station",url,"Saved");
 }
 fclose(f);
}
/* path: "url#name" (a station row or the station playing) */
static void split_station(const char *path,char *url,int ucap,char *name,int ncap) {
 const char *h=strchr(path,'#');int ul=h?(int)(h-path):(int)strlen(path);if(ul>=ucap)ul=ucap-1;
 memcpy(url,path,ul);url[ul]=0;snprintf(name,ncap,"%s",h?h+1:"");
}
static int radio_is_saved(const char *url){for(int i=0;i<saved_n;i++)if(!strcmp(saved_st[i].url,url))return i;return -1;}
static void radio_show_home(void);
static void radio_save(const char *path) {
 char url[400],name[80];split_station(path,url,sizeof(url),name,sizeof(name));
 if(radio_is_saved(url)>=0){notice("Already in your stations");return;}
 Station *s=realloc(saved_st,(saved_n+1)*sizeof(Station));if(!s){notice("Out of memory");return;}
 saved_st=s;station_set(&saved_st[saved_n++],name[0]?name:"Station",url,"Saved");
 notice(radio_write_saved()?"Saved to your stations":"Could not write radio.txt");
}
static void radio_remove(const char *path) {
 char url[400],name[80];split_station(path,url,sizeof(url),name,sizeof(name));
 int k=radio_is_saved(url);if(k<0)return;
 memmove(&saved_st[k],&saved_st[k+1],(saved_n-k-1)*sizeof(Station));saved_n--;
 notice(radio_write_saved()?"Removed from your stations":"Could not write radio.txt");
 if(in_radio && radio_view==RV_HOME){int c=cursor;radio_show_home();cursor=c<count?c:count-1;}
}

/* ---- JSON (just enough for Radio Browser and LRCLIB) ---- */
static const char *js_ws(const char *p){while(*p==' ' || *p=='\n' || *p=='\r' || *p=='\t')p++;return p;}
static int js_hex4(const char *p){int v=0;for(int i=0;i<4;i++){int c=p[i],d=c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;if(d<0)return -1;v=v*16+d;}return v;}
/* p at '"': decodes the string into out (UTF-8, out may be NULL) and returns the position after it. */
static const char *js_string(const char *p,char *out,int cap) {
 int n=0;p++;
 while(*p && *p!='"') {
  unsigned int c;
  if(*p=='\\') {
   p++;
   switch(*p){case 'n':c='\n';break;case 't':c='\t';break;case 'r':c='\r';break;case 'b':c=8;break;case 'f':c=12;break;
   case 'u':{int v=js_hex4(p+1);if(v<0){c='?';break;}p+=4;c=v;
    if(c>=0xD800 && c<0xDC00 && p[1]=='\\' && p[2]=='u'){int lo=js_hex4(p+3);if(lo>=0xDC00 && lo<0xE000){c=0x10000+((c-0xD800)<<10)+(lo-0xDC00);p+=6;}}
    break;}
   case 0:c=0;break;
   default:c=(unsigned char)*p;}
   if(*p)p++;
   if(!out || !c)continue;
   if(c<0x80){if(n<cap-1)out[n++]=c;}
   else if(c<0x800){if(n<cap-2){out[n++]=0xC0|(c>>6);out[n++]=0x80|(c&63);}}
   else if(c<0x10000){if(n<cap-3){out[n++]=0xE0|(c>>12);out[n++]=0x80|((c>>6)&63);out[n++]=0x80|(c&63);}}
   else if(n<cap-4){out[n++]=0xF0|(c>>18);out[n++]=0x80|((c>>12)&63);out[n++]=0x80|((c>>6)&63);out[n++]=0x80|(c&63);}
   continue;
  }
  if(out && n<cap-1)out[n++]=*p;
  p++;
 }
 if(*p=='"')p++;
 if(out && cap>0)out[n]=0;
 return p;
}
static const char *js_skip(const char *p) {
 p=js_ws(p);
 if(*p=='"')return js_string(p,NULL,0);
 if(*p=='{' || *p=='[') {
  int depth=0;
  while(*p) {
   if(*p=='"'){p=js_string(p,NULL,0);continue;}
   if(*p=='{' || *p=='[')depth++;else if(*p=='}' || *p==']'){depth--;if(!depth)return p+1;}
   p++;
  }
  return p;
 }
 while(*p && *p!=',' && *p!='}' && *p!=']')p++;
 return p;
}
/* Inside an object: the next key (1, with *pp at its value) or the end (0, *pp after the '}'). */
static int js_key(const char **pp,char *key,int cap) {
 const char *p=js_ws(*pp);if(*p==',')p=js_ws(p+1);
 if(*p!='"'){if(*p=='}')p++;*pp=p;return 0;}
 p=js_ws(js_string(p,key,cap));if(*p==':')p++;*pp=js_ws(p);return 1;
}
/* A value as text: strings decoded, numbers/true/false/null as written. */
static const char *js_value(const char *p,char *out,int cap) {
 if(*p=='"')return js_string(p,out,cap);
 const char *s=p;p=js_skip(p);int n=(int)(p-s);if(n>=cap)n=cap-1;if(n<0)n=0;memcpy(out,s,n);out[n]=0;
 while(n && (out[n-1]==' ' || out[n-1]=='\n'))out[--n]=0;
 return p;
}

/* ---- the zoid_net worker: Radio Browser lists and LRCLIB lookups ---- */
static SceUID net_job_lock=-1,net_wake=-1;
static void job_lock(void){sceKernelWaitSema(net_job_lock,1,NULL);}
static void job_unlock(void){sceKernelSignalSema(net_job_lock,1);}
/* a directory list: 1 popular, 2 genres, 3 one genre, 4 search */
static int rjob_kind,rjob_posted,rjob_taken,rjob_done;static char rjob_arg[80];
/* a directory list: 5 countries, 6 one country's places, 7 local stations (radio_job_run) */
typedef char Name64[64];
static int rjob_error;static Station *rjob_st;static int rjob_n;static Name64 *rjob_tags,*rjob_codes;static int rjob_ntags;
/* Local stations: the place picked under Local stations (radio-region.txt beside the EBOOT).
   Once a country is set, Popular stations and each genre list stations from that country. */
static char region_cc[8],region_country[64],region_state[64],region_file[PATHLEN];
static void region_load(void) {
 region_cc[0]=region_country[0]=region_state[0]=0;
 FILE *f=fopen(region_file,"r");if(!f)return;
 char *lines[3]={region_cc,region_country,region_state};int caps[3]={sizeof(region_cc),sizeof(region_country),sizeof(region_state)};
 for(int i=0;i<3;i++){char b[96];if(!fgets(b,sizeof(b),f))break;int n=strlen(b);while(n && (b[n-1]=='\n' || b[n-1]=='\r'))b[--n]=0;snprintf(lines[i],caps[i],"%s",b);}
 fclose(f);
 if(strlen(region_cc)!=2)region_cc[0]=region_country[0]=region_state[0]=0;
}
static void region_save(void) {
 if(!region_cc[0]){sceIoRemove(region_file);return;}
 FILE *f=fopen(region_file,"w");if(!f)return;
 fprintf(f,"%s\n%s\n%s\n",region_cc,region_country,region_state);fclose(f);
}
static const char *region_label(void){return region_state[0]?region_state:region_country;}
/* a lyrics lookup */
typedef struct {char path[PATHLEN],artist[96],title[128],album[96];int duration,live;unsigned int key;} LyrJob;
static LyrJob lj_job;static int lj_posted,lj_taken,lj_done;
static int lj_result;static char *lj_text;static unsigned int lj_key;   /* result: 1 synced, 2 plain, 3 none, <0 error */
static char lj_msg[96];

static int station_ok_codec(const char *c){return !strcasecmp(c,"MP3") || !strncasecmp(c,"AAC",3);}
static int fetch_stations(const char *url,Station **out,int *n) {
 *out=NULL;*n=0;
 char *body;int st,len=http_get(url,&body,640*1024,&st);if(len<0)return len;
 int cap=128;Station *s=malloc(cap*sizeof(Station));if(!s){free(body);return NET_E_MEMORY;}
 const char *p=js_ws(body);
 if(*p=='[')p++;
 while(*p) {
  p=js_ws(p);if(*p==',' ){p++;continue;}
  if(*p!='{')break;
  p++;
  char key[32],val[400],name[80]="",url1[400]="",url2[400]="",codec[16]="",cc[8]="",tags[64]="";int kbps=0,hls=0;
  while(js_key(&p,key,sizeof(key))) {
   p=js_value(p,val,sizeof(val));
   if(!strcmp(key,"name"))snprintf(name,sizeof(name),"%s",val);
   else if(!strcmp(key,"url_resolved"))snprintf(url1,sizeof(url1),"%s",val);
   else if(!strcmp(key,"url"))snprintf(url2,sizeof(url2),"%s",val);
   else if(!strcmp(key,"codec"))snprintf(codec,sizeof(codec),"%s",val);
   else if(!strcmp(key,"bitrate"))kbps=atoi(val);
   else if(!strcmp(key,"hls"))hls=atoi(val);
   else if(!strcmp(key,"countrycode"))snprintf(cc,sizeof(cc),"%s",val);
   else if(!strcmp(key,"tags"))snprintf(tags,sizeof(tags),"%s",val);
  }
  const char *u=radio_is_url(url1)?url1:url2;
  if(!name[0] || !radio_is_url(u) || hls || !station_ok_codec(codec) || strchr(u,'#'))continue;
  int dup=0;for(int i=0;i<*n && !dup;i++)if(!strcmp(s[i].url,u))dup=1;if(dup)continue;
  int k=0;while(name[k]==' ')k++; /* some names start with spaces */
  char info[48];char *comma=strchr(tags,',');if(comma)*comma=0;
  if(kbps>0)snprintf(info,sizeof(info),"%s %dk%s%s",!strncasecmp(codec,"AAC",3)?"AAC":"MP3",kbps,cc[0]?"  ":"",cc);
  else snprintf(info,sizeof(info),"%s%s%s",!strncasecmp(codec,"AAC",3)?"AAC":"MP3",cc[0]?"  ":"",cc);
  if(*n>=cap){cap*=2;Station *q=realloc(s,cap*sizeof(Station));if(!q)break;s=q;}
  station_set(&s[(*n)++],name+k,u,info);
  if(*n>=300)break;
 }
 free(body);*out=s;return 1;
}
/* Genres, countries or one country's places: names (and ISO codes, for countries) with at least min stations */
static int fetch_names(const char *url,Name64 **out,Name64 **codes,int *n,int min,int max) {
 *out=*codes=NULL;*n=0;
 char *body;int st,len=http_get(url,&body,512*1024,&st);if(len<0)return len;
 Name64 *t=malloc(max*sizeof(Name64)),*c=malloc(max*sizeof(Name64));if(!t || !c){free(t);free(c);free(body);return NET_E_MEMORY;}
 const char *p=js_ws(body);if(*p=='[')p++;
 while(*p && *n<max) {
  p=js_ws(p);if(*p==','){p++;continue;}
  if(*p!='{')break;
  p++;char key[32],val[96],name[64]="",iso[8]="";int stations=0;
  while(js_key(&p,key,sizeof(key))){p=js_value(p,val,sizeof(val));if(!strcmp(key,"name"))snprintf(name,sizeof(name),"%s",val);else if(!strcmp(key,"stationcount"))stations=atoi(val);else if(!strcmp(key,"iso_3166_1"))snprintf(iso,sizeof(iso),"%s",val);}
  int plain=name[0]!=0;for(int i=0;name[i];i++)if((unsigned char)name[i]<32)plain=0;
  if(plain && stations>=min){snprintf(t[*n],sizeof(Name64),"%s",name);snprintf(c[*n],sizeof(Name64),"%s",iso);(*n)++;}
 }
 free(body);*out=t;*codes=c;return 1;
}

/* ---- iHeartRadio: its cities (markets), a city's lineup, search ---- */
typedef struct {char city[48],state[48];int id,stations;} IhMarket;
static IhMarket *rjob_mk;static int rjob_nmk;
/* the city picked (radio-iheart.txt beside the EBOOT: id, city, state) */
static int ih_id;static char ih_city[48],ih_state[48],ih_file[PATHLEN];
static void ih_load(void) {
 ih_id=0;ih_city[0]=ih_state[0]=0;
 FILE *f=fopen(ih_file,"r");if(!f)return;
 char b[3][96];int got=0;
 for(;got<3 && fgets(b[got],sizeof(b[got]),f);got++){int n=strlen(b[got]);while(n && (b[got][n-1]=='\n' || b[got][n-1]=='\r'))b[got][--n]=0;}
 fclose(f);
 if(got>=2 && atoi(b[0])>0){ih_id=atoi(b[0]);snprintf(ih_city,sizeof(ih_city),"%s",b[1]);snprintf(ih_state,sizeof(ih_state),"%s",got>2?b[2]:"");}
}
static void ih_save(void) {
 if(!ih_id){sceIoRemove(ih_file);return;}
 FILE *f=fopen(ih_file,"w");if(!f)return;
 fprintf(f,"%d\n%s\n%s\n",ih_id,ih_city,ih_state);fclose(f);
}
/* p at an object: the value of key (NULL if missing); stops at the object's end */
static const char *js_find(const char *p,const char *want) {
 p=js_ws(p);if(*p!='{')return NULL;p++;
 char key[32];
 while(js_key(&p,key,sizeof(key))){if(!strcmp(key,want))return p;p=js_skip(p);}
 return NULL;
}
static int fetch_ih_markets(const char *url,IhMarket **out,int *n) {
 *out=NULL;*n=0;
 char *body;int st,len=http_get(url,&body,512*1024,&st);if(len<0)return len;
 const char *p=js_find(body,"hits");
 int cap=512;IhMarket *m=malloc(cap*sizeof(IhMarket));if(!m){free(body);return NET_E_MEMORY;}
 if(p && *p=='[') {
  p++;
  while(*p && *n<cap) {
   p=js_ws(p);if(*p==','){p++;continue;}
   if(*p!='{')break;
   p++;char key[32],val[96],city[48]="",state[48]="",country[8]="";int id=0,stations=0;
   while(js_key(&p,key,sizeof(key))) {
    p=js_value(p,val,sizeof(val));
    if(!strcmp(key,"city"))snprintf(city,sizeof(city),"%s",val);
    else if(!strcmp(key,"stateName"))snprintf(state,sizeof(state),"%s",val);
    else if(!strcmp(key,"countryAbbreviation"))snprintf(country,sizeof(country),"%s",val);
    else if(!strcmp(key,"marketId"))id=atoi(val);
    else if(!strcmp(key,"stationCount"))stations=atoi(val);
   }
   if(!city[0] || !state[0] || id<=0 || stations<=0)continue;
   IhMarket *k=&m[(*n)++];snprintf(k->city,sizeof(k->city),"%s",city);
   if(strcmp(country,"US") && country[0])snprintf(k->state,sizeof(k->state),"%.36s (%s)",state,!strcmp(country,"MX")?"Mexico":country);
   else snprintf(k->state,sizeof(k->state),"%s",state);
   k->id=id;k->stations=stations;
  }
 }
 free(body);*out=m;return 1;
}
/* liveStations: each with a plain Shoutcast/Icecast stream (HLS-only ones are left out).
   order (search): the ids in the order wanted; the directory answers in its own. */
static int fetch_ih_stations(const char *url,Station **out,int *n,const int *order,int norder) {
 *out=NULL;*n=0;
 char *body;int st,len=http_get(url,&body,1024*1024,&st);if(len<0)return len;
 const char *p=js_find(body,"hits");
 int cap=300;Station *s=malloc(cap*sizeof(Station));int *ids=malloc(cap*sizeof(int));
 if(!s || !ids){free(s);free(ids);free(body);return NET_E_MEMORY;}
 if(p && *p=='[') {
  p++;
  while(*p && *n<cap) {
   p=js_ws(p);if(*p==','){p++;continue;}
   if(*p!='{')break;
   p++;char key[32],val[400],name[80]="",band[8]="",call[16]="",freq[12]="",plain[400]="",secure[400]="",city[48]="",sa[8]="";int id=0;
   while(js_key(&p,key,sizeof(key))) {
    if(!strcmp(key,"streams") && *p=='{') {
     const char *end=js_skip(p);p++;char k2[40];
     while(js_key(&p,k2,sizeof(k2))){p=js_value(p,val,sizeof(val));if(!strcmp(k2,"shoutcast_stream"))snprintf(plain,sizeof(plain),"%s",val);else if(!strcmp(k2,"secure_shoutcast_stream"))snprintf(secure,sizeof(secure),"%s",val);}
     p=end;continue;
    }
    if(!strcmp(key,"markets") && *p=='[') { /* the first (primary) market's city */
     const char *end=js_skip(p);p=js_ws(p+1);
     if(*p=='{'){p++;char k2[40];while(js_key(&p,k2,sizeof(k2))){p=js_value(p,val,sizeof(val));if(!strcmp(k2,"city"))snprintf(city,sizeof(city),"%s",val);else if(!strcmp(k2,"stateAbbreviation"))snprintf(sa,sizeof(sa),"%s",val);}}
     p=end;continue;
    }
    p=js_value(p,val,sizeof(val));
    if(!strcmp(key,"name"))snprintf(name,sizeof(name),"%s",val);
    else if(!strcmp(key,"id"))id=atoi(val);
    else if(!strcmp(key,"band"))snprintf(band,sizeof(band),"%s",val);
    else if(!strcmp(key,"callLetters"))snprintf(call,sizeof(call),"%s",val);
    else if(!strcmp(key,"freq"))snprintf(freq,sizeof(freq),"%s",val);
   }
   /* plain HTTP first: HTTPS costs the PSP memory and time, and these streams redirect fine without it */
   const char *u=radio_is_url(plain)?plain:secure;
   if(!name[0] || !radio_is_url(u) || strstr(u,".m3u8") || strchr(u,'#'))continue;
   int dup=0;for(int i=0;i<*n && !dup;i++)if(!strcmp(s[i].url,u))dup=1;if(dup)continue;
   char *dash=strchr(call,'-');if(dash)*dash=0;
   char where[64];if(order)snprintf(where,sizeof(where),"%s%s%s",city,city[0] && sa[0]?", ":"",sa);else snprintf(where,sizeof(where),"%s",call);
   char info[48];
   if(freq[0] && (!strcmp(band,"FM") || !strcmp(band,"AM")))snprintf(info,sizeof(info),"%s %s%s%.30s",freq,band,where[0]?"  ":"",where);
   else snprintf(info,sizeof(info),"Online%s%.36s",where[0] && order?"  ":"",order?where:"");
   ids[*n]=id;station_set(&s[(*n)++],name,u,info);
  }
 }
 if(order) { /* search: best match first, as the search ranked them */
  for(int i=1;i<*n;i++){
   int ri=norder;for(int k=0;k<norder;k++)if(order[k]==ids[i]){ri=k;break;}
   Station t=s[i];int ti=ids[i],j=i;
   while(j>0){int rj=norder;for(int k=0;k<norder;k++)if(order[k]==ids[j-1]){rj=k;break;}if(rj<=ri)break;s[j]=s[j-1];ids[j]=ids[j-1];j--;}
   s[j]=t;ids[j]=ti;
  }
 }
 free(ids);free(body);*out=s;return 1;
}
/* search: the matching stations' ids, then those stations */
static int ih_search(const char *q,Station **out,int *n) {
 *out=NULL;*n=0;
 char url[600];snprintf(url,sizeof(url),"%s/api/v3/search/all?keywords=%s&maxRows=40&bundle=false&station=true&artist=false&track=false&playlist=false&podcast=false",ih_api,q);
 char *body;int st,len=http_get(url,&body,256*1024,&st);if(len<0)return len;
 int ids[40],ni=0;
 const char *r=js_find(body,"results"),*p=r?js_find(r,"stations"):NULL;
 if(p && *p=='[') {
  p++;
  while(*p && ni<40) {
   p=js_ws(p);if(*p==','){p++;continue;}
   if(*p!='{')break;
   p++;char key[32],val[32];int id=0;
   while(js_key(&p,key,sizeof(key))){p=js_value(p,val,sizeof(val));if(!strcmp(key,"id"))id=atoi(val);}
   if(id>0)ids[ni++]=id;
  }
 }
 free(body);
 if(!ni){*out=malloc(sizeof(Station));return 1;}
 int o=snprintf(url,sizeof(url),"%s/api/v2/content/liveStations?limit=50&id=",ih_api);
 for(int i=0;i<ni && o<(int)sizeof(url)-12;i++)o+=snprintf(url+o,sizeof(url)-o,"%s%d",i?",":"",ids[i]);
 return fetch_ih_stations(url,out,n,ids,ni);
}
static void radio_job_run(int kind,const char *arg) {
 Station *st=NULL;int n=0,e,nt=0;Name64 *tags=NULL,*codes=NULL;IhMarket *mk=NULL;int nmk=0;
 e=net_up(wifi_config);
 if(e>0 && kind>=8 && kind<=10) {
  char url[400],q[200];url_escape(q,sizeof(q),arg);
  if(kind==8){snprintf(url,sizeof(url),"%s/api/v2/content/markets?limit=1000",ih_api);e=fetch_ih_markets(url,&mk,&nmk);}
  else if(kind==9)e=ih_search(q,&st,&n);
  else{snprintf(url,sizeof(url),"%s/api/v2/content/liveStations?limit=300&marketId=%d",ih_api,atoi(arg));e=fetch_ih_stations(url,&st,&n,NULL,0);}
 }
 else if(e>0) {
  char url[600],q[200],cc[40]="",place[220]="";url_escape(q,sizeof(q),arg);
  if(region_cc[0])snprintf(cc,sizeof(cc),"&countrycode=%s",region_cc);
  if(region_state[0]){char s[180];url_escape(s,sizeof(s),region_state);snprintf(place,sizeof(place),"&state=%s&stateExact=true",s);}
  if(kind==1 && cc[0]){snprintf(url,sizeof(url),"%s/json/stations/search?order=clickcount&reverse=true&limit=150&hidebroken=true%s",radio_api,cc);e=fetch_stations(url,&st,&n);}
  else if(kind==1){snprintf(url,sizeof(url),"%s/json/stations/topclick/150?hidebroken=true",radio_api);e=fetch_stations(url,&st,&n);}
  else if(kind==2){snprintf(url,sizeof(url),"%s/json/tags?order=stationcount&reverse=true&limit=120&hidebroken=true",radio_api);e=fetch_names(url,&tags,&codes,&nt,20,80);}
  else if(kind==3){snprintf(url,sizeof(url),"%s/json/stations/search?tag=%s&tagExact=true&order=clickcount&reverse=true&limit=150&hidebroken=true%s",radio_api,q,cc);e=fetch_stations(url,&st,&n);}
  else if(kind==5){snprintf(url,sizeof(url),"%s/json/countries?order=stationcount&reverse=true&hidebroken=true",radio_api);e=fetch_names(url,&tags,&codes,&nt,1,260);}
  else if(kind==6){snprintf(url,sizeof(url),"%s/json/states/%s/?order=stationcount&reverse=true&hidebroken=true",radio_api,q);e=fetch_names(url,&tags,&codes,&nt,1,300);}
  else if(kind==7){snprintf(url,sizeof(url),"%s/json/stations/search?order=clickcount&reverse=true&limit=300&hidebroken=true%s%s",radio_api,cc,place);e=fetch_stations(url,&st,&n);}
  else{snprintf(url,sizeof(url),"%s/json/stations/search?name=%s&order=clickcount&reverse=true&limit=150&hidebroken=true",radio_api,q);e=fetch_stations(url,&st,&n);}
 }
 job_lock();
 free(rjob_st);free(rjob_tags);free(rjob_codes);free(rjob_mk);rjob_st=st;rjob_n=n;rjob_tags=tags;rjob_codes=codes;rjob_ntags=nt;rjob_mk=mk;rjob_nmk=nmk;rjob_error=e<0?e:0;rjob_done=rjob_taken;
 job_unlock();
}

/* LRCLIB. Best match: synced lyrics within 3 s of the song's length, then any synced, then plain. */
static char *lrc_pick(const char *p,int duration,int *kind) {
 *kind=0;char *best=NULL;int best_score=-1;
 int array=*p=='[';if(array)p++;
 static char key[32],val[64];
 char *synced=malloc(96*1024),*plain=malloc(96*1024);if(!synced || !plain){free(synced);free(plain);return NULL;}
 while(*p) {
  p=js_ws(p);if(*p==','){p++;continue;}
  if(*p!='{')break;
  p++;synced[0]=plain[0]=0;int dur=0,instrumental=0;
  while(js_key(&p,key,sizeof(key))) {
   if(!strcmp(key,"syncedLyrics") && *p=='"')p=js_string(p,synced,96*1024);
   else if(!strcmp(key,"plainLyrics") && *p=='"')p=js_string(p,plain,96*1024);
   else{p=js_value(p,val,sizeof(val));if(!strcmp(key,"duration"))dur=atoi(val);else if(!strcmp(key,"instrumental"))instrumental=!strcmp(val,"true");}
  }
  int close=duration<=0 || dur<=0 || abs(dur-duration)<=3;
  int score=synced[0]?(close?4:3):plain[0]?(close?2:1):instrumental?(close?2:0):-1;
  if(score>best_score) {
   free(best);best=strdup(synced[0]?synced:plain[0]?plain:"[00:00.00](Instrumental)");best_score=score;
   *kind=synced[0] || instrumental?1:2;
  }
  if(!array || best_score==4)break;
 }
 free(synced);free(plain);
 return best;
}
static int lyrics_job_run(LyrJob *j,char **text) {
 *text=NULL;
 int e=net_up(wifi_config);if(e<0)return e;
 char a[300],t[400],al[300],url[1400];url_escape(a,sizeof(a),j->artist);url_escape(t,sizeof(t),j->title);url_escape(al,sizeof(al),j->album);
 int kind=0;
 /* the exact lookup first (needs the album and length), then a search by title and artist */
 if(j->album[0] && j->duration>0) {
  snprintf(url,sizeof(url),"%s/api/get?artist_name=%s&track_name=%s&album_name=%s&duration=%d",lyrics_api,a,t,al,j->duration);
  char *body;int st,len=http_get(url,&body,256*1024,&st);
  if(len>0){*text=lrc_pick(js_ws(body),j->duration,&kind);free(body);}
  else if(len!=NET_E_HTTP || st!=404)return len;
 }
 if(!*text) {
  snprintf(url,sizeof(url),"%s/api/search?track_name=%s&artist_name=%s",lyrics_api,t,a);
  char *body;int st,len=http_get(url,&body,640*1024,&st);
  if(len<0)return len;
  *text=lrc_pick(js_ws(body),j->duration,&kind);free(body);
 }
 if(!*text)return 3;
 /* keep it beside the song, so it works offline next time */
 if(!j->live) {
  char lrc[PATHLEN];snprintf(lrc,sizeof(lrc),"%s",j->path);char *dot=strrchr(lrc,'.'),*sl=strrchr(lrc,'/');
  if(dot && (!sl || dot>sl)) {
   strcpy(dot,".lrc");
   int fd=sceIoOpen(lrc,PSP_O_RDONLY,0);
   if(fd>=0)sceIoClose(fd);
   else {
    FILE *f=fopen(lrc,"w");
    if(f){fprintf(f,"[ar:%s]\n[ti:%s]\n",j->artist,j->title);if(j->album[0])fprintf(f,"[al:%s]\n",j->album);fprintf(f,"[re:ZOID PSP from LRCLIB]\n%s\n",*text);fclose(f);}
   }
  }
 }
 return kind;
}
static int net_thread(SceSize a,void *b) {
 (void)a;(void)b;
 while(running) {
  SceUInt wait=1000000;sceKernelWaitSema(net_wake,1,&wait);
  if(!running)break;
  job_lock();int rk=0;char rarg[80];if(rjob_posted!=rjob_taken){rjob_taken=rjob_posted;rk=rjob_kind;snprintf(rarg,sizeof(rarg),"%s",rjob_arg);}job_unlock();
  if(rk)radio_job_run(rk,rarg);
  job_lock();int lyr_go=0;static LyrJob j;if(lj_posted!=lj_taken){lj_taken=lj_posted;j=lj_job;lyr_go=1;}job_unlock();
  if(lyr_go) {
   char *text=NULL;int r=lyrics_job_run(&j,&text);
   job_lock();free(lj_text);lj_text=text;lj_result=r;lj_key=j.key;lj_done=lj_taken;job_unlock();
  }
  /* Wi-Fi costs battery: off after 3 minutes with no station playing and nothing fetched */
  if(net_status==2 && !radio_status.phase && !radio_readers_alive() && !(state && radio_is_url(active)) && net_idle_us()>180000000ULL){radio_log("Wi-Fi off after 3 idle minutes");net_down();}
 }
 return 0;
}
static void radio_post(int kind,const char *arg) {
 job_lock();rjob_kind=kind;snprintf(rjob_arg,sizeof(rjob_arg),"%s",arg?arg:"");rjob_posted++;job_unlock();
 sceKernelSignalSema(net_wake,1);
}
static int radio_waiting(void){return rjob_done!=rjob_posted;}

/* ---- the Library views ---- */
static void radio_rows_begin(void){lock(meta_lock);browse_gen++;count=entries_total=0;names_reset();scroll=0;cursor=0;in_playlist=0;in_radio=1;in_pod=0;}
static void radio_add_stations(const Station *s,int n) {
 radio_first=count;
 for(int i=0;i<n;i++){Entry *e=entry_add(s[i].name,0);if(!e)break;e->tagged=1;snprintf(e->title,sizeof(e->title),"%s",s[i].name);snprintf(e->artist,sizeof(e->artist),"%s",s[i].info);}
}
static void radio_show_home(void) {
 radio_rows_begin();radio_view=RV_HOME;snprintf(radio_title,sizeof(radio_title),RADIO_NAME);
 char local[96];if(region_cc[0])snprintf(local,sizeof(local),"Local stations: %s",region_label());else snprintf(local,sizeof(local),"Local stations");
 char ih[96];if(ih_id)snprintf(ih,sizeof(ih),"iHeartRadio: %s",ih_city);else snprintf(ih,sizeof(ih),"iHeartRadio stations");
 entry_add("Search stations",6);entry_add(local,10);entry_add(ih,30);entry_add(region_cc[0]?"Popular in your country":"Popular stations",7);entry_add("Genres",8);
 radio_add_stations(saved_st,saved_n);
 unlock(meta_lock);lib_scroll_f=lib_sel_f=0;
}
static Name64 *genre_names;static int genre_n,genre_cursor,radio_list_kind; /* genres are kept once fetched */
static void radio_show_genres(void) {
 radio_rows_begin();radio_view=RV_GENRES;radio_first=0;snprintf(radio_title,sizeof(radio_title),"Genres");
 for(int i=0;i<genre_n;i++){char cap[64];snprintf(cap,sizeof(cap),"%s",genre_names[i]);if(cap[0]>='a' && cap[0]<='z')cap[0]-=32;if(!entry_add(cap,9))break;}
 unlock(meta_lock);lib_scroll_f=lib_sel_f=0;
}
/* Countries (kept once fetched, busiest first), then the places in the one picked */
static Name64 *country_names,*country_codes;static int country_n,country_cursor;
static Name64 *place_names;static int place_n;static char place_cc[8],place_country[64];
static void radio_show_countries(void) {
 radio_rows_begin();radio_view=RV_COUNTRIES;radio_first=0;snprintf(radio_title,sizeof(radio_title),"Local stations: country");
 entry_add("Anywhere (no local place)",14);
 for(int i=0;i<country_n;i++)if(!entry_add(country_names[i],11))break;
 unlock(meta_lock);lib_scroll_f=lib_sel_f=0;
}
static void radio_show_places(void) {
 radio_rows_begin();radio_view=RV_REGIONS;radio_first=0;snprintf(radio_title,sizeof(radio_title),"%s",place_country);
 char all[96];snprintf(all,sizeof(all),"All of %s",place_country);entry_add(all,12);
 for(int i=0;i<place_n;i++)if(!entry_add(place_names[i],12))break;
 unlock(meta_lock);lib_scroll_f=lib_sel_f=0;
}
/* iHeartRadio's cities (kept once fetched): their states, A to Z (the US first), then one state's cities, busiest first */
static IhMarket *ih_mk;static int ih_mk_n,ih_state_cursor;
static Name64 *ih_states;static int ih_nstates;
static int *ih_rows;static int ih_nrows;static char ih_pick[48];
static int ih_state_cmp(const void *a,const void *b) {
 const char *x=a,*y=b;int fx=strchr(x,'(')!=NULL,fy=strchr(y,'(')!=NULL;
 return fx!=fy?fx-fy:strcasecmp(x,y);
}
static void ih_build_states(void) {
 free(ih_states);ih_states=malloc((ih_mk_n?ih_mk_n:1)*sizeof(Name64));ih_nstates=0;if(!ih_states)return;
 for(int i=0;i<ih_mk_n;i++){int dup=0;for(int k=0;k<ih_nstates && !dup;k++)if(!strcmp(ih_states[k],ih_mk[i].state))dup=1;if(!dup)snprintf(ih_states[ih_nstates++],sizeof(Name64),"%s",ih_mk[i].state);}
 qsort(ih_states,ih_nstates,sizeof(Name64),ih_state_cmp);
}
static void radio_show_ih_states(void) {
 radio_rows_begin();radio_view=RV_IHSTATES;radio_first=0;snprintf(radio_title,sizeof(radio_title),"iHeartRadio: state");
 for(int i=0;i<ih_nstates;i++)if(!entry_add(ih_states[i],33))break;
 unlock(meta_lock);lib_scroll_f=lib_sel_f=0;
}
static void radio_show_ih_cities(void) {
 free(ih_rows);ih_rows=malloc((ih_mk_n?ih_mk_n:1)*sizeof(int));ih_nrows=0;
 if(ih_rows) {
  for(int i=0;i<ih_mk_n;i++)if(!strcmp(ih_mk[i].state,ih_pick))ih_rows[ih_nrows++]=i;
  for(int i=1;i<ih_nrows;i++){int t=ih_rows[i],j=i;while(j>0 && ih_mk[ih_rows[j-1]].stations<ih_mk[t].stations){ih_rows[j]=ih_rows[j-1];j--;}ih_rows[j]=t;}
 }
 radio_rows_begin();radio_view=RV_IHCITIES;radio_first=0;snprintf(radio_title,sizeof(radio_title),"%s",ih_pick);
 for(int i=0;i<ih_nrows;i++){Entry *e=entry_add(ih_mk[ih_rows[i]].city,34);if(!e)break;}
 unlock(meta_lock);lib_scroll_f=lib_sel_f=0;
}
static void radio_open_list(int kind,const char *arg,const char *title,int from) {
 radio_list_kind=kind;
 radio_rows_begin();radio_view=kind==2?RV_GENRES:kind==5?RV_COUNTRIES:kind==6?RV_REGIONS:kind==8?RV_IHSTATES:RV_LIST;radio_from=from;radio_first=0;
 snprintf(radio_title,sizeof(radio_title),"%s",title);
 unlock(meta_lock);lib_scroll_f=lib_sel_f=0;
 radio_post(kind,arg);
}
static void radio_search_done(const char *text) {
 if(!text || !text[0])return;
 char t[96];snprintf(t,sizeof(t),"\"%s\"",text);radio_open_list(4,text,t,RV_HOME);
}
static void ih_search_done(const char *text) {
 if(!text || !text[0])return;
 char t[96];snprintf(t,sizeof(t),"iHeart: \"%s\"",text);radio_open_list(9,text,t,RV_HOME);
}
static void ih_open_lineup(void){char t[96],id[16];snprintf(t,sizeof(t),"iHeart: %s",ih_city);snprintf(id,sizeof(id),"%d",ih_id);radio_open_list(10,id,t,RV_HOME);}
/* Called every frame: shows a finished list. */
static void radio_poll(void) {
 if(!in_radio || radio_view==RV_HOME || radio_waiting() || !rjob_done)return;
 static int shown;if(shown==rjob_done)return;
 job_lock();
 if(rjob_done==rjob_posted) {
  shown=rjob_done;
  if(!rjob_error) { /* name lists: kept, then shown */
   if(radio_view==RV_GENRES){free(genre_names);genre_names=rjob_tags;genre_n=rjob_ntags;rjob_tags=NULL;job_unlock();radio_show_genres();return;}
   if(radio_view==RV_COUNTRIES){free(country_names);free(country_codes);country_names=rjob_tags;country_codes=rjob_codes;country_n=rjob_ntags;rjob_tags=rjob_codes=NULL;job_unlock();radio_show_countries();return;}
   if(radio_view==RV_REGIONS){free(place_names);place_names=rjob_tags;place_n=rjob_ntags;rjob_tags=NULL;job_unlock();radio_show_places();return;}
   if(radio_view==RV_IHSTATES){free(ih_mk);ih_mk=rjob_mk;ih_mk_n=rjob_nmk;rjob_mk=NULL;job_unlock();ih_build_states();radio_show_ih_states();return;}
  }
  radio_rows_begin();
  if(radio_view==RV_LIST) {
   free(list_st);list_st=rjob_st;list_n=rjob_n;rjob_st=NULL;
   if(radio_list_kind==7 && !rjob_error)entry_add("Change local place",13); /* an empty or failed list still shows its message */
   if(radio_list_kind==10 && !rjob_error){entry_add("Search iHeartRadio",31);entry_add("Change city",32);}
   radio_add_stations(list_st,list_n);
  }
  unlock(meta_lock);
 }
 job_unlock();
}
static int radio_entry_path(int i,char *out,int cap) {
 const Station *s=radio_view==RV_HOME?saved_st:list_st;int n=radio_view==RV_HOME?saved_n:list_n,k=i-radio_first;
 if(entries[i].folder || k<0 || k>=n){int r=snprintf(out,cap,"radio:%d",i);return r>0 && r<cap;}
 int r=snprintf(out,cap,"%s#%s",s[k].url,s[k].name);return r>0 && r<cap;
}
static void radio_enter(void){radio_home_cursor=cursor;radio_show_home();}
/* X on a radio row; 0 for a station (which then plays like a song) */
static int radio_activate(int i) {
 Entry *e=&entries[i];
 if(e->folder==6){osk_open("Search stations","",radio_search_done);return 1;}
 if(e->folder==7){char t[96];if(region_cc[0])snprintf(t,sizeof(t),"Popular in %s",region_country);else snprintf(t,sizeof(t),"Popular stations");radio_open_list(1,"",t,RV_HOME);return 1;}
 if(e->folder==8){if(genre_n)radio_show_genres();else radio_open_list(2,"","Genres",RV_HOME);return 1;}
 if(e->folder==9){genre_cursor=i;char t[96];if(region_cc[0])snprintf(t,sizeof(t),"%s (%s)",e->name,region_cc);else snprintf(t,sizeof(t),"%s",e->name);char arg[64];snprintf(arg,sizeof(arg),"%s",e->name);for(char *c=arg;*c;c++)*c=tolower((unsigned char)*c);radio_open_list(3,arg,t,RV_GENRES);return 1;}
 /* Local stations: straight to the list once a place is picked, else the countries first */
 if(e->folder==10 || e->folder==13) {
  if(e->folder==10 && region_cc[0]){char t[96];snprintf(t,sizeof(t),"Local: %s",region_label());radio_open_list(7,"",t,RV_HOME);return 1;}
  if(country_n)radio_show_countries();else radio_open_list(5,"","Local stations: country",RV_HOME);
  return 1;
 }
 if(e->folder==14){region_cc[0]=region_country[0]=region_state[0]=0;region_save();radio_show_home();cursor=1;notice("Stations from everywhere");return 1;}
 /* iHeartRadio: straight to the city's lineup once one is picked, else the states first */
 if(e->folder==30 && ih_id){ih_open_lineup();return 1;}
 if(e->folder==30 || e->folder==32){if(ih_mk_n)radio_show_ih_states();else radio_open_list(8,"","iHeartRadio: state",RV_HOME);return 1;}
 if(e->folder==31){osk_open("Search iHeartRadio","",ih_search_done);return 1;}
 if(e->folder==33){if(i<0 || i>=ih_nstates)return 1;ih_state_cursor=i;snprintf(ih_pick,sizeof(ih_pick),"%s",ih_states[i]);radio_show_ih_cities();return 1;}
 if(e->folder==34) {
  if(i<0 || i>=ih_nrows)return 1;
  const IhMarket *m=&ih_mk[ih_rows[i]];ih_id=m->id;snprintf(ih_city,sizeof(ih_city),"%s",m->city);snprintf(ih_state,sizeof(ih_state),"%s",m->state);ih_save();
  ih_open_lineup();return 1;
 }
 if(e->folder==11) {
  int k=i-1;if(k<0 || k>=country_n)return 1;
  country_cursor=i;snprintf(place_cc,sizeof(place_cc),"%s",country_codes[k]);snprintf(place_country,sizeof(place_country),"%s",country_names[k]);
  radio_open_list(6,place_country,place_country,RV_COUNTRIES);return 1;
 }
 if(e->folder==12) { /* row 0 is the whole country */
  snprintf(region_cc,sizeof(region_cc),"%s",place_cc);snprintf(region_country,sizeof(region_country),"%s",place_country);
  if(i>0 && i-1<place_n)snprintf(region_state,sizeof(region_state),"%s",place_names[i-1]);else region_state[0]=0;
  region_save();genre_cursor=0;
  char t[96];snprintf(t,sizeof(t),"Local: %s",region_label());radio_open_list(7,"",t,RV_HOME);return 1;
 }
 return 0;
}
static void radio_back(void) {
 if(radio_view==RV_LIST && radio_from==RV_GENRES && genre_n){radio_show_genres();cursor=genre_cursor<count?genre_cursor:0;return;}
 if(radio_view==RV_REGIONS && country_n){radio_show_countries();cursor=country_cursor<count?country_cursor:0;return;}
 if(radio_view==RV_IHCITIES && ih_nstates){radio_show_ih_states();cursor=ih_state_cursor<count?ih_state_cursor:0;return;}
 /* back to Internet Radio, on the row this came from: Search 0, Local 1, iHeartRadio 2, Popular 3, Genres 4 */
 if(radio_view!=RV_HOME){int c=radio_view==RV_GENRES?4:radio_view==RV_COUNTRIES || radio_view==RV_REGIONS || (radio_view==RV_LIST && radio_list_kind==7)?1:
   radio_view==RV_IHSTATES || radio_view==RV_IHCITIES || radio_list_kind==9 || radio_list_kind==10?2:radio_list_kind==4?0:3;radio_show_home();cursor=c;return;}
 go_home(HOME_RADIO); /* the top of Internet Radio: Home, on its tile */
}
/* Library header text and the message an empty or loading list shows */
static void radio_info(char *out,int cap) {
 if(radio_waiting())snprintf(out,cap,"%s",net_status==1?"Connecting to Wi-Fi...":"Loading...");
 else{int n=count-radio_first;if(radio_view==RV_GENRES)snprintf(out,cap,"%d genres",count);else if(radio_view==RV_COUNTRIES)snprintf(out,cap,"%d countries",count-1);else if(radio_view==RV_REGIONS)snprintf(out,cap,"%d places",count-1);else if(radio_view==RV_IHSTATES)snprintf(out,cap,"%d states",count);else if(radio_view==RV_IHCITIES)snprintf(out,cap,"%d cit%s",count,count==1?"y":"ies");else snprintf(out,cap,"%d station%s",n,n==1?"":"s");}
}
static int radio_empty(char *l1,int c1,char *l2,int c2) {
 if(!in_radio || count)return 0;
 if(radio_waiting()){snprintf(l1,c1,"%s",net_status==1?"Connecting to Wi-Fi...":"Loading stations...");snprintf(l2,c2,"%s",net_status==2 && net_connected_name[0]?net_connected_name:radio_list_kind>=8 && radio_list_kind<=10?"iHeartRadio directory":"Radio Browser directory");return 1;}
 if(rjob_error){snprintf(l1,c1,"%s",net_error_text(rjob_error));snprintf(l2,c2,"%s",rjob_error==NET_E_SWITCH || rjob_error==NET_E_NOCONFIG || rjob_error==NET_E_JOIN?"Check Wi-Fi, then press Circle and try again":"Press Circle and try again");return 1;}
 snprintf(l1,c1,"No stations found");snprintf(l2,c2,"Only MP3 and AAC stations are listed");return 1;
}
static int row_icon(const Entry *e) {
 switch(e->folder){case 4:return IC_SHUFFLE;case 2:return IC_DOTS;case 5:case 10:case 30:return IC_RADIO;case 6:case 20:case 31:return IC_SEARCH;
  case 21:return IC_WAVE;case 23:case 29:return IC_REPEAT;case 24:return IC_DOWNLOAD;case 25:case 26:return IC_MIC;case 28:return IC_PLUS;}
 return IC_FOLDER;
}
/* rows that do something rather than open a list: no arrow */
static int row_is_action(int f){return f==4 || f==23 || f==28 || f==29;}
/* Hold X on a station row, or on Now Playing while a station plays */
static void radio_item_menu(void) {
 char label[256];
 if(screen==SCR_NOW){snprintf(menu_target,sizeof(menu_target),"%s",active);snprintf(label,sizeof(label),"%s",np_title[0]?np_title:"This station");}
 else {
  Entry *e=&entries[cursor];
  if(e->folder)return;
  entry_label(e,label,sizeof(label));
  if(!entry_path(cursor,menu_target,sizeof(menu_target)))return;
 }
 menu_target_kind=0;menu_target_index=cursor;
 char url[400],name[80];split_station(menu_target,url,sizeof(url),name,sizeof(name));
 menu_begin(label);snprintf(menu_item_name,sizeof(menu_item_name),"%s",label);
 if(screen==SCR_NOW)menu_add("Lyrics",A_LYRICS);else menu_add("Play",A_PLAY);
 if(radio_is_saved(url)>=0)menu_add("Remove from my stations",A_RADIO_REMOVE);else menu_add("Save to my stations",A_RADIO_SAVE);
 menu_add("Cancel",A_CANCEL);
}

/* ---- Now Playing: titles from the station ---- */

static const char *radio_phase_text(void) {
 switch(radio_status.phase){case 1:return net_status==1?"Connecting to Wi-Fi...":"Connecting...";case 2:return "Buffering...";case 4:return "Reconnecting...";}
 return state==1?"Connecting...":NULL;
}
/* "Artist - Title" from the station into the title/artist/album lines */
static void radio_np_tick(void) {
 if(!row_live())return;
 /* paused for 2 minutes: the station stops (it would keep downloading); X starts it again, live */
 {static unsigned long long paused_at;
  if(state==2 && paused){if(!paused_at)paused_at=now_ms;else if(now_ms-paused_at>120000){paused_at=0;request(NULL);}}
  else paused_at=0;}
 unsigned int gen;char t[192];radio_get_title(t,sizeof(t),&gen);
 if(gen==np_radio_gen)return;
 np_radio_gen=gen;
 char url[400],name[80];split_station(active,url,sizeof(url),name,sizeof(name));
 if(!name[0])snprintf(name,sizeof(name),"%s",radio_status.name[0]?radio_status.name:"Internet radio");
 char *dash=strstr(t," - ");
 if(dash && dash>t){*dash=0;snprintf(np_artist,sizeof(np_artist),"%s",t);snprintf(np_title,sizeof(np_title),"%s",dash+3);}
 else if(t[0]){snprintf(np_title,sizeof(np_title),"%s",t);snprintf(np_artist,sizeof(np_artist),"%s",name);}
 else{snprintf(np_title,sizeof(np_title),"%s",name);snprintf(np_artist,sizeof(np_artist),"%s",radio_status.genre[0]?radio_status.genre:"Internet radio");}
 char info[48]="";if(radio_status.kbps>0)snprintf(info,sizeof(info),"  -  %d kbps %s",radio_status.kbps,radio_status.codec==2?"AAC":"MP3");
 snprintf(np_album,sizeof(np_album),"%s%s",name,info);
 np_key++;
 /* a new song on the station: its lyrics are looked up again */
 lyr.n=0;lyr_state=3;lyr_gen++;
}

/* ---- online lyrics: when to ask, and showing the answer ---- */
static unsigned int lj_tried=~0u,lj_waiting_key=~0u;
static unsigned int lyr_song_key(void){return row_live()?0x80000000u|np_radio_gen:np_req;}
static void lyrics_online_tick(void) {
 /* an answer for the song still playing */
 if(lj_done && lj_waiting_key!=~0u) {
  job_lock();
  if(lj_done==lj_posted && lj_key==lj_waiting_key) {
   lj_waiting_key=~0u;
   if(lj_key==lyr_song_key() && lyr_state==4) {
    if(lj_result==1 || lj_result==2){lyrics_parse(lj_text,&lyr);lyr_state=lyr.n?2:5;}
    else if(lj_result==3){lyr_state=5;snprintf(lj_msg,sizeof(lj_msg),"LRCLIB has no lyrics for this song");}
    else{lyr_state=6;snprintf(lj_msg,sizeof(lj_msg),"%s",net_error_text(lj_result));}
    lyr_gen++;
   }
  }
  job_unlock();
 }
 if(lyr_state==3 && pod_is_episode(active)){lyr_state=5;snprintf(lj_msg,sizeof(lj_msg),"Podcast episodes have no lyrics");lyr_gen++;return;}
 if(!online_lyrics || !active[0] || video_active || lyr_state!=3 || np_loading)return;
 if(online_lyrics==1 && screen!=SCR_LYRICS)return;
 unsigned int key=lyr_song_key();if(key==lj_tried)return;
 lj_tried=key;
 LyrJob j;memset(&j,0,sizeof(j));
 snprintf(j.path,sizeof(j.path),"%s",active);j.live=row_live();j.key=key;
 if(j.live) {
  char t[192];radio_get_title(t,sizeof(t),NULL);char *dash=strstr(t," - ");
  if(!dash){if(screen==SCR_LYRICS){lyr_state=5;snprintf(lj_msg,sizeof(lj_msg),"The station has not named the song");lyr_gen++;}return;}
  *dash=0;snprintf(j.artist,sizeof(j.artist),"%s",t);snprintf(j.title,sizeof(j.title),"%s",dash+3);
 }else {
  int known=np_artist[0] && strcmp(np_artist,"Unknown artist") && strncmp(np_artist,"Loading",7);
  snprintf(j.title,sizeof(j.title),"%s",np_title);
  if(known)snprintf(j.artist,sizeof(j.artist),"%s",np_artist);
  else{char *dash=strstr(j.title," - ");if(dash){*dash=0;snprintf(j.artist,sizeof(j.artist),"%s",j.title);memmove(j.title,dash+3,strlen(dash+3)+1);}} /* "Artist - Title.mp3" */
  if(strcmp(np_album,np_folder) && strncmp(np_album,"Loading",7))snprintf(j.album,sizeof(j.album),"%s",np_album);
  j.duration=duration;
 }
 if(!j.artist[0] || !j.title[0]){lyr_state=5;snprintf(lj_msg,sizeof(lj_msg),"Lyrics need the song's artist and title");lyr_gen++;return;}
 job_lock();lj_job=j;lj_posted++;job_unlock();sceKernelSignalSema(net_wake,1);
 lj_waiting_key=key;lyr_state=4;lyr_gen++;
}
/* The lyrics clock: for a station, time since the current title began */
static int radio_song_ms(void){int ms=(int)(radio_status.decoded_ms-radio_status.title_ms)-180;return ms<0?0:ms;}

static void radio_ui_init(const char *dir_prefix,int prefix) {
 snprintf(radio_file,sizeof(radio_file),"%.*sradio.txt",prefix,dir_prefix);
 snprintf(region_file,sizeof(region_file),"%.*sradio-region.txt",prefix,dir_prefix);region_load();
 snprintf(ih_file,sizeof(ih_file),"%.*sradio-iheart.txt",prefix,dir_prefix);ih_load();
 radio_load_saved();
 net_setup();
 net_job_lock=sceKernelCreateSema("zoid_netjob",0,1,1,NULL);net_wake=sceKernelCreateSema("zoid_netwake",0,0,1,NULL);
 radio_setup(live_wanted,wifi_config);
 {char lp[PATHLEN];snprintf(lp,sizeof(lp),"%.*sstream-log.txt",prefix,dir_prefix);radio_set_log(lp);radio_log("ZOID started");}
 SceUID th=sceKernelCreateThread("zoid_net",net_thread,0x30,0x8000,0,NULL);if(th>=0)sceKernelStartThread(th,0,NULL);
}
