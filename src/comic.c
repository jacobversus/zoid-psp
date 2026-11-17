/* Books: pages of a .cbz or .zip (both zips of images) or a folder of images. See comic.h.
   A zip is read from its central directory (the index at the end), never by walking the file,
   so opening a 200 MB volume reads about 40 KB. Entries are inflated with zlib. */
#include <pspkernel.h>
#include <pspiofilemgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <zlib.h>
#include "comic.h"

static unsigned int le16(const unsigned char *p){return p[0]|p[1]<<8;}
static unsigned int le32(const unsigned char *p){return p[0]|p[1]<<8|p[2]<<16|(unsigned int)p[3]<<24;}

int book_image_name(const char *name) {
 const char *dot=strrchr(name,'.');if(!dot)return 0;
 return !strcasecmp(dot,".jpg") || !strcasecmp(dot,".jpeg") || !strcasecmp(dot,".png");
}
/* macOS leaves "._name" copies and __MACOSX folders in archives and on the stick */
static int junk_name(const char *path) {
 const char *base=strrchr(path,'/');base=base?base+1:path;
 if(base[0]=='.')return 1;
 for(const char *p=path;*p;p++)if((p==path || p[-1]=='/') && !strncasecmp(p,"__MACOSX",8))return 1;
 return 0;
}
int book_natural_compare(const char *a,const char *b) {
 while(*a && *b) {
  if(isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
   while(*a=='0')a++;while(*b=='0')b++;
   const char *ea=a,*eb=b;while(isdigit((unsigned char)*ea))ea++;while(isdigit((unsigned char)*eb))eb++;
   if(ea-a!=eb-b)return (int)(ea-a)-(int)(eb-b);
   int c=strncmp(a,b,ea-a);if(c)return c;
   a=ea;b=eb;continue;
  }
  int ca=tolower((unsigned char)*a),cb=tolower((unsigned char)*b);
  if(ca!=cb)return ca-cb;
  a++;b++;
 }
 return (unsigned char)*a-(unsigned char)*b;
}

static const Book *sort_book;
static int page_cmp(const void *x,const void *y) {
 const BookPage *p=x,*q=y;
 return book_natural_compare(sort_book->names+p->name,sort_book->names+q->name);
}
static void sort_pages(Book *b){sort_book=b;qsort(b->pages,b->n,sizeof(BookPage),page_cmp);}

static int read_at(int fd,unsigned int off,void *buf,int len) {
 if(sceIoLseek32(fd,off,PSP_SEEK_SET)<0)return 0;
 int got=0;while(got<len){int k=sceIoRead(fd,(char *)buf+got,len-got);if(k<=0)return 0;got+=k;}
 return 1;
}

static int names_add(Book *b,const char *s,int len,int *cap) {
 if(b->names_len+len+1>*cap) {
  int nc=*cap?*cap*2:8192;while(nc<b->names_len+len+1)nc*=2;
  char *nn=realloc(b->names,nc);if(!nn)return -1;b->names=nn;*cap=nc;
 }
 int at=b->names_len;memcpy(b->names+at,s,len);b->names[at+len]=0;b->names_len+=len+1;return at;
}

static int zip_open(Book *b) {
 int fd=sceIoOpen(b->path,PSP_O_RDONLY,0);if(fd<0)return 0;
 int size=sceIoLseek32(fd,0,PSP_SEEK_END);
 if(size<22){sceIoClose(fd);return 0;}
 /* the end record sits in the last 22 bytes plus a comment of up to 64 KB */
 int tail=size<65557?size:65557;unsigned char *t=malloc(tail);
 if(!t || !read_at(fd,size-tail,t,tail)){free(t);sceIoClose(fd);return 0;}
 int eocd=-1;for(int i=tail-22;i>=0;i--)if(t[i]=='P' && t[i+1]=='K' && t[i+2]==5 && t[i+3]==6){eocd=i;break;}
 if(eocd<0){free(t);sceIoClose(fd);return 0;}
 unsigned int entries=le16(t+eocd+10),cd_size=le32(t+eocd+12),cd_off=le32(t+eocd+16);free(t);
 if(!entries || cd_size>8*1024*1024 || cd_off+cd_size>(unsigned int)size){sceIoClose(fd);return 0;}
 unsigned char *cd=malloc(cd_size);
 if(!cd || !read_at(fd,cd_off,cd,cd_size)){free(cd);sceIoClose(fd);return 0;}
 b->pages=malloc(entries*sizeof(BookPage));int cap=0;
 if(!b->pages){free(cd);sceIoClose(fd);return 0;}
 unsigned int p=0;
 for(unsigned int i=0;i<entries && p+46<=cd_size;i++) {
  const unsigned char *e=cd+p;if(le32(e)!=0x02014b50)break;
  unsigned int method=le16(e+10),csize=le32(e+20),usize=le32(e+24),nl=le16(e+28),xl=le16(e+30),cl=le16(e+32),off=le32(e+42);
  if(p+46+nl>cd_size)break;
  char name[512];int n=nl<sizeof(name)-1?nl:sizeof(name)-1;memcpy(name,e+46,n);name[n]=0;
  p+=46+nl+xl+cl;
  if(!n || name[n-1]=='/' || junk_name(name) || !book_image_name(name))continue;
  if(method!=0 && method!=8)continue;
  if(csize==0xFFFFFFFF || usize==0xFFFFFFFF || !usize)continue; /* zip64: not for a single page */
  int at=names_add(b,name,n,&cap);if(at<0)break;
  BookPage *pg=&b->pages[b->n++];pg->off=off;pg->csize=csize;pg->size=usize;pg->method=method;pg->name=at;
 }
 free(cd);
 if(!b->n){sceIoClose(fd);return 0;}
 b->fd=fd;b->is_zip=1;sort_pages(b);return 1;
}

/* ---- names beyond plain ASCII ---- */
char book_names_log[512];
char book_short_name[16];
int book_odd_name;
static int names_logged;
int book_entry_archive(const char *show){return book_archive_name(show) || (book_short_name[0] && book_archive_name(book_short_name));}
static int odd_name(const char *s){for(;*s;s++)if((unsigned char)*s>=0x80 || *s=='?')return 1;return 0;}
static void log_name(const char *dir,const SceIoDirent *e,const SceIoFatDirentPrivate *pv,const char *used,int long_ok) {
 if(!book_names_log[0] || names_logged>=60)return;
 FILE *f=fopen(book_names_log,names_logged?"a":"w");if(!f)return;
 names_logged++;
 fprintf(f,"in %s\n  d_name:",dir);for(const unsigned char *p=(const unsigned char *)e->d_name;*p;p++)fprintf(f," %02X",*p);
 fprintf(f,"\n  short:");for(const unsigned char *p=(const unsigned char *)pv->s_name;*p;p++)fprintf(f," %02X",*p);
 fprintf(f," (%s)  long opens: %s  opened by: %s\n",pv->s_name,long_ok?"yes":"no",used);
 fclose(f);
}
int book_entry_opens(const char *dir,const char *name,int isdir) {
 char p[1024];snprintf(p,sizeof(p),"%s/%s",dir,name);
 if(isdir){int d=sceIoDopen(p);if(d<0)return 0;sceIoDclose(d);return 1;}
 SceIoStat st;return sceIoGetstat(p,&st)>=0;
}
int book_dread(int d,const char *dir,SceIoDirent *e,char *name,int ncap,char *show,int scap) {
 static SceIoFatDirentPrivate pv;
 memset(e,0,sizeof(*e));memset(&pv,0,sizeof(pv));pv.size=sizeof(pv);e->d_private=&pv;
 int r=sceIoDread(d,e);
 e->d_private=NULL;
 book_short_name[0]=0;book_odd_name=0;
 if(r<=0){name[0]=show[0]=0;return r;}
 snprintf(book_short_name,sizeof(book_short_name),"%s",pv.s_name);
 book_odd_name=odd_name(e->d_name);
 /* d_name is the long name; only s_name is taken from d_private (PPSSPP lays out the rest differently) */
 const char *lng=e->d_name;
 snprintf(name,ncap,"%s",e->d_name);
 /* with dir given: make sure the entry opens by the name it will be opened by, whatever letters it
    has (a PSP may hand back a name it can't open again, without any sign of it in the name) */
 if(dir && strcmp(e->d_name,".") && strcmp(e->d_name,"..")) {
  int isdir=FIO_S_ISDIR(e->d_stat.st_mode);
  int long_ok=book_entry_opens(dir,e->d_name,isdir);
  if(!long_ok && pv.s_name[0] && book_entry_opens(dir,pv.s_name,isdir))snprintf(name,ncap,"%s",pv.s_name);
  if(!long_ok || book_odd_name)log_name(dir,e,&pv,name,long_ok);
 }
 /* the screen: '?' is no letter a name can hold on FAT, so it stands for one the PSP couldn't give */
 int o=0;
 for(const char *c=lng;*c && o<scap-4;c++){if(*c=='?'){show[o++]=(char)0xE2;show[o++]=(char)0x96;show[o++]=(char)0xA1;}else show[o++]=*c;}
 show[o]=0;
 return r;
}
int book_folder_title(const char *dir,char *out,int cap) {
 char p[1024];snprintf(p,sizeof(p),"%s/zoid-title.txt",dir);
 FILE *f=fopen(p,"r");if(!f)return 0;
 char line[256];int ok=fgets(line,sizeof(line),f)!=NULL;fclose(f);
 if(!ok)return 0;
 char *s=line;if((unsigned char)s[0]==0xEF && (unsigned char)s[1]==0xBB && (unsigned char)s[2]==0xBF)s+=3; /* UTF-8 BOM */
 int n=strlen(s);while(n && (s[n-1]=='\n' || s[n-1]=='\r' || s[n-1]==' '))s[--n]=0;
 if(!n)return 0;
 snprintf(out,cap,"%s",s);return 1;
}
/* each folder named by its zoid-title.txt; a short 8.3 name (it has '~') looked up as its long one */
void book_show_path(const char *path,char *out,int cap) {
 char dir[1024]="";int o=0;const char *p=path;
 /* the device and first folder ("ms0:/BOOKS") stay as they are */
 const char *first=strchr(path,'/');if(first){const char *second=strchr(first+1,'/');p=second?second:path+strlen(path);}
 o=snprintf(out,cap,"%.*s",(int)(p-path),path);snprintf(dir,sizeof(dir),"%.*s",(int)(p-path),path);
 while(*p=='/' && o<cap-1) {
  p++;const char *end=strchr(p,'/');if(!end)end=p+strlen(p);
  char part[256];snprintf(part,sizeof(part),"%.*s",(int)(end-p),p);
  char shown[512];snprintf(shown,sizeof(shown),"%s",part);
  if(strchr(part,'~')) {
   int d=sceIoDopen(dir);
   if(d>=0){SceIoDirent e;char nm[256],sh[512];while(book_dread(d,dir,&e,nm,sizeof(nm),sh,sizeof(sh))>0)if(!strcmp(nm,part)){snprintf(shown,sizeof(shown),"%s",sh);break;}sceIoDclose(d);}
  }
  size_t dl=strlen(dir);snprintf(dir+dl,sizeof(dir)-dl,"/%s",part);
  if(*end=='/' || !book_archive_name(part)) { /* a folder: its title, if it has one; a '/' in it ("1/2") travels as BOOK_SLASH */
   if(book_folder_title(dir,shown,sizeof(shown)))for(char *c=shown;*c;c++)if(*c=='/')*c=BOOK_SLASH;
  }
  o+=snprintf(out+o,cap-o,"/%s",shown);if(o>=cap)o=cap-1;
  p=end;
 }
}

static int folder_open(Book *b) {
 int d=sceIoDopen(b->path);if(d<0)return 0;
 int cap=0,pcap=0;SceIoDirent e;char nm[256],sh[512];
 while(book_dread(d,b->path,&e,nm,sizeof(nm),sh,sizeof(sh))>0) {
  if(FIO_S_ISDIR(e.d_stat.st_mode) || junk_name(sh) || !book_image_name(sh) || e.d_stat.st_size<=0)continue;
  if(b->n>=pcap){int nc=pcap?pcap*2:256;BookPage *np=realloc(b->pages,nc*sizeof(BookPage));if(!np)break;b->pages=np;pcap=nc;}
  int at=names_add(b,nm,strlen(nm),&cap);if(at<0)break;
  BookPage *pg=&b->pages[b->n++];memset(pg,0,sizeof(*pg));pg->size=(unsigned int)e.d_stat.st_size;pg->name=at;
 }
 sceIoDclose(d);
 if(!b->n)return 0;
 sort_pages(b);return 1;
}

int book_archive_name(const char *p){int n=strlen(p);return n>4 && (!strcasecmp(p+n-4,".cbz") || !strcasecmp(p+n-4,".zip"));}
int book_open(Book *b,const char *path) {
 memset(b,0,sizeof(*b));b->fd=-1;
 strncpy(b->path,path,sizeof(b->path)-1);
 int ok=book_archive_name(path)?zip_open(b):folder_open(b);
 if(!ok){book_close(b);return 0;}
 return 1;
}
void book_close(Book *b) {
 if(b->fd>=0)sceIoClose(b->fd);
 free(b->pages);free(b->names);
 memset(b,0,sizeof(*b));b->fd=-1;
}
const char *book_page_name(const Book *b,int page){return page>=0 && page<b->n?b->names+b->pages[page].name:"";}
int book_reopen(Book *b) {
 if(!b->is_zip)return 1;
 if(b->fd>=0)sceIoClose(b->fd);
 b->fd=sceIoOpen(b->path,PSP_O_RDONLY,0);return b->fd>=0;
}

unsigned char *book_read(Book *b,int page,int *len,int max_bytes) {
 if(page<0 || page>=b->n)return NULL;
 BookPage *pg=&b->pages[page];
 if((int)pg->size>max_bytes || (b->is_zip && (int)pg->csize>max_bytes))return NULL;
 unsigned char *out=malloc(pg->size+16);if(!out)return NULL;
 if(!b->is_zip) {
  char full[1024];snprintf(full,sizeof(full),"%s/%s",b->path,b->names+pg->name);
  int fd=sceIoOpen(full,PSP_O_RDONLY,0);
  if(fd<0 || !read_at(fd,0,out,pg->size)){if(fd>=0)sceIoClose(fd);free(out);return NULL;}
  sceIoClose(fd);*len=pg->size;return out;
 }
 unsigned char lh[30];
 if(!read_at(b->fd,pg->off,lh,30) || le32(lh)!=0x04034b50){free(out);return NULL;}
 unsigned int data=pg->off+30+le16(lh+26)+le16(lh+28);
 if(pg->method==0) {
  if(pg->csize!=pg->size || !read_at(b->fd,data,out,pg->size)){free(out);return NULL;}
  *len=pg->size;return out;
 }
 unsigned char *in=malloc(pg->csize);
 if(!in || !read_at(b->fd,data,in,pg->csize)){free(in);free(out);return NULL;}
 z_stream z;memset(&z,0,sizeof(z));
 if(inflateInit2(&z,-MAX_WBITS)!=Z_OK){free(in);free(out);return NULL;}
 z.next_in=in;z.avail_in=pg->csize;z.next_out=out;z.avail_out=pg->size;
 int r=inflate(&z,Z_FINISH);int got=pg->size-z.avail_out;
 inflateEnd(&z);free(in);
 if((r!=Z_STREAM_END && r!=Z_OK && r!=Z_BUF_ERROR) || got<=0){free(out);return NULL;}
 *len=got;return out;
}
