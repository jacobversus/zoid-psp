/* The Books smoke test (run-test.ps1 -Kind Books, always muted). BOOKS on the emulator's stick holds
   trimmed copies of real volumes made by run-test.ps1: a series folder with a progressive-JPEG volume
   (with a two-page spread at the end) and a PNG volume, a baseline-JPEG volume at the top, a folder
   of images, and macOS "._" junk that must stay hidden. One step a frame or more; waits time out. */
#include <stdarg.h>
static int bt_step,bt_at,bt_mem0;
static void bt_next(int f){bt_step++;bt_at=f;}
static int bt_ready(int page,int level){Slot *s=slot_find(page,level);return s && s->img.px;}
static PageImg *bt_img(int page,int level){Slot *s=slot_find(page,level);return s && s->img.px?&s->img:NULL;}
static void bt_info(const char *fmt,...) {
 FILE *f=fopen("ms0:/smoke.txt","a");if(!f)return;
 va_list ap;va_start(ap,fmt);fprintf(f,"info: ");vfprintf(f,fmt,ap);fprintf(f,"\n");va_end(ap);fclose(f);
}
static int bt_find(const char *prefix){for(int i=0;i<sh_n;i++)if(sh[i].kind!=SH_RESUME && !strncmp(sh[i].name,prefix,strlen(prefix)))return i;return -1;}
static int bt_used(void){struct mallinfo mi=mallinfo();return mi.uordblks;}
/* waits for a page; 1 when it is there, -1 after about 25 s */
static int bt_wait(int f,int page,int level,const char *what) {
 if(bt_ready(page,level))return 1;
 if(f-bt_at>1500){check(what,0);return -1;}
 return 0;
}
static void books_test_frame(int f) {
 if(f<20)return;
 /* every page made: how long it took in the emulator (a real PSP may differ) */
 if(ld_state==3 && ld_result==1)bt_info("decoded %s p%d level %d: %dx%d -> %dx%d %s in %u ms",bk_base(bk.path),ld_page+1,ld_level,ld_img.src_w,ld_img.src_h,ld_img.w,ld_img.h,ld_img.fmt==PG_GRAY?"gray":"color",ld_ms);
 int since=f-bt_at,w;
 switch(bt_step) {
 case 0:
  sceIoRemove(marks_file);marks_load();sceIoRemove(bm_file);bm_load();sceIoRemove(view_file);view_load();
  go_home(HOME_BOOKS);check("Books is the fifth tile on Home",HOME_TILES==5 && home_sel==HOME_BOOKS && !strcmp(home_names[HOME_BOOKS],"Books"));
  capture("ms0:/books-home-0.rgba");bt_next(f);break;
 case 1:case 2:case 3:case 4: /* Home in every theme, with five tiles */
  if(since<3)break;
  theme=bt_step;T=&themes[theme];fonts_build(theme);go_home(HOME_BOOKS);
  {char p[40];snprintf(p,sizeof(p),"ms0:/books-home-%d.rgba",theme);capture(p);}
  bt_next(f);break;
 case 5:
  if(since<3)break;
  theme=0;T=&themes[0];fonts_build(0);
  home_input(PSP_CTRL_CROSS);
  check("X on Books opens the shelf in the theme picked (Zoid here), with its font",screen==SCR_BOOKS && bk_mode==0 && T==&themes[0] && fonts[0].data==&font_zoid_small[0][0] && P_BG==themes[0].bg);
  {int junk=0;for(int i=0;i<sh_n;i++)if(sh[i].name[0]=='.')junk++;
   check("the shelf lists two folders, then two books, and hides macOS junk",sh_n==4 && sh[0].kind==SH_SERIES && sh[1].kind==SH_SERIES && sh[2].kind==SH_BOOK && !junk && bt_find("Gunsmith Cats - Burst")>=0 && sh[bt_find("Gunsmith Cats - Burst")].kind==SH_SERIES);}
  for(int i=0;i<sh_n;i++)bt_info("shelf %d: kind %d title \"%s\" note \"%s\"",i,sh[i].kind,sh[i].title,sh[i].note);
  {int r=bt_find("Gunsmith Cats - Revised");check("a volume's brackets become its note",r>=0 && !strcmp(sh[r].title,"Gunsmith Cats - Revised Edition 03") && !strcmp(sh[r].note,"Dark Horse 2007, fixed"));}
  capture("ms0:/books-shelf.rgba");bt_next(f);break;
 case 6:
  if(since<3)break;
  sh_sel=bt_find("Gunsmith Cats - Burst");shelf_input(PSP_CTRL_CROSS);
  check("X on a series opens it",!strcmp(bk_base(bk_dir),"Gunsmith Cats - Burst") && sh_n==2);
  check("volumes are named Volume 1 and Volume 4",sh_n==2 && !strcmp(sh[0].title,"Volume 1") && !strcmp(sh[1].title,"Volume 4"));
  for(int i=0;i<sh_n;i++)bt_info("series %d: \"%s\" note \"%s\"",i,sh[i].title,sh[i].note);
  capture("ms0:/books-series.rgba");bt_next(f);break;
 case 7:
  if(since<3)break;
  bt_mem0=bt_used();sh_sel=0;shelf_input(PSP_CTRL_CROSS);
  check("X on a volume opens the reader",bk_mode==1 && bk_is_open && bk.n>=10);
  check("a manga opens right to left",rd_rtl==1);
  bt_info("Volume 1: %d pages, first \"%s\", last \"%s\"",bk.n,book_page_name(&bk,0),book_page_name(&bk,bk.n-1));
  bt_next(f);break;
 case 8: /* page 1 of every volume here is its color wraparound cover */
  if(!(w=bt_wait(f,0,LV_W1,"the first page decodes")))break;
  if(w>0){PageImg *im=bt_img(0,LV_W1);check("the cover fills the full width (480) and keeps its color",im->w==480 && im->fmt==PG_RGB565);}
  capture("ms0:/books-page1.rgba");bt_next(f);break;
 case 9:
  if(since<2)break;
  rd_turn(1);rd_turn(1);bt_next(f);break;
 case 10:
  if(!(w=bt_wait(f,2,LV_W1,"an inside page decodes")))break;
  if(w>0){PageImg *im=bt_img(2,LV_W1);check("inside pages fill the full width as 8-bit gray, taller than the screen",im->w==480 && im->fmt==PG_GRAY && im->h>272);}
  capture("ms0:/books-page3.rgba");bt_next(f);break;
 case 11:
  rd_input(0,PSP_CTRL_DOWN,NULL);
  if(since<40)break;
  check("holding Down scrolls the page",rd_y>100);bt_info("scrolled to %.0f",rd_y);
  capture("ms0:/books-scrolled.rgba");bt_next(f);break;
 case 12:
  if(since<2)break;
  if(!(w=bt_wait(f,3,LV_W1,"the next page is decoded ahead")))break;
  check("the next page is ready before R is pressed",w>0);
  rd_input(PSP_CTRL_LTRIGGER,0,NULL);check("right to left: L turns to the next page, at its top",rd_page==3 && rd_y==0);
  bt_next(f);break;
 case 13:
  if(since<2)break;
  capture("ms0:/books-page4.rgba");bt_next(f);break;
 case 14:
  if(since<2)break;
  rd_input(PSP_CTRL_RTRIGGER,0,NULL);check("right to left: R goes back a page",rd_page==2);
  rd_input(PSP_CTRL_CROSS,0,NULL);check("X zooms in to 1.5x",rd_level==LV_Z1);
  bt_next(f);break;
 case 15:
  if(!(w=bt_wait(f,2,LV_Z1,"1.5x zoom decodes")))break;
  if(w>0){PageImg *im=bt_img(2,LV_Z1);check("1.5x is made at 720 wide, not stretched",im->w==720);}
  check("zooming in a right-to-left book starts at the right edge",rd_x>=239);
  capture("ms0:/books-zoom15.rgba");bt_next(f);break;
 case 16:
  if(since<2)break;
  rd_input(PSP_CTRL_CROSS,0,NULL);check("X again zooms to 2x",rd_level==LV_Z2);bt_next(f);break;
 case 17:
  if(!(w=bt_wait(f,2,LV_Z2,"2x zoom decodes")))break;
  if(w>0){PageImg *im=bt_img(2,LV_Z2);check("2x is made at 960 wide",im->w==960);}
  {SceCtrlData p;memset(&p,0,sizeof(p));p.Lx=255;p.Ly=128;float x0=rd_x;for(int i=0;i<20;i++)rd_input(0,0,&p);check("the stick pans across a zoomed page",rd_x>x0+50);}
  capture("ms0:/books-zoom2.rgba");bt_next(f);break;
 case 18:
  if(since<2)break;
  rd_input(PSP_CTRL_CIRCLE,0,NULL);rd_input(PSP_CTRL_CIRCLE,0,NULL);check("Circle zooms back out to full width",rd_level==LV_W1 && bk_mode==1);
  {static const int want[3]={408,336,264};int ok=1;
   for(int i=0;i<3;i++){rd_input(PSP_CTRL_SQUARE,0,NULL);if(rd_level!=LV_O1+i || lv_width(rd_level,0)!=want[i])ok=0;}
   check("Square zooms out three steps: 408, 336, 264 wide",ok);}
  rd_input(PSP_CTRL_SQUARE,0,NULL);check("then Square shows the whole page",rd_level==LV_FIT);
  bt_next(f);break;
 case 19:
  if(!(w=bt_wait(f,2,LV_FIT,"the whole page decodes")))break;
  if(w>0){PageImg *im=bt_img(2,LV_FIT);check("the whole page fits the screen",im->h<=272 && im->w<=480 && (im->h>=270 || im->w>=478));bt_info("whole page: %dx%d",im->w,im->h);}
  capture("ms0:/books-whole.rgba");bt_next(f);break;
 case 20:
  if(since<2)break;
  rd_input(PSP_CTRL_CROSS,0,NULL);check("X from the whole page steps in to 55%",rd_level==LV_O3);
  rd_input(PSP_CTRL_SQUARE,0,NULL);check("Square steps back out to the whole page",rd_level==LV_FIT);
  rd_input(PSP_CTRL_SQUARE,0,NULL);check("Square again returns to full width",rd_level==LV_W1);
  /* holding X zooms smoothly to a size between the steps, kept when let go; holding Square zooms out */
  rd_input(PSP_CTRL_CROSS,PSP_CTRL_CROSS,NULL);for(int i=0;i<25;i++)rd_input(0,PSP_CTRL_CROSS,NULL);rd_input(0,0,NULL);
  check("holding X zooms smoothly past 1.5x, between the steps",rd_level==LV_FREE && rd_free>1.6f && rd_free<1.95f && !rd_zooming);
  bt_info("held X: %.3fx, saved as %d",rd_free,lv_code(rd_level));
  {int m=mark_find(bk.path);check("the smooth zoom is saved with the book",m>=0 && marks[m].level==lv_code(LV_FREE) && marks[m].level>1000);}
  {float z=rd_free;rd_input(PSP_CTRL_SQUARE,PSP_CTRL_SQUARE,NULL);for(int i=0;i<20;i++)rd_input(0,PSP_CTRL_SQUARE,NULL);rd_input(0,0,NULL);
   check("holding Square zooms smoothly out",rd_level==LV_FREE && rd_free<z-0.3f);}
  rd_input(PSP_CTRL_CIRCLE,0,NULL);check("Circle from a smooth zoom steps out to full width",rd_level==LV_W1 && bk_mode==1);
  rd_input(PSP_CTRL_TRIANGLE,0,NULL);check("Triangle opens Bookmarks",rd_menu && !rd_help);
  rd_input(PSP_CTRL_DOWN,0,NULL);rd_input(PSP_CTRL_DOWN,0,NULL);rd_input(PSP_CTRL_CROSS,0,NULL);check("its last row shows the controls",rd_help && !rd_menu && !rd_vert);
  bt_next(f);break;
 case 21:
  if(since<2)break;
  capture("ms0:/books-help.rgba");bt_next(f);break;
 case 22:
  if(since<2)break;
  rd_input(PSP_CTRL_TRIANGLE,0,NULL);check("Triangle closes the controls",!rd_help);
  {int p=rd_page;rd_input(PSP_CTRL_SELECT,0,NULL);check("SELECT switches to left to right",rd_rtl==0);
   rd_input(PSP_CTRL_RTRIGGER,0,NULL);check("left to right: R is the next page",rd_page==p+1);
   rd_input(PSP_CTRL_LTRIGGER,0,NULL);check("left to right: L goes back",rd_page==p);
   rd_input(PSP_CTRL_SELECT,0,NULL);check("SELECT again: right to left",rd_rtl==1);}
  while(rd_page<bk.n-1)rd_turn(1);
  bt_next(f);break;
 case 23:
  if(!(w=bt_wait(f,bk.n-1,LV_W1,"the last page decodes")))break;
  if(w>0){PageImg *im=bt_img(bk.n-1,LV_W1);check("a two-page spread is shown across the full width",im->src_w>im->src_h && im->w==480);}
  capture("ms0:/books-spread.rgba");bt_next(f);break;
 case 24:
  if(since<2)break;
  rd_input(PSP_CTRL_LTRIGGER,0,NULL);check("the next-page button on the last page stays there",rd_page==bk.n-1);
  rd_page=0;rd_y=0;bt_next(f);break;
 case 25: /* next page pressed five frames in a row: pages being made for skipped pages are dropped */
  rd_input(PSP_CTRL_LTRIGGER,0,NULL);
  if(since<5)break;
  check("fast page turns keep up",rd_page==5);bt_next(f);break;
 case 26:
  if(!(w=bt_wait(f,5,LV_W1,"the page after fast turns decodes")))break;
  check("the page after fast turns shows",w>0);
  rd_input(PSP_CTRL_SELECT,0,NULL); /* Volume 1 left to right: Volume 4 should follow it */
  rd_input(PSP_CTRL_CIRCLE,0,NULL);
  check("Circle at full width closes the book",bk_mode==0 && !bk_is_open && sh_n==2 && sh_sel==0);
  {int used=bt_used();bt_info("heap in use: %d before opening, %d after closing",bt_mem0,used);check("closing a book frees its pages",used-bt_mem0<96*1024);}
  marks_save();marks_load();
  {char p[PATHLEN];snprintf(p,sizeof(p),"%s/%s",bk_dir,sh[0].name);int m=mark_find(p);check("the page you were on is saved and read back",m>=0 && marks[m].page==5 && marks[m].pages>=10);
   check("the reading direction is saved with it",m>=0 && marks[m].rtl==0);}
  capture("ms0:/books-series-progress.rgba");bt_next(f);break;
 case 27:
  if(since<3)break;
  sh_sel=1;shelf_input(PSP_CTRL_CROSS);check("the PNG volume opens",bk_mode==1 && bk.n>=6);
  check("a new volume takes its series' reading direction",rd_rtl==0);
  rd_input(PSP_CTRL_SELECT,0,NULL);check("and can be switched back",rd_rtl==1);
  bt_next(f);break;
 case 28:
  if(!(w=bt_wait(f,0,LV_W1,"a PNG page decodes")))break;
  if(w>0){PageImg *im=bt_img(0,LV_W1);check("PNG pages decode at full width",im->w==480);}
  bt_next(f);break;
 case 29:
  if(!(w=bt_wait(f,1,LV_W1,"the second PNG page decodes")))break;
  rd_turn(1);bt_next(f);break;
 case 30:
  if(since<2)break;
  capture("ms0:/books-png.rgba");bt_next(f);break;
 case 31:
  if(since<2)break;
  rd_input(PSP_CTRL_CIRCLE,0,NULL);shelf_input(PSP_CTRL_CIRCLE);
  check("Circle in a series goes back up, with Continue reading on top",bk_at_root() && sh_n==5 && sh[0].kind==SH_RESUME && !strcmp(sh[0].title,"Gunsmith Cats - Burst: Volume 4"));
  capture("ms0:/books-resume.rgba");bt_next(f);break;
 case 32:
  if(since<3)break;
  sh_sel=bt_find("Gunsmith Cats - Revised");shelf_input(PSP_CTRL_CROSS);rd_turn(1);rd_turn(1);bt_next(f);break;
 case 33:
  if(!(w=bt_wait(f,2,LV_W1,"a baseline JPEG page decodes")))break;
  capture("ms0:/books-revised.rgba");bt_next(f);break;
 case 34:
  if(since<2)break;
  rd_input(PSP_CTRL_CIRCLE,0,NULL);sh_sel=bt_find("Folder Book");shelf_input(PSP_CTRL_CROSS);
  check("a folder of images opens as a book, without its junk files",bk_mode==1 && bk.n==8);
  bt_next(f);break;
 case 35:
  if(!(w=bt_wait(f,0,LV_W1,"a page from a folder decodes")))break;
  check("a page from a folder shows",w>0);
  capture("ms0:/books-folder.rgba");bt_next(f);break;
 case 36:
  if(since<2)break;
  rd_input(PSP_CTRL_CIRCLE,0,NULL);shelf_input(PSP_CTRL_CIRCLE);
  check("Circle on the shelf goes Home, on Books, in the theme's own font",screen==SCR_HOME && home_sel==HOME_BOOKS && T==&themes[0] && fonts[0].data==&font_zoid_small[0][0]);
  {SceIoStat st;check("progress is in zoid-books.txt",sceIoGetstat(marks_file,&st)>=0 && st.st_size>0);}
  capture("ms0:/books-home-after.rgba");bt_next(f);break;
 /* music while reading (muted, as every test build is): three songs from Queue Test */
 case 37:
  if(since<3)break;
  bindings[0]=0;bindings[1]=1;bindings[2]=2;bindings[3]=6;shuffle=0;repeat_mode=0;hold_idx=0;
  {static Paths p;paths_clear(&p);char a[PATHLEN];for(int i=1;i<=3;i++){snprintf(a,sizeof(a),"%s/Queue Test/0%d - Queue Test.mp3",root,i);paths_add(&p,a);}queue_start_paths(&p,0);}
  home_activate(HOME_BOOKS);bt_next(f);break;
 case 38:
  if(!(state==2 && elapsed>=1)){if(since>1500){check("music starts for the Books music test",0);bt_step=45;}break;}
  check("Books opens with the music still playing",screen==SCR_BOOKS && is_playing());
  shelf_input(PSP_CTRL_START);check("START on the shelf pauses and says so",paused && !strcmp(bk_toast,"Music paused"));
  shelf_input(PSP_CTRL_START);check("START again plays and names the song",!paused && !strncmp(bk_toast,"Playing: ",9));
  shelf_input(PSP_CTRL_RTRIGGER);check("R on the shelf goes to the next song",!strcmp(bk_toast,"Next song"));
  bt_next(f);break;
 case 39:
  if(!(strstr(active,"/02 - ") && !strncmp(bk_toast,"Playing: ",9))){if(since>600){check("the next song plays and its name is shown",0);bt_next(f);}break;}
  check("the next song plays and its name is shown",1);bt_info("toast: %s",bk_toast);
  capture("ms0:/books-shelf-music.rgba");bt_next(f);break;
 case 40:
  if(since<3)break;
  {int s=bt_find("Gunsmith Cats - Burst");sh_sel=s;
   shelf_input(PSP_CTRL_TRIANGLE);check("Triangle on the shelf goes to Now Playing, in the theme's own look",screen==SCR_NOW && np_return==SCR_BOOKS && T==&themes[theme]);
   input(PSP_CTRL_CIRCLE);check("Circle on Now Playing comes back to Books, on the same row",screen==SCR_BOOKS && bk_mode==0 && T==&themes[theme] && sh_sel==s);
   shelf_input(PSP_CTRL_TRIANGLE);input(PSP_CTRL_TRIANGLE);check("so does Triangle",screen==SCR_BOOKS && sh_sel==s);}
  shelf_input(PSP_CTRL_CROSS);sh_sel=0;shelf_input(PSP_CTRL_CROSS);
  check("Volume 1 opens again",bk_mode==1 && strstr(bk.path,"Burst 01"));
  rd_page=bk.n-1;rd_y=0;rd_turn(1);
  check("next page on the last page offers the next volume",rd_page==bk.n-1 && strstr(bk.path,"Burst 01") && strstr(bk_toast,"again for Volume 4"));bt_info("toast: %s",bk_toast);
  bt_next(f);break;
 case 41:
  if(since==2)capture("ms0:/books-next-volume.rgba"); /* taken as this frame ends */
  if(since<4)break;
  rd_turn(1);check("next page again opens Volume 4",bk_mode==1 && strstr(bk.path,"Burst 04") && !strcmp(bk_toast,"Volume 4"));
  rd_page=bk.n-1;rd_turn(1);check("the last volume ends there",rd_page==bk.n-1 && !strcmp(bk_toast,"That was the last page"));
  {char p[PATHLEN];snprintf(p,sizeof(p),"%s/Gunsmith Cats - Revised Edition 03 (Dark Horse 2007) [fixed].cbz",books_root);rd_open(p);rd_page=bk.n-1;rd_turn(1);
   check("a loose book at the top of BOOKS has no next volume",bk_is_open && strstr(bk.path,"Revised") && rd_page==bk.n-1 && !strcmp(bk_toast,"That was the last page"));}
  bt_next(f);break;
 case 42:
  if(since<2)break;
  {int was=paused;rd_input(PSP_CTRL_START,PSP_CTRL_START,NULL);rd_input(0,0,NULL);check("a tap of START in a book pauses",paused!=was && paused);
   rd_input(PSP_CTRL_START,PSP_CTRL_START,NULL);rd_input(0,0,NULL);check("and plays",!paused && !strncmp(bk_toast,"Playing: ",9));}
  rd_input(PSP_CTRL_START,PSP_CTRL_START,NULL);for(int i=0;i<70;i++)rd_input(0,PSP_CTRL_START,NULL);rd_input(0,0,NULL);
  check("holding START in a book goes to the next song",!strcmp(bk_toast,"Next song") && !paused);
  bt_next(f);break;
 case 43:
  if(!(strstr(active,"/03 - ") && !strncmp(bk_toast,"Playing: ",9))){if(since>600){check("the song after that plays and its name is shown",0);bt_next(f);}break;}
  check("the song after that plays and its name is shown",1);
  rd_help=1;bt_next(f);break;
 case 44:
  if(since==2)capture("ms0:/books-help-music.rgba");
  if(since<4)break;
  rd_help=0;
  books_leave(SCR_HOME);home_input(PSP_CTRL_TRIANGLE);check("Triangle on Home goes to Now Playing",screen==SCR_NOW && np_return==SCR_HOME);
  input(PSP_CTRL_CIRCLE);check("Circle on Now Playing goes back Home",screen==SCR_HOME);
  home_activate(HOME_MUSIC);input(PSP_CTRL_TRIANGLE);input(PSP_CTRL_CIRCLE);check("from the Music list it goes back to the list",screen==SCR_LIBRARY);
  request(NULL);go_home(HOME_BOOKS);bt_next(f);break;
 /* folders inside folders, like a file explorer, and a .zip book */
 case 45:
  if(since<3)break;
  bk_dir[0]=0;home_activate(HOME_BOOKS); /* Books opens where you were: start from the top */
  {int l=bt_find("Library");check("a folder of folders is a folder, with what it holds as its note",l>=0 && sh[l].kind==SH_SERIES && !strcmp(sh[l].title,"Library") && !strcmp(sh[l].note,"3 folders"));
   if(l<0){bt_step=48;break;}sh_sel=l;}
  shelf_input(PSP_CTRL_CROSS);
  for(int i=0;i<sh_n;i++)bt_info("library %d: kind %d \"%s\" note \"%s\"",i,sh[i].kind,sh[i].title,sh[i].note);
  check("inside it: folders first, an empty one too, then a folder of pictures",sh_n==3 && !strcmp(sh[0].name,"Chapters") && !strcmp(sh[0].note,"2 books") && !strcmp(sh[1].name,"Empty") && !strcmp(sh[1].note,"Empty") && sh[2].kind==SH_BOOK && !strcmp(sh[2].note,"2 pictures"));
  sh_sel=1;shelf_input(PSP_CTRL_CROSS);check("an empty folder opens, and says so",sh_n==0 && !strcmp(bk_base(bk_dir),"Empty"));
  shelf_input(PSP_CTRL_CIRCLE);check("Circle goes back up to it",!strcmp(bk_base(bk_dir),"Library") && sh_sel==1);
  sh_sel=0;shelf_input(PSP_CTRL_CROSS);
  check("a folder two down opens, with its .zip book",!strcmp(bk_base(bk_dir),"Chapters") && sh_n==2 && sh[0].kind==SH_BOOK && !strcmp(sh[0].title,"Chapter 01"));
  capture("ms0:/books-nested.rgba");bt_next(f);break;
 case 46:
  if(since<3)break;
  sh_sel=0;shelf_input(PSP_CTRL_CROSS);check("a .zip book opens",bk_mode==1 && bk_is_open && bk.n==4);
  bt_next(f);break;
 case 47:
  if(!(w=bt_wait(f,0,LV_W1,"a page from a .zip decodes")))break;
  check("a page from a .zip shows",w>0);
  rd_input(PSP_CTRL_CIRCLE,0,NULL);check("Circle goes back to its folder",bk_mode==0 && !strcmp(bk_base(bk_dir),"Chapters"));
  shelf_input(PSP_CTRL_CIRCLE);shelf_input(PSP_CTRL_CIRCLE);check("and up to the top, one folder at a time",bk_at_root());
  books_leave(SCR_HOME);bt_next(f);break;
 /* bookmarks: any spot in any book, from the book or from the shelf */
 case 48:
  if(since<3)break;
  {char p[PATHLEN];snprintf(p,sizeof(p),"%s/Library/Chapters/Chapter 01.zip",books_root);home_activate(HOME_BOOKS);rd_open(p);}
  rd_goto(2,0,LV_W1);
  rd_input(PSP_CTRL_TRIANGLE,0,NULL);check("Triangle in a book opens Bookmarks, on Bookmark this page",rd_menu && rd_menu_sel==0);
  rd_input(PSP_CTRL_CROSS,0,NULL);check("X bookmarks the page",!rd_menu && bm_n==1 && bms[0].page==2 && strstr(bms[0].path,"Chapter 01.zip") && !strcmp(bk_toast,"Bookmarked page 3"));
  bm_load();check("the bookmark is saved to zoid-bookmarks.txt",bm_n==1 && bms[0].page==2);
  rd_goto(0,0,LV_W1);rd_input(PSP_CTRL_TRIANGLE,0,NULL);
  {int ids[RD_MENU_MAX];int n=rd_menu_items(ids);check("the book's bookmarks are listed",n==4 && ids[1]==0);}
  rd_input(PSP_CTRL_DOWN,0,NULL);bt_next(f);break;
 case 49:
  if(since==2)capture("ms0:/books-bookmark-menu.rgba");
  if(since<4)break;
  rd_input(PSP_CTRL_CROSS,0,NULL);check("X on a bookmark goes to its page",!rd_menu && rd_page==2);
  bt_next(f);break;
 case 50:
  if(!(w=bt_wait(f,2,LV_W1,"the bookmarked page decodes")))break;
  bt_next(f);break;
 case 51:
  if(since==2)capture("ms0:/books-bookmarked.rgba");
  if(since<4)break;
  rd_input(PSP_CTRL_CIRCLE,0,NULL);
  snprintf(bk_dir,sizeof(bk_dir),"%s",books_root);sh_bm=0;shelf_scan(NULL);
  {int r=-1;for(int i=0;i<sh_n;i++)if(sh[i].kind==SH_BMARKS)r=i;
   check("the top of the shelf has Bookmarks, under Continue reading",r==1 && sh[0].kind==SH_RESUME && !strcmp(sh[r].note,"1 bookmark"));
   if(r<0){bt_step=53;break;}sh_sel=r;}
  shelf_input(PSP_CTRL_CROSS);
  check("X opens the list of every bookmark",sh_bm && sh_n==1 && sh[0].kind==SH_BMARK && !strcmp(sh[0].title,"Chapters: Chapter 01") && !strcmp(sh[0].note,"Library / Chapters"));
  bt_info("bookmark: \"%s\" note \"%s\"",sh_n?sh[0].title:"",sh_n?sh[0].note:"");
  capture("ms0:/books-bookmarks.rgba");bt_next(f);break;
 case 52:
  if(since<3)break;
  rd_close();snprintf(bk_dir,sizeof(bk_dir),"%s",books_root);
  shelf_input(PSP_CTRL_CROSS);check("X on a bookmark opens its book at its page",bk_mode==1 && bk_is_open && rd_page==2 && strstr(bk.path,"Chapter 01.zip"));
  rd_input(PSP_CTRL_CIRCLE,0,NULL);check("Circle in the book comes back to the bookmarks",bk_mode==0 && sh_bm && sh_sel==0);
  shelf_input(PSP_CTRL_SELECT);check("SELECT removes a bookmark",bm_n==0 && sh_n==0 && sh_bm);
  shelf_input(PSP_CTRL_CIRCLE);
  {int r=-1;for(int i=0;i<sh_n;i++)if(sh[i].kind==SH_BMARKS)r=i;check("Circle goes back up, and with none left the Bookmarks row goes",!sh_bm && bk_at_root() && r<0);}
  books_leave(SCR_HOME);bt_next(f);break;
 /* large progressive JPEGs (Do You Like Big Girls): a 4500 x 3206 spread needs about 29 MB to decode */
 case 53:
  if(since<3)break;
  {char p[PATHLEN];snprintf(p,sizeof(p),"%s/Library/Chapters/Chapter 02.cbz",books_root);SceIoStat st;
   if(sceIoGetstat(p,&st)<0){bt_info("no large progressive volume in the test books (make-book-test.ps1 -Progressive)");bt_step=56;break;}
   home_activate(HOME_BOOKS);rd_open(p);check("the large progressive volume opens",bk_is_open && bk.n==2);}
  bt_info("heap in use before its pages: %d",bt_used());
  bt_next(f);break;
 case 54:case 55: { /* page 1, an ordinary page, then page 2, the biggest spread */
  int pg=bt_step-53;Slot *s=slot_find(pg-1,LV_W1);
  if(s && s->failed){check(pg==1?"an ordinary large progressive page decodes":"the 4500 x 3206 progressive spread decodes",0);if(pg==1)rd_goto(1,0,LV_W1);bt_next(f);break;}
  if(!(w=bt_wait(f,pg-1,LV_W1,pg==1?"an ordinary large progressive page decodes":"the 4500 x 3206 progressive spread decodes")))break;
  if(w>0){PageImg *im=bt_img(pg-1,LV_W1);check(pg==1?"an ordinary large progressive page decodes":"the 4500 x 3206 progressive spread decodes",im->w==480);}
  if(pg==1)rd_goto(1,0,LV_W1);else{capture("ms0:/books-big-spread.rgba");}
  bt_next(f);break;}
 /* Books in every theme: the shelf, then a page with Bookmarks open over it (50 frames each) */
 case 56: {
  if(since<3)break;
  if(bk_is_open && since==3)books_leave(SCR_HOME);
  int th=(since-4)/50,s=(since-4)%50;char p[64];
  static const char *tn[THEME_N]={"zoid","cyber","phos","y2k","studio","paper","ms1","ms2","msp"};
  if(since<4)break;
  if(th>=THEME_N){theme=0;T=&themes[0];fonts_build(0);go_home(HOME_BOOKS);bt_next(f);break;}
  if(s==0){theme=th;T=&themes[th];fonts_build(th);bk_dir[0]=0;sh_bm=0;go_home(HOME_BOOKS);home_activate(HOME_BOOKS);sh_sel=2;
   check(th==5?"Books under Paper is the Manga Paper look, in Cambria":"Books takes this theme's colors and font",screen==SCR_BOOKS && P_INK==(th==5?0x1F1D1Au:themes[th].fg) && (th!=5 || fonts[0].data==&font_paper_small[0][0]));}
  if(s==3){snprintf(p,sizeof(p),"ms0:/books-shelf-%s.rgba",tn[th]);capture(p);}
  if(s==4){char b[PATHLEN];snprintf(b,sizeof(b),"%s/Gunsmith Cats - Revised Edition 03 (Dark Horse 2007) [fixed].cbz",books_root);rd_open(b);rd_goto(1,0,LV_W1);}
  if(s==44){rd_input(PSP_CTRL_TRIANGLE,0,NULL);rd_input(PSP_CTRL_DOWN,0,NULL);}
  if(s==47){snprintf(p,sizeof(p),"ms0:/books-read-%s.rgba",tn[th]);capture(p);}
  if(s==48){rd_menu=0;rd_close();bk_mode=0;}
  break;}
 /* upright: the PSP turned a quarter to the left, D-pad at the bottom */
 case 57:
  if(since<3)break;
  {char b[PATHLEN];snprintf(b,sizeof(b),"%s/Gunsmith Cats - Revised Edition 03 (Dark Horse 2007) [fixed].cbz",books_root);home_activate(HOME_BOOKS);rd_open(b);rd_goto(1,0,LV_W1);}
  rd_input(PSP_CTRL_TRIANGLE,0,NULL);rd_input(PSP_CTRL_DOWN,0,NULL);
  {int ids[RD_MENU_MAX];rd_menu_items(ids);check("Bookmarks has a Read upright row",ids[rd_menu_sel]==-3);}
  rd_input(PSP_CTRL_CROSS,0,NULL);
  {int was=rd_vert;view_load();check("X on it turns the reader upright, remembered in zoid-books-view.txt",was==1 && rd_vert==1 && !rd_menu);}
  bt_next(f);break;
 case 58:
  if(!(w=bt_wait(f,1,LV_W1,"an upright page decodes")))break;
  if(w>0){PageImg *im=bt_img(1,LV_W1);check("upright, a page fills the 272 width",im->w==272);bt_info("upright page: %dx%d",im->w,im->h);}
  bt_next(f);break;
 case 59:
  if(since==2)capture("ms0:/books-upright.rgba");
  if(since<4)break;
  {static const int want[4]={340,408,476,544};int ok=1;
   for(int i=0;i<4;i++){rd_input(PSP_CTRL_SQUARE,0,NULL);if(rd_level!=LV_Z1+i || lv_width(rd_level,1)!=want[i])ok=0;}
   check("upright, Square zooms in four shorter steps: 340, 408, 476, 544 wide",ok);
   rd_input(PSP_CTRL_SQUARE,0,NULL);check("and stops at 2x",rd_level==LV_Z4);}
  bt_next(f);break;
 case 60:
  if(!(w=bt_wait(f,1,LV_Z4,"the upright 2x zoom decodes")))break;
  if(w>0){PageImg *im=bt_img(1,LV_Z4);check("upright 2x is made at 544 wide",im->w==544);}
  rd_x=100;rd_y=100;
  {float x0=rd_x,y0=rd_y;for(int i=0;i<10;i++)rd_input(0,PSP_CTRL_UP,NULL);check("upright, D-pad Up (now on the left) pans left",rd_x<x0-20 && rd_y==y0);
   x0=rd_x;for(int i=0;i<10;i++)rd_input(0,PSP_CTRL_DOWN,NULL);check("D-pad Down pans right",rd_x>x0+20);
   y0=rd_y;for(int i=0;i<10;i++)rd_input(0,PSP_CTRL_RIGHT,NULL);check("D-pad Right (now on top) pans up",rd_y<y0-20);
   y0=rd_y;for(int i=0;i<10;i++)rd_input(0,PSP_CTRL_LEFT,NULL);check("D-pad Left pans down",rd_y>y0+20);
   SceCtrlData p;memset(&p,0,sizeof(p));p.Lx=255;p.Ly=128;y0=rd_y;for(int i=0;i<10;i++)rd_input(0,0,&p);check("the stick turns too: pushed toward the top pans up",rd_y<y0-20);}
  bt_next(f);break;
 case 61:
  if(since==2)capture("ms0:/books-upright-zoom.rgba");
  if(since<4)break;
  rd_input(PSP_CTRL_CIRCLE,0,NULL);check("Circle zooms out one step",rd_level==LV_Z3);
  rd_input(PSP_CTRL_CIRCLE,0,NULL);rd_input(PSP_CTRL_CIRCLE,0,NULL);rd_input(PSP_CTRL_CIRCLE,0,NULL);check("and back to full width",rd_level==LV_W1 && bk_mode==1);
  rd_input(PSP_CTRL_TRIANGLE,0,NULL);check("right to left, upright: Triangle is the next page",rd_page==2 && !rd_menu);
  rd_input(PSP_CTRL_CROSS,0,NULL);check("Cross goes back",rd_page==1 && rd_level==LV_W1);
  rd_input(PSP_CTRL_SELECT,0,NULL);rd_input(PSP_CTRL_CROSS,0,NULL);check("left to right, upright: Cross is the next page",rd_page==2);
  rd_input(PSP_CTRL_SELECT,0,NULL);rd_input(PSP_CTRL_TRIANGLE,0,NULL);check("right to left again",rd_rtl==1 && rd_page==3);
  rd_input(PSP_CTRL_LTRIGGER,0,NULL);check("L shows the whole page",rd_level==LV_FIT && rd_page==3);
  rd_input(PSP_CTRL_LTRIGGER,0,NULL);
  rd_input(PSP_CTRL_RTRIGGER,0,NULL);check("R opens Bookmarks",rd_menu);
  rd_input(PSP_CTRL_LEFT,0,NULL);check("in the menu, D-pad Left (now at the bottom) moves down",rd_menu_sel==1);
  bt_next(f);break;
 case 62:
  if(since==2)capture("ms0:/books-upright-menu.rgba");
  if(since<4)break;
  rd_input(PSP_CTRL_LEFT,0,NULL);rd_input(PSP_CTRL_CROSS,0,NULL);check("the controls, upright",rd_help);
  bt_next(f);break;
 case 63:
  if(since==2)capture("ms0:/books-upright-help.rgba");
  if(since<4)break;
  rd_input(PSP_CTRL_RTRIGGER,0,NULL);check("R closes the controls",!rd_help);
  theme=5;T=&themes[5];fonts_build(5);bk_style();rd_goto(1,0,LV_W1);
  bt_next(f);break;
 case 64:
  if(!(w=bt_wait(f,1,LV_W1,"the upright page under Paper")))break;
  if(since<4)break;
  capture("ms0:/books-upright-paper.rgba");bt_next(f);break;
 case 65:
  if(since<3)break;
  theme=0;T=&themes[0];fonts_build(0);bk_style();
  rd_input(PSP_CTRL_RTRIGGER,0,NULL);rd_input(PSP_CTRL_LEFT,0,NULL);rd_input(PSP_CTRL_CROSS,0,NULL);
  {int was=rd_vert;view_load();check("Read sideways turns it back, and is remembered",was==0 && rd_vert==0);}
  rd_input(PSP_CTRL_LTRIGGER,0,NULL);check("sideways again, L is the next page",rd_page==2);
  books_leave(SCR_HOME);bt_next(f);break;
 /* A real PSP cuts "Ranma ½" off at the ½, so fix-names.ps1 renames it "Ranma 1-2" and leaves
    zoid-title.txt saying "Ranma 1/2". Made here and removed after, so the shelf above stays as it was. */
 case 66: {
  if(since<3)break;
  if(bk_is_open)books_leave(SCR_HOME);
  char dir[96],vol[160],tf[128];snprintf(dir,sizeof(dir),"%s/Ranma 1-2",books_root);
  snprintf(vol,sizeof(vol),"%s/Ranma 1-2 - Volume_01_(VIZ)_(Flipped).cbz",dir);snprintf(tf,sizeof(tf),"%s/zoid-title.txt",dir);
  sceIoMkdir(dir,0777);
  {FILE *t=fopen(tf,"w");if(t){fputs("Ranma 1/2\n",t);fclose(t);}}
  {char u[96];snprintf(u,sizeof(u),"%s/Unopenable",books_root);sceIoMkdir(u,0777);}
  {char from[96];snprintf(from,sizeof(from),"%s/Library/Chapters/Chapter 01.zip",books_root);
   FILE *a=fopen(from,"rb"),*b=fopen(vol,"wb");char buf[16384];size_t k;
   if(a && b)while((k=fread(buf,1,sizeof(buf),a))>0)fwrite(buf,1,k,b);
   if(a)fclose(a);if(b)fclose(b);}
  bk_dir[0]=0;sh_bm=0;go_home(HOME_BOOKS);home_activate(HOME_BOOKS);
  {int r=bt_find("Unopenable");check("a folder that won't open is still listed, like a file explorer",r>=0 && sh[r].kind==SH_SERIES && !strcmp(sh[r].note,"Couldn't open"));
   char u[96];snprintf(u,sizeof(u),"%s/Unopenable",books_root);sceIoRmdir(u);}
  {int r=bt_find("Ranma 1-2");check("Ranma 1-2 is on the shelf as Ranma 1/2 (its zoid-title.txt), a series of 1 book",r>=0 && sh[r].kind==SH_SERIES && !strcmp(sh[r].title,"Ranma 1/2") && !strcmp(sh[r].note,"1 book"));
   if(r>=0){sh_sel=r;shelf_input(PSP_CTRL_CROSS);}}
  check("X opens it",!bk_at_root() && sh_n==1);
  if(sh_n==1){bt_info("Ranma volume: title \"%s\" note \"%s\"",sh[0].title,sh[0].note);
   check("its volume is Volume 1, the folder's own name dropped",sh[0].kind==SH_BOOK && !strcmp(sh[0].title,"Volume 1") && !strcmp(sh[0].note,"VIZ, Flipped"));}
  {char t[PATHLEN];snprintf(t,sizeof(t),"%s",bk_base(bk_shown(bk_dir)));book_unslash(t);check("the address bar says Ranma 1/2",!strcmp(t,"Ranma 1/2"));}
  {unsigned int cp=0xE000;GCell *g=sys_glyph_or_box(&cp);check("a letter no PSP font has is drawn as a square",g && cp==0x25A1);
   unsigned int half=0xBD;g=sys_glyph_or_box(&half);check("\xC2\xBD comes from the PSP's Latin font",g && half==0xBD);}
  capture("ms0:/books-odd-names.rgba");
  bt_next(f);break;}
 case 67:
  if(since<3)break;
  if(sh_n==1){sh_sel=0;shelf_input(PSP_CTRL_CROSS);}
  check("the volume opens",bk_is_open && bk.n==4);
  {char t[160];bk_full_title(bk.path,t,sizeof(t));bt_info("continue reading: \"%s\"",t);check("Continue reading names it Ranma 1/2: Volume 1",!strcmp(t,"Ranma 1/2: Volume 1"));}
  bt_next(f);break;
 case 68:
  if(!(w=bt_wait(f,0,LV_W1,"its first page shows")))break;
  if(w>0)check("its first page shows",1);
  books_leave(SCR_HOME);
  {char dir[96],vol[160],tf[128];snprintf(dir,sizeof(dir),"%s/Ranma 1-2",books_root);snprintf(vol,sizeof(vol),"%s/Ranma 1-2 - Volume_01_(VIZ)_(Flipped).cbz",dir);snprintf(tf,sizeof(tf),"%s/zoid-title.txt",dir);
   sceIoRemove(vol);sceIoRemove(tf);sceIoRmdir(dir);}
  bt_next(f);break;
 case 69:
  if(since<3)break;
  if(bk_is_open)books_leave(SCR_HOME);
  {FILE *r=fopen("ms0:/smoke.txt","a");if(r){fprintf(r,"books complete: %d\n",f);fclose(r);}}
  bt_next(f);break;
 }
}
