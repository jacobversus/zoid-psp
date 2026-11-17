/* Books: manga and comics, in the theme picked in Settings. Under Paper it is the Manga Paper look
   (desktop ZOID's Paper theme: warm off-white paper, black serif type, red-pen accents); the other themes
   lend Books their colors, font, backdrop, panels and selection bar (bk_style below).

   BOOKS on the Memory Stick holds .cbz and .zip files and folders of images, in folders as deep
   as you like. The shelf browses them like a file explorer: folders first, then books; the reader shows one page at a time.

   Reading: pages fill the width of the screen. D-pad Up/Down (or the stick) scrolls the page,
   L and R turn pages: in a right-to-left book (manga, the default) L is the next page, as a printed
   manga turns; in a left-to-right book R is. SELECT switches direction. X zooms in (1.5x, 2x),
   Circle zooms out of those and at normal size goes back to the shelf. Square zooms out a step at a
   time (85%, 70%, 55%, then the whole page; once more, back to full width). Holding X or Square zooms
   smoothly to any size between, and every change of size glides.
   Triangle opens Bookmarks: bookmark the page (a red ribbon marks it), read upright (rd_vert), jump to this book's
   bookmarks, or see the controls. Every bookmark in every book is also listed under Bookmarks at
   the top of the shelf (zoid-bookmarks.txt). START plays or pauses music, which keeps playing while you read;
   held, it goes to the next song. At the last page, next page again opens the next volume in the
   same folder. On the shelf the music buttons set in Settings work (L and R change songs), and
   Triangle goes to Now Playing, whose Circle comes back to the same shelf row.

   Pages are decoded on a low-priority thread (zoid_books) straight to the size they are shown
   (page.c), so every page is drawn pixel for pixel. The page before and the two after are kept
   ready. Where you are in each book is saved to zoid-books.txt a few seconds after you stop on a
   page and whenever you leave a book. */
#include "comic.h"
#include "page.h"

/* Books wears the theme picked in Settings. Under Paper it is the Manga Paper look below, exactly;
   any other theme lends its colors (from its Theme), its font, its backdrop, and its own panels and
   selection bar (menu_panel and menu_row_sel in hud.h). bk_style() picks the palette. */
typedef struct {unsigned int bg,surround,panel,list,line,ink,muted,faint,red,sel,folder,sel_ink,sel_muted,badge,badge_ink;} BookPalette;
static BookPalette BP;
#define P_BG BP.bg
#define P_SURROUND BP.surround /* round a page smaller than the screen */
#define P_PANEL BP.panel
#define P_LIST BP.list
#define P_LINE BP.line
#define P_INK BP.ink
#define P_MUTED BP.muted
#define P_FAINT BP.faint
#define P_RED BP.red           /* the accent: Paper's red pen, each theme's accent elsewhere */
#define P_SEL BP.sel
#define P_FOLDER BP.folder
#define P_SEL_INK BP.sel_ink   /* words on the selection bar */
#define P_SEL_MUTED BP.sel_muted
static void bk_style(void) {
 if(PAPER){BP=(BookPalette){0xEFEBE3,0xE4E0D6,0xF6F3EC,0xFAF8F3,0xD3CCBE,0x1F1D1A,0x6E685E,0x9A9284,0xB83A2E,0xF0DCD3,0x8A7F6E,0x1F1D1A,0x6E685E,0x1F1D1A,0xFAF8F3};return;}
 BP.bg=T->bg;BP.surround=T->bg2;BP.panel=T->panel;BP.list=T->raised;BP.line=T->line;BP.ink=T->fg;BP.muted=T->dim;BP.faint=T->faint;
 BP.red=T->accent;BP.sel=T->select;BP.folder=T->folder;BP.sel_ink=T->hot;BP.sel_muted=mix(T->hot,T->select,110);
 BP.badge=MS1?0x1e2a62:T->panel;BP.badge_ink=MS1?0xffffff:T->fg;
}
/* the screen behind the shelf: the theme's own (its gradient, the Zoidstation night or waves...) */
static void bk_backdrop(void) {
 if(PAPER){rect(0,0,480,272,C(P_BG,255));return;}
 rect(0,0,480,272,C(T->bg,255));
 if(MODERN)grad(0,0,480,200,C(T->bg2,255),C(T->bg,255));
 else if(Y2K){grad(0,0,480,136,C(0x0b4f72,255),C(0x084563,255));grad(0,136,480,136,C(0x084563,255),C(0x063a55,255));}
 else if(MS)ms_background();
}
/* a panel over the page or the shelf (the controls, Bookmarks, a message) */
static void bk_panel(float x,float y,float w,float h){if(PAPER){rect(x,y,w,h,C(P_LIST,255));frame_rect(x,y,w,h,1,C(P_LINE,255));}else menu_panel(x,y,w,h);}
/* the selection bar: Paper's pale red with a red edge, or the theme's own */
static void bk_sel_bar(float x,float y,float w,float h){if(PAPER){rect(x,y,w,h,C(P_SEL,255));rect(x,y,3,h,C(P_RED,255));}else menu_row_sel(x,y,w,h);}

static char books_root[24]="ms0:/BOOKS";
static int bk_mode; /* 0 the shelf, 1 reading */
static int rd_vert; /* the reader held upright (see RW below) */

/* ---- where you are in each book (zoid-books.txt) ---- */
typedef struct {char path[PATHLEN];int page,pages,scroll,level,rtl;unsigned int stamp;} Mark; /* rtl: 1 right to left (manga), 0 left to right, -1 not set */
#define MARK_MAX 200
/* a saved zoom: a level (0-8, the enum in the reader below), or a smooth zoom as 1000 + its size in thousandths */
static int zoom_code_ok(int c){return (c>=0 && c<=8) || (c>=1050 && c<=3000);}
static Mark *marks;static int mark_n;static unsigned int mark_clock;
static char marks_file[PATHLEN];static unsigned long long marks_due;
static int mark_find(const char *path){for(int i=0;i<mark_n;i++)if(!strcmp(marks[i].path,path))return i;return -1;}
static void marks_load(void) {
 if(!marks)marks=calloc(MARK_MAX,sizeof(Mark));if(!marks)return;
 mark_n=0;mark_clock=0;FILE *f=fopen(marks_file,"r");if(!f)return;
 char line[PATHLEN+80];
 while(mark_n<MARK_MAX && fgets(line,sizeof(line),f)) {
  Mark m;memset(&m,0,sizeof(m));int at=0;
  m.rtl=-1;
  if(sscanf(line,"%d\t%d\t%d\t%d\t%u\t%d\t%n",&m.page,&m.pages,&m.scroll,&m.level,&m.stamp,&m.rtl,&at)<6 || !at) { /* lines saved before reading direction */
   at=0;m.rtl=-1;if(sscanf(line,"%d\t%d\t%d\t%d\t%u\t%n",&m.page,&m.pages,&m.scroll,&m.level,&m.stamp,&at)<5 || !at)continue;
  }
  if(m.rtl<-1 || m.rtl>1)m.rtl=-1;
  char *p=line+at;p[strcspn(p,"\r\n")]=0;if(!p[0])continue;
  snprintf(m.path,sizeof(m.path),"%s",p);
  if(!zoom_code_ok(m.level))m.level=1;if(m.scroll<0 || m.scroll>1000)m.scroll=0;
  marks[mark_n++]=m;if(m.stamp>mark_clock)mark_clock=m.stamp;
 }
 fclose(f);
}
static void marks_save(void) {
 marks_due=0;if(!marks)return;
 char tmp[PATHLEN+8];snprintf(tmp,sizeof(tmp),"%s.new",marks_file);
 FILE *f=fopen(tmp,"w");if(!f)return;
 int ok=1;for(int i=0;i<mark_n;i++)if(fprintf(f,"%d\t%d\t%d\t%d\t%u\t%d\t%s\n",marks[i].page,marks[i].pages,marks[i].scroll,marks[i].level,marks[i].stamp,marks[i].rtl,marks[i].path)<0)ok=0;
 if(fclose(f))ok=0;
 if(!ok){sceIoRemove(tmp);return;}
 sceIoRemove(marks_file);sceIoRename(tmp,marks_file);
}
static void mark_put(const char *path,int page,int pages,int scroll,int level,int rtl) {
 if(!marks)return;
 int i=mark_find(path);
 if(i<0) {
  if(mark_n<MARK_MAX)i=mark_n++;
  else{i=0;for(int k=1;k<mark_n;k++)if(marks[k].stamp<marks[i].stamp)i=k;} /* full: the one read longest ago goes */
  memset(&marks[i],0,sizeof(Mark));snprintf(marks[i].path,sizeof(marks[i].path),"%s",path);
 }
 Mark *m=&marks[i];
 if(m->page==page && m->pages==pages && m->scroll==scroll && m->level==level && m->rtl==rtl && m->stamp==mark_clock)return;
 m->page=page;m->pages=pages;m->scroll=scroll;m->level=level;m->rtl=rtl;m->stamp=++mark_clock;
 marks_due=now_ms+3000; /* written once you settle, not on every page */
}
static int mark_latest(void) {
 int best=-1;for(int i=0;i<mark_n;i++)if(best<0 || marks[i].stamp>marks[best].stamp)best=i;return best;
}

/* ---- bookmarks (zoid-bookmarks.txt): spots you chose to come back to, in any book, as many as you like.
   One a page; each keeps the scroll and zoom it was made at. ---- */
typedef struct {char path[PATHLEN];int page,scroll,level;unsigned int stamp;} Bmark;
#define BM_MAX 300
static Bmark *bms;static int bm_n;static unsigned int bm_clock;
static char bm_file[PATHLEN];
static void bm_load(void) {
 if(!bms)bms=calloc(BM_MAX,sizeof(Bmark));if(!bms)return;
 bm_n=0;bm_clock=0;FILE *f=fopen(bm_file,"r");if(!f)return;
 char line[PATHLEN+64];
 while(bm_n<BM_MAX && fgets(line,sizeof(line),f)) {
  Bmark b;memset(&b,0,sizeof(b));int at=0;
  if(sscanf(line,"%d\t%d\t%d\t%u\t%n",&b.page,&b.scroll,&b.level,&b.stamp,&at)<4 || !at)continue;
  char *p=line+at;p[strcspn(p,"\r\n")]=0;if(!p[0])continue;
  snprintf(b.path,sizeof(b.path),"%s",p);
  if(b.page<0)b.page=0;if(!zoom_code_ok(b.level))b.level=1;if(b.scroll<0 || b.scroll>1000)b.scroll=0;
  bms[bm_n++]=b;if(b.stamp>bm_clock)bm_clock=b.stamp;
 }
 fclose(f);
}
/* written at once: a bookmark is something you meant to keep */
static void bm_save(void) {
 if(!bms)return;
 char tmp[PATHLEN+8];snprintf(tmp,sizeof(tmp),"%s.new",bm_file);
 FILE *f=fopen(tmp,"w");if(!f)return;
 int ok=1;for(int i=0;i<bm_n;i++)if(fprintf(f,"%d\t%d\t%d\t%u\t%s\n",bms[i].page,bms[i].scroll,bms[i].level,bms[i].stamp,bms[i].path)<0)ok=0;
 if(fclose(f))ok=0;
 if(!ok){sceIoRemove(tmp);return;}
 sceIoRemove(bm_file);sceIoRename(tmp,bm_file);
}
static int bm_find(const char *path,int page){for(int i=0;i<bm_n;i++)if(bms[i].page==page && !strcmp(bms[i].path,path))return i;return -1;}
static void bm_del(int i){if(i<0 || i>=bm_n)return;memmove(&bms[i],&bms[i+1],(bm_n-i-1)*sizeof(Bmark));bm_n--;bm_save();}
static void bm_put(const char *path,int page,int scroll,int level) {
 if(!bms)return;
 int i=bm_find(path,page);
 if(i<0) {
  if(bm_n<BM_MAX)i=bm_n++;
  else{i=0;for(int k=1;k<bm_n;k++)if(bms[k].stamp<bms[i].stamp)i=k;} /* full: the oldest goes */
  memset(&bms[i],0,sizeof(Bmark));snprintf(bms[i].path,sizeof(bms[i].path),"%s",path);bms[i].page=page;
 }
 bms[i].scroll=scroll;bms[i].level=level;bms[i].stamp=++bm_clock;
 bm_save();
}

/* ---- names on the shelf ---- */
static int bk_junk(const char *n){return n[0]=='.' || !strncasecmp(n,"__MACOSX",8);}
/* a RAR book (.cbr or .rar, by its long name or its short one): listed, but the PSP can't open it */
static int bk_rar_name(const char *p){int n=strlen(p);return n>4 && (!strcasecmp(p+n-4,".cbr") || !strcasecmp(p+n-4,".rar"));}
static int bk_entry_rar(const char *show){return bk_rar_name(show) || (book_short_name[0] && bk_rar_name(book_short_name));}
/* "Gunsmith Cats - Burst 01 (Dark Horse 2007).cbz" in "Gunsmith Cats - Burst" -> "Volume 1",
   with "Dark Horse 2007" as its note. Bracketed parts become the note; a leading series name goes. */
static void bk_clean(const char *name,const char *series,char *title,int tcap,char *note,int ncap) {
 char s[256];snprintf(s,sizeof(s),"%s",name);
 if(book_archive_name(s) || bk_rar_name(s))s[strlen(s)-4]=0;
 note[0]=0;
 char out[256];int o=0;
 for(int i=0;s[i] && o<(int)sizeof(out)-1;) {
  if(s[i]=='(' || s[i]=='[') {
   char close=s[i]=='('?')':']';int j=i+1;while(s[j] && s[j]!=close)j++;
   int len=j-i-1;
   if(len>0){int nl=strlen(note);if(nl && nl<ncap-3){strcat(note,", ");nl+=2;}if(nl+len<ncap){memcpy(note+nl,s+i+1,len);note[nl+len]=0;}}
   i=s[j]?j+1:j;continue;
  }
  if(s[i]=='_'){out[o++]=' ';i++;continue;}
  out[o++]=s[i++];
 }
 out[o]=0;
 char *t=out;
 int sl=series?strlen(series):0;
 if(sl && !strncasecmp(t,series,sl) && t[sl]){char *r=t+sl;while(*r==' ' || *r=='-' || *r=='.' || *r==',')r++;if(*r)t=r;}
 while(*t==' ')t++;
 int n=strlen(t);while(n && (t[n-1]==' ' || t[n-1]=='-'))t[--n]=0;
 /* collapse doubled spaces */
 {int w=0;for(int r=0;t[r];r++)if(!(t[r]==' ' && w && t[w-1]==' '))t[w++]=t[r];t[w]=0;}
 /* "01", "v01", "Vol. 1", "Volume 01" -> "Volume 1" */
 const char *d=t;
 if(!strncasecmp(d,"volume",6))d+=6;else if(!strncasecmp(d,"vol",3))d+=3;else if((d[0]=='v' || d[0]=='V') && isdigit((unsigned char)d[1]))d+=1;
 while(*d=='.' || *d==' ')d++;
 if(isdigit((unsigned char)*d)) {
  const char *e=d;while(isdigit((unsigned char)*e))e++;
  const char *rest=e;while(*rest==' ')rest++;
  if(!*rest || d!=t) {
   int num=atoi(d);
   if(*rest)snprintf(title,tcap,"Volume %d %s",num,rest);else snprintf(title,tcap,"Volume %d",num);
   return;
  }
 }
 snprintf(title,tcap,"%s",t[0]?t:name);
}

/* ---- the shelf ---- */
enum {SH_SERIES=1,SH_BOOK,SH_RESUME,SH_BMARKS,SH_BMARK}; /* BMARKS: the Bookmarks row at the top; BMARK: one bookmark */
/* name: what it is opened by (a short 8.3 name when its long one won't open, see book_dread); show: its long name */
typedef struct {char name[200];char show[200];char title[128];char note[96];int kind,count,bm,rar;} Shelf; /* rar: a .cbr, listed so its folder isn't empty, though the PSP can't open it */
/* a path as it reads on screen: folders by their zoid-title.txt, short names as long ones (book_show_path), remembered */
static const char *bk_shown(const char *path) {
 static char from[8][PATHLEN],to[8][PATHLEN];static int next;
 if(!strncmp(path,books_root,strlen(books_root)) && !path[strlen(books_root)])return path;
 for(int i=0;i<8;i++)if(!strcmp(from[i],path))return to[i];
 int i=next;next=(next+1)%8;
 snprintf(from[i],PATHLEN,"%s",path);book_show_path(path,to[i],PATHLEN);return to[i];
}
static Shelf *sh;static int sh_n,sh_cap,sh_sel,sh_top;static float sh_scroll,sh_sel_f; /* sh_top: the first row in view; sh_scroll and sh_sel_f glide after it and sh_sel */
/* a new list: the chosen row a little way down, without gliding there */
static void sh_jump(void){sh_top=sh_sel>2?sh_sel-2:0;sh_scroll=sh_top;sh_sel_f=sh_sel;}static int sh_bm; /* 1 while the shelf shows the bookmarks list */
static char bk_dir[PATHLEN],sh_resume[PATHLEN];static int bk_dir_failed;
static char bk_toast[96];static unsigned long long bk_toast_until;
/* a message at the foot of the screen, cut short with "..." so it never runs off the screen */
static void bk_say_for(const char *s,int ms) {
 snprintf(bk_toast,sizeof(bk_toast),"%s",s);
 int n=strlen(bk_toast);
 int room=bk_mode==1 && rd_vert?236:430; /* upright, the reader is 272 wide */
 if(text_w(bk_toast,F_SMALL)>room){while(n>4){n--;while(n>0 && (bk_toast[n]&0xC0)==0x80)n--;strcpy(bk_toast+n,"...");if(text_w(bk_toast,F_SMALL)<=room)break;}}
 bk_toast_until=now_ms+ms;
}
static void bk_say(const char *s){bk_say_for(s,1600);}

/* ---- music while you read ---- */
static char bk_song_was[PATHLEN];static unsigned long long bk_song_at;
static void bk_say_song(void){char b[180];snprintf(b,sizeof(b),"Playing: %s",np_title[0]?np_title:"music");bk_say_for(b,2200);}
static void bk_play_pause(void) {
 if(!active[0]){bk_say("No music yet: start some from Music on Home");return;}
 int was=is_playing();toggle_play();
 if(was)bk_say("Music paused");else bk_say_song();
}
/* previous / next: the song changes when the player gets there, and its name is shown then */
static void bk_skip(int dir) {
 if(!active[0]){bk_say("No music yet: start some from Music on Home");return;}
 if(skipping()){if(dir<0)skip_back();else skip_fwd();bk_say(dir<0?"Back 15 seconds":"Forward 30 seconds");return;}
 int restart=dir<0 && elapsed>3 && (!radio_is_url(active) || radio_is_episode(active)); /* as queue_previous does */
 snprintf(bk_song_was,sizeof(bk_song_was),"%s",active);bk_song_at=restart?0:now_ms;
 if(dir<0)skip_back();else skip_fwd();
 bk_say(restart?"Back to the start of the song":dir<0?"Previous song":"Next song");
}
static void bk_song_tick(void) {
 if(!bk_song_at)return;
 if(now_ms-bk_song_at>6000){bk_song_at=0;return;}
 if(np_path[0] && strcmp(np_path,bk_song_was) && (!np_loading || now_ms-bk_song_at>1500)){bk_song_at=0;bk_say_song();}
}
/* the music buttons set in Settings (START, L, R, Square) work on the shelf; the rest are left for outside Books */
static void books_leave(int to);
static void bk_shelf_action(int a) {
 static const char *rep[3]={"Repeat off","Repeat one","Repeat all"};
 switch(a) {
 case 0:bk_play_pause();break;
 case 1:bk_skip(-1);break;
 case 2:bk_skip(1);break;
 case 3:if(active[0]){request(NULL);bk_say("Music stopped");}break;
 case 4:queue_toggle_shuffle();bk_say(shuffle?"Shuffle on":"Shuffle off");break;
 case 5:queue_cycle_repeat();bk_say(rep[repeat_mode%3]);break;
 case 7:if(active[0])seek_by(-1);break;
 case 8:if(active[0])seek_by(1);break;
 case 10:if(active[0])books_leave(SCR_NOW);break;
 case 11:screen_off=1;break;
 }
}
/* A folder, as a file explorer shows it. One that holds only pictures is a book (its pages);
   anything else is a folder you open: one with books (a series), with more folders, or empty. */
static int bk_dir_kind(const char *path,int *folders,int *books,int *images) {
 *folders=*books=*images=0;
#ifdef ZOID_BOOKS_TEST
 if(strstr(path,"/Unopenable"))return 0; /* the test's stand-in for a folder a PSP won't open */
#endif
 int d=sceIoDopen(path);if(d<0)return 0;
 SceIoDirent e;char nm[256],sh[256];
 while(book_dread(d,NULL,&e,nm,sizeof(nm),sh,sizeof(sh))>0) { /* only counting: no need to check each opens */
  if(!bk_junk(sh)) {
   if(FIO_S_ISDIR(e.d_stat.st_mode)){if(strcmp(sh,".") && strcmp(sh,".."))(*folders)++;}
   else if(book_entry_archive(sh) || bk_entry_rar(sh))(*books)++; /* a .cbr still counts: the folder isn't empty */
   else if(book_image_name(sh))(*images)++;
  }
 }
 sceIoDclose(d);
 return !*folders && !*books && *images?SH_BOOK:SH_SERIES;
}
/* "2 folders, 31 books" */
static void bk_folder_note(int folders,int books,char *out,int cap) {
 char f[24]="",b[24]="";
 if(folders)snprintf(f,sizeof(f),"%d folder%s",folders,folders==1?"":"s");
 if(books)snprintf(b,sizeof(b),"%d book%s",books,books==1?"":"s");
 if(f[0] && b[0])snprintf(out,cap,"%s, %s",f,b);else if(f[0] || b[0])snprintf(out,cap,"%s",f[0]?f:b);else snprintf(out,cap,"Empty");
}
/* Continue reading and Bookmarks at the top, then folders, then books, each in natural order */
static int sh_rank(int kind){return kind==SH_RESUME?0:kind==SH_BMARKS?1:kind==SH_SERIES?2:3;}
static int sh_cmp(const void *a,const void *b) {
 const Shelf *x=a,*y=b;
 if(sh_rank(x->kind)!=sh_rank(y->kind))return sh_rank(x->kind)-sh_rank(y->kind);
 return book_natural_compare(x->show,y->show);
}
static Shelf *sh_add(void) {
 if(sh_n>=sh_cap){int nc=sh_cap?sh_cap*2:64;Shelf *ns=realloc(sh,nc*sizeof(Shelf));if(!ns)return NULL;sh=ns;sh_cap=nc;}
 Shelf *s=&sh[sh_n++];memset(s,0,sizeof(*s));return s;
}
static int bk_at_root(void){return !strcmp(bk_dir,books_root);}
static const char *bk_base(const char *p){const char *s=strrchr(p,'/');return s?s+1:p;}
/* a book named the way it reads away from its folder: "Gunsmith Cats - Burst: Volume 4" */
static void bk_full_title(const char *path,char *out,int cap) {
 /* the volume is cleaned by its folder's own name; the folder is shown by its title */
 char dir[PATHLEN],sdir[PATHLEN];snprintf(dir,sizeof(dir),"%s",path);char *sl=strrchr(dir,'/');if(sl)*sl=0;
 const char *shown=bk_shown(path);snprintf(sdir,sizeof(sdir),"%s",shown);sl=strrchr(sdir,'/');if(sl)*sl=0;
 int top=!strcmp(dir,books_root);
 char t[128],note[96];bk_clean(bk_base(shown),top?NULL:bk_base(dir),t,sizeof(t),note,sizeof(note));
 if(!top)snprintf(out,cap,"%s: %s",bk_base(sdir),t);else snprintf(out,cap,"%s",t);
 book_unslash(out);
}
/* the bookmarks list: every bookmark in every book, the newest first */
static int bm_cmp_new(const void *a,const void *b){const Shelf *x=a,*y=b;return (int)bms[y->bm].stamp-(int)bms[x->bm].stamp;}
static void bm_scan(int select) {
 sh_n=0;sh_sel=0;sh_scroll=0;
 for(int i=0;i<bm_n;i++) {
  Shelf *s=sh_add();if(!s)break;
  s->kind=SH_BMARK;s->bm=i;bk_full_title(bms[i].path,s->title,sizeof(s->title));
  snprintf(s->name,sizeof(s->name),"%s",bk_base(bms[i].path));
  const char *shown=bk_shown(bms[i].path);snprintf(s->show,sizeof(s->show),"%s",bk_base(shown));
  /* where it is: the folders below BOOKS */
  const char *rel=shown+strlen(books_root);while(*rel=='/')rel++;
  const char *last=strrchr(rel,'/');
  int o=0;
  for(const char *c=rel;last && c<last && o<(int)sizeof(s->note)-4;c++){if(*c=='/'){s->note[o++]=' ';s->note[o++]='/';s->note[o++]=' ';}else s->note[o++]=*c;}
  s->note[o]=0;book_unslash(s->note);
 }
 qsort(sh,sh_n,sizeof(Shelf),bm_cmp_new);
 sh_sel=select<0?0:select>=sh_n?sh_n-1:select;if(sh_sel<0)sh_sel=0;
 sh_jump();
}
static void shelf_scan(const char *select) {
 if(sh_bm){bm_scan(0);if(select)for(int i=0;i<sh_n;i++)if(!strcmp(sh[i].name,select)){sh_sel=i;break;}sh_jump();return;}
 sh_n=0;sh_sel=0;sh_scroll=0;
 char series_buf[256];snprintf(series_buf,sizeof(series_buf),"%s",bk_base(bk_dir)); /* volumes lose the folder's own name */
 const char *series=bk_at_root()?NULL:series_buf;
 /* at the top: the book read last, if it is still there, and your bookmarks */
 if(bk_at_root()) {
  int m=mark_latest();SceIoStat st;
  if(m>=0 && sceIoGetstat(marks[m].path,&st)>=0) {
   Shelf *s=sh_add();
   if(s){s->kind=SH_RESUME;snprintf(sh_resume,sizeof(sh_resume),"%s",marks[m].path);
    bk_full_title(marks[m].path,s->title,sizeof(s->title));
    snprintf(s->name,sizeof(s->name),"%s",bk_base(marks[m].path));snprintf(s->show,sizeof(s->show),"%s",s->title);}
  }
  if(bm_n) {
   Shelf *s=sh_add();
   if(s){s->kind=SH_BMARKS;snprintf(s->title,sizeof(s->title),"Bookmarks");s->count=bm_n;
    snprintf(s->note,sizeof(s->note),"%d bookmark%s",bm_n,bm_n==1?"":"s");}
  }
 }
 int d=sceIoDopen(bk_dir);bk_dir_failed=d<0;
 if(d>=0) {
  SceIoDirent e;char name[256],show[256];
  while(book_dread(d,bk_dir,&e,name,sizeof(name),show,sizeof(show))>0) {
   int isdir=FIO_S_ISDIR(e.d_stat.st_mode);
   if(bk_junk(show) || !strcmp(show,".") || !strcmp(show,".."))continue;
   int kind=0,folders=0,books=0,images=0;
   /* Like a file explorer: every folder is listed, even one that won't open, and every book file
      by its long name's type or its short name's (a name the PSP mangles still has its .CBZ) */
   int unopened=0;
   if(isdir){char p[PATHLEN];snprintf(p,sizeof(p),"%s/%s",bk_dir,name);kind=bk_dir_kind(p,&folders,&books,&images);if(!kind){kind=SH_SERIES;unopened=1;}}
   else if(book_entry_archive(show))kind=SH_BOOK;
   int rar=!kind && !isdir && bk_entry_rar(show);if(rar)kind=SH_BOOK;
   if(!kind)continue;
   Shelf *s=sh_add();if(!s)break;
   s->kind=kind;s->rar=rar;s->count=kind==SH_SERIES?folders+books:images;snprintf(s->name,sizeof(s->name),"%s",name);snprintf(s->show,sizeof(s->show),"%s",show);
   if(kind==SH_SERIES){snprintf(s->title,sizeof(s->title),"%s",show);
    {char p[PATHLEN];snprintf(p,sizeof(p),"%s/%s",bk_dir,name);if(book_folder_title(p,s->title,sizeof(s->title)))snprintf(s->show,sizeof(s->show),"%s",s->title);}if(unopened)snprintf(s->note,sizeof(s->note),"Couldn't open");else bk_folder_note(folders,books,s->note,sizeof(s->note));}
   else {
    bk_clean(show,series,s->title,sizeof(s->title),s->note,sizeof(s->note));
    if(isdir && !s->note[0])snprintf(s->note,sizeof(s->note),"%d picture%s",images,images==1?"":"s");
   }
  }
  sceIoDclose(d);
 }
 qsort(sh,sh_n,sizeof(Shelf),sh_cmp);
 if(select)for(int i=0;i<sh_n;i++)if(sh[i].kind!=SH_RESUME && !strcmp(sh[i].name,select)){sh_sel=i;break;}
 sh_jump();
}

/* ---- the reader: pages cached at the size they are shown ---- */
/* Held sideways (the usual way) or upright: the PSP turned a quarter to the left, D-pad at the bottom,
   so a page fills a 272 x 480 screen (portrait_begin in gfx.h). Remembered in zoid-books-view.txt. */
static char view_file[PATHLEN];
#define RW (rd_vert?272:480) /* the reader's screen, as you hold it */
#define RH (rd_vert?480:272)
/* whole page, full width, then the zooms: sideways 1.5x and 2x; upright four shorter steps, 1.25x to 2x.
   Below full width, three steps out: 85%, 70%, 55% (any narrower than the whole page are skipped).
   Holding X or Square zooms smoothly: LV_FREE, rd_free times the screen's width. The numbers are saved
   in marks and bookmarks, so new levels go at the end. */
enum {LV_FIT,LV_W1,LV_Z1,LV_Z2,LV_Z3,LV_Z4,LV_O1,LV_O2,LV_O3,LV_FREE,LV_N};
#define ZOOM_MAX 2.0f
static float rd_free=1;
static float lv_factor(int level,int vert) {
 static const float side[LV_N]={1,1,1.5f,2,2,2,.85f,.7f,.55f,1},up[LV_N]={1,1,1.25f,1.5f,1.75f,2,.85f,.7f,.55f,1};
 if(level==LV_FREE)return rd_free;
 return (vert?up:side)[level];
}
static int lv_width(int level,int vert){return (int)((vert?272:480)*lv_factor(level,vert)+0.5f);}
/* 1.75x and the upright-only steps don't exist sideways: 2x stands in */
static int lv_fix(int level){if(level<0 || level>=LV_N)return LV_W1;if(!rd_vert && (level==LV_Z3 || level==LV_Z4))return LV_Z2;return level;}
static int lv_code(int level){return level==LV_FREE?1000+(int)(rd_free*1000+0.5f):level;}
static int lv_from_code(int c) {
 if(c>=1000){rd_free=(c-1000)/1000.0f;if(rd_free<0.05f)rd_free=0.05f;if(rd_free>ZOOM_MAX)rd_free=ZOOM_MAX;return LV_FREE;}
 return c==LV_FREE?LV_W1:lv_fix(c);
}
static void view_save(void){FILE *f=fopen(view_file,"w");if(f){fprintf(f,"%d\n",rd_vert);fclose(f);}}
static void view_load(void){FILE *f=fopen(view_file,"r");int v=0;if(f){if(fscanf(f,"%d",&v)!=1)v=0;fclose(f);}rd_vert=v==1;}
typedef struct {int page,level,w,vert,failed;PageImg img;} Slot; /* w: the width it was made at */
#define SLOT_N 8
static Slot slot[SLOT_N];
static Book bk;static int bk_is_open;
static int rd_page,rd_level=LV_W1,rd_help,rd_pending=-1;static float rd_x,rd_y;
static int rd_menu,rd_menu_sel;static float rd_menu_scroll; /* Triangle's Bookmarks panel */
/* Reading direction. Right to left (manga, the default): L is the next page, as a printed manga turns,
   and a zoomed page starts at its top-right corner, where the first panel is. Left to right
   (Western comics): R is next and pages start top-left. SELECT switches; each book remembers it,
   and a new volume takes the direction last used in its folder (its series). */
static int rd_rtl=1;
/* the volume after this one in its series folder, found the first time you reach the last page */
static char rd_next[PATHLEN];static int rd_next_known;static unsigned long long rd_next_until;
static unsigned long long rd_shown_until,rd_last_input;
static unsigned int gray_clut[256] __attribute__((aligned(64)));
/* the loader thread's one job */
static SceUID ld_sema=-1,ld_thread=-1;
static volatile int ld_state,ld_result,ld_abort; /* state: 0 idle, 1 queued, 2 working, 3 done */
static int ld_page,ld_level,ld_w,ld_vert,ld_power_seen;static PageImg ld_img;
static unsigned int ld_ms; /* how long the last page took */

static int ld_main(SceSize a,void *b) {
 (void)a;(void)b;
 for(;;) {
  sceKernelWaitSema(ld_sema,1,NULL);
  if(ld_state!=1)continue;
  ld_state=2;
  unsigned long long t0=sceKernelGetSystemTimeWide();
  int len=0;unsigned char *data=ld_abort?NULL:book_read(&bk,ld_page,&len,24*1024*1024);
  int r=0;
  if(ld_abort)r=-1;
  else if(data)r=page_decode(data,len,ld_w,ld_level==LV_FIT?(ld_vert?480:272):0,&ld_img,&ld_abort);
  free(data);
  ld_ms=(unsigned int)((sceKernelGetSystemTimeWide()-t0)/1000);
  ld_result=r;ld_state=3;
 }
 return 0;
}
static void ld_start(void) {
 if(ld_thread>=0)return;
 for(int i=0;i<256;i++)gray_clut[i]=0xff000000u|i<<16|i<<8|i;
 sceKernelDcacheWritebackRange(gray_clut,sizeof(gray_clut));
 ld_sema=sceKernelCreateSema("zoid_books",0,0,1,NULL);
 ld_thread=sceKernelCreateThread("zoid_books",ld_main,0x30,0x20000,0,NULL);
 if(ld_thread>=0)sceKernelStartThread(ld_thread,0,NULL);
}
static void slot_clear(Slot *s){page_free(&s->img);s->page=-1;s->failed=0;}
static Slot *slot_find_w(int page,int level,int w){for(int i=0;i<SLOT_N;i++)if(slot[i].page==page && slot[i].level==level && slot[i].w==w && slot[i].vert==rd_vert && (slot[i].img.px || slot[i].failed))return &slot[i];return NULL;}
static Slot *slot_find(int page,int level){return slot_find_w(page,level,lv_width(level,rd_vert));}
static Slot *slot_free(void){for(int i=0;i<SLOT_N;i++)if(!slot[i].img.px && !slot[i].failed)return &slot[i];return NULL;}
/* the size a page is shown at, from its own size */
static void lv_size(int level,int sw,int sh,float *dw,float *dh) {
 if(level==LV_FIT){float s=(float)RW/sw,t=(float)RH/sh;if(t<s)s=t;*dw=sw*s;*dh=sh*s;}
 else{*dw=lv_width(level,rd_vert);*dh=sh*(float)*dw/sw;}
}
/* The width the page is drawn at: it glides to its zoom's width (rd_glide in rd_draw), so a zoom
   never jumps. A new page, or the reader turned, starts at its zoom. */
static float rd_show_w;static int rd_show_page=-1,rd_show_vert=-1;
static void rd_shown(int sw,int sh,float *dw,float *dh) {
 lv_size(rd_level,sw,sh,dw,dh);
 if(rd_show_page==rd_page && rd_show_vert==rd_vert && rd_show_w>0){*dw=rd_show_w;*dh=sh*rd_show_w/sw;}
}
static int page_dims(int page,int *sw,int *sh) {
 for(int i=0;i<SLOT_N;i++)if(slot[i].page==page && slot[i].img.px){*sw=slot[i].img.src_w;*sh=slot[i].img.src_h;return 1;}
 return 0;
}
typedef struct {int page,level,w;} Want;
static int rd_zooming; /* X or Square held: the size changes every frame, so nothing is made at it yet */
static int rd_wants(Want *w) {
 int n=0;
#define WANT(p,l) do{if((p)>=0 && (p)<bk.n){w[n].page=(p);w[n].level=(l);w[n].w=lv_width(l,rd_vert);n++;}}while(0)
 if(rd_level==LV_FIT){WANT(rd_page,LV_FIT);WANT(rd_page+1,LV_FIT);WANT(rd_page-1,LV_FIT);WANT(rd_page,LV_W1);}
 else{WANT(rd_page,LV_W1);if(rd_level!=LV_W1 && !rd_zooming)WANT(rd_page,rd_level);WANT(rd_page+1,LV_W1);WANT(rd_page-1,LV_W1);WANT(rd_page+2,LV_W1);}
#undef WANT
 return n;
}
static int wanted_v(const Want *w,int n,int page,int level,int width,int vert){if(vert!=rd_vert)return 0;for(int i=0;i<n;i++)if(w[i].page==page && w[i].level==level && w[i].w==width)return 1;return 0;}
/* once a frame: take a finished page, drop pages no longer needed, start the next one */
static void rd_service(void) {
 if(!bk_is_open)return;
 Want w[8];int n=rd_wants(w);
 if(ld_state==3) {
  if(ld_result==1 && wanted_v(w,n,ld_page,ld_level,ld_w,ld_vert) && !slot_find_w(ld_page,ld_level,ld_w)) {
   Slot *s=slot_free();
   if(s){s->page=ld_page;s->level=ld_level;s->w=ld_w;s->vert=ld_vert;s->failed=0;s->img=ld_img;}else page_free(&ld_img);
  }else if(ld_result==1)page_free(&ld_img);
  else if(ld_result==0 && ld_vert==rd_vert){Slot *s=slot_free();if(s){memset(s,0,sizeof(*s));s->page=ld_page;s->level=ld_level;s->w=ld_w;s->vert=ld_vert;s->failed=1;}}
  memset(&ld_img,0,sizeof(ld_img));ld_state=0;
 }
 for(int i=0;i<SLOT_N;i++)if((slot[i].img.px || slot[i].failed) && !wanted_v(w,n,slot[i].page,slot[i].level,slot[i].w,slot[i].vert))slot_clear(&slot[i]);
 if(ld_state==2 && !wanted_v(w,n,ld_page,ld_level,ld_w,ld_vert))ld_abort=1;
 /* after sleep the open .cbz no longer reads: open it again and retry what failed */
 if(ld_state==0 && power_events!=ld_power_seen){ld_power_seen=power_events;book_reopen(&bk);for(int i=0;i<SLOT_N;i++)if(slot[i].failed)slot_clear(&slot[i]);}
 if(ld_state==0)for(int i=0;i<n;i++)if(!slot_find_w(w[i].page,w[i].level,w[i].w)) {
  ld_page=w[i].page;ld_level=w[i].level;ld_w=w[i].w;ld_vert=rd_vert;ld_abort=0;ld_state=1;sceKernelSignalSema(ld_sema,1);break;
 }
}
static int rd_scroll_permille(void) {
 int sw,sh;if(!page_dims(rd_page,&sw,&sh))return rd_pending>=0?rd_pending:0;
 float dw,dh;rd_shown(sw,sh,&dw,&dh);return dh>RH?(int)(rd_y*1000/(dh-RH)+0.5f):0;
}
static void rd_mark(void){if(bk_is_open)mark_put(bk.path,rd_page,bk.n,rd_scroll_permille(),lv_code(rd_level),rd_rtl);}
/* a book never opened before: the direction last used in the same folder, else right to left */
static int rd_dir_for(const char *path) {
 const char *sl=strrchr(path,'/');int dl=sl?(int)(sl-path):0,best=-1;
 for(int i=0;i<mark_n;i++){const char *s2=strrchr(marks[i].path,'/');
  if(marks[i].rtl>=0 && s2 && s2-marks[i].path==dl && !strncmp(marks[i].path,path,dl) && (best<0 || marks[i].stamp>marks[best].stamp))best=i;}
 return best>=0?marks[best].rtl:1;
}
static void rd_close(void) {
 if(!bk_is_open)return;
 rd_mark();
 ld_abort=1;
 for(int t=0;t<1500 && (ld_state==1 || ld_state==2);t++)sceKernelDelayThread(2000);
 if(ld_state==3){page_free(&ld_img);memset(&ld_img,0,sizeof(ld_img));ld_state=0;}
 for(int i=0;i<SLOT_N;i++)slot_clear(&slot[i]);
 book_close(&bk);bk_is_open=0;
 bk_toast_until=0; /* a reader message doesn't follow you to the shelf */
}
static int rd_open(const char *path) {
 rd_close();ld_start();
 for(int i=0;i<SLOT_N;i++){memset(&slot[i],0,sizeof(Slot));slot[i].page=-1;}
 if(ld_thread<0 || !book_open(&bk,path)){bk_say("This book couldn't be opened");return 0;}
 bk_is_open=1;ld_power_seen=power_events;
 int m=mark_find(path);
 rd_page=0;rd_level=LV_W1;rd_pending=0;rd_rtl=m>=0 && marks[m].rtl>=0?marks[m].rtl:rd_dir_for(path);
 if(m>=0){rd_page=marks[m].page;if(rd_page<0 || rd_page>=bk.n)rd_page=0;rd_level=lv_from_code(marks[m].level);rd_pending=marks[m].scroll;}
 rd_zooming=0;rd_show_page=-1;
 rd_x=rd_rtl?1e9f:0;rd_y=0;rd_help=0;rd_menu=0;bk_mode=1;rd_shown_until=now_ms+2000;rd_last_input=now_ms;
 rd_next[0]=0;rd_next_known=0;rd_next_until=0;
 rd_mark();return 1;
}
/* The next book in the same folder, in shelf order: a .cbz or a folder of images. Loose books at the
   top of BOOKS aren't a series, so they have none. */
static int bk_next_book(const char *path,char *out,int cap) {
 char dir[PATHLEN];snprintf(dir,sizeof(dir),"%s",path);char *sl=strrchr(dir,'/');if(!sl)return 0;*sl=0;
 if(!strcmp(dir,books_root))return 0;
 /* in shelf order, which goes by the long names */
 char me[256];snprintf(me,sizeof(me),"%s",bk_base(bk_shown(path)));
 int d=sceIoDopen(dir);if(d<0)return 0;
 char best[256]="",best_show[256]="";SceIoDirent e;char name[256],show[256];
 while(book_dread(d,dir,&e,name,sizeof(name),show,sizeof(show))>0) {
  int isdir=FIO_S_ISDIR(e.d_stat.st_mode);
  if(bk_junk(show) || !strcmp(show,".") || !strcmp(show,".."))continue;
  if(book_natural_compare(show,me)<=0 || (best[0] && book_natural_compare(show,best_show)>=0))continue;
  if(isdir){char p[PATHLEN];int f,c,im;snprintf(p,sizeof(p),"%s/%s",dir,name);if(bk_dir_kind(p,&f,&c,&im)!=SH_BOOK)continue;}
  else if(!book_archive_name(show))continue;
  snprintf(best,sizeof(best),"%s",name);snprintf(best_show,sizeof(best_show),"%s",show);
 }
 sceIoDclose(d);
 if(!best[0])return 0;
 snprintf(out,cap,"%s/%s",dir,best);return 1;
}
/* "Volume 4", as the shelf names it */
static void bk_book_title(const char *path,char *t,int cap) {
 char dir[PATHLEN],note[96];snprintf(dir,sizeof(dir),"%s",path);char *sl=strrchr(dir,'/');if(sl)*sl=0;
 bk_clean(bk_base(bk_shown(path)),strcmp(dir,books_root)?bk_base(dir):NULL,t,cap,note,sizeof(note));
}
static void rd_clamp(float dw,float dh) {
 float mx=dw>RW?dw-RW:0,my=dh>RH?dh-RH:0;
 if(rd_x<0)rd_x=0;if(rd_x>mx)rd_x=mx;if(rd_y<0)rd_y=0;if(rd_y>my)rd_y=my;
}
/* zoom about the middle of the screen: from one drawn size to the next, keeping the spot in the middle */
static void rd_rescale(float w0,float h0,float w1,float h1) {
 float cx=w0>RW?(rd_x+RW/2)/w0:rd_rtl?1:0,cy=h0>RH?(rd_y+RH/2)/h0:0.5f;
 rd_x=cx*w1-RW/2;rd_y=cy*h1-RH/2;rd_clamp(w1,h1);
}
/* once a frame, before drawing: the drawn width moves a quarter of the way to the zoom's width */
static void rd_glide(int sw,int sh) {
 float tw,th;lv_size(rd_level,sw,sh,&tw,&th);
 if(rd_show_page!=rd_page || rd_show_vert!=rd_vert || rd_show_w<=0){rd_show_page=rd_page;rd_show_vert=rd_vert;rd_show_w=tw;return;}
 if(rd_show_w==tw)return;
 float w0=rd_show_w,w1=w0+(tw-w0)*0.25f;
 if(fabsf(tw-w1)<0.5f)w1=tw;
 rd_rescale(w0,sh*w0/sw,w1,sh*w1/sw);rd_show_w=w1;
}
/* how wide this page is at its zoom, and at the whole page. Not made yet, the whole page counts as
   just under full width, so the steps go as they always did: whole page, full width, in. */
static float rd_fit_w(void){int sw,sh;float dw,dh;if(page_dims(rd_page,&sw,&sh)){lv_size(LV_FIT,sw,sh,&dw,&dh);return dw;}return RW-2;}
/* (a whole page as wide as the screen, as upright pages usually are, still counts as under full width) */
static float rd_cur_w(void){float fw=rd_fit_w();return rd_level==LV_FIT?(fw<RW-2?fw:RW-2):lv_width(rd_level,rd_vert);}
/* "85%", "Full width", "Whole page" */
static void rd_say_zoom(void) {
 char t[32];
 if(rd_level==LV_FIT)snprintf(t,sizeof(t),"Whole page");
 else if(rd_level==LV_W1)snprintf(t,sizeof(t),"Full width");
 else snprintf(t,sizeof(t),"%d%%",(int)(lv_factor(rd_level,rd_vert)*100+0.5f));
 bk_say_for(t,900);
}
static void rd_set_level(int level) {
 if(level<0 || level>=LV_N || level==rd_level)return;
 rd_level=level;rd_mark();rd_say_zoom();
}
/* the next step in (dir 1) or out (-1) from this page's size; out, the whole page is the last. -1: none */
static int rd_step_level(int dir) {
 static const int steps[]={LV_O3,LV_O2,LV_O1,LV_W1,LV_Z1,LV_Z2,LV_Z3,LV_Z4};
 float cw=rd_cur_w(),fw=rd_fit_w(),bw=0;int best=-1;
 for(int i=0;i<(int)(sizeof(steps)/sizeof(steps[0]));i++) {
  int l=steps[i];if(lv_fix(l)!=l)continue;
  float w=lv_width(l,rd_vert);if(l!=LV_W1 && w<=fw+1)continue; /* full width is always a step */
  if(dir>0 && w>cw+1 && (best<0 || w<bw)){best=l;bw=w;}
  if(dir<0 && w<cw-1 && (best<0 || w>bw)){best=l;bw=w;}
 }
 if(dir<0 && best<0 && rd_level!=LV_FIT)best=LV_FIT;
 return best;
}
/* holding X or Square: from the size it is at, 1.5% a frame (about 2.4x a second), between the whole page and 2x */
static void rd_zoom_hold(int dir) {
 float fw=rd_fit_w()/RW;
 if(!rd_zooming){rd_zooming=1;rd_free=rd_cur_w()/RW;rd_level=LV_FREE;}
 rd_free*=dir>0?1.015f:1/1.015f;
 if(rd_free>ZOOM_MAX)rd_free=ZOOM_MAX;if(rd_free<fw)rd_free=fw;
 {char t[16];snprintf(t,sizeof(t),"%d%%",(int)(rd_free*100+0.5f));bk_say_for(t,900);}
}
/* let go: a size next to a step (or the whole page) becomes that step, so its page is reused */
static void rd_zoom_end(void) {
 static const int steps[]={LV_O3,LV_O2,LV_O1,LV_W1,LV_Z1,LV_Z2,LV_Z3,LV_Z4};
 rd_zooming=0;
 if(rd_level!=LV_FREE)return;
 if(rd_free*RW<=rd_fit_w()+2)rd_level=LV_FIT;
 else for(int i=0;i<(int)(sizeof(steps)/sizeof(steps[0]));i++){int l=steps[i];if(lv_fix(l)==l && fabsf(lv_factor(l,rd_vert)-rd_free)<0.015f){rd_level=l;break;}}
 rd_mark();
}
static void rd_turn(int dir) {
 int np=rd_page+dir;
 if(np>=bk.n) {
  /* the end of a volume: next page again, while the message shows, opens the next one */
  if(rd_next_until && now_ms<rd_next_until && rd_next[0]) {
   char p[PATHLEN],t[128];snprintf(p,sizeof(p),"%s",rd_next);
   if(rd_open(p)){bk_book_title(p,t,sizeof(t));bk_say(t);}else{bk_mode=0;shelf_scan(NULL);}
   return;
  }
  if(!rd_next_known){rd_next_known=1;if(!bk_next_book(bk.path,rd_next,sizeof(rd_next)))rd_next[0]=0;}
  if(rd_next[0]) {
   char t[128],b[180];bk_book_title(rd_next,t,sizeof(t));
   snprintf(b,sizeof(b),"Last page. %s again for %s",rd_vert?(rd_rtl?"Triangle":"Cross"):rd_rtl?"L":"R",t);
   bk_say_for(b,3000);rd_next_until=now_ms+3000;
  }else bk_say("That was the last page");
  rd_mark();return;
 }
 rd_next_until=0;
 if(np<0){bk_say("This is the first page");return;}
 rd_page=np;rd_x=rd_rtl?1e9f:0;rd_y=0;rd_pending=-1;rd_shown_until=now_ms+1600;rd_mark();
}
static void books_leave(int to);
/* to a page, at a scroll (0-1000 down the page) and zoom: a bookmark's spot */
static void rd_goto(int page,int scroll,int level) {
 if(page>=bk.n)page=bk.n-1;if(page<0)page=0;
 level=zoom_code_ok(level)?lv_from_code(level):LV_W1;rd_zooming=0;
 rd_page=page;rd_level=level;rd_pending=scroll;rd_x=rd_rtl?1e9f:0;rd_y=0;
 rd_next_until=0;rd_shown_until=now_ms+1600;rd_mark();
}

/* ---- Triangle in a book (R when upright): bookmark this page, go to this book's bookmarks, turn the reader, or see the controls ---- */

#define RD_MENU_MAX 64
/* the rows: -1 bookmark (or unbookmark) this page, a bookmark's index, -3 upright / sideways, -2 the controls */
static int rd_menu_items(int *ids) {
 int n=0;ids[n++]=-1;
 for(int i=0;i<bm_n && n<RD_MENU_MAX-2;i++)if(!strcmp(bms[i].path,bk.path)) {
  int k=n++;while(k>1 && bms[ids[k-1]].page>bms[i].page){ids[k]=ids[k-1];k--;}ids[k]=i; /* by page */
 }
 ids[n++]=-3;ids[n++]=-2;return n;
}
/* Upright or sideways: the same page at the same spot down it; pages made for the other way are dropped
   as rd_service finds them unwanted. Remembered for every book. */
static void rd_set_vert(int v) {
 if(v==rd_vert)return;
 int at=rd_scroll_permille();
 if(rd_zooming)rd_zoom_end();
 rd_vert=v;rd_level=lv_fix(rd_level);
 rd_pending=at;rd_x=rd_rtl?1e9f:0;rd_y=0;rd_shown_until=now_ms+1600;
 view_save();rd_mark();
 bk_say(v?"Upright: D-pad at the bottom":"Sideways");
}
static void rd_bookmark_here(void) {
 char t[64];int b=bm_find(bk.path,rd_page);
 if(b>=0){bm_del(b);snprintf(t,sizeof(t),"Bookmark on page %d removed",rd_page+1);}
 else{bm_put(bk.path,rd_page,rd_scroll_permille(),lv_code(rd_level));snprintf(t,sizeof(t),"Bookmarked page %d",rd_page+1);}
 bk_say(t);
}
static void rd_menu_input(unsigned int press) {
 int ids[RD_MENU_MAX];int n=rd_menu_items(ids);
 if(rd_menu_sel>=n)rd_menu_sel=n-1;
 if(press&PSP_CTRL_UP && rd_menu_sel>0)rd_menu_sel--;
 if(press&PSP_CTRL_DOWN && rd_menu_sel<n-1)rd_menu_sel++;
 if(press&(PSP_CTRL_CIRCLE|(rd_vert?PSP_CTRL_RTRIGGER:PSP_CTRL_TRIANGLE))){rd_menu=0;return;}
 int id=ids[rd_menu_sel];
 if(press&PSP_CTRL_SELECT && id>=0) {
  char t[64];snprintf(t,sizeof(t),"Bookmark on page %d removed",bms[id].page+1);bm_del(id);bk_say(t);
  if(rd_menu_sel>=n-3)rd_menu_sel=n-4>0?n-4:0;
  return;
 }
 if(press&PSP_CTRL_CROSS) {
  rd_menu=0;
  if(id==-1)rd_bookmark_here();
  else if(id==-2)rd_help=1;
  else if(id==-3)rd_set_vert(!rd_vert);
  else{char t[32];rd_goto(bms[id].page,bms[id].scroll,bms[id].level);snprintf(t,sizeof(t),"Page %d",rd_page+1);bk_say(t);}
 }
}
/* Upright, the D-pad turns with the PSP: right is up, left is down, up is left, down is right */
static unsigned int rd_turn_dpad(unsigned int b) {
 unsigned int out=b&~(PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT);
 if(b&PSP_CTRL_RIGHT)out|=PSP_CTRL_UP;if(b&PSP_CTRL_LEFT)out|=PSP_CTRL_DOWN;
 if(b&PSP_CTRL_UP)out|=PSP_CTRL_LEFT;if(b&PSP_CTRL_DOWN)out|=PSP_CTRL_RIGHT;
 return out;
}

static void rd_input(unsigned int press,unsigned int raw,const SceCtrlData *pad) {
 if(press || raw)rd_last_input=now_ms;
 if(rd_vert){press=rd_turn_dpad(press);raw=rd_turn_dpad(raw);}
 if(rd_help){if(press&(PSP_CTRL_TRIANGLE|PSP_CTRL_CIRCLE|PSP_CTRL_CROSS|PSP_CTRL_RTRIGGER))rd_help=0;return;}
 if(rd_menu){rd_menu_input(press);return;}
 /* the buttons. Sideways: L / R turn, X zooms in, Square zooms out to the whole page, Triangle the menu.
    Upright the face buttons sit Triangle on the left, Cross on the right, Circle on top, Square below:
    Triangle and Cross turn (the left one is next in a right-to-left book), Square zooms in,
    L zooms out to the whole page, R the menu. Circle zooms out of a zoom in, then goes back, either way. */
 unsigned int next,prev,zin,whole,menu;
 if(rd_vert){next=rd_rtl?PSP_CTRL_TRIANGLE:PSP_CTRL_CROSS;prev=rd_rtl?PSP_CTRL_CROSS:PSP_CTRL_TRIANGLE;zin=PSP_CTRL_SQUARE;whole=PSP_CTRL_LTRIGGER;menu=PSP_CTRL_RTRIGGER;}
 else{next=rd_rtl?PSP_CTRL_LTRIGGER:PSP_CTRL_RTRIGGER;prev=rd_rtl?PSP_CTRL_RTRIGGER:PSP_CTRL_LTRIGGER;zin=PSP_CTRL_CROSS;whole=PSP_CTRL_SQUARE;menu=PSP_CTRL_TRIANGLE;}
 if(press&next)rd_turn(1);
 if(press&prev)rd_turn(-1);
 if(press&PSP_CTRL_SELECT){rd_rtl^=1;
  if(rd_vert)bk_say(rd_rtl?"Right to left: Triangle is the next page":"Left to right: Cross is the next page");
  else bk_say(rd_rtl?"Right to left: L is the next page":"Left to right: R is the next page");
  rd_mark();}
 /* zoom: a press is a step, holding on zooms smoothly (after about a quarter second) */
 static unsigned int zbtn;static int zheld;
 if(press&zin){if(rd_zooming)rd_zoom_end();int l=rd_step_level(1);if(l>=0)rd_set_level(l);else bk_say("Zoomed in all the way");zbtn=zin;zheld=0;}
 if(press&PSP_CTRL_CIRCLE) {
  if(rd_zooming)rd_zoom_end();zbtn=0;
  if(rd_level==LV_W1){char came[256];snprintf(came,sizeof(came),"%s",bk_base(bk.path));rd_close();bk_mode=0;shelf_scan(came);return;}
  if(rd_cur_w()>lv_width(LV_W1,rd_vert)+1)rd_set_level(rd_step_level(-1)); /* zoomed in: a step out */
  else rd_set_level(LV_W1); /* zoomed out: back to full width */
 }
 if(press&whole) {
  if(rd_zooming)rd_zoom_end();
  if(rd_level==LV_FIT){rd_set_level(LV_W1);zbtn=0;} /* past the whole page: round to full width */
  else{rd_set_level(rd_step_level(-1));zbtn=whole;zheld=0;}
 }
 if(zbtn && (raw&zbtn)){if(++zheld>=14)rd_zoom_hold(zbtn==zin?1:-1);}
 else if(zbtn){zbtn=0;if(rd_zooming)rd_zoom_end();}
 if(press&menu){rd_menu=1;rd_menu_sel=0;rd_menu_scroll=0;}
 /* START plays or pauses when let go; held (as long as Hold X time in Settings) it goes to the next song */
 {static int held=-1;
  if(press&PSP_CTRL_START)held=0;
  else if(held>=0 && (raw&PSP_CTRL_START)){if(++held>=hold_frames[hold_idx]){held=-1;bk_skip(1);}}
  else if(held>=0){held=-1;bk_play_pause();}}
 /* scrolling: the D-pad speeds up the longer it is held; the stick goes as far as it is pushed */
 static int hold;
 unsigned int dirs=raw&(PSP_CTRL_UP|PSP_CTRL_DOWN|PSP_CTRL_LEFT|PSP_CTRL_RIGHT);
 hold=dirs?hold+1:0;
 float sp=4+(hold<36?hold:36)*0.35f,vx=0,vy=0;
 if(dirs&PSP_CTRL_DOWN)vy+=sp;if(dirs&PSP_CTRL_UP)vy-=sp;
 if(dirs&PSP_CTRL_RIGHT)vx+=sp;if(dirs&PSP_CTRL_LEFT)vx-=sp;
 if(pad) {
  int ax=pad->Lx-128,ay=pad->Ly-128;const int dead=36;
  if(rd_vert){int t=ax;ax=ay;ay=-t;} /* the stick turns with the PSP too */
  if(ay>dead || ay<-dead){vy+=(ay>0?ay-dead:ay+dead)*0.18f;rd_last_input=now_ms;}
  if(ax>dead || ax<-dead){vx+=(ax>0?ax-dead:ax+dead)*0.18f;rd_last_input=now_ms;}
 }
 if(vx || vy){rd_x+=vx;rd_y+=vy;rd_pending=-1;}
}

/* Draws a page from its cache buffer in 64 x 256 pieces (big textured sprites thrash the GPU's
   texture cache). Gray pages go through a 256-entry gray palette. */
static void page_draw(const PageImg *im,float ox,float oy,float s) {
 int bpp=im->fmt==PG_GRAY?1:2;
 sceGuDisable(GU_BLEND);sceGuTexFunc(GU_TFX_REPLACE,GU_TCC_RGB);
 if(bpp==1){sceGuClutMode(GU_PSM_8888,0,0xff,0);sceGuClutLoad(256/8,gray_clut);sceGuTexMode(GU_PSM_T8,0,0,0);}
 else sceGuTexMode(GU_PSM_5650,0,0,0);
 linear(s<0.999f || s>1.001f);
 for(int cy=0;cy<im->h;cy+=256) {
  int rows=im->h-cy<256?im->h-cy:256;float y0=oy+cy*s,y1=y0+rows*s;
  if(y1<0 || y0>RH)continue;
  for(int cx=0;cx<im->w;cx+=64) {
   int cols=im->w-cx<64?im->w-cx:64;float x0=ox+cx*s,x1=x0+cols*s;
   if(x1<0 || x0>RW)continue;
   sceGuTexImage(0,64,256,im->stride,im->px+(cy*im->stride+cx)*bpp);sceGuTexFlush();
   sprite(0,0,cols,rows,x0,y0,cols*s,rows*s,0xffffffff);
  }
 }
 sceGuTexMode(GU_PSM_8888,0,0,0);sceGuTexFunc(GU_TFX_MODULATE,GU_TCC_RGBA);linear(0);sceGuEnable(GU_BLEND);
 sceGuTexImage(0,512,512,512,atlas);sceGuTexFlush();
}

static void bk_toast_draw(void) {
 if(now_ms>=bk_toast_until)return;
 int a=bk_toast_until-now_ms<250?(int)((bk_toast_until-now_ms)*255/250):255;
 float cx=bk_mode==1?RW/2:240,w=text_w(bk_toast,F_SMALL)+28,x=cx-w/2,y=bk_mode==1?(rd_vert?RH-64:230):208; /* on the shelf: above the button hints; upright, above the page number */
 rrect(x,y,w,26,5,C(BP.badge,a*235/255));if(!PAPER)rect(x+5,y,w-10,1,C(P_RED,a));text_mid(cx,y+13,bk_toast,C(BP.badge_ink,a),F_SMALL);
}
static float bk_keycap(float x,float cy,const char *k,unsigned int ink) {
 float w=text_w(k,F_SMALL)+10;frame_rect(x,cy-8,w,16,1,C(ink,170));text_mid(x+w/2,cy,k,C(ink,255),F_SMALL);return w;
}
static float bk_hint(float x,float cy,int ic,const char *key,const char *label,unsigned int ink) {
 if(ic>=0){icon(ic,x+8,cy,18,C(ink,255));x+=19;}else x+=bk_keycap(x,cy,key,ink)+5;
 x+=text_vc(x,cy,label,C(ink,255),F_SMALL);return x+16;
}
static void rd_draw(void) {
 const float W=RW,H=RH;
 int sw,sh;
 if(!page_dims(rd_page,&sw,&sh)) {
  /* nothing to show yet: the page is on its way, or it couldn't be read */
  if(rd_vert)rect(0,0,W,H,C(P_BG,255));else bk_backdrop();
  Slot *f=slot_find(rd_page,rd_level==LV_FIT?LV_FIT:LV_W1);
  char b[80];float my=H/2;
  if(f && f->failed){const char *pn=book_page_name(&bk,rd_page);float pw=text_w(pn,F_SMALL);if(pw>W-40)pw=W-40;
   text_mid(W/2,my-14,rd_vert?"Page couldn't be opened":"This page couldn't be opened",C(P_INK,255),F_MEDIUM);text_fit(W/2-pw/2,my+4,pn,C(P_MUTED,255),F_SMALL,(int)(W-40));}
  else {
   snprintf(b,sizeof(b),"Page %d",rd_page+1);text_mid(W/2,my-12,b,C(P_INK,255),F_MEDIUM);
   for(int k=0;k<3;k++){int on=(int)(now_ms/220)%3==k;disk(W/2-12+k*12,my+12,on?3.2f:2.4f,C(on?P_RED:P_LINE,255));}
  }
 }else {
  rd_glide(sw,sh);
  float dw,dh;rd_shown(sw,sh,&dw,&dh);
  if(rd_pending>=0){rd_y=dh>H?rd_pending*(dh-H)/1000.0f:0;rd_pending=-1;}
  rd_clamp(dw,dh);
  float ox=dw<W?(W-dw)/2:-rd_x,oy=dh<H?(H-dh)/2:-rd_y;
  /* the page made nearest this size, a little bigger if there is one (it shrinks more cleanly than it
     grows); while a zoom glides, or one is made, another size stands in */
  Slot *s=NULL;
  for(int i=0;i<SLOT_N;i++)if(slot[i].page==rd_page && slot[i].img.px) {
   if(!s){s=&slot[i];continue;}
   float a=s->img.w,b=slot[i].img.w,need=dw-0.5f;
   if(b>=need?(a<need || b<a):(a<need && b>a))s=&slot[i];
  }
  if(dw<W || dh<H){rect(0,0,W,H,C(P_SURROUND,255));frame_rect(ox-1,oy-1,dw+2,dh+2,1,C(P_LINE,255));}
  if(s && s->img.px) {
   float scale=dw/s->img.w;
   if(scale>0.999f && scale<1.001f){ox=(int)(ox+(ox<0?-0.5f:0.5f));oy=(int)(oy+(oy<0?-0.5f:0.5f));scale=1;}
   page_draw(&s->img,ox,oy,scale);
  }
  /* where you are on the page: a thin red mark at the right edge while scrolling */
  if(dh>H) {
   unsigned long long since=now_ms-rd_last_input;int a=since<900?200:since<1300?(int)(200-(since-900)/2):0;
   if(a>0){float th=H*H/dh,ty=rd_y/(dh-H)*(H-th);rect(W-4,ty,3,th,C(P_RED,a));}
  }
 }
 /* a bookmarked page wears a small red ribbon at its top corner */
 if(bm_find(bk.path,rd_page)>=0)icon(IC_BOOKMARK,rd_rtl?24:W-24,9,26,C(P_RED,235));
 /* the page number, for a moment after turning */
 if(now_ms<rd_shown_until+300) {
  int a=now_ms<rd_shown_until?255:(int)(255-(now_ms-rd_shown_until)*255/300);
  char b[32];snprintf(b,sizeof(b),"%d / %d",rd_page+1,bk.n);
  float w=text_w(b,F_SMALL)+18,x=W-10-w,y=H-28;
  rrect(x,y,w,20,4,C(BP.badge,a*215/255));text_mid(x+w/2,y+10,b,C(BP.badge_ink,a),F_SMALL);
  {float pw=(w-12)*(rd_page+1)/(float)bk.n;rect(rd_rtl?x+w-6-pw:x+6,y+17,pw,1.5f,C(P_RED,a));} /* fills the way the book reads */
 }
 if(rd_help) {
  /* sideways the panel is wide; upright it is tall, one button a row */
  float x=rd_vert?10:70,y=rd_vert?24:20,w=rd_vert?252:340,h=rd_vert?432:232;
  rect(0,0,W,H,PAPER?C(P_INK,90):C(0x000000,120)); /* dim the page: ink on Paper, black elsewhere */
  bk_panel(x,y,w,h);
  if(MS1)text(x+18,y+7,"Reading",C(P_INK,255),F_MEDIUM); /* its panel has its own rule */
  else{text(x+18,y+8,"Reading",C(P_INK,255),F_LARGE);rect(x+18,y+40,w-36,1.5f,C(P_RED,255));}
  struct Row {int ic;const char *key,*what;};
  struct Row side[8]={
   {-1,"L / R",rd_rtl?"Next page / previous page":"Previous page / next page"},{-1,"Up / Down","Scroll the page"},{-1,"Stick","Scroll, and pan when zoomed in"},
   {IC_BTN_CROSS,NULL,"Zoom in, hold to zoom smoothly"},{IC_BTN_CIRCLE,NULL,"Zoom out, then back to the shelf"},{IC_BTN_SQUARE,NULL,"Zoom out, down to the whole page"},{-1,"SELECT",rd_rtl?"Reading right to left (manga)":"Reading left to right (comics)"},{-1,"START","Play / pause music, hold: next song"}};
  struct Row up[10]={
   {IC_BTN_TRIANGLE,NULL,rd_rtl?"Next page":"Previous page"},{IC_BTN_CROSS,NULL,rd_rtl?"Previous page":"Next page"},
   {IC_BTN_SQUARE,NULL,"Zoom in (hold: smooth)"},{IC_BTN_CIRCLE,NULL,"Zoom out, then back"},{-1,"D-pad","Scroll and pan"},{-1,"Stick","Scroll and pan"},
   {-1,"L","Zoom out to whole page"},{-1,"R","Bookmarks and more"},{-1,"SELECT",rd_rtl?"Right to left (manga)":"Left to right (comics)"},{-1,"START","Music: play / pause"}};
  const struct Row *rows=rd_vert?up:side;
  int n=rd_vert?10:8;float kx=rd_vert?x+40:x+56,tx=rd_vert?x+80:x+104,rs=rd_vert?34:20;
  for(int i=0;i<n;i++) {
   float cy=y+66+i*rs;
   if(rows[i].ic>=0)icon(rows[i].ic,kx,cy,18,C(P_INK,255));else{float kw=text_w(rows[i].key,F_SMALL)+10;bk_keycap(kx-kw/2,cy,rows[i].key,P_INK);}
   text_fit(tx,cy-fonts[F_SMALL].ch/2.0f,rows[i].what,C(P_INK,255),F_SMALL,(int)(x+w-tx-10));
  }
  if(rd_vert){float kw=text_w("R",F_SMALL)+10;bk_keycap(x+w-74-kw/2,y+h-14,"R",P_MUTED);}
  else icon(IC_BTN_TRIANGLE,x+w-74,y+h-14,16,C(P_MUTED,255));
  text_vc(x+w-62,y+h-14,"Close",C(P_MUTED,255),F_SMALL);
 }
 if(rd_menu) {
  int ids[RD_MENU_MAX];int n=rd_menu_items(ids);if(rd_menu_sel>=n)rd_menu_sel=n-1;
  float x=rd_vert?10:90,y=rd_vert?40:18,w=rd_vert?252:300,h=rd_vert?400:236;const float rh=24;const int vis=rd_vert?13:7;
  rect(0,0,W,H,PAPER?C(P_INK,90):C(0x000000,120)); /* dim the page: ink on Paper, black elsewhere */
  bk_panel(x,y,w,h);
  if(MS1)text(x+18,y+7,"Bookmarks",C(P_INK,255),F_MEDIUM); /* its panel has its own rule */
  else{text(x+18,y+8,"Bookmarks",C(P_INK,255),F_LARGE);rect(x+18,y+40,w-36,1.5f,C(P_RED,255));}
  float want=rd_menu_sel<3?0:rd_menu_sel-3;if(want>n-vis)want=n-vis;if(want<0)want=0;
  rd_menu_scroll+=(want-rd_menu_scroll)*0.35f;if(fabsf(want-rd_menu_scroll)<0.01f)rd_menu_scroll=want;
  float top=y+48;clip((int)x,(int)top,(int)w,(int)(rh*vis));
  for(int i=(int)rd_menu_scroll;i<n && i<(int)rd_menu_scroll+vis+1;i++) {
   float ry=top+(i-rd_menu_scroll)*rh,cy=ry+rh/2;int id=ids[i];
   int sl=i==rd_menu_sel;if(sl)bk_sel_bar(x+8,ry+1,w-16,rh-2);
   unsigned int ink=sl?P_SEL_INK:P_INK,muted=sl?P_SEL_MUTED:P_MUTED,ac=sl && !PAPER?mix(P_RED,P_SEL_INK,150):P_RED;
   char t[64];
   if(id==-1) {
    int here=bm_find(bk.path,rd_page)>=0;
    icon(here?IC_BOOKMARK:IC_PLUS,x+30,cy,18,C(ac,255));
    snprintf(t,sizeof(t),here?(rd_vert?"Remove bookmark, page %d":"Remove the bookmark on page %d"):"Bookmark page %d",rd_page+1);
    text_fit(x+48,cy-fonts[F_SMALL].ch/2.0f,t,C(ink,255),F_SMALL,(int)(w-60));
   }else if(id==-2) {
    icon(IC_BTN_TRIANGLE,x+30,cy,16,C(muted,255));text_vc(x+48,cy,"Controls",C(ink,255),F_SMALL);
   }else if(id==-3) {
    icon(IC_REPEAT,x+30,cy,16,C(muted,255));text_vc(x+48,cy,rd_vert?"Read sideways":"Read upright (D-pad at the bottom)",C(ink,255),F_SMALL);
   }else {
    icon(IC_BOOKMARK,x+30,cy,18,C(ink,255));
    snprintf(t,sizeof(t),"Page %d",bms[id].page+1);text_vc(x+48,cy,t,C(ink,255),F_SMALL);
    if(bms[id].page==rd_page)text_right(x+w-18,cy-7,"This page",C(muted,255),F_SMALL);
   }
  }
  unclip();
  if(n==3)text_mid(x+w/2,top+rh*3+10,"No bookmarks in this book yet",C(P_FAINT,255),F_SMALL);
  /* the foot */
  float fx=x+16,fy=y+h-14;
  fx=bk_hint(fx,fy,IC_BTN_CROSS,NULL,"Choose",P_MUTED);
  if(ids[rd_menu_sel]>=0)fx=bk_hint(fx,fy,-1,"SELECT","Remove",P_MUTED);
  if(rd_vert)bk_hint(fx,fy,-1,"R","Close",P_MUTED);else bk_hint(fx,fy,IC_BTN_TRIANGLE,NULL,"Close",P_MUTED);
 }
 if(!rd_help && !rd_menu)bk_toast_draw(); /* it would cover the foot of the panels */
}

/* ---- the shelf, drawn exactly as this theme draws its Music list: the same heading, column bar,
   striped rows, selection bar, scroll bar, mini player along the foot and battery in the corner ---- */
static int bk_rows(void){return MODERN?LIB_ROWS:MS?(int)((226-48)/ms_row_h()):HUD_ROWS;}
/* what the right-hand column says about a row, and how far through it you are (-1: no bar) */
static void bk_row_status(const Shelf *s,char *st,int cap,float *frac,int *fin) {
 st[0]=0;*frac=-1;*fin=0;
 if(s->rar){snprintf(st,cap,"RAR: convert on a PC");return;}
 if(s->kind==SH_BMARKS){snprintf(st,cap,"%s",s->note);return;}
 if(s->kind==SH_BMARK) {
  const Bmark *b=&bms[s->bm];int m=mark_find(b->path);
  if(m>=0 && marks[m].pages>0){snprintf(st,cap,"Page %d of %d",b->page+1,marks[m].pages);*frac=(b->page+1)/(float)marks[m].pages;}
  else snprintf(st,cap,"Page %d",b->page+1);
  return;
 }
 if(s->kind==SH_RESUME) {
  int m=mark_find(sh_resume);
  if(m>=0 && marks[m].pages>0){snprintf(st,cap,"Page %d of %d",marks[m].page+1,marks[m].pages);*frac=(marks[m].page+1)/(float)marks[m].pages;}
  return;
 }
 if(s->kind==SH_BOOK) {
  char p[PATHLEN];snprintf(p,sizeof(p),"%s/%s",bk_dir,s->name);int m=mark_find(p);
  if(m<0)snprintf(st,cap,"Unread");
  else if(marks[m].pages>0 && marks[m].page>=marks[m].pages-1){snprintf(st,cap,"Finished");*fin=1;}
  else if(marks[m].pages>0){snprintf(st,cap,"Page %d of %d",marks[m].page+1,marks[m].pages);*frac=(marks[m].page+1)/(float)marks[m].pages;}
  return;
 }
 /* a folder: what is in it, and how many of its books you have started */
 char p[PATHLEN];int pl=snprintf(p,sizeof(p),"%s/%s/",bk_dir,s->name),started=0;
 for(int k=0;k<mark_n;k++)if(!strncmp(marks[k].path,p,pl))started++;
 if(started)snprintf(st,cap,"%s, %d started",s->note,started);else snprintf(st,cap,"%s",s->note);
}
/* a row's name: "Continue" before the book read last; a book's note (publisher, year) after its title when it fits */
static void bk_row_title(float x,float cy,int w,const Shelf *s,int i,int sel,unsigned int ink,unsigned int muted,unsigned int ac) {
 float fy=cy-fonts[F_MEDIUM].ch/2.0f;
 if(s->kind==SH_RESUME){char tag[24];if(MODERN)snprintf(tag,sizeof(tag),"Continue");else upper(tag,"Continue",sizeof(tag));
  float tw=text_vc(x,cy,tag,C(ac,255),F_SMALL)+8;x+=tw;w-=(int)tw;}
 const char *note=(s->kind==SH_BOOK || s->kind==SH_BMARK) && !s->rar?s->note:"";
 int tw=text_w(s->title,F_MEDIUM);
 if(note[0] && tw+10+48<=w){text(x,fy,s->title,C(ink,255),F_MEDIUM);text_fit(x+tw+10,cy-fonts[F_SMALL].ch/2.0f,note,C(muted,255),F_SMALL,w-tw-10);}
 else if(sel)marquee(&mq_row,i*7919+sh_n,x,fy,s->title,C(ink,255),F_MEDIUM,w);
 else text_fit(x,fy,s->title,C(ink,255),F_MEDIUM,w);
}
/* the right-hand column: the words from column x (or right-aligned before the bar), then a short progress bar at the right edge */
static void bk_row_column(float x,float right,int align_right,float cy,const Shelf *s,unsigned int c,unsigned int ac,unsigned int track) {
 char st[64];float frac;int fin;bk_row_status(s,st,sizeof(st),&frac,&fin);
 const float bw=frac>=0?36:0,end=right-(bw?bw+8:0);
 float fy=cy-fonts[F_SMALL].ch/2.0f;
 if(st[0]) {
  if(fin){icon(IC_CHECK,(align_right?end-text_w(st,F_SMALL)-14:x)+6,cy,12,C(ac,255));if(!align_right)x+=15;}
  if(align_right)text_right(end,fy,st,C(fin?ac:c,255),F_SMALL);else text_fit(x,fy,st,C(fin?ac:c,255),F_SMALL,(int)(end-x));
 }
 if(bw){rect(right-bw,cy-1,bw,2,C(track,255));rect(right-bw,cy-1,bw*frac,2,C(ac,255));}
}
static int bk_row_icon(const Shelf *s){return s->kind==SH_SERIES?IC_FOLDER:s->kind==SH_BMARKS || s->kind==SH_BMARK?IC_BOOKMARK:IC_BOOK;}
/* the heading's two lines: where you are, and what is here */
static void bk_heading_text(char *name,int ncap,char *info,int icap) {
 int folders=0,books=0;for(int i=0;i<sh_n;i++){if(sh[i].kind==SH_SERIES)folders++;else if(sh[i].kind==SH_BOOK)books++;}
 if(sh_bm){snprintf(name,ncap,"Bookmarks");snprintf(info,icap,"%d bookmark%s, newest first",sh_n,sh_n==1?"":"s");return;}
 if(bk_at_root())snprintf(name,ncap,"Books");else{snprintf(name,ncap,"%s",bk_base(bk_shown(bk_dir)));book_unslash(name);}
 char what[48];bk_folder_note(folders,books,what,sizeof(what));
 /* in a folder below a folder: the one above it too */
 if(!bk_at_root()) {
  char up[PATHLEN];snprintf(up,sizeof(up),"%s",bk_shown(bk_dir));char *sl=strrchr(up,'/');if(sl)*sl=0;
  if(strcmp(up,books_root)){char pn[128];snprintf(pn,sizeof(pn),"%s",bk_base(up));book_unslash(pn);snprintf(info,icap,"%s  -  in %s",what,pn);return;}
 }
 snprintf(info,icap,"%s",what);
}
/* the empty shelf's two lines */
static int bk_empty_text(const char **l1,const char **l2) {
 if(sh_n)return 0;
 if(sh_bm){*l1="No bookmarks";*l2="In a book, Triangle bookmarks the page you are on";}
 else if(!bk_at_root()){*l1=bk_dir_failed?"This folder couldn't be opened":"This folder is empty";*l2="";}
 else{*l1="No books yet";*l2="Put .cbz or .zip books, or folders of pictures, in BOOKS on the Memory Stick";}
 return 1;
}
/* the foot: the theme's mini player while music plays (Triangle goes to Now Playing); otherwise, and
   in Bookmarks (SELECT removes), the theme's strip with what the buttons do */
static void bk_foot(void) {
 const char *x_label=sh_sel<sh_n && (sh[sh_sel].kind==SH_SERIES || sh[sh_sel].kind==SH_BMARKS)?"Open":"Read",*o_label=bk_at_root() && !sh_bm?"Home":"Back";
 if(active[0] && !sh_bm){if(MODERN)draw_mini();else hud_mini();return;}
 if(MODERN) {
  float y=234;rect(0,y,480,38,C(T->panel,235));rect(0,y,480,1,C(T->line,255));
  float x=16;if(sh_n)x=hint(x,y+10,IC_BTN_CROSS,x_label);x=hint(x,y+10,IC_BTN_CIRCLE,o_label);
  if(sh_bm && sh_n)x=hint_key(x,y+10,"SELECT","Remove");
  if(active[0])x=hint(x,y+10,IC_BTN_TRIANGLE,"Now Playing");
  (void)x;return;
 }
 float y=230;char a[24],b[24],c[24],d[24];
 hud_frame(4,y,472,38,0);
 const char *idle=CYBER?"// PICK A BOOK":PHOS?"> PICK A BOOK":STUDIO?"READY":"Pick a book";
 if(sh_bm)idle=CYBER?"// BOOKMARKS":PHOS?"> BOOKMARKS":STUDIO?"BOOKMARKS":"Bookmarks";
 if(PAPER)ITALIC(text(16,y+10,idle,C(PP_MUTED,255),F_MEDIUM));else text(16,y+10,idle,C(MS?ms_dim():T->dim,255),F_MEDIUM);
 /* chips, right-aligned like the mini player's */
 upper(a,x_label,sizeof(a));upper(b,o_label,sizeof(b));upper(c,"Remove",sizeof(c));upper(d,"Playing",sizeof(d));
 unsigned int lc=PAPER?PP_MUTED:MS?ms_dim():T->dim;
 float w=chip_w(IC_BTN_CIRCLE,b);if(sh_n)w+=6+chip_w(IC_BTN_CROSS,a);if(sh_bm && sh_n)w+=6+chip_w(-1,"SELECT")+4+text_w(c,F_SMALL);if(active[0])w+=6+chip_w(IC_BTN_TRIANGLE,d);
 float x=468-w;
 if(sh_n){hud_chip(x,y+11,IC_BTN_CROSS,a);x+=6+chip_w(IC_BTN_CROSS,a);}
 hud_chip(x,y+11,IC_BTN_CIRCLE,b);x+=6+chip_w(IC_BTN_CIRCLE,b);
 if(sh_bm && sh_n){hud_chip(x,y+11,-1,"SELECT");x+=chip_w(-1,"SELECT")+4;x+=text_vc(x,y+19,c,C(lc,255),F_SMALL)+6;}
 if(active[0])hud_chip(x,y+11,IC_BTN_TRIANGLE,d);
}
/* a message: the theme's own notice, as Music shows one */
static void bk_notice(void) {
 if(now_ms>=bk_toast_until)return;
 char was[96];unsigned long long was_until=toast_until;snprintf(was,sizeof(was),"%s",toast);
 snprintf(toast,sizeof(toast),"%s",bk_toast);toast_until=bk_toast_until;draw_notice();
 snprintf(toast,sizeof(toast),"%s",was);toast_until=was_until;
}
static void shelf_draw(void) {
 bk_backdrop();
 char name[160],info[200],up[200];bk_heading_text(name,sizeof(name),info,sizeof(info));
 const int rows=bk_rows();
 if(sh_sel<sh_top)sh_top=sh_sel;if(sh_sel>=sh_top+rows)sh_top=sh_sel-rows+1;
 if(sh_top>sh_n-rows)sh_top=sh_n-rows;if(sh_top<0)sh_top=0;
 sh_scroll+=(sh_top-sh_scroll)*0.4f;sh_sel_f+=(sh_sel-sh_sel_f)*0.5f;
 if(fabsf(sh_top-sh_scroll)<0.01f)sh_scroll=sh_top;if(fabsf(sh_sel-sh_sel_f)<0.01f)sh_sel_f=sh_sel;
 const char *l1,*l2;int empty=bk_empty_text(&l1,&l2);
 if(MODERN) {
  /* Zoid: as the Music list (draw_library) */
  icon(IC_CHEV_L,18,19,18,C(T->dim,255));
  text_fit(30,8,name,C(T->fg,255),F_MEDIUM,250);text_right(400,11,info,C(T->dim,255),F_SMALL);
  rect(0,36,480,1,C(T->line,255));
  int bar=sh_n>rows;float right=bar?462:470;
  clip(0,LIB_TOP-2,480,rows*ROW_H+2);
  if(sh_n)rrect(8,LIB_TOP+(sh_sel_f-sh_scroll)*ROW_H,right-8,ROW_H-2,6,C(T->raised,255));
  for(int i=(int)sh_scroll;i<sh_n && i<=(int)sh_scroll+rows;i++) {
   Shelf *s=&sh[i];float y=LIB_TOP+(i-sh_scroll)*ROW_H,cy=y+11;int sel=i==sh_sel;
   unsigned int ink=sel?T->fg:mix(T->dim,T->fg,150);
   unsigned int icc=s->kind==SH_SERIES?(sel?T->accent:mix(T->accent,T->dim,140)):s->kind==SH_BOOK?T->faint:T->accent;
   icon(bk_row_icon(s),28,cy,s->kind==SH_SERIES?18:17,C(icc,255));
   bk_row_title(44,cy,(int)(right-44-170),s,i,sel,ink,T->dim,T->accent);
   bk_row_column(0,right-10,1,cy,s,T->dim,T->accent,T->line);
  }
  unclip();
  if(bar){float track=rows*ROW_H-4,thumb=track*rows/sh_n;if(thumb<14)thumb=14;
   rrect(468,LIB_TOP,4,track,2,C(T->line,255));rrect(468,LIB_TOP+(track-thumb)*sh_scroll/(sh_n-rows),4,thumb,2,C(T->dim,255));}
  if(empty){icon(sh_bm?IC_BOOKMARK:IC_BOOK,240,110,40,C(T->faint,255));text_center(240,136,l1,C(T->dim,255),F_MEDIUM);if(l2[0])text_center(240,160,l2,C(T->faint,255),F_SMALL);}
 }else if(MS) {
  /* the Zoidstations: as ms_library */
  ms_heading(name,info,sh_bm?IC_BOOKMARK:IC_BOOK);
  const float top=48;const int rh=ms_row_h();
  if(MS1)ms1_well(4,top-2,472,rows*rh+4,M1_WELL);
  clip(0,(int)top,480,rows*rh);
  if(sh_n)ms_row_sel(MS1?6:8,top+(sh_sel_f-sh_scroll)*rh,MS1?468:464,rh-(MS1?0:2));
  for(int i=(int)sh_scroll;i<sh_n && i<=(int)sh_scroll+rows;i++) {
   Shelf *s=&sh[i];float y=top+(i-sh_scroll)*rh,cy=y+rh/2.0f-(MS1?0:1);int sel=i==sh_sel;
   if(MS1 && !sel && (i&1))rect(6,y,468,rh,C(0xbfc3c8,255));
   unsigned int tc=sel?ms_sel_ink():ms_ink(),ac=sel?(MS1?0xd6e2f4:0xffffff):ms_dim(),hot=sel?0xffffff:MS1?M1_RED:0xffffff;
   unsigned int icc=MS1?(sel?0xffffff:s->kind==SH_SERIES?M1_SEL:s->kind==SH_BOOK?M1_DIM:M1_RED):(sel || s->kind!=SH_BOOK?0xffffff:ms_dim());
   icon(bk_row_icon(s),22,cy,MSP && sel?18:15,C(icc,255));
   bk_row_title(36,cy,272,s,i,sel,tc,ac,hot);
   bk_row_column(316,462,0,cy,s,ac,hot,MS1?0x9ea3a9:0x5a6a8a);
  }
  unclip();
  if(sh_n>rows){float track=rows*rh,thumb=track*rows/sh_n;if(thumb<12)thumb=12;float ty=top+(track-thumb)*sh_scroll/(sh_n-rows);
   if(MS1){rect(472,top,3,track,C(0x9ea3a9,255));rect(472,ty,3,thumb,C(M1_DARK,255));}
   else{rect(475,top,1,track,C(0xffffff,40));rrect(474,ty,3,thumb,1.5f,C(0xffffff,MS2?200:220));if(MS2)glow(475,ty+thumb/2,8,C(M2_GLOW,90));}}
  if(empty){text_center(240,110,l1,C(ms_ink(),255),F_MEDIUM);if(l2[0])text_center(240,134,l2,C(ms_dim(),255),F_SMALL);}
 }else {
  /* the HUD skins (Cyberpunk, Phosphor, Y2K, Studio, Paper): as draw_hud_library */
  if(PAPER){paper_button(6,5,20,20,0,0);icon(IC_CHEV_L,16,15,14,C(PP_INK,255));}
  else{rect(6,5,20,20,C(T->accent,12));frame_rect(6,5,20,20,1,C(T->accent,CYBER?200:120));icon(IC_CHEV_L,16,15,14,C(T->accent,255));}
  upper(up,name,sizeof(up));int nw=text_w(up,F_MEDIUM);if(nw>230)nw=230;
  if(PAPER)ITALIC(text_fit(32,4,up,C(PP_INK,255),F_MEDIUM,230));
  else if(CYBER)text_fit(32,4,up,C(T->dim,255),F_MEDIUM,230);else text_fit(32,4,up,C(STUDIO?0xe2e2e2:T->accent,255),F_MEDIUM,230);
  {char line[220],ui[200];float bx=32+nw+8;
   if(PAPER)ITALIC(text_fit(bx,8,info,C(PP_MUTED,255),F_SMALL,(int)(400-bx)));
   else{upper(ui,info,sizeof(ui));snprintf(line,sizeof(line),"%s %s",CYBER?"//":"-",ui);text_fit(bx,8,line,C(T->dim,180),F_SMALL,(int)(400-bx));}}
  {char t1[16],t2[16];upper(t1,"Title",sizeof(t1));upper(t2,"Progress",sizeof(t2));hud_header(t1,t2);}
  clip(0,HUD_TOP,480,rows*HUD_ROW);
  for(int i=(int)sh_scroll;i<sh_n && i<=(int)sh_scroll+rows;i++)hud_row_bg(HUD_TOP+(i-sh_scroll)*HUD_ROW,i&1);
  if(sh_n)hud_row_sel(HUD_TOP+(sh_sel_f-sh_scroll)*HUD_ROW);
  for(int i=(int)sh_scroll;i<sh_n && i<=(int)sh_scroll+rows;i++) {
   Shelf *s=&sh[i];float y=HUD_TOP+(i-sh_scroll)*HUD_ROW,cy=y+11;int sel=i==sh_sel;
   unsigned int tc=sel && !PAPER?T->hot:T->fg,mc=sel?T->fg:T->dim,ac=PAPER?PP_RED:sel?T->hot:T->accent;
   unsigned int icc=s->kind==SH_SERIES?T->folder:s->kind==SH_BOOK?T->dim:ac;
   icon(bk_row_icon(s),16,cy,15,C(icc,255));
   bk_row_title(30,cy,268,s,i,sel,tc,mc,ac);
   bk_row_column(316,466,0,cy,s,mc,ac,PAPER?PP_LINE:T->line);
  }
  unclip();
  if(sh_n>rows){float track=rows*HUD_ROW,thumb=track*rows/sh_n;if(thumb<12)thumb=12;
   if(!PAPER)rect(476,HUD_TOP,3,track,C(T->bg2,255));rect(476,HUD_TOP+(track-thumb)*sh_scroll/(sh_n-rows),3,thumb,C(CYBER?0x2a3a55:PAPER?PP_RIM:T->select,255));}
  if(empty){char u1[96],u2[120];upper(u1,l1,sizeof(u1));upper(u2,l2,sizeof(u2));
   text_center(240,110,u1,C(T->dim,255),F_MEDIUM);if(l2[0])text_center(240,134,u2,C(T->faint,255),F_SMALL);}
 }
 bk_foot();
 bk_notice();
}

static void shelf_input(unsigned int press) {
 if(press&PSP_CTRL_UP && sh_sel>0)sh_sel--;
 if(press&PSP_CTRL_DOWN && sh_sel<sh_n-1)sh_sel++;
 if(press&PSP_CTRL_LEFT){sh_sel-=bk_rows();if(sh_sel<0)sh_sel=0;}
 if(press&PSP_CTRL_RIGHT){sh_sel+=bk_rows();if(sh_sel>sh_n-1)sh_sel=sh_n>0?sh_n-1:0;}
 {const unsigned int keys[]={PSP_CTRL_START,PSP_CTRL_LTRIGGER,PSP_CTRL_RTRIGGER,PSP_CTRL_SQUARE};
  for(int i=0;i<4;i++)if(press&keys[i]){bk_shelf_action(bindings[i]);if(screen!=SCR_BOOKS)return;}}
 if(press&PSP_CTRL_TRIANGLE && active[0]){books_leave(SCR_NOW);return;}
 if(press&PSP_CTRL_CROSS && sh_sel<sh_n) {
  Shelf *s=&sh[sh_sel];char p[PATHLEN];
  if(s->kind==SH_RESUME)rd_open(sh_resume);
  else if(s->kind==SH_BMARKS){sh_bm=1;shelf_scan(NULL);}
  else if(s->kind==SH_BMARK) {
   Bmark b=bms[s->bm];
   if(rd_open(b.path)){char t[160],n[128];rd_goto(b.page,b.scroll,b.level);bk_full_title(b.path,n,sizeof(n));snprintf(t,sizeof(t),"%s, page %d",n,rd_page+1);bk_say(t);}
  }
  else if(s->kind==SH_SERIES){snprintf(p,sizeof(p),"%s/%s",bk_dir,s->name);snprintf(bk_dir,sizeof(bk_dir),"%s",p);shelf_scan(NULL);}
  else if(s->rar)bk_say_for("A RAR book (.cbr): run Shrink Books for PSP on it",3500);
  else{snprintf(p,sizeof(p),"%s/%s",bk_dir,s->name);rd_open(p);}
  return;
 }
 if(press&PSP_CTRL_SELECT && sh_bm && sh_sel<sh_n) {
  char t[64];snprintf(t,sizeof(t),"Bookmark on page %d removed",bms[sh[sh_sel].bm].page+1);
  int was=sh_sel;bm_del(sh[sh_sel].bm);bm_scan(was);bk_say(t);return;
 }
 if(press&PSP_CTRL_CIRCLE) {
  if(sh_bm){sh_bm=0;shelf_scan(NULL);for(int i=0;i<sh_n;i++)if(sh[i].kind==SH_BMARKS){sh_sel=i;break;}return;}
  if(bk_at_root()){books_leave(SCR_HOME);return;}
  char came[256];snprintf(came,sizeof(came),"%s",bk_base(bk_dir));
  char *sl=strrchr(bk_dir,'/');if(sl)*sl=0;
  if(strncmp(bk_dir,books_root,strlen(books_root)))snprintf(bk_dir,sizeof(bk_dir),"%s",books_root);
  shelf_scan(came);
 }
}

/* ---- entering, leaving, and the frame ---- */
static char bk_back_sel[200]; /* the shelf row you were on when you went to Now Playing */
static void books_enter_at(const char *select) {
 bk_style(); /* Books wears the theme picked in Settings */
 screen=SCR_BOOKS;bk_mode=0;
 if(!bk_dir[0] || strncmp(bk_dir,books_root,strlen(books_root)) || !dir_exists(bk_dir))snprintf(bk_dir,sizeof(bk_dir),"%s",books_root);
 if(!dir_exists(books_root))sceIoMkdir(books_root,0777); /* so there is a place to put them */
 shelf_scan(select);
}
static void books_enter(void){books_enter_at(NULL);}
/* back from Now Playing: the same shelf, on the same row */
static void books_return(void){char s[200];snprintf(s,sizeof(s),"%s",bk_back_sel);bk_back_sel[0]=0;books_enter_at(s[0]?s:NULL);}
static void books_leave(int to) {
 bk_back_sel[0]=0;
 if(to==SCR_NOW && bk_mode==0 && sh_sel<sh_n)snprintf(bk_back_sel,sizeof(bk_back_sel),"%s",sh[sh_sel].name);
 rd_close();bk_mode=0;
 if(marks_due)marks_save();
 /* (Books never changed the theme, so there is nothing to restore) */
 if(to==SCR_NOW && active[0]){np_return=SCR_BOOKS;screen=SCR_NOW;np_focus=2;}else go_home(HOME_BOOKS);
}
static void books_init(const char *startup,int prefix) {
 snprintf(marks_file,sizeof(marks_file),"%.*szoid-books.txt",prefix,startup);
 marks_load();
 snprintf(bm_file,sizeof(bm_file),"%.*szoid-bookmarks.txt",prefix,startup);bm_load();
 snprintf(view_file,sizeof(view_file),"%.*szoid-books-view.txt",prefix,startup);view_load();
 snprintf(book_names_log,sizeof(book_names_log),"%.*szoid-books-names.txt",prefix,startup); /* odd names, for checking on a PSP */
 for(int i=0;i<SLOT_N;i++)slot[i].page=-1;
}
static void books_shutdown(void){if(bk_is_open)rd_mark();if(marks_due)marks_save();}
/* the Home tile's second line */
static void books_home_sub(char *out,int cap){snprintf(out,cap,"%s",mark_latest()>=0?"Reading":"Manga");}
static void books_frame(unsigned int press,unsigned int raw,const SceCtrlData *pad) {
 if(bk_mode==1){rd_service();rd_input(press,raw,pad);}
 else shelf_input(press);
 if(screen!=SCR_BOOKS)return; /* left for Home or Now Playing */
 bk_song_tick();
 if(bk_mode==1)rd_service();
 if(marks_due && now_ms>=marks_due)marks_save();
 /* the PSP's own backlight timer: kept awake while you read, for up to 5 minutes without a button */
 {static unsigned long long tick;if(bk_mode==1 && now_ms-rd_last_input<300000 && now_ms-tick>2000){tick=now_ms;scePowerTick(PSP_POWER_TICK_DISPLAY);}}
 bk_style();frame_begin(C(P_BG,255));
 if(bk_mode==1 && rd_vert){portrait_begin(C(P_BG,255));rd_draw();portrait_end();} /* drawn upright, then turned onto the screen */
 else if(bk_mode==1)rd_draw();else shelf_draw();
 if(PHOS && bk_mode==0)phosphor_glass(); /* the tube over the shelf, as on every Phosphor screen */
 if(bk_mode==0)draw_battery(); /* the theme's own battery, where every other screen has it */
 draw_volume();
 frame_end();
}
