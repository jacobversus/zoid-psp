/* Song lists (the queue, an open playlist, songs gathered for a menu action) and library rows.
   Paths are packed end to end in one growing block, so a list costs about as much memory as
   its text: 10,000 songs fit in roughly 1 MB, where fixed 512-byte slots needed 5 MB. */
#define SONG_MAX 10000  /* songs in one list (the whole library shuffled, a playlist) */
#define ENTRY_MAX 10000 /* rows in one library folder; more are counted and reported, not shown */
typedef struct {char *pool;int *off;int n,cap,used,pool_cap;} Paths;
static int paths_add(Paths *l,const char *p) {
 int len=strlen(p)+1;
 if(l->n>=SONG_MAX || len>PATHLEN)return 0;
 if(l->n>=l->cap){int c=l->cap?l->cap*2:256;if(c>SONG_MAX)c=SONG_MAX;int *o=realloc(l->off,c*sizeof(int));if(!o)return 0;l->off=o;l->cap=c;}
 if(l->used+len>l->pool_cap){int c=l->pool_cap?l->pool_cap*2:16384;while(c<l->used+len)c*=2;char *q=realloc(l->pool,c);if(!q)return 0;l->pool=q;l->pool_cap=c;}
 memcpy(l->pool+l->used,p,len);l->off[l->n++]=l->used;l->used+=len;return 1;
}
static const char *paths_get(const Paths *l,int i){return i>=0 && i<l->n?l->pool+l->off[i]:"";}
static void paths_clear(Paths *l){l->n=l->used=0;}
static void paths_swap(Paths *a,Paths *b){Paths t=*a;*a=*b;*b=t;}

/* Library rows. Names live in 64 KB blocks that are reused on each browse, so a row's name
   pointer stays valid until the next browse or playlist_open. */
#define NAME_BLOCK 65536
static char *name_blocks[64];static int name_nblocks,name_block,name_used;
static void names_reset(void){name_block=0;name_used=0;}
static const char *name_keep(const char *s) {
 int len=strlen(s)+1;if(len>NAME_BLOCK)return "";
 if(name_block<name_nblocks && name_used+len>NAME_BLOCK){name_block++;name_used=0;}
 if(name_block>=name_nblocks) {
  if(name_nblocks>=64)return "";
  char *b=malloc(NAME_BLOCK);if(!b)return "";
  name_blocks[name_nblocks++]=b;name_block=name_nblocks-1;name_used=0;
 }
 char *out=name_blocks[name_block]+name_used;memcpy(out,s,len);name_used+=len;return out;
}
/* folder: 0 a song, 1 a folder, 2 a playlist (.m3u), 3 the Videos entry, 4 the Shuffle all songs entry */
/* album, track, disc: read from the tags when the folder is sorted by them; date: the file's modified time */
typedef struct {const char *name,*album;int folder,tagged,track,disc;unsigned long long date;char title[112],artist[80];} Entry;
static Entry *entries;static int entry_cap,count,entries_total; /* entries_total: rows found, even past ENTRY_MAX */
/* Callers hold meta_lock (the tag thread reads rows), so the array may move here. */
static Entry *entry_add(const char *name,int folder) {
 entries_total++;
 if(count>=ENTRY_MAX)return NULL;
 if(count>=entry_cap){int c=entry_cap?entry_cap*2:512;if(c>ENTRY_MAX)c=ENTRY_MAX;Entry *e=realloc(entries,c*sizeof(Entry));if(!e)return NULL;entries=e;entry_cap=c;}
 Entry *n=&entries[count++];n->name=name_keep(name);n->album="";n->folder=folder;n->tagged=folder!=0;n->track=n->disc=0;n->date=0;n->title[0]=n->artist[0]=0;return n;
}
