/* Whole-folder play and playlists.
   Holding X on a library row (or on Now Playing) opens a small menu:
   folders can be played or shuffled whole, subfolders included (a whole artist);
   songs and folders can be added to a playlist. Playlists are .m3u files in
   MUSIC/Playlists, which sits at the top of the library; opening one lists its
   songs. New playlists are named with the PSP's on-screen keyboard. */
#include <psputility_osk.h>

static Paths picked; /* songs gathered for a menu action */

/* ---- gathering every song under a folder ---- */
/* one folder's songs and subfolders, names packed in a pool (a folder may hold thousands) */
typedef struct {int off,dir,album,title,disc,track;unsigned long long date;} DirItem; /* album, title: pool offsets */
static const char *dir_sort_pool;
static int collect_fast; /* gathering to shuffle: the order does not matter, so no tags are read */
static void dir_key(const DirItem *d,SortKey *k){const char *p=dir_sort_pool;k->name=p+d->off;k->album=p+d->album;k->title=p+d->title;k->disc=d->disc;k->track=d->track;k->date=d->date;}
static int dir_item_cmp(const void *a,const void *b) {
 const DirItem *x=a,*y=b;if(x->dir!=y->dir)return x->dir-y->dir; /* songs before subfolders */
 SortKey kx,ky;dir_key(x,&kx);dir_key(y,&ky);
 return x->dir?folder_order(music_sort,&kx,&ky):song_order(music_sort,&kx,&ky);
}
static int pool_put(char **pool,int *used,int *cap,const char *s) {
 int len=strlen(s)+1;
 if(*used+len>*cap){int c=*cap?*cap*2:4096;while(c<*used+len)c*=2;char *t=realloc(*pool,c);if(!t)return -1;*pool=t;*cap=c;}
 memcpy(*pool+*used,s,len);*used+=len;return *used-len;
}
static void collect_dir(const char *dir,int depth) {
 if(depth>8 || picked.n>=SONG_MAX)return;
 DirItem *items=NULL;char *pool=NULL;int n=0,cap=0,used=0,pool_cap=0;
 int d=sceIoDopen(dir);
 if(d>=0) {
  SceIoDirent e;memset(&e,0,sizeof(e));
  while(n<ENTRY_MAX && sceIoDread(d,&e)>0) {
   if(e.d_name[0]!='.') {
    int isdir=FIO_S_ISDIR(e.d_stat.st_mode);
    if(isdir || mp3(e.d_name)) {
     if(n>=cap){int c=cap?cap*2:64;DirItem *t=realloc(items,c*sizeof(DirItem));if(!t)break;items=t;cap=c;}
     int off=pool_put(&pool,&used,&pool_cap,e.d_name);if(off<0)break;
     DirItem *it=&items[n++];it->off=off;it->dir=isdir;it->album=it->title=off+strlen(e.d_name); /* the name's own end: "" */
     it->disc=it->track=0;it->date=sort_date(&e.d_stat.sce_st_mtime);
    }
   }
   memset(&e,0,sizeof(e));
  }
  sceIoDclose(d);
 }
 if(n && !collect_fast && sort_needs_tags(music_sort))for(int i=0;i<n;i++)if(!items[i].dir) {
  char path[PATHLEN];int len=snprintf(path,sizeof(path),"%s/%s",dir,pool+items[i].off);if(len<0 || len>=(int)sizeof(path))continue;
  static Tags t;read_tags(path,&t,0);
  int a=pool_put(&pool,&used,&pool_cap,t.album),ti=pool_put(&pool,&used,&pool_cap,t.title);
  if(a>=0)items[i].album=a;if(ti>=0)items[i].title=ti;items[i].disc=t.disc;items[i].track=t.track;
 }
 if(n){dir_sort_pool=pool;qsort(items,n,sizeof(DirItem),dir_item_cmp);}
 for(int i=0;i<n && picked.n<SONG_MAX;i++) {
  const char *name=pool+items[i].off;
  char path[PATHLEN];int len=snprintf(path,sizeof(path),"%s/%s",dir,name);if(len<0 || len>=(int)sizeof(path))continue;
  if(items[i].dir){if(strcmp(name,PLAYLIST_DIR))collect_dir(path,depth+1);}
  else paths_add(&picked,path);
 }
 free(items);free(pool);
}

/* ---- playlist files ---- */
static void playlist_dir(char *out,int cap){snprintf(out,cap,"%s/%s",root,PLAYLIST_DIR);}
/* Reads a playlist into out (full paths). Lines may be /MUSIC/... (this memory stick),
   a full device path, or relative to the playlist's folder. */
static int playlist_read(const char *file,Paths *out) {
 paths_clear(out);
 int fd=sceIoOpen(file,PSP_O_RDONLY,0);if(fd<0)return 0;
 int size=sceIoLseek32(fd,0,PSP_SEEK_END);sceIoLseek32(fd,0,PSP_SEEK_SET);
 if(size<=0 || size>4*1024*1024){sceIoClose(fd);return 0;}
 char *buf=malloc(size+1);if(!buf){sceIoClose(fd);return 0;}
 int got=sceIoRead(fd,buf,size);sceIoClose(fd);if(got<0)got=0;buf[got]=0;
 char device[16];{const char *c=strchr(root,':');int k=c?(int)(c-root)+1:0;if(k>15)k=15;memcpy(device,root,k);device[k]=0;}
 char base[PATHLEN];snprintf(base,sizeof(base),"%s",file);{char *s=strrchr(base,'/');if(s)*s=0;}
 char *line=buf;
 if((unsigned char)line[0]==0xEF && (unsigned char)line[1]==0xBB && (unsigned char)line[2]==0xBF)line+=3; /* UTF-8 BOM (.m3u8) */
 while(line && *line && out->n<SONG_MAX) {
  char *next=strpbrk(line,"\r\n");if(next){*next=0;next++;while(*next=='\r' || *next=='\n')next++;}
  while(*line==' ' || *line=='\t')line++;
  for(char *c=line;*c;c++)if(*c=='\\')*c='/';
  if(*line && *line!='#') {
   char p[PATHLEN];int len;
   if(strchr(line,':'))len=snprintf(p,PATHLEN,"%s",line);
   else if(*line=='/')len=snprintf(p,PATHLEN,"%s%s",device,line);
   else len=snprintf(p,PATHLEN,"%s/%s",base,line);
   if(len>0 && len<PATHLEN)paths_add(out,p);
  }
  line=next;
 }
 free(buf);return out->n;
}
/* Writes paths without the device name (/MUSIC/...), the form most players read. */
static int playlist_write(const char *file,const Paths *paths) {
 int fd=sceIoOpen(file,PSP_O_WRONLY|PSP_O_CREAT|PSP_O_TRUNC,0777);if(fd<0)return 0;
 static char out[16384];int o=0,ok=1;
 memcpy(out,"#EXTM3U\n",8);o=8;
 for(int i=0;i<paths->n && ok;i++) {
  const char *p=paths_get(paths,i),*c=strchr(p,':');if(c)p=c+1;
  int len=strlen(p);
  if(o+len+1>(int)sizeof(out)){ok=sceIoWrite(fd,out,o)==o;o=0;}
  memcpy(out+o,p,len);o+=len;out[o++]='\n';
 }
 if(ok && o)ok=sceIoWrite(fd,out,o)==o;
 sceIoClose(fd);return ok;
}
/* A set of paths for duplicate checks: open addressing on a hash, indices into a Paths list. */
static unsigned int path_hash(const char *s){unsigned int h=2166136261u;for(;*s;s++)h=(h^(unsigned char)*s)*16777619u;return h;}
static int set_find_or_add(int *slots,int mask,const Paths *l,int index) {
 const char *p=paths_get(l,index);
 for(unsigned int h=path_hash(p)&mask;;h=(h+1)&mask) {
  if(slots[h]<0){slots[h]=index;return 0;}
  if(!strcmp(paths_get(l,slots[h]),p))return 1;
 }
}
/* Adds the staged songs that are not already in the playlist. Returns how many were added. */
static int playlist_add_stage(const char *file) {
 static Paths have;int n=playlist_read(file,&have),added=0;
 int size=1;while(size<(n+picked.n)*2)size<<=1;
 int *slots=malloc(size*sizeof(int));if(!slots)return -1;
 for(int i=0;i<size;i++)slots[i]=-1;
 for(int i=0;i<n;i++)set_find_or_add(slots,size-1,&have,i);
 for(int i=0;i<picked.n && have.n<SONG_MAX;i++) {
  if(!paths_add(&have,paths_get(&picked,i)))break;
  if(set_find_or_add(slots,size-1,&have,have.n-1)){have.n--;have.used-=strlen(paths_get(&picked,i))+1;}else added++;
 }
 free(slots);
 if(added && !playlist_write(file,&have))return -1;
 return added;
}
/* Playlist file names in MUSIC/Playlists, sorted. */
#define PL_MAX 40
static char pl_names[PL_MAX][128];static int pl_count;
static void playlist_scan(void) {
 char dir[PATHLEN];playlist_dir(dir,sizeof(dir));pl_count=0;
 int d=sceIoDopen(dir);if(d<0)return;
 SceIoDirent e;memset(&e,0,sizeof(e));
 while(pl_count<PL_MAX && sceIoDread(d,&e)>0){if(!FIO_S_ISDIR(e.d_stat.st_mode) && m3u(e.d_name) && strlen(e.d_name)<128)snprintf(pl_names[pl_count++],128,"%s",e.d_name);memset(&e,0,sizeof(e));}
 sceIoDclose(d);
 for(int i=1;i<pl_count;i++)for(int j=i;j>0 && strcasecmp(pl_names[j-1],pl_names[j])>0;j--){char t[128];memcpy(t,pl_names[j],128);memcpy(pl_names[j],pl_names[j-1],128);memcpy(pl_names[j-1],t,128);}
}
static int file_exists(const char *path){SceIoStat st;return sceIoGetstat(path,&st)>=0;}
/* A file name from what was typed: no characters the memory stick refuses, no leading/trailing spaces. */
static void playlist_file_for(const char *name,char *out,int cap) {
 char clean[64];int o=0;
 for(const char *c=name;*c && o<56;c++){unsigned char ch=*c;if(ch<32 || strchr("/\\:*?\"<>|",ch))continue;clean[o++]=ch;}
 while(o && clean[o-1]==' ')o--;clean[o]=0;
 const char *s=clean;while(*s==' ')s++;
 char dir[PATHLEN];playlist_dir(dir,sizeof(dir));
 snprintf(out,cap,"%s/%s.m3u",dir,*s?s:"Playlist");
 for(int k=2;file_exists(out) && k<100;k++)snprintf(out,cap,"%s/%s %d.m3u",dir,*s?s:"Playlist",k);
}
static void playlist_default_name(char *out,int cap) {
 char dir[PATHLEN],file[PATHLEN];playlist_dir(dir,sizeof(dir));
 for(int k=1;k<1000;k++){snprintf(out,cap,"Playlist %d",k);snprintf(file,sizeof(file),"%s/%s.m3u",dir,out);if(!file_exists(file))return;}
}
/* Opens a playlist in the library: its songs become the list. */
static void playlist_open(const char *file) {
 static Paths tmp;playlist_read(file,&tmp);
 lock(meta_lock);browse_gen++;
 paths_swap(&pl_paths,&tmp);count=entries_total=0;names_reset();cursor=0;scroll=0;in_playlist=1;
 snprintf(playlist_file,sizeof(playlist_file),"%s",file);
 for(int i=0;i<pl_paths.n;i++){const char *p=paths_get(&pl_paths,i),*b=strrchr(p,'/');if(!entry_add(b?b+1:p,0))break;}
 if(count<pl_paths.n)pl_paths.n=count; /* rows and paths stay in step */
 unlock(meta_lock);
}

/* ---- toast for the library and menus ---- */
static void notice(const char *s){show_toast(-2,s);toast_until=now_ms+1600;}

/* ---- naming with the PSP's on-screen keyboard ---- */
static int osk_active;static SceUtilityOskData osk_data;static SceUtilityOskParams osk_params;
static unsigned short osk_desc[32],osk_in[64],osk_out[64];
static void (*osk_done)(const char *text); /* called with the name, or NULL when cancelled */
static void osk_ascii16(unsigned short *dst,const char *src,int cap){int i=0;for(;src[i] && i<cap-1;i++)dst[i]=(unsigned char)src[i];dst[i]=0;}
static void osk_frame(void) { /* runs after each frame is drawn, before it is shown */
 if(!osk_active)return;
 switch(sceUtilityOskGetStatus()) {
 case PSP_UTILITY_DIALOG_VISIBLE:sceUtilityOskUpdate(1);break;
 case PSP_UTILITY_DIALOG_QUIT:sceUtilityOskShutdownStart();break;
 case PSP_UTILITY_DIALOG_NONE:case PSP_UTILITY_DIALOG_FINISHED: {
  osk_active=0;frame_hook=NULL;
  char text[64];int i=0;for(;osk_out[i] && i<63;i++)text[i]=osk_out[i]<128?(char)osk_out[i]:'?';text[i]=0;
  if(osk_done)osk_done(osk_data.result==PSP_UTILITY_OSK_RESULT_CANCELLED?NULL:text);
  break;
 }
 default:break;
 }
}
static int osk_open(const char *title,const char *initial,void (*done)(const char *)) {
 memset(&osk_data,0,sizeof(osk_data));memset(&osk_params,0,sizeof(osk_params));memset(osk_out,0,sizeof(osk_out));
 osk_ascii16(osk_desc,title,32);osk_ascii16(osk_in,initial,64);
 osk_data.language=PSP_UTILITY_OSK_LANGUAGE_DEFAULT;osk_data.lines=1;osk_data.unk_24=1;osk_data.inputtype=PSP_UTILITY_OSK_INPUTTYPE_ALL;
 osk_data.desc=osk_desc;osk_data.intext=osk_in;osk_data.outtextlength=64;osk_data.outtextlimit=40;osk_data.outtext=osk_out;
 osk_params.base.size=sizeof(osk_params);
 sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE,&osk_params.base.language);
 sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN,&osk_params.base.buttonSwap);
 osk_params.base.graphicsThread=17;osk_params.base.accessThread=19;osk_params.base.fontThread=18;osk_params.base.soundThread=16;
 osk_params.datacount=1;osk_params.data=&osk_data;
 osk_done=done;
 if(sceUtilityOskInitStart(&osk_params)<0)return 0;
 osk_active=1;frame_hook=osk_frame;return 1;
}

/* ---- the hold-X menu ---- */
/* A_PL_DELETE asks first, A_DEL_CONFIRM shows the cannot-be-undone warning, A_PL_DELETE_YES deletes
   (a song, a folder with everything in it, or a playlist). A_EQ_* are the equalizer preset menus (eq_ui.h). */
enum {A_NONE,A_PLAY_ALL,A_SHUFFLE_ALL,A_ADD,A_PLAY,A_REMOVE,A_PL_PLAY,A_PL_SHUFFLE,A_PL_DELETE,A_PL_DELETE_YES,A_NEW_PL,A_PICK_PL,A_CANCEL,
 A_DEL_CONFIRM,A_EQ_PICK,A_EQ_SAVE,A_EQ_DELMENU,A_EQ_DEL,A_EQ_DEL_YES,A_LYRICS,A_RADIO_SAVE,A_RADIO_REMOVE,
 A_POD_SAVE,A_POD_CANCEL_DL,A_POD_DELETE,A_POD_DELETE_YES,A_POD_PLAYED,A_POD_UNPLAYED,A_POD_REFRESH,A_POD_SUB,A_POD_UNSUB,A_POD_UNSUB_YES,
 A_SORT,A_SORT_PICK};
static void pod_menu_run(int act),pod_item_menu(void);static int pod_menu_ok(int i),pod_is_episode(const char *path); /* podcast_ui.h */
static void open_lyrics(void);
static void eq_menu_run(int act,int sel);
#define MENU_MAX (PL_MAX+2)
static int menu_open,menu_n,menu_sel,menu_scroll,menu_act[MENU_MAX],menu_warn;static char menu_item_name[96];
static char menu_title[96],menu_label[MENU_MAX][64];
static char menu_target[PATHLEN];static int menu_target_kind,menu_target_index; /* 0 song, 1 folder, 2 playlist */
static void menu_add(const char *label,int act){if(menu_n<MENU_MAX){snprintf(menu_label[menu_n],64,"%s",label);menu_act[menu_n++]=act;}}
static void menu_begin(const char *title){menu_warn=0;menu_n=0;menu_sel=0;menu_scroll=0;menu_open=1;snprintf(menu_title,sizeof(menu_title),"%s",title);}
static void stage_target(void) {
 paths_clear(&picked);
 if(menu_target_kind==1)collect_dir(menu_target,0);
 else if(menu_target_kind==2)playlist_read(menu_target,&picked);
 else paths_add(&picked,menu_target);
}
static void menu_pick_playlist(void) {
 playlist_scan();menu_begin("Add to playlist");
 menu_add("New playlist...",A_NEW_PL);
 for(int i=0;i<pl_count;i++){char nm[64];snprintf(nm,sizeof(nm),"%s",pl_names[i]);char *dot=strrchr(nm,'.');if(dot)*dot=0;menu_add(nm,A_PICK_PL);}
}
static void report_added(int added,const char *file) {
 char nm[64];const char *b=strrchr(file,'/');snprintf(nm,sizeof(nm),"%s",b?b+1:file);char *dot=strrchr(nm,'.');if(dot)*dot=0;
 char msg[96];
 if(added<0)snprintf(msg,sizeof(msg),"Could not save %s",nm);
 else if(!added)snprintf(msg,sizeof(msg),"Already in %s",nm);
 else snprintf(msg,sizeof(msg),"Added %d song%s to %s",added,added==1?"":"s",nm);
 notice(msg);
 if(in_playlist && !strcmp(file,playlist_file)){int c=cursor;playlist_open(file);cursor=c<count?c:count-1;if(cursor<0)cursor=0;}
}
/* A new playlist with the staged songs. */
static void playlist_create(const char *name) {
 char dir[PATHLEN],file[PATHLEN];playlist_dir(dir,sizeof(dir));sceIoMkdir(dir,0777);
 playlist_file_for(name,file,sizeof(file));
 int ok=playlist_write(file,&picked);
 report_added(ok?picked.n:-1,file);
 if(!in_playlist && !strcmp(folder,root)){int c=cursor;browse();cursor=c<count?c:0;}
}
static void named_new_playlist(const char *text) {
 if(!text){notice("Cancelled");return;}
 char name[64];snprintf(name,sizeof(name),"%s",text);if(!name[0])playlist_default_name(name,sizeof(name));
 playlist_create(name);
}
/* ---- deleting from the library ---- */
static int remove_tree(const char *dir,int depth) {
 if(depth>8)return -1;
 int d=sceIoDopen(dir);if(d<0)return d;
 SceIoDirent e;memset(&e,0,sizeof(e));
 while(sceIoDread(d,&e)>0) {
  if(strcmp(e.d_name,".") && strcmp(e.d_name,"..")) {
   char path[PATHLEN];int len=snprintf(path,sizeof(path),"%s/%s",dir,e.d_name);
   if(len>0 && len<(int)sizeof(path)){if(FIO_S_ISDIR(e.d_stat.st_mode))remove_tree(path,depth+1);else sceIoRemove(path);}
  }
  memset(&e,0,sizeof(e));
 }
 sceIoDclose(d);return sceIoRmdir(dir);
}
/* Deletes menu_target. Playback stops first if the song playing is the target or inside it. */
static void delete_target(void) {
 int n=strlen(menu_target),kind=menu_target_kind;
 if(active[0] && (!strcmp(active,menu_target) || (kind==1 && !strncmp(active,menu_target,n) && active[n]=='/'))){request(NULL);for(int w=0;w<100 && state;w++)sceKernelDelayThread(10000);}
 /* the next song may already be open on the second decoder; close it so the file can go */
 engine_drop_prepared();
 int r=kind==1?remove_tree(menu_target,0):sceIoRemove(menu_target);
 if(r>=0 && kind!=2)queue_forget(menu_target,kind==1);
 if(r<0){notice("Could not delete it");return;}
 int c=cursor;
 if(in_playlist) {
  /* a song deleted from inside a playlist also leaves the playlist */
  static Paths keep;paths_clear(&keep);for(int i=0;i<pl_paths.n;i++)if(strcmp(paths_get(&pl_paths,i),menu_target))paths_add(&keep,paths_get(&pl_paths,i));
  playlist_write(playlist_file,&keep);playlist_open(playlist_file);
 }else browse();
 cursor=c<count?c:count-1;if(cursor<0)cursor=0;
 notice(kind==1?"Folder deleted":kind==2?"Playlist deleted":"Song deleted");
}
/* ---- sorting ---- */
static int sort_menu_mode[SORT_N];
static void sort_menu(void) {
 int vf=in_video_folder(),cur=vf?video_sort:music_sort;
 menu_begin(vf?"Sort videos by":"Sort songs by");
 for(int m=0;m<SORT_N;m++) {
  if(vf && !sort_for_video(m))continue;
  char l[64];snprintf(l,sizeof(l),m==cur?"%s  (now)":"%s",vf && m==SORT_NAME_ZA?"File name Z-A":sort_names[m]);
  if(m==cur)menu_sel=menu_n;
  sort_menu_mode[menu_n]=m;menu_add(l,A_SORT_PICK);
 }
 menu_add("Cancel",A_CANCEL);
}
/* Applies a sort order to MUSIC or VIDEO: the list re-sorts with the cursor on the same row,
   and a queue playing from this folder takes the new order. */
static void sort_pick(int m) {
 int vf=in_video_folder();if(vf)video_sort=m;else music_sort=m;save_settings();
 char keep[256]="";if(cursor>=0 && cursor<count)snprintf(keep,sizeof(keep),"%s",entries[cursor].name);
 browse();for(int i=0;i<count;i++)if(!strcmp(entries[i].name,keep)){cursor=i;break;}
 lib_scroll_f=lib_sel_f=cursor>LIB_ROWS?cursor-LIB_ROWS/2:0;
 if(!vf)queue_resort();
 char msg[64];snprintf(msg,sizeof(msg),"Sorted: %s",vf && m==SORT_NAME_ZA?"File name Z-A":sort_names[m]);notice(msg);
}
static void menu_run(int act,int sel) {
 menu_open=0;
 switch(act) {
 case A_SORT:sort_menu();break;
 case A_SORT_PICK:if(sel>=0 && sel<SORT_N)sort_pick(sort_menu_mode[sel]);break;
 case A_PLAY_ALL:case A_SHUFFLE_ALL:case A_PL_PLAY:case A_PL_SHUFFLE:
  collect_fast=act==A_SHUFFLE_ALL;
  if(act==A_PLAY_ALL && menu_target_kind==1 && sort_needs_tags(music_sort)){show_toast(-2,"Gathering songs in order...");toast_until=now_ms+5000;draw();}
  stage_target();collect_fast=0;
  if(!picked.n){notice("No songs here");break;}
  if(act==A_PLAY_ALL || act==A_PL_PLAY){shuffle=0;queue_start_paths(&picked,0);}else queue_start_paths(&picked,1);
  screen=SCR_NOW;np_focus=2;break;
 case A_PLAY:queue_start(menu_target_index);screen=SCR_NOW;np_focus=2;break;
 case A_ADD:stage_target();if(!picked.n){notice("No songs here");break;}menu_pick_playlist();break;
 case A_NEW_PL: {
  char def[64];playlist_default_name(def,sizeof(def));
  if(!osk_open("Playlist name",def,named_new_playlist))playlist_create(def);
  break;
 }
 case A_PICK_PL: {
  char dir[PATHLEN],file[PATHLEN];playlist_dir(dir,sizeof(dir));
  snprintf(file,sizeof(file),"%s/%s",dir,pl_names[sel-1]);report_added(playlist_add_stage(file),file);break;
 }
 case A_REMOVE: {
  int i=menu_target_index;if(!in_playlist || i<0 || i>=count)break;
  static Paths keep;paths_clear(&keep);for(int k=0;k<pl_paths.n;k++)if(k!=i)paths_add(&keep,paths_get(&pl_paths,k));
  if(!playlist_write(playlist_file,&keep)){notice("Could not save the playlist");break;}
  playlist_open(playlist_file);cursor=i<count?i:count-1;if(cursor<0)cursor=0;notice("Removed from the playlist");break;
 }
 case A_PL_DELETE: {
  char t[128];snprintf(t,sizeof(t),"Delete \"%.80s\"?",menu_item_name);
  menu_begin(t);menu_add(menu_target_kind==1?"Delete folder":menu_target_kind==2?"Delete playlist":"Delete song",A_DEL_CONFIRM);menu_add("Cancel",A_CANCEL);break;
 }
 case A_DEL_CONFIRM:
  /* the second popup: Cancel is first, so a double press of X does nothing */
  menu_begin(menu_target_kind==1?"Delete all of it?":"Are you sure?");menu_warn=1;
  menu_add("Cancel",A_CANCEL);menu_add("Delete permanently",A_PL_DELETE_YES);break;
 case A_PL_DELETE_YES:delete_target();break;
 case A_LYRICS:open_lyrics();break;
 case A_RADIO_SAVE:radio_save(menu_target);break;
 case A_RADIO_REMOVE:radio_remove(menu_target);break;
 case A_POD_SAVE:case A_POD_CANCEL_DL:case A_POD_DELETE:case A_POD_DELETE_YES:case A_POD_PLAYED:case A_POD_UNPLAYED:
 case A_POD_REFRESH:case A_POD_SUB:case A_POD_UNSUB:case A_POD_UNSUB_YES:pod_menu_run(act);break;
 default:if(act>=A_EQ_PICK && act<A_LYRICS)eq_menu_run(act,sel);break;
 }
}
/* Every song under MUSIC (all folders, up to SONG_MAX), shuffled. The first scan of a big memory
   stick can take a few seconds, so a notice is drawn before it starts. */
static void draw(void);
static void shuffle_library(void) {
 show_toast(-2,"Gathering every song...");toast_until=now_ms+5000;draw();
 paths_clear(&picked);collect_fast=1;collect_dir(root,0);collect_fast=0;
 if(!picked.n){notice("No songs found in MUSIC");return;}
 queue_start_paths(&picked,1);screen=SCR_NOW;np_focus=2;
 char msg[64];snprintf(msg,sizeof(msg),picked.n>=SONG_MAX?"Shuffling the first %d songs":"Shuffling %d songs",picked.n);notice(msg);
}
static int menu_allowed(void){return (screen==SCR_LIBRARY && count>0 && entries[cursor].folder!=3 && entries[cursor].folder!=5 && (!in_radio || !entries[cursor].folder) && (!in_pod || pod_menu_ok(cursor))) || (screen==SCR_NOW && active[0]);}
/* Holding X: what the row under the cursor (or the song playing) can do. */
static void open_item_menu(void) {
 if(screen==SCR_NOW) {
  snprintf(menu_target,sizeof(menu_target),"%s",active);menu_target_kind=0;
  if(pod_is_episode(active)){pod_item_menu();return;} /* an episode: save, played */
  if(radio_is_url(active)){radio_item_menu();return;} /* a station: Lyrics, Save */
  menu_begin(np_title[0]?np_title:"This song");menu_add("Lyrics",A_LYRICS);menu_add("Add to playlist",A_ADD);menu_add("Cancel",A_CANCEL);return;
 }
 if(cursor<0 || cursor>=count)return;
 if(in_radio){radio_item_menu();return;}
 if(in_pod){pod_item_menu();return;}
 Entry *e=&entries[cursor];char label[256];entry_label(e,label,sizeof(label));
 char sort_label[64];snprintf(sort_label,sizeof(sort_label),"Sort: %s",in_video_folder() && video_sort==SORT_NAME_ZA?"File name Z-A":sort_names[in_video_folder()?video_sort:music_sort]);
 if(in_video_folder()){menu_begin(label);menu_add(sort_label,A_SORT);menu_add("Cancel",A_CANCEL);return;}
 if(!entry_path(cursor,menu_target,sizeof(menu_target)))return;
 menu_target_kind=e->folder;menu_target_index=cursor;
 if(e->folder==4){snprintf(menu_target,sizeof(menu_target),"%s",root);menu_target_kind=1;} /* every song in MUSIC; never deleted from here */
 if(e->folder==2){char *dot=strrchr(label,'.');if(dot)*dot=0;}
 menu_begin(label);snprintf(menu_item_name,sizeof(menu_item_name),"%s",label);
 if(e->folder==4){menu_add("Shuffle all",A_SHUFFLE_ALL);menu_add("Play all in order",A_PLAY_ALL);menu_add("Add all to playlist",A_ADD);menu_add(sort_label,A_SORT);}
 else if(e->folder==1){menu_add("Play all",A_PLAY_ALL);menu_add("Shuffle all",A_SHUFFLE_ALL);menu_add("Add all to playlist",A_ADD);menu_add(sort_label,A_SORT);menu_add("Delete folder",A_PL_DELETE);}
 else if(e->folder==2){menu_add("Play",A_PL_PLAY);menu_add("Shuffle",A_PL_SHUFFLE);menu_add("Delete playlist",A_PL_DELETE);}
 else{menu_add("Play",A_PLAY);menu_add("Add to playlist",A_ADD);if(in_playlist)menu_add("Remove from playlist",A_REMOVE);else menu_add(sort_label,A_SORT);menu_add("Delete song",A_PL_DELETE);} /* a playlist keeps its own order */
 menu_add("Cancel",A_CANCEL);
}
static void menu_input(unsigned int press) {
 if(press&PSP_CTRL_CIRCLE){menu_open=0;return;}
 if(press&PSP_CTRL_UP)menu_sel=(menu_sel+menu_n-1)%menu_n;
 if(press&PSP_CTRL_DOWN)menu_sel=(menu_sel+1)%menu_n;
 if(press&PSP_CTRL_CROSS)menu_run(menu_act[menu_sel],menu_sel);
}
