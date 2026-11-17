/* Podcasts: the Podcasts tile on the Home screen.
   Browse Apple's podcast directory (the iTunes Search API: search, top charts, categories; free,
   no account), see a show's episodes from its RSS feed, subscribe, load new episodes, and save
   episodes for offline listening.
   Subscriptions live in PODCASTS on the Memory Stick: a folder per show holding show.txt (the feed,
   its episodes, what was played and where each was left), cover.jpg and the saved episodes.
   An episode that is not saved streams on demand (radio.c: Range requests, so it seeks and it ends).
   MP3 episodes stream; M4A ones play once saved. Playing an episode again carries on where it was left.
   Feeds are read as they arrive on the zoid_pod thread, keeping the newest 200 episodes, so a long
   feed never has to fit in memory. Episodes save one at a time on the zoid_dl thread, into a .part
   file that carries on where it stopped if Wi-Fi drops. */
#include <psprtc.h>
#ifdef ZOID_NET_TEST
static const char *pod_api="http://127.0.0.1:8088"; /* net-test-server.cs */
#else
static const char *pod_api="https://itunes.apple.com";
#endif
#define POD_NAME "Podcasts"
#define POD_EP_MAX 200   /* episodes kept from a feed */
#define POD_SHOW_MAX 400 /* in show.txt: the feed's newest plus older ones still saved */
#define POD_SUB_MAX 64
#define POD_DL_MAX 48
#define POD_E_WRITE -1201
enum {PV_HOME=1,PV_RESULTS,PV_CATS,PV_SHOW,PV_SAVED};
/* rows (Entry.folder): 20 Search, 21 Top podcasts, 22 Categories, 23 Load new episodes (every show), 24 Saved episodes,
   25 a subscribed show, 26 a directory result, 27 a category, 28 Subscribe, 29 Load new episodes (this show); 0 an episode */
typedef struct {unsigned int key;int played,isnew,fresh,dur,bytes,pos;char date[12],ext[6],file[96],url[400],title[128],dir[64];} PodEp;
typedef struct {char feed[400],title[96],author[64],art[300];PodEp *eps;int n;} PodShow;
typedef struct {char title[96],author[64],feed[400],art[300],genre[40];unsigned int id;} PodResult;
typedef struct {char dir[64],title[96],author[64],feed[400];int newn,saved,total;} PodSub;
static int pod_view,pod_first,pod_from,pod_res_from,pod_res_cursor,pod_res_home_row,pod_cat_cursor,pod_home_cursor,pod_auto_sub,pod_err;
static char pod_title[96];
static PodEp *pod_eps;static int pod_n;            /* the episodes on screen (PV_SHOW, PV_SAVED) */
static PodShow pod_cur;static char pod_cur_dir[64]; /* the show on screen; pod_cur_dir stays "" until subscribed */
static PodResult *pod_res;static int pod_res_n;static char pod_res_title[96];
static PodSub pod_subs[POD_SUB_MAX];static int pod_sub_n;
static int pod_sub_count(void){return pod_sub_n;}
static SceUID pod_lock=-1,pod_wake=-1,dl_wake=-1;
static void plock(void){sceKernelWaitSema(pod_lock,1,NULL);}
static void punlock(void){sceKernelSignalSema(pod_lock,1);}
static const struct {const char *name;int id;} pod_cats[]={{"Arts",1301},{"Business",1321},{"Comedy",1303},{"Education",1304},{"Fiction",1483},
 {"Health & Fitness",1512},{"History",1487},{"Kids & Family",1305},{"Leisure",1502},{"Music",1310},{"News",1489},{"Religion & Spirituality",1314},
 {"Science",1533},{"Society & Culture",1324},{"Sports",1545},{"Technology",1318},{"True Crime",1488},{"TV & Film",1309}};
#define POD_CAT_N ((int)(sizeof(pod_cats)/sizeof(pod_cats[0])))

/* ---- names and files ---- */
static void utf8_cut(char *s) { /* drops a letter cut short at the end */
 int n=strlen(s),i=n;while(i>0 && ((unsigned char)s[i-1]&0xC0)==0x80)i--;
 if(i>0){unsigned char c=s[i-1];int need=c>=0xF0?4:c>=0xE0?3:c>=0xC0?2:1;if(n-(i-1)<need)s[i-1]=0;}
}
static void pod_safe(char *out,int cap,const char *s) { /* a file or folder name */
 int n=0;
 for(const unsigned char *p=(const unsigned char *)s;*p && n<cap-1;p++) {
  unsigned char c=*p;if(c<32 || strchr("/\\:*?\"<>|#",c))c=' ';
  if(c==' ' && (!n || out[n-1]==' '))continue;
  out[n++]=c;
 }
 out[n]=0;utf8_cut(out);n=strlen(out);
 while(n && (out[n-1]==' ' || out[n-1]=='.'))out[--n]=0;
 if(!n)snprintf(out,cap,"Podcast");
}
static void pod_clean(char *s){for(;*s;s++)if(*s=='\t' || *s=='\n' || *s=='\r')*s=' ';}
static void show_file(const char *dir,char *out,int cap){snprintf(out,cap,"%s/%s/show.txt",pod_root,dir);}
static char *pod_field(char **p){char *s=*p;if(!s)return "";char *t=strchr(s,'\t');if(t){*t=0;*p=t+1;}else *p=NULL;return s;}
/* show.txt: a header, then one line per episode. Callers hold pod_lock. */
static int show_load(const char *dir,PodShow *s,int with_eps) {
 memset(s,0,sizeof(*s));
 char path[PATHLEN];show_file(dir,path,sizeof(path));
 FILE *f=fopen(path,"r");if(!f)return 0;
 char line[1400];int cap=0;
 int ok=fgets(line,sizeof(line),f) && !strncmp(line,"ZOID_PODCAST_1",14);
 while(ok && fgets(line,sizeof(line),f)) {
  int n=strlen(line);while(n && (line[n-1]=='\n' || line[n-1]=='\r'))line[--n]=0;
  char *p=line,*k=pod_field(&p);
  if(!strcmp(k,"feed"))snprintf(s->feed,sizeof(s->feed),"%s",pod_field(&p));
  else if(!strcmp(k,"title"))snprintf(s->title,sizeof(s->title),"%s",pod_field(&p));
  else if(!strcmp(k,"author"))snprintf(s->author,sizeof(s->author),"%s",pod_field(&p));
  else if(!strcmp(k,"art"))snprintf(s->art,sizeof(s->art),"%s",pod_field(&p));
  else if(!strcmp(k,"E") && with_eps && s->n<POD_SHOW_MAX) {
   if(s->n>=cap){int c=cap?cap*2:64;PodEp *q=realloc(s->eps,c*sizeof(PodEp));if(!q)break;s->eps=q;cap=c;}
   PodEp *e=&s->eps[s->n];memset(e,0,sizeof(*e));
   char *fl=pod_field(&p);e->played=strchr(fl,'p')!=NULL;e->isnew=strchr(fl,'n')!=NULL;
   e->key=strtoul(pod_field(&p),NULL,16);
   snprintf(e->date,sizeof(e->date),"%s",pod_field(&p));
   e->dur=atoi(pod_field(&p));e->bytes=atoi(pod_field(&p));e->pos=atoi(pod_field(&p));
   snprintf(e->ext,sizeof(e->ext),"%s",pod_field(&p));snprintf(e->file,sizeof(e->file),"%s",pod_field(&p));
   snprintf(e->url,sizeof(e->url),"%s",pod_field(&p));snprintf(e->title,sizeof(e->title),"%s",pod_field(&p));
   snprintf(e->dir,sizeof(e->dir),"%s",dir);
   if(e->url[0])s->n++;
  }
 }
 fclose(f);
 if(!ok){free(s->eps);s->eps=NULL;s->n=0;}
 return ok;
}
static int show_save(const char *dir,const PodShow *s) {
 char path[PATHLEN],tmp[PATHLEN+8];show_file(dir,path,sizeof(path));snprintf(tmp,sizeof(tmp),"%s.tmp",path);
 FILE *f=fopen(tmp,"w");if(!f)return 0;
 int ok=fprintf(f,"ZOID_PODCAST_1\nfeed\t%s\ntitle\t%s\nauthor\t%s\nart\t%s\n",s->feed,s->title,s->author,s->art)>0;
 for(int i=0;i<s->n && ok;i++) {
  const PodEp *e=&s->eps[i];
  ok=fprintf(f,"E\t%s%s-\t%08x\t%s\t%d\t%d\t%d\t%s\t%s\t%s\t%s\n",e->played?"p":"",e->isnew?"n":"",e->key,e->date,e->dur,e->bytes,e->pos,e->ext,e->file,e->url,e->title)>0;
 }
 if(fclose(f)!=0)ok=0;
 if(!ok){sceIoRemove(tmp);return 0;}
 sceIoRemove(path);return sceIoRename(tmp,path)>=0;
}
/* the shows in PODCASTS (folders with a show.txt) */
static int pod_list_dirs(char (*out)[64],int max) {
 int n=0,d=sceIoDopen(pod_root);if(d<0)return 0;
 SceIoDirent e;memset(&e,0,sizeof(e));
 while(n<max && sceIoDread(d,&e)>0) {
  if(e.d_name[0]!='.' && FIO_S_ISDIR(e.d_stat.st_mode) && strlen(e.d_name)<64){char p[PATHLEN];show_file(e.d_name,p,sizeof(p));if(file_exists(p))snprintf(out[n++],64,"%s",e.d_name);}
  memset(&e,0,sizeof(e));
 }
 sceIoDclose(d);return n;
}
static void pod_ext(const char *type,const char *url,char *ext) {
 char t[48];int i=0;for(;type[i] && i<47;i++)t[i]=tolower((unsigned char)type[i]);t[i]=0;
 if(strstr(t,"mpeg") || strstr(t,"mp3")){strcpy(ext,"mp3");return;}
 if(strstr(t,"m4a") || strstr(t,"audio/mp4") || strstr(t,"m4b")){strcpy(ext,"m4a");return;}
 if(strstr(t,"video/")){strcpy(ext,"mp4");return;}
 char u[400];snprintf(u,sizeof(u),"%s",url);char *q=strchr(u,'?');if(q)*q=0;
 const char *dot=strrchr(u,'.'),*sl=strrchr(u,'/');
 if(dot && (!sl || dot>sl)) {
  dot++;
  if(!strcasecmp(dot,"m4a") || !strcasecmp(dot,"m4b") || !strcasecmp(dot,"mp4")){snprintf(ext,6,"%s",!strcasecmp(dot,"mp4")?"mp4":"m4a");return;}
  if(!strcasecmp(dot,"m4v")){strcpy(ext,"mp4");return;}
  if(!strcasecmp(dot,"aac") || !strcasecmp(dot,"ogg") || !strcasecmp(dot,"opus")){snprintf(ext,6,"%s",!strcasecmp(dot,"aac")?"aac":"ogg");return;}
 }
 strcpy(ext,"mp3");
}
static int pod_playable(const char *ext){return !strcmp(ext,"mp3") || !strcmp(ext,"m4a") || !strcmp(ext,"mp4");}
static int pod_streamable(const char *ext){return !strcmp(ext,"mp3");}

/* ---- RSS (just enough for podcast feeds) ---- */
static const char *x_find(const char *s,const char *e,const char *name) { /* "<name" then a space, '>' or '/' */
 int n=strlen(name);
 for(const char *p=s;p<e;p++)if(*p=='<' && !strncmp(p+1,name,n)){char c=p[1+n];if(c==' ' || c=='>' || c=='/' || c=='\t' || c=='\r' || c=='\n')return p;}
 return NULL;
}
static int x_put(char *out,int n,int cap,unsigned int c) {
 if(c<0x80){if(n<cap-1)out[n++]=c;}
 else if(c<0x800){if(n<cap-2){out[n++]=0xC0|(c>>6);out[n++]=0x80|(c&63);}}
 else if(c<0x10000){if(n<cap-3){out[n++]=0xE0|(c>>12);out[n++]=0x80|((c>>6)&63);out[n++]=0x80|(c&63);}}
 else if(n<cap-4){out[n++]=0xF0|(c>>18);out[n++]=0x80|((c>>12)&63);out[n++]=0x80|((c>>6)&63);out[n++]=0x80|(c&63);}
 return n;
}
/* text between s and e: CDATA as written, entities decoded, tags dropped, white space collapsed */
static void x_decode(const char *s,const char *e,char *out,int cap) {
 int n=0,space=0;
 while(s<e && n<cap-1) {
  if(e-s>=9 && !strncmp(s,"<![CDATA[",9)) {
   s+=9;const char *end=strstr(s,"]]>");if(!end || end>e)end=e;
   for(;s<end && n<cap-1;s++){unsigned char c=*s;if(c<=' '){space=n>0;continue;}if(space){out[n++]=' ';space=0;if(n>=cap-1)break;}out[n++]=c;}
   s=end<e?end+3:e;continue;
  }
  if(*s=='<'){while(s<e && *s!='>')s++;if(s<e)s++;space=n>0;continue;}
  unsigned int c=(unsigned char)*s;int cp=0;
  if(c=='&') {
   const char *semi=s+1;while(semi<e && semi-s<10 && *semi!=';')semi++;
   if(semi<e && *semi==';') {
    int len=semi-s-1;const char *nm=s+1;cp=1;
    if(len==3 && !strncmp(nm,"amp",3))c='&';else if(len==2 && !strncmp(nm,"lt",2))c='<';else if(len==2 && !strncmp(nm,"gt",2))c='>';
    else if(len==4 && !strncmp(nm,"quot",4))c='"';else if(len==4 && !strncmp(nm,"apos",4))c='\'';else if(len==4 && !strncmp(nm,"nbsp",4))c=' ';
    else if(nm[0]=='#')c=(nm[1]=='x' || nm[1]=='X')?strtoul(nm+2,NULL,16):strtoul(nm+1,NULL,10);
    else cp=0;
    if(cp)s=semi+1;else{c='&';s++;}
   }else s++;
  }else s++;
  if(c<=' ' || c==0xA0){if(c)space=n>0;continue;}
  if(space){out[n++]=' ';space=0;if(n>=cap-1)break;}
  if(cp)n=x_put(out,n,cap,c);else out[n++]=c;
 }
 out[n]=0;utf8_cut(out);
}
static int x_text(const char *s,const char *e,const char *name,char *out,int cap) {
 out[0]=0;const char *p=x_find(s,e,name);if(!p)return 0;
 const char *gt=p;while(gt<e && *gt!='>')gt++;if(gt>=e || gt[-1]=='/')return 0;
 char close[48];snprintf(close,sizeof(close),"</%s>",name);
 const char *c=strstr(gt+1,close);if(!c || c>e)return 0;
 x_decode(gt+1,c,out,cap);return out[0]!=0;
}
static int x_attr(const char *s,const char *e,const char *tag,const char *attr,char *out,int cap) {
 out[0]=0;const char *p=x_find(s,e,tag);if(!p)return 0;
 const char *gt=p;while(gt<e && *gt!='>')gt++;
 int n=strlen(attr);
 for(const char *a=p+1;a+n<gt;a++) {
  if((a[-1]==' ' || a[-1]=='\t' || a[-1]=='\n' || a[-1]=='\r') && !strncmp(a,attr,n)) {
   const char *v=a+n;while(v<gt && *v==' ')v++;if(*v!='=')continue;v++;while(v<gt && *v==' ')v++;
   char q=*v;if(q!='"' && q!='\'')continue;
   const char *end=v+1;while(end<gt && *end!=q)end++;
   x_decode(v+1,end,out,cap);return 1;
  }
 }
 return 0;
}
static void x_date(const char *s,char *out,int cap) { /* "Tue, 03 Nov 2026 08:00:00 +0000" or "2026-11-03T..." to "2026-11-03" */
 static const char *mon="janfebmaraprmayjunjulaugsepoctnovdec";
 out[0]=0;while(*s==' ')s++;
 if(isdigit((unsigned char)s[0]) && s[4]=='-'){snprintf(out,cap,"%.10s",s);return;}
 const char *c=strchr(s,',');if(c)s=c+1;
 int d,y;char m[4];if(sscanf(s," %d %3s %d",&d,m,&y)!=3)return;
 for(int i=0;i<3;i++)m[i]=tolower((unsigned char)m[i]);
 const char *f=strstr(mon,m);if(!f || (f-mon)%3)return;
 if(y<100)y+=2000;
 snprintf(out,cap,"%04d-%02d-%02d",y,(int)(f-mon)/3+1,d);
}
static int x_duration(const char *s) { /* "1:02:03", "62:03" or "3723" */
 int v[3]={0},k=0;
 for(const char *p=s;*p && k<3;) {while(*p==' ')p++;if(!isdigit((unsigned char)*p))break;v[k++]=atoi(p);while(isdigit((unsigned char)*p))p++;if(*p!=':')break;p++;}
 return k==3?v[0]*3600+v[1]*60+v[2]:k==2?v[0]*60+v[1]:k==1?v[0]:0;
}
/* an enclosure URL with spaces in it gets them escaped */
static void pod_fix_url(char *u,int cap) {
 char t[400];int n=0;
 for(const char *p=u;*p && n<(int)sizeof(t)-4;p++){if(*p==' '){t[n++]='%';t[n++]='2';t[n++]='0';}else t[n++]=*p;}
 t[n]=0;snprintf(u,cap,"%s",t);
}
static int pod_item(const char *s,const char *e,PodEp *ep) {
 memset(ep,0,sizeof(*ep));
 char type[48],len[24],tmp[200];
 x_attr(s,e,"enclosure","url",ep->url,sizeof(ep->url));pod_fix_url(ep->url,sizeof(ep->url));
 if(!radio_is_url(ep->url) || strchr(ep->url,'#') || strchr(ep->url,'\t') || strlen(ep->url)>=sizeof(ep->url)-1)return 0;
 x_attr(s,e,"enclosure","type",type,sizeof(type));x_attr(s,e,"enclosure","length",len,sizeof(len));
 ep->bytes=atoi(len);if(ep->bytes<0)ep->bytes=0;
 if(!x_text(s,e,"title",ep->title,sizeof(ep->title)))snprintf(ep->title,sizeof(ep->title),"Episode");
 pod_clean(ep->title);
 if(x_text(s,e,"pubDate",tmp,sizeof(tmp)))x_date(tmp,ep->date,sizeof(ep->date));
 if(x_text(s,e,"itunes:duration",tmp,sizeof(tmp)))ep->dur=x_duration(tmp);
 x_text(s,e,"guid",tmp,sizeof(tmp));
 ep->key=path_hash(tmp[0]?tmp:ep->url);
 pod_ext(type,ep->url,ep->ext);
 return 1;
}
static void pod_channel(const char *s,const char *e,PodShow *out) {
 if(!out->title[0]){x_text(s,e,"title",out->title,sizeof(out->title));pod_clean(out->title);}
 if(!out->author[0]){x_text(s,e,"itunes:author",out->author,sizeof(out->author));pod_clean(out->author);}
 if(!out->art[0] && !x_attr(s,e,"itunes:image","href",out->art,sizeof(out->art))){const char *im=x_find(s,e,"image");if(im)x_text(im,e,"url",out->art,sizeof(out->art));}
}
static char *pod_item_start(char *s){for(char *p=s;(p=strstr(p,"<item"));p++){char c=p[5];if(c=='>' || c==' ' || c=='\t' || c=='\r' || c=='\n')return p;}return NULL;}
/* newest first (dates as YYYY-MM-DD compare as text) */
static void pod_sort(PodEp *eps,int n) {
 if(n<2)return;
 int *ix=malloc(n*sizeof(int));PodEp *tmp=malloc(n*sizeof(PodEp));if(!ix || !tmp){free(ix);free(tmp);return;}
 for(int i=0;i<n;i++)ix[i]=i;
 for(int i=1;i<n;i++){int v=ix[i],j=i;while(j>0 && strcmp(eps[ix[j-1]].date,eps[v].date)<0){ix[j]=ix[j-1];j--;}ix[j]=v;}
 for(int i=0;i<n;i++)tmp[i]=eps[ix[i]];memcpy(eps,tmp,n*sizeof(PodEp));free(ix);free(tmp);
}
/* Reads a feed as it arrives, keeping the newest POD_EP_MAX episodes. Stops early once full on a newest-first feed. */
static int pod_fetch_feed(const char *url,PodShow *out) {
 snprintf(out->feed,sizeof(out->feed),"%s",url);out->eps=NULL;out->n=0;
 int e=net_up(wifi_config);if(e<0)return e;
 const int cap=192*1024;
 Http *h=malloc(sizeof(Http));char *buf=malloc(cap+1);PodEp *eps=malloc(POD_EP_MAX*sizeof(PodEp));
 if(!h || !buf || !eps){free(h);free(buf);free(eps);return NET_E_MEMORY;}
 memset(h,0,sizeof(Http));
 int r=http_open(h,url,NULL,15000);
 if(r<0 || h->status!=200){int code=r<0?r:NET_E_HTTP;if(r>=0)http_close(h);free(h);free(buf);free(eps);return code;}
 int len=0,eof=0,head=0,n=0,desc=1,skipping=0,err=0;char last[12]="";
 for(;;) {
  if(!eof && len<cap){int g=http_read(h,buf+len,cap-len>8192?8192:cap-len,15000);if(g<0){err=g;eof=1;}else if(!g)eof=1;else len+=g;}
  buf[len]=0;
  if(skipping) { /* an item bigger than the buffer: dropped */
   char *end=strstr(buf,"</item>");
   if(end){int k=end+7-buf;memmove(buf,buf+k,len-k+1);len-=k;skipping=0;}
   else{if(len>16){memmove(buf,buf+len-16,17);len=16;}if(eof)break;continue;}
  }
  char *it=pod_item_start(buf);
  if(!head) {
   if(!it && !eof && len<cap)continue;
   pod_channel(buf,it?it:buf+len,out);head=1;
   if(it){int k=it-buf;memmove(buf,it,len-k+1);len-=k;it=buf;}
  }
  int full=0;
  while(it) {
   char *end=strstr(it,"</item>");if(!end)break;
   PodEp ep;
   if(pod_item(it,end,&ep)) {
    int dup=0;for(int i=0;i<n && !dup;i++)if(eps[i].key==ep.key)dup=1;
    if(!dup) {
     if(last[0] && ep.date[0] && strcmp(ep.date,last)>0)desc=0;
     if(ep.date[0])snprintf(last,sizeof(last),"%s",ep.date);
     if(n<POD_EP_MAX)eps[n++]=ep;
     else{int mi=0;for(int i=1;i<n;i++)if(strcmp(eps[i].date,eps[mi].date)<0)mi=i;if(strcmp(ep.date,eps[mi].date)>0)eps[mi]=ep;}
    }
   }
   int k=end+7-buf;memmove(buf,buf+k,len-k+1);len-=k;it=pod_item_start(buf);
   if(n>=POD_EP_MAX && desc){full=1;break;}
  }
  if(full)break;
  if(!it && len>64 && !eof){memmove(buf,buf+len-64,65);len=64;} /* no item begins here: keep a tail where one may */
  else if(it && len>=cap){skipping=1;len=0;}
  if(eof)break;
 }
 http_close(h);free(h);free(buf);
 if(!n){free(eps);return err?err:(head?1:NET_E_HTTP);}
 pod_sort(eps,n);out->eps=eps;out->n=n;return 1;
}

/* ---- the directory (iTunes Search API) ---- */
static void pod_country(char *out){if(region_cc[0]){out[0]=tolower((unsigned char)region_cc[0]);out[1]=tolower((unsigned char)region_cc[1]);out[2]=0;}else strcpy(out,"us");}
static int pod_parse_results(const char *body,PodResult **out,int *n,int max) {
 *out=NULL;*n=0;PodResult *r=malloc(max*sizeof(PodResult));if(!r)return NET_E_MEMORY;
 const char *p=js_ws(body);if(*p!='{'){free(r);return NET_E_HTTP;}p++;
 char key[32],val[600];
 while(js_key(&p,key,sizeof(key))) {
  if(strcmp(key,"results") || *p!='['){p=js_skip(p);continue;}
  p++;
  for(;;) {
   p=js_ws(p);if(*p==','){p++;continue;}if(*p!='{')break;p++;
   PodResult x;memset(&x,0,sizeof(x));char k2[32];
   while(js_key(&p,k2,sizeof(k2))) {
    p=js_value(p,val,sizeof(val));
    if(!strcmp(k2,"collectionName"))snprintf(x.title,sizeof(x.title),"%s",val);
    else if(!strcmp(k2,"artistName"))snprintf(x.author,sizeof(x.author),"%s",val);
    else if(!strcmp(k2,"feedUrl"))snprintf(x.feed,sizeof(x.feed),"%s",val);
    else if(!strcmp(k2,"artworkUrl600"))snprintf(x.art,sizeof(x.art),"%s",val);
    else if(!strcmp(k2,"primaryGenreName"))snprintf(x.genre,sizeof(x.genre),"%s",val);
    else if(!strcmp(k2,"collectionId"))x.id=strtoul(val,NULL,10);
   }
   /* the 600 px art is more than the 256 px cover needs */
   char *sz=strstr(x.art,"600x600");if(sz)memcpy(sz,"300x300",7);
   pod_clean(x.title);pod_clean(x.author);
   if(x.title[0] && radio_is_url(x.feed) && *n<max)r[(*n)++]=x;
  }
  if(*p==']')p++;
 }
 *out=r;return 1;
}
static int pod_search(const char *text,PodResult **res,int *n) {
 int e=net_up(wifi_config);if(e<0)return e;
 char q[200],cc[4],url[600];url_escape(q,sizeof(q),text);pod_country(cc);
 snprintf(url,sizeof(url),"%s/search?media=podcast&entity=podcast&limit=50&country=%s&term=%s",pod_api,cc,q);
 char *body;int st,len=http_get(url,&body,640*1024,&st);if(len<0)return len;
 e=pod_parse_results(body,res,n,50);free(body);return e;
}
/* The top chart (all, or one category): its ids, then their feeds from a lookup, in chart order. */
static int pod_top(int genre,PodResult **res,int *n) {
 *res=NULL;*n=0;
 int e=net_up(wifi_config);if(e<0)return e;
 char cc[4],url[900],g[24]="";pod_country(cc);if(genre)snprintf(g,sizeof(g),"/genre=%d",genre);
 snprintf(url,sizeof(url),"%s/%s/rss/toppodcasts/limit=50%s/json",pod_api,cc,g);
 char *body;int st,len=http_get(url,&body,640*1024,&st);if(len<0)return len;
 unsigned int ids[50];int k=0;
 for(const char *p=body;k<50 && (p=strstr(p,"\"id\":{\"label\":\""));) {
  p+=15;const char *q=strchr(p,'"');if(!q)break;
  int is_show=0;for(const char *s=p;s+9<=q;s++)if(!strncmp(s,"/podcast/",9))is_show=1; /* not the chart's own link */
  const char *im=strstr(q,"\"im:id\":\"");
  if(is_show && im && im-q<160)ids[k++]=strtoul(im+9,NULL,10);
  p=q;
 }
 free(body);
 if(!k)return 1;
 int o=snprintf(url,sizeof(url),"%s/lookup?entity=podcast&country=%s&id=",pod_api,cc);
 for(int i=0;i<k && o<(int)sizeof(url)-14;i++)o+=snprintf(url+o,sizeof(url)-o,"%s%u",i?",":"",ids[i]);
 len=http_get(url,&body,640*1024,&st);if(len<0)return len;
 PodResult *got;int gn;e=pod_parse_results(body,&got,&gn,50);free(body);if(e<0)return e;
 PodResult *r=malloc(50*sizeof(PodResult));if(!r){free(got);return NET_E_MEMORY;}
 for(int i=0;i<k;i++)for(int j=0;j<gn;j++)if(got[j].id==ids[i]){r[(*n)++]=got[j];break;}
 free(got);*res=r;return 1;
}

/* ---- jobs for the zoid_pod thread ---- */
static int pj_kind,pj_posted,pj_taken,pj_done,pj_error,pj_new,pj_genre;static char pj_arg[400];
static PodResult *pj_res;static int pj_res_n;static PodShow pj_show;
static volatile int pod_all_i,pod_all_n; /* Load new episodes on every show: which one */
static char pa_dir[64],pa_url[300];static int pa_posted,pa_taken;
static int pod_want; /* the job the screen waits for: 1 search, 2 top chart, 3 a feed, 4 refresh one show, 5 refresh all */
static void pod_post(int kind,const char *arg,int genre) {
 plock();pj_kind=kind;snprintf(pj_arg,sizeof(pj_arg),"%s",arg?arg:"");pj_genre=genre;pj_posted++;punlock();
 pod_want=kind;sceKernelSignalSema(pod_wake,1);
}
static int pod_waiting(void){return pod_want && pj_done!=pj_posted;}
/* A subscribed show brought up to date: the new episodes, or an error. Episodes keep what was
   played, where they were left and their saved files; old ones still saved stay listed. */
static int pod_refresh_dir(const char *dir) {
 PodShow old;plock();int ok=show_load(dir,&old,0);punlock();if(!ok)return NET_E_HTTP;
 PodShow f;memset(&f,0,sizeof(f));int e=pod_fetch_feed(old.feed,&f);if(e<0)return e;
 plock();
 if(!show_load(dir,&old,1)){punlock();free(f.eps);return NET_E_HTTP;}
 PodEp *m=malloc((f.n+old.n)*sizeof(PodEp)+1);int mn=0,newn=0;
 char *used=calloc(old.n+1,1);
 if(m && used) {
  for(int i=0;i<f.n;i++) {
   PodEp x=f.eps[i];snprintf(x.dir,sizeof(x.dir),"%s",dir);int found=0;
   for(int j=0;j<old.n && !found;j++)if(!used[j] && (old.eps[j].key==x.key || !strcmp(old.eps[j].url,x.url))) {
    used[j]=1;found=1;PodEp *o=&old.eps[j];
    x.played=o->played;x.pos=o->pos;x.isnew=o->isnew;snprintf(x.file,sizeof(x.file),"%s",o->file);if(o->file[0])snprintf(x.ext,sizeof(x.ext),"%s",o->ext);x.key=o->key;
   }
   if(!found){x.isnew=1;newn++;}
   m[mn++]=x;
  }
  for(int j=0;j<old.n;j++)if(!used[j] && old.eps[j].file[0] && mn<POD_SHOW_MAX)m[mn++]=old.eps[j];
  pod_sort(m,mn);
  PodShow s=old;s.eps=m;s.n=mn;if(f.author[0] && !s.author[0])snprintf(s.author,sizeof(s.author),"%s",f.author);
  if(!show_save(dir,&s))newn=POD_E_WRITE;
 }else newn=NET_E_MEMORY;
 punlock();
 free(m);free(used);free(old.eps);free(f.eps);
 return newn;
}
static void pod_fetch_art(const char *dir,const char *url) {
 if(net_up(wifi_config)<0)return;
 char *body;int st,len=http_get(url,&body,1024*1024,&st);if(len<=0)return;
 char path[PATHLEN];snprintf(path,sizeof(path),"%s/%s/cover.jpg",pod_root,dir);
 int fd=sceIoOpen(path,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(fd>=0){sceIoWrite(fd,body,len);sceIoClose(fd);}
 free(body);
}
static int pod_thread(SceSize a,void *b) {
 (void)a;(void)b;
 while(running) {
  SceUInt w=1000000;sceKernelWaitSema(pod_wake,1,&w);
  if(!running)break;
  plock();int k=0,genre=0;char arg[400];if(pj_posted!=pj_taken){pj_taken=pj_posted;k=pj_kind;genre=pj_genre;snprintf(arg,sizeof(arg),"%s",pj_arg);}punlock();
  if(k) {
   PodResult *res=NULL;int rn=0,e=1,newn=0;PodShow sh;memset(&sh,0,sizeof(sh));
   if(k==1)e=pod_search(arg,&res,&rn);
   else if(k==2)e=pod_top(genre,&res,&rn);
   else if(k==3)e=pod_fetch_feed(arg,&sh);
   else if(k==4){e=pod_refresh_dir(arg);if(e>=0)newn=e;}
   else {
    static char dirs[POD_SUB_MAX][64];int nd=pod_list_dirs(dirs,POD_SUB_MAX);pod_all_n=nd;
    for(int i=0;i<nd && running;i++){pod_all_i=i;int r=pod_refresh_dir(dirs[i]);if(r>=0)newn+=r;else if(e>0)e=r;}
    if(newn)e=1; /* some shows answered */
   }
   plock();
   free(pj_res);free(pj_show.eps);pj_res=res;pj_res_n=rn;pj_show=sh;pj_error=e<0?e:0;pj_new=newn;pj_done=pj_taken;
   punlock();
  }
  plock();int art=0;char d[64],u[300];if(pa_posted!=pa_taken){pa_taken=pa_posted;art=1;snprintf(d,sizeof(d),"%s",pa_dir);snprintf(u,sizeof(u),"%s",pa_url);}punlock();
  if(art)pod_fetch_art(d,u);
 }
 return 0;
}

/* ---- saving episodes (the zoid_dl thread) ---- */
typedef struct {char dir[64],file[96],url[400],title[128];unsigned int key;} DlItem;
static DlItem dl_q[POD_DL_MAX];static int dl_n; /* waiting; dl_q[0] is the one saving while dl_busy */
static volatile int dl_busy,dl_got,dl_total,dl_gen,dl_cancel;static volatile unsigned int dl_key;static char dl_dir[64],dl_msg[160];
static int dl_index(const char *dir,unsigned int key){for(int i=0;i<dl_n;i++)if(dl_q[i].key==key && !strcmp(dl_q[i].dir,dir))return i;return -1;}
static const char *pod_error_text(int e){return e==POD_E_WRITE?"Could not write to the Memory Stick (is it full?)":net_error_text(e);}
static int dl_run(DlItem *it) {
 char path[PATHLEN],part[PATHLEN+8];snprintf(path,sizeof(path),"%s/%s/%s",pod_root,it->dir,it->file);snprintf(part,sizeof(part),"%s.part",path);
 int e=net_up(wifi_config);if(e<0)return e;
 int have=0;SceIoStat st;memset(&st,0,sizeof(st));if(sceIoGetstat(part,&st)>=0)have=(int)st.st_size;
 Http *h=malloc(sizeof(Http));unsigned char *buf=malloc(32768);if(!h || !buf){free(h);free(buf);return NET_E_MEMORY;}
 memset(h,0,sizeof(Http));
 char extra[48];if(have>0)snprintf(extra,sizeof(extra),"Range: bytes=%d-\r\n",have);else extra[0]=0;
 e=http_open(h,it->url,extra,15000);
 if(e<0 || (h->status!=200 && h->status!=206)){if(e>=0)http_close(h);free(h);free(buf);return e<0?e:NET_E_HTTP;}
 if(h->status==200)have=0; /* no Range: from the start */
 dl_total=h->status==206?(h->range_total>0?h->range_total:(h->length>0?have+h->length:0)):(h->length>0?h->length:0);
 dl_got=have;
 int fd=sceIoOpen(part,PSP_O_WRONLY|PSP_O_CREAT|(have?PSP_O_APPEND:PSP_O_TRUNC),0777);
 int r=1;
 if(fd<0)r=POD_E_WRITE;
 else {
  for(;;) {
   if(dl_cancel || !running){r=NET_E_STOPPED;break;}
   int g=http_read(h,buf,32768,15000);if(!g)break;if(g<0){r=g;break;}
   if(sceIoWrite(fd,buf,g)!=g){r=POD_E_WRITE;break;}
   dl_got+=g;
  }
  sceIoClose(fd);
 }
 http_close(h);free(h);free(buf);
 if(r>0 && dl_total>0 && dl_got<dl_total)r=NET_E_CLOSED;
 if(r>0){sceIoRemove(path);if(sceIoRename(part,path)<0)r=POD_E_WRITE;}
 else if(r==NET_E_STOPPED || r==POD_E_WRITE)sceIoRemove(part); /* cancelled: nothing left behind */
 return r;
}
static int dl_thread(SceSize a,void *b) {
 (void)a;(void)b;
 while(running) {
  SceUInt w=1000000;sceKernelWaitSema(dl_wake,1,&w);
  while(running) {
   plock();if(!dl_n){punlock();break;}
   DlItem it=dl_q[0];dl_key=it.key;snprintf(dl_dir,sizeof(dl_dir),"%s",it.dir);dl_got=dl_total=0;dl_cancel=0;dl_busy=1;punlock();
   int r=dl_run(&it);
   plock();
   int k=dl_index(it.dir,it.key);if(k>=0){memmove(&dl_q[k],&dl_q[k+1],(dl_n-k-1)*sizeof(DlItem));dl_n--;}
   if(r>0){PodShow s;if(show_load(it.dir,&s,1)){for(int i=0;i<s.n;i++)if(s.eps[i].key==it.key)snprintf(s.eps[i].file,sizeof(s.eps[i].file),"%s",it.file);show_save(it.dir,&s);free(s.eps);}}
   if(r>0)snprintf(dl_msg,sizeof(dl_msg),"Saved: %.100s",it.title);
   else if(r==NET_E_STOPPED)snprintf(dl_msg,sizeof(dl_msg),"Stopped saving");
   else snprintf(dl_msg,sizeof(dl_msg),"Could not save: %s",pod_error_text(r));
   dl_busy=0;dl_gen++;
   punlock();
  }
 }
 return 0;
}

/* ---- what an episode row says ---- */
static void pod_when(const PodEp *e,char *out,int cap) {
 static int year;if(!year){ScePspDateTime t;memset(&t,0,sizeof(t));if(sceRtcGetCurrentClockLocalTime(&t)>=0)year=t.year;}
 static const char *mons[12]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
 char d[24]="",l[24]="";int y,m,dd;
 if(sscanf(e->date,"%d-%d-%d",&y,&m,&dd)==3 && m>=1 && m<=12){if(y==year)snprintf(d,sizeof(d),"%s %d",mons[m-1],dd);else snprintf(d,sizeof(d),"%s %d, %d",mons[m-1],dd,y);}
 int mins=(e->dur+30)/60;if(e->dur>0){if(mins>=60)snprintf(l,sizeof(l),"%d hr %d min",mins/60,mins%60);else snprintf(l,sizeof(l),"%d min",mins?mins:1);}
 snprintf(out,cap,"%s%s%s",d,d[0] && l[0]?", ":"",l);
}
static void pod_status(const PodEp *e,char *out,int cap) {
 char when[48];
 if(dl_busy && dl_key==e->key && !strcmp(dl_dir,e->dir)){int pc=dl_total>0?(int)((long long)dl_got*100/dl_total):0;snprintf(out,cap,"Saving %d%%",pc);return;}
 if(dl_index(e->dir,e->key)>=0){snprintf(out,cap,"Waiting to save");return;}
 if(e->file[0]){snprintf(out,cap,"%s",e->played?"Played, saved":"Saved");return;}
 if(e->played){snprintf(out,cap,"Played");return;}
 if(e->pos>10 && e->dur>e->pos){int left=(e->dur-e->pos+59)/60;snprintf(out,cap,"%s%d min left",e->fresh?"New  ":"",left);return;}
 pod_when(e,when,sizeof(when));snprintf(out,cap,"%s%s",e->fresh?"New  ":"",when);
}
static int pod_ep_icon(int i) {
 int k=i-pod_first;if(k<0 || k>=pod_n)return IC_MIC;
 const PodEp *e=&pod_eps[k];return e->file[0]?IC_DOWNLOAD:e->played?IC_CHECK:IC_MIC;
}

/* ---- the Library views ---- */
static void pod_rows_begin(void){lock(meta_lock);browse_gen++;count=entries_total=0;names_reset();scroll=0;cursor=0;in_playlist=0;in_radio=0;in_pod=1;}
static void pod_rows_end(void){unlock(meta_lock);lib_scroll_f=lib_sel_f=0;}
static void pod_scan_subs(void) {
 static char dirs[POD_SUB_MAX][64];int nd=pod_list_dirs(dirs,POD_SUB_MAX);pod_sub_n=0;
 for(int i=0;i<nd;i++) {
  PodShow s;plock();int ok=show_load(dirs[i],&s,1);punlock();if(!ok)continue;
  PodSub *p=&pod_subs[pod_sub_n++];memset(p,0,sizeof(*p));
  snprintf(p->dir,sizeof(p->dir),"%s",dirs[i]);snprintf(p->title,sizeof(p->title),"%s",s.title[0]?s.title:dirs[i]);
  snprintf(p->author,sizeof(p->author),"%s",s.author);snprintf(p->feed,sizeof(p->feed),"%s",s.feed);
  for(int k=0;k<s.n;k++){if(s.eps[k].isnew && !s.eps[k].played)p->newn++;if(s.eps[k].file[0])p->saved++;}
  p->total=s.n;free(s.eps);
 }
 for(int i=1;i<pod_sub_n;i++){PodSub t=pod_subs[i];int j=i;while(j>0 && natural_compare(pod_subs[j-1].title,t.title)>0){pod_subs[j]=pod_subs[j-1];j--;}pod_subs[j]=t;}
}
static void pod_show_home(void) {
 pod_scan_subs(); /* the counts (new, saved) are read fresh each time */
 pod_rows_begin();pod_view=PV_HOME;snprintf(pod_title,sizeof(pod_title),POD_NAME);
 entry_add("Search podcasts",20);entry_add(region_cc[0]?"Top podcasts in your country":"Top podcasts",21);entry_add("Categories",22);
 if(pod_sub_n){entry_add("Load new episodes",23);entry_add("Saved episodes",24);}
 pod_first=count;
 for(int i=0;i<pod_sub_n;i++) {
  PodSub *s=&pod_subs[i];Entry *e=entry_add(s->title,25);if(!e)break;
  if(s->newn)snprintf(e->artist,sizeof(e->artist),"%d new",s->newn);else if(s->saved)snprintf(e->artist,sizeof(e->artist),"%d saved",s->saved);else snprintf(e->artist,sizeof(e->artist),"%s",s->author);
 }
 pod_rows_end();
}
static void pod_show_results(void) {
 pod_rows_begin();pod_view=PV_RESULTS;snprintf(pod_title,sizeof(pod_title),"%s",pod_res_title);pod_first=0;
 for(int i=0;i<pod_res_n;i++){Entry *e=entry_add(pod_res[i].title,26);if(!e)break;snprintf(e->artist,sizeof(e->artist),"%s",pod_res[i].author);}
 pod_rows_end();
}
static void pod_show_cats(void) {
 pod_rows_begin();pod_view=PV_CATS;snprintf(pod_title,sizeof(pod_title),"Categories");pod_first=0;
 for(int i=0;i<POD_CAT_N;i++)entry_add(pod_cats[i].name,27);
 pod_rows_end();
}
static void pod_row_text(int i) {
 int k=i-pod_first;if(k<0 || k>=pod_n || entries[i].folder)return;
 if(pod_view==PV_SAVED){for(int s=0;s<pod_sub_n;s++)if(!strcmp(pod_subs[s].dir,pod_eps[k].dir)){snprintf(entries[i].artist,sizeof(entries[i].artist),"%s",pod_subs[s].title);return;}return;}
 pod_status(&pod_eps[k],entries[i].artist,sizeof(entries[i].artist));
}
static void pod_show_eps(void) {
 pod_rows_begin();pod_view=PV_SHOW;snprintf(pod_title,sizeof(pod_title),"%s",pod_cur.title[0]?pod_cur.title:POD_NAME);
 if(pod_n || pod_cur_dir[0])entry_add(pod_cur_dir[0]?"Load new episodes":"Subscribe",pod_cur_dir[0]?29:28);
 pod_first=count;
 for(int k=0;k<pod_n;k++){Entry *e=entry_add(pod_eps[k].title,0);if(!e)break;e->tagged=1;snprintf(e->title,sizeof(e->title),"%s",pod_eps[k].title);pod_row_text(count-1);}
 pod_rows_end();
}
static void pod_set_eps(PodEp *eps,int n){free(pod_eps);pod_eps=eps;pod_n=n;}
/* a subscribed show: its episodes from show.txt; what was new is marked for this visit, then forgotten */
static void pod_open_sub(const char *dir) {
 PodShow s;plock();int ok=show_load(dir,&s,1);
 int any=0;if(ok)for(int k=0;k<s.n;k++){s.eps[k].fresh=s.eps[k].isnew;if(s.eps[k].isnew){s.eps[k].isnew=0;any=1;}}
 if(ok && any)show_save(dir,&s);
 punlock();
 if(!ok){notice("Could not read this podcast's show.txt");return;}
 snprintf(pod_cur_dir,sizeof(pod_cur_dir),"%s",dir);
 snprintf(pod_cur.feed,sizeof(pod_cur.feed),"%s",s.feed);snprintf(pod_cur.title,sizeof(pod_cur.title),"%s",s.title);
 snprintf(pod_cur.author,sizeof(pod_cur.author),"%s",s.author);snprintf(pod_cur.art,sizeof(pod_cur.art),"%s",s.art);
 pod_set_eps(s.eps,s.n);
 for(int i=0;i<pod_sub_n;i++)if(!strcmp(pod_subs[i].dir,dir))pod_subs[i].newn=0;
 pod_show_eps();
}
static void pod_show_saved(void) {
 PodEp *all=NULL;int n=0,cap=0;
 for(int i=0;i<pod_sub_n;i++) {
  PodShow s;plock();int ok=show_load(pod_subs[i].dir,&s,1);punlock();if(!ok)continue;
  for(int k=0;k<s.n;k++)if(s.eps[k].file[0] && n<POD_SHOW_MAX){if(n>=cap){cap=cap?cap*2:32;PodEp *q=realloc(all,cap*sizeof(PodEp));if(!q)break;all=q;}all[n++]=s.eps[k];}
  free(s.eps);
 }
 pod_sort(all,n);pod_set_eps(all,n);
 pod_rows_begin();pod_view=PV_SAVED;snprintf(pod_title,sizeof(pod_title),"Saved episodes");pod_first=0;
 for(int k=0;k<pod_n;k++){Entry *e=entry_add(pod_eps[k].title,0);if(!e)break;e->tagged=1;snprintf(e->title,sizeof(e->title),"%s",pod_eps[k].title);pod_row_text(count-1);}
 pod_rows_end();
}
static void pod_open_results(int kind,const char *arg,int genre,const char *title,int from) {
 pod_res_from=from;snprintf(pod_res_title,sizeof(pod_res_title),"%s",title);
 pod_rows_begin();pod_view=PV_RESULTS;snprintf(pod_title,sizeof(pod_title),"%s",title);pod_first=0;pod_rows_end();
 pod_post(kind,arg,genre);
}
static void pod_search_done(const char *text) {
 if(!text || !text[0])return;
 char t[96];snprintf(t,sizeof(t),"\"%s\"",text);pod_res_home_row=0;pod_open_results(1,text,0,t,PV_HOME);
}
static void pod_enter(void){pod_scan_subs();pod_show_home();}
static int pod_entry_path_ep(const PodEp *e,char *out,int cap) {
 if(e->file[0] && e->dir[0]){int r=snprintf(out,cap,"%s/%s/%s",pod_root,e->dir,e->file);return r>0 && r<cap;}
 char t[128];snprintf(t,sizeof(t),"%s",e->title);for(char *c=t;*c;c++)if(*c=='|' || *c=='#')*c=' ';
 int r=snprintf(out,cap,"%s#@%d,%d|%s|%s",e->url,e->dur,e->bytes,e->dir,t);
 if(r>=cap) { /* the title is cut to fit */
  int base=snprintf(out,cap,"%s#@%d,%d|%s|",e->url,e->dur,e->bytes,e->dir);if(base>=cap-1)return 0;
  snprintf(out+base,cap-base,"%s",t);utf8_cut(out);r=strlen(out);
 }
 return r>0 && r<cap;
}
static int pod_entry_path(int i,char *out,int cap) {
 int k=i-pod_first;
 if(entries[i].folder || (pod_view!=PV_SHOW && pod_view!=PV_SAVED) || k<0 || k>=pod_n){int r=snprintf(out,cap,"pod:%d",i);return r>0 && r<cap;}
 return pod_entry_path_ep(&pod_eps[k],out,cap);
}
/* An episode playing (streamed or saved): its show folder, and its URL or file name. */
static int pod_is_episode(const char *path){int n=strlen(pod_root);return radio_is_episode(path) || (!strncmp(path,pod_root,n) && path[n]=='/');}
static int pod_episode_parts(const char *path,char *dir,int dcap,char *url,int ucap,char *file,int fcap) {
 dir[0]=url[0]=file[0]=0;
 if(radio_is_episode(path)) {
  const char *h=strchr(path,'#');int ul=h-path;if(ul>=ucap)ul=ucap-1;memcpy(url,path,ul);url[ul]=0;
  const char *b=strchr(h,'|');if(b){b++;const char *b2=strchr(b,'|');int dl=b2?(int)(b2-b):(int)strlen(b);if(dl>=dcap)dl=dcap-1;memcpy(dir,b,dl);dir[dl]=0;}
  return 1;
 }
 int n=strlen(pod_root);if(strncmp(path,pod_root,n) || path[n]!='/')return 0;
 const char *d=path+n+1,*sl=strchr(d,'/');if(!sl || strchr(sl+1,'/'))return 0;
 int dl=sl-d;if(dl>=dcap)dl=dcap-1;memcpy(dir,d,dl);dir[dl]=0;snprintf(file,fcap,"%s",sl+1);
 return 1;
}
static int pod_ep_matches(const PodEp *e,const char *url,const char *file){return file[0]?!strcmp(e->file,file):!strcmp(e->url,url);}
/* changes one episode in its show.txt and in the list on screen. played/pos: -1 keeps them; file: NULL keeps it */
static void pod_ep_update(const char *dir,unsigned int key,int played,int pos,const char *file) {
 if(dir[0]) {
  PodShow s;plock();
  if(show_load(dir,&s,1)){for(int i=0;i<s.n;i++)if(s.eps[i].key==key){if(played>=0)s.eps[i].played=played;if(pos>=0)s.eps[i].pos=pos;if(file)snprintf(s.eps[i].file,sizeof(s.eps[i].file),"%s",file);s.eps[i].isnew=0;}show_save(dir,&s);free(s.eps);}
  punlock();
 }
 for(int k=0;k<pod_n;k++)if(pod_eps[k].key==key && !strcmp(pod_eps[k].dir,dir)){PodEp *e=&pod_eps[k];if(played>=0)e->played=played;if(pos>=0)e->pos=pos;if(file)snprintf(e->file,sizeof(e->file),"%s",file);e->fresh=0;}
}
/* Where an episode was left; near its end it counts as played. */
static void pod_progress_save(const char *path,int pos,int dur) {
 char dir[64],url[400],file[96];if(pos<0 || !pod_episode_parts(path,dir,sizeof(dir),url,sizeof(url),file,sizeof(file)))return;
 int tail=dur/10;if(tail>30)tail=30;if(tail<2)tail=2;
 int played=dur>0 && pos>=dur-tail;if(pos<5)pos=0;
 unsigned int key=0;int found=0;
 if(dir[0]){PodShow s;plock();if(show_load(dir,&s,1)){for(int i=0;i<s.n && !found;i++)if(pod_ep_matches(&s.eps[i],url,file)){key=s.eps[i].key;found=1;if(s.eps[i].played)played=1;}free(s.eps);}punlock();}
 else for(int k=0;k<pod_n && !found;k++)if(!pod_eps[k].dir[0] && pod_ep_matches(&pod_eps[k],url,file)){key=pod_eps[k].key;found=1;}
 if(found)pod_ep_update(dir,key,played,played?0:pos,NULL);
}
static char pt_path[PATHLEN];static int pt_pos=-1,pt_dur,pt_state;static unsigned long long pt_saved;
static void pod_tick(void) {
 if(strcmp(active,pt_path)) {
  if(pt_path[0])pod_progress_save(pt_path,pt_pos,pt_dur);
  if(pod_is_episode(active))snprintf(pt_path,sizeof(pt_path),"%s",active);else pt_path[0]=0;
  pt_pos=-1;pt_dur=0;pt_saved=now_ms;
 }
 if(!pt_path[0]){pt_state=state;return;}
 if(state==2 && seek_ack==serial){pt_pos=elapsed;pt_dur=duration;}
 if((pt_state==2 && state!=2) || (state==2 && !paused && now_ms-pt_saved>30000)){pod_progress_save(pt_path,pt_pos,pt_dur);pt_saved=now_ms;}
 pt_state=state;
}
/* Now Playing (meta thread): the episode's title, the show's name and the date. 1 when path is an episode. */
static int pod_meta(const char *path,char *title,int tc,char *show,int sc,char *album,int ac,char *dir,int dcap) {
 char url[400],file[96];if(!pod_episode_parts(path,dir,dcap,url,sizeof(url),file,sizeof(file)))return 0;
 title[0]=show[0]=album[0]=0;
 if(radio_is_episode(path)){const char *t=strrchr(path,'|');snprintf(title,tc,"%s",t?t+1:"");}
 if(dir[0]) {
  PodShow s;plock();int ok=show_load(dir,&s,1);punlock();
  if(ok) {
   snprintf(show,sc,"%s",s.title);
   for(int i=0;i<s.n;i++)if(pod_ep_matches(&s.eps[i],url,file)){snprintf(title,tc,"%s",s.eps[i].title);pod_when(&s.eps[i],album,ac);break;}
   free(s.eps);
  }
 }else{plock();snprintf(show,sc,"%s",pod_cur.title);punlock();}
 if(!album[0])snprintf(album,ac,"Podcast");
 return 1;
}

/* ---- saving, deleting, subscribing ---- */
static void pod_post_art(const char *dir,const char *url){if(!radio_is_url(url))return;plock();snprintf(pa_dir,sizeof(pa_dir),"%s",dir);snprintf(pa_url,sizeof(pa_url),"%s",url);pa_posted++;punlock();sceKernelSignalSema(pod_wake,1);}
static void pod_subscribe(void) {
 if(pod_cur_dir[0])return;
 if(!pod_cur.feed[0] || !pod_n){notice("Wait for the episodes to load");return;}
 char base[64],dir[64],p[PATHLEN];pod_safe(base,48,pod_cur.title);snprintf(dir,sizeof(dir),"%s",base);
 sceIoMkdir(pod_root,0777);
 for(int k=2;k<60;k++){snprintf(p,sizeof(p),"%s/%s",pod_root,dir);if(!dir_exists(p))break;snprintf(dir,sizeof(dir),"%s %d",base,k);}
 snprintf(p,sizeof(p),"%s/%s",pod_root,dir);sceIoMkdir(p,0777);
 for(int k=0;k<pod_n;k++){snprintf(pod_eps[k].dir,sizeof(pod_eps[k].dir),"%s",dir);pod_eps[k].isnew=0;}
 PodShow s=pod_cur;s.eps=pod_eps;s.n=pod_n;
 plock();int ok=show_save(dir,&s);punlock();
 if(!ok){for(int k=0;k<pod_n;k++)pod_eps[k].dir[0]=0;remove_tree(p,0);notice("Could not write to the Memory Stick");return;}
 snprintf(pod_cur_dir,sizeof(pod_cur_dir),"%s",dir);
 pod_post_art(dir,pod_cur.art);
 pod_scan_subs();
 if(in_pod && pod_view==PV_SHOW){int c=cursor;pod_show_eps();cursor=c<count?c:0;}
 char msg[140];snprintf(msg,sizeof(msg),"Subscribed to %.100s",pod_cur.title);notice(msg);
}
static int pod_save_ep(PodEp *e) {
 if(!e->dir[0]) {
  if(in_pod && pod_view==PV_SHOW && !pod_cur_dir[0] && e>=pod_eps && e<pod_eps+pod_n){pod_subscribe();if(!pod_cur_dir[0])return 0;}
  else{notice("Subscribe to this podcast to save its episodes");return 0;}
 }
 if(e->file[0]){notice("Already saved");return 0;}
 if(!pod_playable(e->ext)){notice("ZOID cannot play this episode's format");return 0;}
 char name[80],file[96],path[PATHLEN];char both[200];snprintf(both,sizeof(both),"%s %s",e->date,e->title);pod_safe(name,70,both);
 snprintf(file,sizeof(file),"%s.%s",name,e->ext);snprintf(path,sizeof(path),"%s/%s/%s",pod_root,e->dir,file);
 if(file_exists(path)){snprintf(file,sizeof(file),"%s %08x.%s",name,e->key,e->ext);}
 plock();
 if(dl_index(e->dir,e->key)>=0){punlock();notice("Already saving");return 0;}
 if(dl_n>=POD_DL_MAX){punlock();notice("Too many episodes waiting to save");return 0;}
 DlItem *d=&dl_q[dl_n++];memset(d,0,sizeof(*d));
 snprintf(d->dir,sizeof(d->dir),"%s",e->dir);snprintf(d->file,sizeof(d->file),"%s",file);snprintf(d->url,sizeof(d->url),"%s",e->url);snprintf(d->title,sizeof(d->title),"%s",e->title);d->key=e->key;
 punlock();
 sceKernelSignalSema(dl_wake,1);
 return 1;
}
static void pod_cancel_dl(const char *dir,unsigned int key) {
 plock();int k=dl_index(dir,key);
 if(k==0 && dl_busy)dl_cancel=1;
 else if(k>0 || (k==0 && !dl_busy)){memmove(&dl_q[k],&dl_q[k+1],(dl_n-k-1)*sizeof(DlItem));dl_n--;}
 punlock();
}
/* stops what plays from path (a file, or with folder=1 anything inside it) before it is deleted */
static void pod_stop_playing(const char *path,int folder) {
 int n=strlen(path);char d[64],u[8],f[8];
 int hit=active[0] && (!strcmp(active,path) || (folder && !strncmp(active,path,n) && active[n]=='/'));
 if(!hit && folder && radio_is_episode(active) && pod_episode_parts(active,d,sizeof(d),u,sizeof(u),f,sizeof(f))){const char *b=strrchr(path,'/');hit=b && !strcmp(d,b+1);}
 if(hit){pt_path[0]=0;request(NULL);for(int w=0;w<100 && state;w++)sceKernelDelayThread(10000);}
 engine_drop_prepared();
}
static void pod_delete_ep(const char *dir,unsigned int key,const char *file) {
 char path[PATHLEN];snprintf(path,sizeof(path),"%s/%s/%s",pod_root,dir,file);
 pod_stop_playing(path,0);
 sceIoRemove(path);queue_forget(path,0);
 pod_ep_update(dir,key,-1,-1,"");
 if(in_pod && pod_view==PV_SAVED){int c=cursor;pod_show_saved();cursor=c<count?c:count-1;if(cursor<0)cursor=0;}
 notice("Saved copy deleted");
}
static void pod_unsubscribe(const char *dir) {
 char p[PATHLEN];snprintf(p,sizeof(p),"%s/%s",pod_root,dir);
 pod_stop_playing(p,1);
 plock();
 for(int i=dl_n-1;i>=0;i--)if(!strcmp(dl_q[i].dir,dir) && !(i==0 && dl_busy)){memmove(&dl_q[i],&dl_q[i+1],(dl_n-i-1)*sizeof(DlItem));dl_n--;}
 int busy=dl_busy && !strcmp(dl_dir,dir);if(busy)dl_cancel=1;
 punlock();
 for(int w=0;w<300 && busy && dl_busy;w++)sceKernelDelayThread(10000);
 plock();remove_tree(p,0);punlock();
 queue_forget(p,1);
 char title[96]="";for(int i=0;i<pod_sub_n;i++)if(!strcmp(pod_subs[i].dir,dir))snprintf(title,sizeof(title),"%s",pod_subs[i].title);
 if(!strcmp(pod_cur_dir,dir)){pod_cur_dir[0]=0;for(int k=0;k<pod_n;k++)pod_eps[k].dir[0]=pod_eps[k].file[0]=0;}
 pod_scan_subs();
 if(in_pod){pod_show_home();cursor=pod_sub_n?pod_first:0;}
 char msg[140];snprintf(msg,sizeof(msg),"Unsubscribed from %.100s",title[0]?title:"the podcast");notice(msg);
}

/* ---- every frame: finished jobs, saved episodes, row text ---- */
static void pod_poll(void) {
 pod_tick();
 static int seen_dl;
 if(dl_gen!=seen_dl) {
  seen_dl=dl_gen;notice(dl_msg);
  /* saved files show up in the list on screen */
  if(pod_cur_dir[0] && pod_n){PodShow s;plock();int ok=show_load(pod_cur_dir,&s,1);punlock();
   if(ok){for(int k=0;k<pod_n;k++)if(!strcmp(pod_eps[k].dir,pod_cur_dir))for(int i=0;i<s.n;i++)if(s.eps[i].key==pod_eps[k].key)snprintf(pod_eps[k].file,sizeof(pod_eps[k].file),"%s",s.eps[i].file);free(s.eps);}}
  if(in_pod && pod_view==PV_HOME){int c=cursor;pod_scan_subs();pod_show_home();cursor=c<count?c:0;}
 }
 if(pod_want && pj_done==pj_posted && pj_done) {
  int k=pod_want;pod_want=0;
  plock();
  int e=pj_error,newn=pj_new;PodResult *res=NULL;int rn=0;PodShow sh;memset(&sh,0,sizeof(sh));
  if(k==1 || k==2){res=pj_res;rn=pj_res_n;pj_res=NULL;pj_res_n=0;}
  if(k==3){sh=pj_show;pj_show.eps=NULL;pj_show.n=0;}
  punlock();
  if(k<=3)pod_err=e;
  if((k==1 || k==2) && in_pod && pod_view==PV_RESULTS){free(pod_res);pod_res=res;pod_res_n=rn;res=NULL;pod_show_results();}
  else if(k==3 && in_pod && pod_view==PV_SHOW && !pod_cur_dir[0]) {
   if(!pod_cur.title[0])snprintf(pod_cur.title,sizeof(pod_cur.title),"%s",sh.title);
   if(!pod_cur.author[0])snprintf(pod_cur.author,sizeof(pod_cur.author),"%s",sh.author);
   if(!pod_cur.art[0])snprintf(pod_cur.art,sizeof(pod_cur.art),"%s",sh.art);
   pod_set_eps(sh.eps,sh.n);sh.eps=NULL;pod_show_eps();
   if(pod_auto_sub && !e){pod_auto_sub=0;pod_subscribe();}
  }
  else if(k==4 || k==5) {
   char msg[96];
   if(e && !newn)snprintf(msg,sizeof(msg),"%s",pod_error_text(e));
   else if(!newn)snprintf(msg,sizeof(msg),"No new episodes");else snprintf(msg,sizeof(msg),"%d new episode%s",newn,newn==1?"":"s");
   notice(msg);
   pod_scan_subs();
   if(in_pod && pod_view==PV_SHOW && pod_cur_dir[0]){int c=cursor;char d[64];snprintf(d,sizeof(d),"%s",pod_cur_dir);pod_open_sub(d);cursor=c<count?c:0;}
   else if(in_pod && pod_view==PV_HOME){int c=cursor;pod_show_home();cursor=c<count?c:0;}
  }
  free(res);free(sh.eps);
 }
 /* the rows on screen follow saving progress */
 static int tick;if(in_pod && (pod_view==PV_SHOW || pod_view==PV_SAVED) && ++tick%10==0){int from=scroll-2<0?0:scroll-2;for(int i=from;i<count && i<scroll+LIB_ROWS+4;i++)pod_row_text(i);}
}

/* ---- X, Circle and the header ---- */
static int pod_activate(int i) {
 Entry *e=&entries[i];int k=i-pod_first;
 switch(e->folder) {
 case 20:osk_open("Search podcasts","",pod_search_done);return 1;
 case 21:pod_res_home_row=1;pod_open_results(2,"",0,region_cc[0]?"Top podcasts in your country":"Top podcasts",PV_HOME);return 1;
 case 22:pod_show_cats();return 1;
 case 23:pod_post(5,"",0);return 1;
 case 24:pod_show_saved();return 1;
 case 25:if(k>=0 && k<pod_sub_n){pod_home_cursor=i;pod_from=PV_HOME;pod_open_sub(pod_subs[k].dir);}return 1;
 case 26:
  if(k<0 || k>=pod_res_n)return 1;
  pod_res_cursor=i;pod_from=PV_RESULTS;
  for(int s=0;s<pod_sub_n;s++)if(!strcmp(pod_subs[s].feed,pod_res[k].feed)){pod_open_sub(pod_subs[s].dir);return 1;} /* already subscribed */
  pod_cur_dir[0]=0;memset(&pod_cur,0,sizeof(pod_cur));
  snprintf(pod_cur.feed,sizeof(pod_cur.feed),"%s",pod_res[k].feed);snprintf(pod_cur.title,sizeof(pod_cur.title),"%s",pod_res[k].title);
  snprintf(pod_cur.author,sizeof(pod_cur.author),"%s",pod_res[k].author);snprintf(pod_cur.art,sizeof(pod_cur.art),"%s",pod_res[k].art);
  pod_set_eps(NULL,0);pod_show_eps();pod_post(3,pod_cur.feed,0);
  return 1;
 case 27:if(i<POD_CAT_N){pod_cat_cursor=i;pod_open_results(2,"",pod_cats[i].id,pod_cats[i].name,PV_CATS);}return 1;
 case 28:pod_subscribe();return 1;
 case 29:if(pod_cur_dir[0])pod_post(4,pod_cur_dir,0);return 1;
 case 0: {
  if(k<0 || k>=pod_n)return 1;
  PodEp *ep=&pod_eps[k];
  if(!ep->file[0] && !pod_streamable(ep->ext)) {
   if(!pod_playable(ep->ext)){notice("ZOID cannot play this episode's format");return 1;}
   if(pod_save_ep(ep))notice("M4A episodes play once saved: saving it now");
   return 1;
  }
  /* carry on where it was left */
  char p[PATHLEN];if(pod_entry_path_ep(ep,p,sizeof(p)) && !(state && !strcmp(p,active)) && ep->pos>10 && !ep->played)next_request_seek=ep->pos-3;
  return 0;
 }
 }
 return 1;
}
static void pod_back(void) {
 pod_want=0;
 if(pod_view==PV_SHOW) {
  if(pod_from==PV_RESULTS && pod_res){pod_show_results();cursor=pod_res_cursor<count?pod_res_cursor:0;return;}
  pod_scan_subs();pod_show_home();cursor=pod_home_cursor<count?pod_home_cursor:0;return;
 }
 if(pod_view==PV_RESULTS) {
  if(pod_res_from==PV_CATS){pod_show_cats();cursor=pod_cat_cursor;return;}
  pod_show_home();cursor=pod_res_home_row;return;
 }
 if(pod_view==PV_CATS){pod_show_home();cursor=2;return;}
 if(pod_view==PV_SAVED){pod_scan_subs();pod_show_home();cursor=4<count?4:0;return;}
 go_home(HOME_PODCASTS);
}
static void pod_info(char *out,int cap) {
 if(pod_waiting()) {
  if(pod_want==5)snprintf(out,cap,"Checking %d of %d...",pod_all_i+1,pod_all_n?pod_all_n:1);
  else if(pod_want==4)snprintf(out,cap,"Checking for new episodes...");
  else snprintf(out,cap,"%s",net_status==1?"Connecting to Wi-Fi...":"Loading...");
  return;
 }
 char sv[32]="";if(dl_busy)snprintf(sv,sizeof(sv),"Saving %d%%  -  ",dl_total>0?(int)((long long)dl_got*100/dl_total):0);
 if(pod_view==PV_HOME)snprintf(out,cap,"%s%d subscribed",sv,pod_sub_n);
 else if(pod_view==PV_RESULTS)snprintf(out,cap,"%d podcast%s",count,count==1?"":"s");
 else if(pod_view==PV_CATS)snprintf(out,cap,"%d categories",count);
 else if(pod_view==PV_SAVED)snprintf(out,cap,"%s%d saved",sv,pod_n);
 else{int saved=0;for(int k=0;k<pod_n;k++)if(pod_eps[k].file[0])saved++;snprintf(out,cap,"%s%d episodes, %d saved",sv,pod_n,saved);}
}
static int pod_empty(char *l1,int c1,char *l2,int c2) {
 if(!in_pod || count)return 0;
 if(pod_waiting()){snprintf(l1,c1,"%s",net_status==1?"Connecting to Wi-Fi...":pod_view==PV_SHOW?"Loading episodes...":"Loading podcasts...");snprintf(l2,c2,"%s",net_status==2 && net_connected_name[0]?net_connected_name:pod_view==PV_SHOW?"From the podcast's feed":"Apple Podcasts directory");return 1;}
 if(pod_err){snprintf(l1,c1,"%s",pod_error_text(pod_err));snprintf(l2,c2,"%s",pod_err==NET_E_SWITCH || pod_err==NET_E_NOCONFIG || pod_err==NET_E_JOIN?"Check Wi-Fi, then press Circle and try again":"Press Circle and try again");return 1;}
 if(pod_view==PV_SAVED){snprintf(l1,c1,"No saved episodes");snprintf(l2,c2,"Hold X on an episode to save it for offline listening");return 1;}
 if(pod_view==PV_SHOW){snprintf(l1,c1,"No episodes found");snprintf(l2,c2,"The feed lists no audio ZOID can play");return 1;}
 snprintf(l1,c1,"No podcasts found");snprintf(l2,c2,"Try other words");return 1;
}

/* ---- hold X ---- */
static PodEp pod_menu_ep;static char pod_menu_dir[64];static int pod_menu_res;
/* the episode playing, from its show (or the list on screen) */
static int pod_find_active(PodEp *out) {
 char dir[64],url[400],file[96];if(!pod_episode_parts(active,dir,sizeof(dir),url,sizeof(url),file,sizeof(file)))return 0;
 for(int k=0;k<pod_n;k++)if(!strcmp(pod_eps[k].dir,dir) && pod_ep_matches(&pod_eps[k],url,file)){*out=pod_eps[k];return 1;}
 if(!dir[0])return 0;
 PodShow s;plock();int ok=show_load(dir,&s,1);punlock();if(!ok)return 0;
 int found=0;for(int i=0;i<s.n && !found;i++)if(pod_ep_matches(&s.eps[i],url,file)){*out=s.eps[i];found=1;}
 free(s.eps);return found;
}
static void pod_ep_menu_items(const PodEp *e) {
 int q=dl_index(e->dir,e->key)>=0;
 if(q)menu_add("Stop saving",A_POD_CANCEL_DL);
 else if(e->file[0])menu_add("Delete saved copy",A_POD_DELETE);
 else if(pod_playable(e->ext))menu_add("Save for offline",A_POD_SAVE);
 menu_add(e->played?"Mark as unplayed":"Mark as played",e->played?A_POD_UNPLAYED:A_POD_PLAYED);
}
static void pod_item_menu(void) {
 menu_target_index=-1;
 if(screen==SCR_NOW) {
  if(!pod_find_active(&pod_menu_ep)){menu_begin(np_title[0]?np_title:"This episode");menu_add("Cancel",A_CANCEL);return;}
  menu_begin(pod_menu_ep.title);pod_ep_menu_items(&pod_menu_ep);menu_add("Cancel",A_CANCEL);return;
 }
 Entry *e=&entries[cursor];int k=cursor-pod_first;
 if(!e->folder && k>=0 && k<pod_n){pod_menu_ep=pod_eps[k];menu_target_index=cursor;menu_begin(pod_menu_ep.title);menu_add("Play",A_PLAY);pod_ep_menu_items(&pod_menu_ep);menu_add("Cancel",A_CANCEL);}
 else if(e->folder==25 && k>=0 && k<pod_sub_n){snprintf(pod_menu_dir,sizeof(pod_menu_dir),"%s",pod_subs[k].dir);menu_begin(pod_subs[k].title);menu_add("Load new episodes",A_POD_REFRESH);menu_add("Unsubscribe",A_POD_UNSUB);menu_add("Cancel",A_CANCEL);}
 else if(e->folder==26 && k>=0 && k<pod_res_n){pod_menu_res=k;menu_begin(pod_res[k].title);menu_add("Subscribe",A_POD_SUB);menu_add("Cancel",A_CANCEL);}
}
static int pod_menu_ok(int i){int f=entries[i].folder;return f==0 || f==25 || f==26;}
static void pod_menu_run(int act) {
 PodEp *live=NULL;for(int k=0;k<pod_n;k++)if(pod_eps[k].key==pod_menu_ep.key && !strcmp(pod_eps[k].dir,pod_menu_ep.dir))live=&pod_eps[k];
 switch(act) {
 case A_POD_SAVE:if(pod_save_ep(live?live:&pod_menu_ep))notice("Saving for offline listening");break;
 case A_POD_CANCEL_DL:pod_cancel_dl(pod_menu_ep.dir,pod_menu_ep.key);break;
 case A_POD_DELETE:menu_begin("Delete the saved copy?");menu_add("Cancel",A_CANCEL);menu_add("Delete saved copy",A_POD_DELETE_YES);break;
 case A_POD_DELETE_YES:pod_delete_ep(pod_menu_ep.dir,pod_menu_ep.key,pod_menu_ep.file);break;
 case A_POD_PLAYED:case A_POD_UNPLAYED:pod_ep_update(pod_menu_ep.dir,pod_menu_ep.key,act==A_POD_PLAYED,0,NULL);notice(act==A_POD_PLAYED?"Marked as played":"Marked as unplayed");break;
 case A_POD_REFRESH:pod_post(4,pod_menu_dir,0);break;
 case A_POD_SUB:if(in_pod && pod_view==PV_RESULTS && pod_menu_res<count){cursor=pod_menu_res;pod_auto_sub=1;pod_activate(pod_menu_res);}break;
 case A_POD_UNSUB: {
  int saved=0;char title[96]="";for(int i=0;i<pod_sub_n;i++)if(!strcmp(pod_subs[i].dir,pod_menu_dir)){saved=pod_subs[i].saved;snprintf(title,sizeof(title),"%s",pod_subs[i].title);}
  char t[128];snprintf(t,sizeof(t),"Unsubscribe from \"%.80s\"?",title);menu_begin(t);
  if(saved){char l[64];snprintf(l,sizeof(l),"Unsubscribe, delete %d saved",saved);menu_warn=1;menu_add("Cancel",A_CANCEL);menu_add(l,A_POD_UNSUB_YES);}
  else{menu_add("Unsubscribe",A_POD_UNSUB_YES);menu_add("Cancel",A_CANCEL);}
  break;
 }
 case A_POD_UNSUB_YES:pod_unsubscribe(pod_menu_dir);break;
 }
}

static void pod_ui_init(void) {
 pod_lock=sceKernelCreateSema("zoid_pod",0,1,1,NULL);pod_wake=sceKernelCreateSema("zoid_podwake",0,0,1,NULL);dl_wake=sceKernelCreateSema("zoid_dlwake",0,0,1,NULL);
 SceUID th=sceKernelCreateThread("zoid_pod",pod_thread,0x30,0x10000,0,NULL);if(th>=0)sceKernelStartThread(th,0,NULL);
 th=sceKernelCreateThread("zoid_dl",dl_thread,0x31,0x10000,0,NULL);if(th>=0)sceKernelStartThread(th,0,NULL);
 pod_scan_subs();
}
/* Now Playing for a saved episode, before its tags are read: the title and show from the list in memory */
static void pod_np_quick(const char *path) {
 char dir[64],url[8],file[96];if(radio_is_episode(path) || !pod_episode_parts(path,dir,sizeof(dir),url,sizeof(url),file,sizeof(file)))return;
 for(int k=0;k<pod_n;k++)if(!strcmp(pod_eps[k].dir,dir) && !strcmp(pod_eps[k].file,file)) {
  snprintf(np_title,sizeof(np_title),"%s",pod_eps[k].title);
  for(int s=0;s<pod_sub_n;s++)if(!strcmp(pod_subs[s].dir,dir))snprintf(np_artist,sizeof(np_artist),"%s",pod_subs[s].title);
  return;
 }
}
