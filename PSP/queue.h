/* The queue is shared with the audio/preparation threads: everything here runs under queue_lock.
   queue_count always equals queue_paths.n. */
static Paths queue_paths;
static void engine_drop_prepared(void); /* audioengine.h */
static int queue_count,queue_order[SONG_MAX],queue_pos,shuffle,repeat_mode;
/* shuffle_oneoff: Shuffle all (the whole library, a folder, a playlist) turned shuffle on just for that
   queue. Picking an album afterwards plays it in order again, and shuffle is never saved on that way. */
static int shuffle_oneoff;
static int future_order[SONG_MAX],future_valid;
static int queue_scratch[SONG_MAX],queue_map[SONG_MAX]; /* working space for queue_next / queue_forget */
static unsigned int queue_random=0x51a7;
static int queue_rand(int n){queue_random=queue_random*1664525+1013904223;return (queue_random>>8)%n;}
static void reorder_locked(int current) {
 for(int i=0;i<queue_count;i++)queue_order[i]=i;
 if(shuffle && queue_count){queue_order[0]=current;queue_order[current]=0;for(int i=queue_count-1;i>1;i--){int j=1+queue_rand(i),v=queue_order[i];queue_order[i]=queue_order[j];queue_order[j]=v;}queue_pos=0;}else queue_pos=current;
 future_valid=0;
}
static void future_locked(void) {
 if(future_valid)return;
 for(int i=0;i<queue_count;i++)future_order[i]=queue_order[i];
 for(int i=queue_count-1;i>0;i--){int j=queue_rand(i+1),v=future_order[i];future_order[i]=future_order[j];future_order[j]=v;}
 if(queue_count>1 && future_order[0]==queue_order[queue_pos]){int j=1+queue_rand(queue_count-1),v=future_order[0];future_order[0]=future_order[j];future_order[j]=v;}
 future_valid=1;
}
static int play_locked(int continuous,unsigned int expected) {
 if(!queue_count)return 0;
 const char *path=paths_get(&queue_paths,queue_order[queue_pos]);
 if(path[0])return request_mode_guarded(path,continuous,expected);
 return 0;
}
/* Plays every song the library shows (a folder, or an open playlist), from the one picked. */
static void queue_start(int selected_entry) {
 sceKernelWaitSema(queue_lock,1,NULL);paths_clear(&queue_paths);int current=0;
 if(shuffle_oneoff){shuffle=0;shuffle_oneoff=0;}
 for(int i=0;i<count;i++){char p[PATHLEN];if(!entries[i].folder && entry_path(i,p,sizeof(p))){if(i==selected_entry)current=queue_paths.n;if(!paths_add(&queue_paths,p))break;}}
 queue_count=queue_paths.n;
 reorder_locked(current);play_locked(0,0);sceKernelSignalSema(queue_lock,1);
}
/* Plays a list of full paths (a whole artist, a playlist, the library); shuffled turns shuffle on and starts anywhere. */
static void queue_start_paths(const Paths *src,int shuffled) {
 if(src->n<=0)return;
 sceKernelWaitSema(queue_lock,1,NULL);
 paths_clear(&queue_paths);for(int i=0;i<src->n;i++)if(!paths_add(&queue_paths,paths_get(src,i)))break;
 queue_count=queue_paths.n;
 if(shuffled){if(!shuffle)shuffle_oneoff=1;shuffle=1;}else if(shuffle_oneoff){shuffle=0;shuffle_oneoff=0;}
 reorder_locked(shuffled && queue_count?queue_rand(queue_count):0);play_locked(0,0);sceKernelSignalSema(queue_lock,1);
}
#ifdef ZOID_QUEUE_TEST
static void queue_play(void){sceKernelWaitSema(queue_lock,1,NULL);play_locked(0,0);sceKernelSignalSema(queue_lock,1);}
#endif
static void queue_toggle_shuffle(void){sceKernelWaitSema(queue_lock,1,NULL);int current=queue_count?queue_order[queue_pos]:0;shuffle^=1;shuffle_oneoff=0;reorder_locked(current);sceKernelSignalSema(queue_lock,1);}
static void queue_cycle_repeat(void){sceKernelWaitSema(queue_lock,1,NULL);repeat_mode=(repeat_mode+1)%3;future_valid=0;sceKernelSignalSema(queue_lock,1);}
static int queue_peek_next(char *path) {
 int next=-1;sceKernelWaitSema(queue_lock,1,NULL);
 if(queue_count){if(repeat_mode==1)next=queue_order[queue_pos];else if(queue_pos+1<queue_count)next=queue_order[queue_pos+1];else if(repeat_mode==2){if(shuffle){future_locked();next=future_order[0];}else next=queue_order[0];}}
 if(next>=0)snprintf(path,PATHLEN,"%s",paths_get(&queue_paths,next));else path[0]=0;
 sceKernelSignalSema(queue_lock,1);return next>=0;
}
static void queue_next(int automatic,unsigned int expected) {
 sceKernelWaitSema(queue_lock,1,NULL);
 if(automatic && serial!=expected){sceKernelSignalSema(queue_lock,1);return;}
 int old_pos=queue_pos,started=1;if(automatic)memcpy(queue_scratch,queue_order,queue_count*sizeof(int));
 if(queue_count){
  if(automatic && repeat_mode==1)started=play_locked(1,expected);
  else if(queue_pos+1<queue_count){queue_pos++;future_valid=0;started=play_locked(automatic,expected);}
  else if(repeat_mode==2){if(shuffle){future_locked();memcpy(queue_order,future_order,queue_count*sizeof(int));future_valid=0;}queue_pos=0;started=play_locked(automatic,expected);}
 }
 if(!started && automatic){queue_pos=old_pos;memcpy(queue_order,queue_scratch,queue_count*sizeof(int));future_valid=0;}
 sceKernelSignalSema(queue_lock,1);
}
static void queue_previous(void) {
  if(elapsed>3 && (!radio_is_url(active) || radio_is_episode(active))){seek_to(0);return;} /* a station has no start to go back to */
 sceKernelWaitSema(queue_lock,1,NULL);int restart=0;
 if(queue_pos>0){queue_pos--;future_valid=0;play_locked(0,0);}else if(repeat_mode==2 && queue_count){queue_pos=queue_count-1;future_valid=0;play_locked(0,0);}else if(active[0])restart=1;
 sceKernelSignalSema(queue_lock,1);
 if(restart)seek_to(0);
}

/* After the sort changes: when the queue is this folder's songs, it takes the new order and the song
   playing carries on (Next follows the list as now shown). */
static int queue_resort(void) {
 int songs=0;for(int i=0;i<count;i++)if(!entries[i].folder)songs++;
 sceKernelWaitSema(queue_lock,1,NULL);
 int same=songs>0 && songs==queue_count,n=strlen(folder);
 for(int i=0;i<queue_count && same;i++){const char *q=paths_get(&queue_paths,i);same=!strncmp(q,folder,n) && q[n]=='/' && !strchr(q+n+1,'/');}
 if(!same){sceKernelSignalSema(queue_lock,1);return 0;}
 char now[PATHLEN];snprintf(now,sizeof(now),"%s",paths_get(&queue_paths,queue_order[queue_pos]));
 paths_clear(&queue_paths);int current=0;
 for(int i=0;i<count;i++){char p[PATHLEN];if(!entries[i].folder && entry_path(i,p,sizeof(p))){if(!strcmp(p,now))current=queue_paths.n;paths_add(&queue_paths,p);}}
 queue_count=queue_paths.n;
 reorder_locked(current); /* shuffled: a new shuffle from the song playing */
 sceKernelSignalSema(queue_lock,1);
 engine_drop_prepared(); /* the next song may have been opened ahead in the old order */
 return 1;
}
/* Drops deleted songs from the queue: the song at path, or (folder) every song under it.
   The order and position are kept for the songs that remain. */
static void queue_forget(const char *path,int folder) {
 sceKernelWaitSema(queue_lock,1,NULL);
 int n=strlen(path),kept=0;static Paths keep;paths_clear(&keep);
 for(int i=0;i<queue_count;i++) {
  const char *q=paths_get(&queue_paths,i);
  int gone=folder?(!strncmp(q,path,n) && q[n]=='/'):!strcmp(q,path);
  if(gone || !paths_add(&keep,q))queue_map[i]=-1;else queue_map[i]=kept++;
 }
 if(kept!=queue_count) {
  int m=0,pos=0;
  for(int i=0;i<queue_count;i++){if(i==queue_pos)pos=m;if(queue_map[queue_order[i]]>=0)queue_scratch[m++]=queue_map[queue_order[i]];}
  memcpy(queue_order,queue_scratch,sizeof(int)*m);paths_swap(&queue_paths,&keep);queue_count=m;
  queue_pos=m?(pos<m?pos:m-1):0;future_valid=0;
 }
 sceKernelSignalSema(queue_lock,1);
}
