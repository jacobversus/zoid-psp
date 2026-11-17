/* Lyrics for the song playing, read on the metadata thread.
   Sources, in order: a .lrc file with the song's name beside it, then an embedded ID3 USLT frame.
   Either may be synced LRC ("[01:23.45]words", several stamps per line allowed, [offset:ms])
   or plain text. Text folds to the ASCII font like the tags do. */
#define LYR_MAX 300
#define LYR_LEN 120
typedef struct {int ms;char text[LYR_LEN];} LyrLine;
typedef struct {int n,synced;LyrLine line[LYR_MAX];} Lyrics;
static Lyrics lyr_back,lyr;             /* lyr_back: handed over under meta_lock; lyr: the screen's copy */
static volatile int lyr_back_ready;static volatile unsigned int lyr_back_req;
static int lyr_state;                   /* 0 nothing playing, 1 loading, 2 ready, 3 none found */
static unsigned int lyr_gen;            /* bumps when new lyrics arrive */

/* "mm:ss", "mm:ss.xx" or "mm:ss:xx" between a and end; -1 when it is not a time. */
static int lyr_stamp(const char *a,const char *end) {
 int mm=0,ss=0,frac=0,fd=0;const char *p=a;
 if(p>=end || !isdigit((unsigned char)*p))return -1;
 while(p<end && isdigit((unsigned char)*p))mm=mm*10+(*p++-'0');
 if(p>=end || *p!=':')return -1;p++;
 if(p>=end || !isdigit((unsigned char)*p))return -1;
 while(p<end && isdigit((unsigned char)*p))ss=ss*10+(*p++-'0');
 if(p<end && (*p=='.' || *p==':')){p++;while(p<end && isdigit((unsigned char)*p)){if(fd<3){frac=frac*10+(*p-'0');fd++;}p++;}}
 if(p!=end)return -1;
 while(fd<3){frac*=10;fd++;}
 return (mm*60+ss)*1000+frac;
}
static int lyr_cmp(const void *a,const void *b){return ((const LyrLine *)a)->ms-((const LyrLine *)b)->ms;}
/* Parses LRC or plain text (ASCII with \n line breaks). */
static void lyrics_parse(const char *src,Lyrics *L) {
 L->n=0;L->synced=0;int offset=0;
 static LyrLine plain[LYR_MAX];int np=0;
 for(const char *p=src;*p;) {
  const char *e=strchr(p,'\n');int len=e?(int)(e-p):(int)strlen(p);
  char line[512];if(len>511)len=511;memcpy(line,p,len);line[len]=0;
  p=e?e+1:p+len;
  char *s=line;int stamps[16],ns=0,meta=0;
  while(*s==' ')s++;
  while(*s=='[') {
   char *close=strchr(s,']');if(!close)break;
   int ms=lyr_stamp(s+1,close);
   if(ms>=0){if(ns<16)stamps[ns++]=ms;}
   else{if(!strncasecmp(s+1,"offset:",7))offset=atoi(s+8);meta=1;}
   s=close+1;while(*s==' ')s++;
  }
  int n=strlen(s);while(n && s[n-1]==' ')s[--n]=0;
  if(ns) {
   L->synced=1;
   for(int k=0;k<ns && L->n<LYR_MAX;k++){LyrLine *l=&L->line[L->n++];l->ms=stamps[k]-offset;if(l->ms<0)l->ms=0;snprintf(l->text,LYR_LEN,"%s",s);}
  }else if(!meta && np<LYR_MAX){plain[np].ms=-1;snprintf(plain[np].text,LYR_LEN,"%s",s);np++;}
 }
 if(L->synced){qsort(L->line,L->n,sizeof(LyrLine),lyr_cmp);return;}
 /* plain text: no blank lines at either end */
 int a=0,b=np;while(a<b && !plain[a].text[0])a++;while(b>a && !plain[b-1].text[0])b--;
 for(int i=a;i<b;i++)L->line[L->n++]=plain[i];
}
/* Reads a whole small file; NULL when missing. */
static unsigned char *lyr_read_file(const char *path,int *len) {
 int fd=sceIoOpen(path,PSP_O_RDONLY,0);if(fd<0)return NULL;
 int size=sceIoLseek32(fd,0,PSP_SEEK_END);sceIoLseek32(fd,0,PSP_SEEK_SET);
 if(size<=0 || size>96*1024){sceIoClose(fd);return NULL;}
 unsigned char *b=malloc(size+2);if(!b){sceIoClose(fd);return NULL;}
 int got=sceIoRead(fd,b,size);sceIoClose(fd);
 if(got<=0){free(b);return NULL;}
 b[got]=b[got+1]=0;*len=got;return b;
}
static void lyrics_load(const char *path,const Tags *t,Lyrics *L) {
 L->n=0;L->synced=0;
 int cap=96*1024;char *text=malloc(cap);if(!text)return;text[0]=0;
 /* a .lrc beside the song (same name), as UTF-8, UTF-16 or Latin-1 */
 char lrc[PATHLEN];snprintf(lrc,sizeof(lrc),"%s",path);
 char *dot=strrchr(lrc,'.'),*sl=strrchr(lrc,'/');
 if(dot && (!sl || dot>sl)) {
  const char *ext[2]={".lrc",".LRC"};
  for(int i=0;i<2 && !text[0];i++) {
   strcpy(dot,ext[i]);int len=0;unsigned char *raw=lyr_read_file(lrc,&len);if(!raw)continue;
   unsigned char *buf=malloc(len+3);
   if(buf) {
    int utf16=len>=2 && ((raw[0]==0xFF && raw[1]==0xFE) || (raw[0]==0xFE && raw[1]==0xFF));
    int skip=!utf16 && len>=3 && raw[0]==0xEF && raw[1]==0xBB && raw[2]==0xBF?3:0;
    /* plain bytes with no valid UTF-8 multibyte runs are treated as Latin-1 */
    int enc=utf16?1:3;
    if(!utf16)for(int k=skip;k<len;k++) {
     int c=raw[k];if(c<0x80)continue;
     int l=(c&0xE0)==0xC0?2:(c&0xF0)==0xE0?3:(c&0xF8)==0xF0?4:0,ok=l && k+l<=len;
     for(int j=1;ok && j<l;j++)if((raw[k+j]&0xC0)!=0x80)ok=0;
     if(!ok){enc=0;break;}
     k+=l-1;
    }
    buf[0]=enc;memcpy(buf+1,raw+skip,len-skip);buf[len-skip+1]=buf[len-skip+2]=0;
    tag_decode(buf,len-skip+1,text,cap,1);free(buf);
   }
   free(raw);
  }
 }
 /* the embedded lyrics frame: encoding, language, description, then the text */
 if(!text[0] && t->lyr_len>4 && t->lyr_len<=96*1024) {
  int fd=sceIoOpen(path,PSP_O_RDONLY,0);
  if(fd>=0) {
   unsigned char *f=malloc(t->lyr_len+2);
   if(f && sceIoLseek32(fd,t->lyr_off,PSP_SEEK_SET)>=0 && sceIoRead(fd,f,t->lyr_len)==t->lyr_len) {
    int enc=f[0],i=4,n=t->lyr_len;
    if(enc==1 || enc==2){while(i+1<n && (f[i] || f[i+1]))i+=2;i+=2;}else{while(i<n && f[i])i++;i++;}
    if(i<n){f[i-1]=enc;tag_decode(f+i-1,n-i+1,text,cap,1);}
   }
   free(f);sceIoClose(fd);
  }
 }
 if(text[0])lyrics_parse(text,L);
 free(text);
}
