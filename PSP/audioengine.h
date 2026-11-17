/* Two independently prepared MP3 decoders feed a persistent PCM stream.
   The next file's scan, codec initialization and first decode happen off the
   playback thread. No zero padding or output release is inserted at a handoff. */
typedef struct {
 Mp3Index index;
 char path[PATHLEN];
 int fd,handle,rate,channels,first_bytes,error_stage,error_code;
 SoftDec *soft; /* FLAC/WAV/M4A/radio: decoded through softdec instead of by sceMp3 */
 int live;      /* an internet radio station: no length, no seeking, nothing prepared ahead */
 unsigned long long position,wanted;
 volatile int status; /* 0 idle, 1 preparing, 2 ready, 3 playing, 4 failed */
 unsigned char mp3[32768] __attribute__((aligned(64)));
 unsigned char pcm[16384] __attribute__((aligned(64)));
 short *first;
} AudioTrack;
static AudioTrack tracks[2] __attribute__((aligned(64)));
static SceUID prepare_lock;
static volatile int current_slot;
static int output_reserved,output_rate,output_fill,output_buffer;
static short output_pcm[2][2048] __attribute__((aligned(64)));
static unsigned long long last_submit_us;
static volatile unsigned long long play_started_us;
/* Background Memory Stick reads (cover art, row tags, preparing the next song) wait while a song
   opens and for its first seconds, so the player's small stream buffer is never starved. */
static volatile int video_active,video_audio_ended;static volatile unsigned int clock_ms; /* video: see video_ui.h */
static char emb_path[PATHLEN];static int emb_start,emb_end; /* an MP3 soundtrack inside a video file */
/* Notes from the sound side for video.log (the video loop copies them out; this thread never writes files) */
static int test_sound_fail;
static volatile int test_raw_peak; /* tests: the loudest decoded sample since the track started */
static volatile unsigned int live_generation; /* the request a radio station was opened for */
static int live_wanted(void){return running && serial==live_generation;}
static char audio_notes_buf[8][160];static volatile int audio_notes;
static void audio_note(const char *fmt,...){va_list ap;va_start(ap,fmt);vsnprintf(audio_notes_buf[audio_notes&7],sizeof(audio_notes_buf[0]),fmt,ap);va_end(ap);audio_notes++;}
static int playback_busy(void){return video_active || state==1 || (state==2 && sceKernelGetSystemTimeWide()-play_started_us<4000000ULL);}
static void wait_playback_settled(void){while(running && playback_busy())sceKernelDelayThread(50000);}
/* video: while one plays the queue stands still, nothing is prepared ahead, and the video follows clock_ms */
static volatile int prefetch_hits,transition_count,boundary_waiting,max_boundary_feed_us;
static void engine_trace(const char *what,AudioTrack *t) {
#ifdef ZOID_ENGINE_TRACE
 FILE *f=fopen("ms0:/engine-trace.txt","a");if(f){fprintf(f,"%llu %s slot=%d current=%d handle=%d status=%d serial=%u path=%s\n",sceKernelGetSystemTimeWide(),what,(int)(t-tracks),current_slot,t->handle,t->status,serial,t->path);fclose(f);}
#else
 (void)what;(void)t;
#endif
}
static void track_close(AudioTrack *t) {
 engine_trace("close",t);
 if(t->handle>=0)sceMp3ReleaseMp3Handle(t->handle);
 if(t->soft){soft_close(t->soft);t->soft=NULL;}
 if(t->fd>=0)sceIoClose(t->fd);
 t->fd=t->handle=-1;t->first_bytes=0;
}
static int fill_track(AudioTrack *t) {
 unsigned char *dst;SceInt32 size,pos;
 int r=sceMp3GetInfoToAddStreamData(t->handle,&dst,&size,&pos);if(r<0)return r;
 if(size<=0)return 0;
 r=sceIoLseek32(t->fd,pos,PSP_SEEK_SET);if(r<0)return r;
 r=sceIoRead(t->fd,dst,size);if(r<=0)return r;
 int result=sceMp3NotifyAddStreamData(t->handle,r);return result<0?result:r;
}
/* While a video plays, the MP3 decoder and the H.264 decoder share the Media Engine; on a real PSP
   restarting the MP3 decoder while a key frame decoded (right after a jump) failed with 0x80671402,
   so during a video they take turns (me_enter, as the AAC decoder does). */
static int mp3_decode(AudioTrack *t,short **pcm){int v=video_active;if(v)me_enter();int r=sceMp3Decode(t->handle,pcm);if(v)me_leave();return r;}
static int mp3_init(AudioTrack *t){int v=video_active;if(v)me_enter();int r=sceMp3Init(t->handle);if(v)me_leave();return r;}
static int track_error(AudioTrack *t,int stage,int code){t->error_stage=stage;t->error_code=code;track_close(t);return 0;}
/* Next decoded chunk in bytes (pcm points at it); 0 at the end, negative on error. */
static int track_decode(AudioTrack *t,short **pcm) {
 if(t->soft){int n=soft_read(t->soft,(short *)t->pcm,SOFT_CHUNK);*pcm=(short *)t->pcm;if(n<0 && t->live)return n;return n>0?n*2*t->channels:0;}
 int r=sceMp3CheckStreamDataNeeded(t->handle);if(r<0)return r;
 if(r>0){r=fill_track(t);if(r<0)return r;}
 return mp3_decode(t,pcm);
}
/* FLAC/WAV: the "index" is one sample per frame, so the shared playback code needs no changes. */
static int prepare_soft(AudioTrack *t,int target) {
 unsigned long long total;int err;
 /* a streamed episode starts where asked (a Range request), so its seeking is a reopen */
 int start=t->live && target>0?target:0;
 t->soft=soft_open_at(t->path,start,&t->rate,&t->channels,&total,&err);if(!t->soft)return track_error(t,18,err);
 Mp3Index *ix=&t->index;ix->frames=(int)total;ix->samples=1;ix->rate=t->rate;ix->trim_start=ix->trim_end=0;
 ix->exact=total>0 && !t->live; /* an episode's length is an estimate: it plays until the stream ends */
 t->wanted=t->position=0;
 if(start)t->wanted=t->position=(unsigned long long)start*t->rate;
 else if(target>0){unsigned long long w=(unsigned long long)target*t->rate;if(w>=total)w=total>(unsigned long long)t->rate?total-t->rate:0;if(soft_seek(t->soft,w))t->wanted=t->position=w;}
 t->first_bytes=track_decode(t,&t->first);
 if(t->first_bytes<=0)return track_error(t,8,t->first_bytes);
 return 1;
}
static int prepare_track(AudioTrack *t,const char *path,int target,unsigned int generation,int background) {
 unsigned long long t0=sceKernelGetSystemTimeWide();
 track_close(t);snprintf(t->path,sizeof(t->path),"%s",path);t->error_stage=t->error_code=0;
 t->live=radio_is_url(path);
 if(t->live){if(background)return track_error(t,18,-1);live_generation=generation;} /* a station is opened only when it is played */
 /* a video's soundtrack stored inside its MP4 (see video_ui.h) plays as MP3 from that byte range */
 int embedded=emb_path[0] && !strcmp(path,emb_path);
 if(!embedded && soft_kind(path))return prepare_soft(t,target);
 if(test_sound_fail && embedded && target>0)return track_error(t,8,(int)0x80671402); /* tests: a PSP refusing the sound after a jump */
 t->fd=sceIoOpen(path,PSP_O_RDONLY,0);if(t->fd<0)return track_error(t,1,t->fd);
 int end=sceIoLseek32(t->fd,0,PSP_SEEK_END),start=stream_start(t->fd,end);
 if(embedded){start=emb_start;end=emb_end;}
 if(start<0)return track_error(t,2,start);
 if(!index_mp3(&t->index,t->fd,start,end,path,generation,background))return track_error(t,15,-1);
 if(!background && serial!=generation)return track_error(t,15,-2);
 Mp3Index *ix=&t->index;unsigned long long total=(unsigned long long)ix->frames*ix->samples;
 t->wanted=(unsigned long long)target*ix->rate+ix->trim_start;
 if(t->wanted>=total)t->wanted=total>(unsigned long long)ix->samples?total-ix->samples:0;
 int stream=ix->offsets[0];t->position=0;
 if(target>0) {
  /* Seek by the Xing table of contents (or linearly), starting 12 frames early so the
     bit reservoir is primed; the extra samples are skipped. */
  int frame=t->wanted/ix->samples;frame=frame>12?frame-12:0;
  double p=(double)frame/ix->frames,byte;
  if(ix->has_toc){double x=p*100;int i=(int)x;if(i>99)i=99;double lo=ix->toc[i],hi=i<99?ix->toc[i+1]:256;byte=ix->toc_base+(lo+(hi-lo)*(x-i))/256*(end-ix->toc_base);}
  else byte=stream+p*(end-stream);
  int size,found=byte>stream?find_frame(ix,t->fd,(int)byte,end,&size):-1;
  if(found>=0){stream=found;t->position=(unsigned long long)frame*ix->samples;}
 }
 SceMp3InitArg a;memset(&a,0,sizeof(a));
 a.mp3StreamStart=stream;a.mp3StreamEnd=end;a.mp3Buf=t->mp3;a.mp3BufSize=sizeof(t->mp3);a.pcmBuf=t->pcm;a.pcmBufSize=sizeof(t->pcm);
 t->handle=sceMp3ReserveMp3Handle(&a);if(t->handle<0)return track_error(t,3,t->handle);
 engine_trace("reserved",t);
 int r=fill_track(t);if(r<=0)return track_error(t,4,r);
 r=mp3_init(t);if(r<0){if(embedded)audio_note("sound restart at %d s: init failed %08X (stream %d)",target,(unsigned int)r,stream);return track_error(t,5,r);}
 sceMp3SetLoopNum(t->handle,0);t->rate=sceMp3GetSamplingRate(t->handle);t->channels=sceMp3GetMp3ChannelNum(t->handle);
 if(t->rate<=0 || t->channels<1 || t->channels>2)return track_error(t,6,t->rate);
 if(sceMp3CheckStreamDataNeeded(t->handle)>0){r=fill_track(t);if(r<0)return track_error(t,4,r);}
 /* After a jump the first frames can be refused (on a real PSP: 0x80671402), as they lean on audio
    from before the jump point: skip up to 24 of them (each about 26 ms) instead of giving up. */
 int skipped=0;
 for(;;) {
  t->first_bytes=mp3_decode(t,&t->first);
  if(t->first_bytes>0 || target<=0 || skipped>=24)break;
  skipped++;t->position+=ix->samples;
  if(sceMp3CheckStreamDataNeeded(t->handle)>0 && fill_track(t)<0)break;
 }
 if(embedded && target>0)audio_note("sound restart at %d s: stream %d (box %d-%d), %s, init ok, first decode %08X after %d skipped, %u ms",
  target,stream,emb_start,emb_end,ix->has_toc?"TOC":"linear",(unsigned int)t->first_bytes,skipped,(unsigned int)((sceKernelGetSystemTimeWide()-t0)/1000));
 if(t->first_bytes<=0)return track_error(t,8,t->first_bytes);
 return 1;
}
static int prepare_ahead(SceSize a,void *b) {
 (void)a;(void)b;
 while(running) {
  char next[PATHLEN];
  if(video_active || state!=2 || (playback_busy() && duration-elapsed>fade_seconds+15) || !queue_peek_next(next) || tracks[current_slot].live || radio_is_url(next)){sceKernelDelayThread(10000);continue;}
  sceKernelWaitSema(prepare_lock,1,NULL);int slot=1-current_slot;AudioTrack *t=&tracks[slot];
  if(t->status==1 || (t->status==2 && strcmp(t->path,next)==0)){sceKernelSignalSema(prepare_lock,1);sceKernelDelayThread(10000);continue;}
  t->status=1;snprintf(t->path,sizeof(t->path),"%s",next);sceKernelSignalSema(prepare_lock,1);
  int ok=prepare_track(t,next,0,0,1);
  engine_trace("prepared",t);
  sceKernelWaitSema(prepare_lock,1,NULL);t->status=ok?2:4;sceKernelSignalSema(prepare_lock,1);
  if(!ok)sceKernelDelayThread(100000);
 }
 return 0;
}
static AudioTrack *acquire_track(const char *path,int target,unsigned int generation) {
 while(running && serial==generation) {
  sceKernelWaitSema(prepare_lock,1,NULL);int other=1-current_slot;AudioTrack *t=&tracks[other];
  if(target==0 && t->status==2 && strcmp(t->path,path)==0){current_slot=other;t->status=3;prefetch_hits++;engine_trace("take prepared",t);sceKernelSignalSema(prepare_lock,1);return t;}
  if(target==0 && t->status==1 && strcmp(t->path,path)==0){sceKernelSignalSema(prepare_lock,1);sceKernelDelayThread(1000);continue;}
  t=&tracks[current_slot];t->status=3;sceKernelSignalSema(prepare_lock,1);
  if(prepare_track(t,path,target,generation,0))return t;
  /* a film's soundtrack after a jump: try again a little further on (the picture follows the sound) */
  for(int retry=0;retry<3 && video_active && running && serial==generation;retry++){sceKernelDelayThread(50000);if(prepare_track(t,path,target+1+retry*2,generation,0))return t;}
  if(serial==generation)fail(t->error_stage,t->error_code);
  return NULL;
 }
 return NULL;
}
static int release_output(void) {
 /* Wait for the actual DMA completion instead of polling release/busy. */
 for(int i=0;i<3;i++) {
  sceAudioSRCOutputBlocking(0,NULL);
  int r=sceAudioSRCChRelease();
  if((unsigned int)r!=0x80268002 && (unsigned int)r!=0x80260002)return r;
 }
 return (int)0x80268002;
}
static int submit_output(void) {
 unsigned long long now=sceKernelGetSystemTimeWide();
 if(boundary_waiting && last_submit_us){int wait=now-last_submit_us;if(wait>max_boundary_feed_us)max_boundary_feed_us=wait;boundary_waiting=0;transition_count++;}
 /* Test builds exercise decoding/output timing but must never make sound. */
#ifdef ZOID_SMOKE_TEST
 int volume=0;
#else
 int volume=PSP_AUDIO_VOLUME_MAX;
#endif
 eq_process(output_pcm[output_buffer],1024,output_rate);
 boost_process(output_pcm[output_buffer],1024);
 int r=sceAudioSRCOutputBlocking(volume,output_pcm[output_buffer]);
 if(r<0){fail(10,r);return 0;}
 last_submit_us=sceKernelGetSystemTimeWide();output_fill=0;output_buffer^=1;return 1;
}
static void finish_output(int pad) {
 if(output_reserved){
  if(pad && output_fill){memset(output_pcm[output_buffer]+output_fill*2,0,(1024-output_fill)*4);submit_output();}
  int r=release_output();if(r<0)fail(16,r);
 }
 output_reserved=output_fill=0;last_submit_us=0;boundary_waiting=0;
}
static int stream_pcm(short *pcm,int frames,int channels,int rate,unsigned int generation) {
 if(output_reserved && output_rate!=rate)finish_output(1);
 if(!output_reserved){int r=sceAudioSRCChReserve(1024,rate,2);if(r<0){fail(9,r);return 0;}output_reserved=1;output_rate=rate;}
 for(int i=0;i<frames && running && serial==generation;i++) {
  output_pcm[output_buffer][output_fill*2]=pcm[i*channels];output_pcm[output_buffer][output_fill*2+1]=pcm[i*channels+channels-1];output_fill++;
  if(output_fill==1024 && !submit_output())return 0;
 }
 return 1;
}
/* ---- crossfade and trim silence ----
   Crossfade: during the outgoing song's last seconds the next song, already prepared on the
   other decoder, is decoded too and the two are mixed with equal-power gains. When the outgoing
   song ends, the incoming one simply continues (acquire_track takes the prepared deck), and any
   of its samples decoded but not yet played are carried over, so nothing restarts or skips.
   Trim silence: a quiet intro (under about -44 dB, up to 10 s) is skipped, and a silent tail
   longer than 0.8 s ends the song early, leaving a short pause. */
#define TRIM_LEVEL 200
static short carry_pcm[2304*2];static int carry_frames;static char carry_path[PATHLEN];
static volatile int crossfade_count;
static AudioTrack *crossfade_partner(AudioTrack *t) {
 char next[PATHLEN];if(repeat_mode==1 || !queue_peek_next(next) || !strcmp(next,t->path))return NULL;
 sceKernelWaitSema(prepare_lock,1,NULL);AudioTrack *b=&tracks[1-current_slot];
 int ok=b->status==2 && !strcmp(b->path,next) && b->rate==t->rate && (b->handle>=0 || b->soft);
 sceKernelSignalSema(prepare_lock,1);return ok?b:NULL;
}
/* Decodes the next chunk of the incoming deck as stereo; 0 when it has nothing more. */
static int deck_chunk(AudioTrack *b,short *buf) {
 for(int guard=0;guard<64;guard++) {
  short *pcm=NULL;int bytes;
  if(b->first_bytes){pcm=b->first;bytes=b->first_bytes;b->first_bytes=0;}
  else bytes=track_decode(b,&pcm);
  if(bytes<=0 || !pcm || bytes>(int)sizeof(b->pcm))return 0;
  int ch=b->channels,fr=bytes/(2*ch),skip=0;
  if(b->position+fr<=b->wanted){b->position+=fr;continue;}
  if(b->position<b->wanted)skip=b->wanted-b->position;
  int n=fr-skip;for(int i=0;i<n;i++){buf[i*2]=pcm[(skip+i)*ch];buf[i*2+1]=pcm[(skip+i)*ch+ch-1];}
  b->position+=fr;return n;
 }
 return 0;
}
static int chunk_peak(const short *pcm,int frames,int ch){int p=0;for(int i=0;i<frames*ch;i+=2){int v=abs(pcm[i]);if(v>p)p=v;}return p;}
/* After waking from sleep, Memory Stick file handles are stale: drop the prepared deck. */
static void engine_drop_prepared(void) {
 sceKernelWaitSema(prepare_lock,1,NULL);AudioTrack *o=&tracks[1-current_slot];
 if(o->status==2 || o->status==4){track_close(o);o->status=0;}
 sceKernelSignalSema(prepare_lock,1);carry_frames=0;
}
static int play_track(const char *path,unsigned int generation,int target,int continuous) {
 if(!continuous)finish_output(0);
 state=1;elapsed=target;
 AudioTrack *t=acquire_track(path,target,generation);if(!t){state=0;return 0;}
 unsigned long long raw_end=(unsigned long long)t->index.frames*t->index.samples-t->index.trim_end;
 /* without a Xing frame count the length is an estimate: play until the decoder runs out */
 unsigned long long cut_end=t->index.exact?raw_end:~0ULL;
 duration=(raw_end-t->index.trim_start+t->rate-1)/t->rate;
 play_started_us=sceKernelGetSystemTimeWide();state=2;seek_ack=generation;int natural_end=0;
 /* movies, stations and podcast episodes get the volume boost (eq.h); music is left as mastered */
 {int n=strlen(pod_root),pod=!strncmp(path,pod_root,n) && path[n]=='/',src=video_active || t->live || pod;if(src!=boost_src)boost_reset();boost_src=src;}
#ifdef ZOID_SMOKE_TEST
 test_raw_peak=0;
#endif
 /* a deck handed over from a crossfade: first play what it decoded but had not yet output */
 if(carry_frames && !strcmp(carry_path,path))stream_pcm(carry_pcm,carry_frames,2,t->rate,generation);
 carry_frames=0;
 int trim_lead=mix_mode==1 && continuous && !t->live,silent_run=0,fading=0;AudioTrack *in=NULL;
 unsigned long long fade_at=0,fade_len=1,fade_done=0;
 if(mix_mode==2 && !video_active && !t->live && repeat_mode!=1){unsigned long long want=(unsigned long long)fade_seconds*t->rate;if(raw_end>t->index.trim_start+want*3)fade_at=raw_end-want;}
 static short in_buf[2304*2],mix_buf[2304*2];int in_have=0,in_read=0;
 while(running && serial==generation) {
  if(paused){sceKernelDelayThread(10000);continue;}
  short *pcm=NULL;int bytes;
  if(t->first_bytes){pcm=t->first;bytes=t->first_bytes;t->first_bytes=0;}
  else {
   bytes=track_decode(t,&pcm);
  }
  if(bytes==0 || (unsigned int)bytes==0x80671402){natural_end=1;break;}
  if(bytes<0){fail(8,bytes);break;}
  if(!pcm || bytes>(int)sizeof(t->pcm) || bytes%(2*t->channels)){fail(8,-2);break;}
  int frames=bytes/(2*t->channels),skip=0;
  if(t->position>=cut_end){natural_end=1;break;}
  if(t->position+frames>cut_end)frames=cut_end-t->position;
  if(t->position+frames<=t->wanted){t->position+=frames;continue;}
  if(t->position<t->wanted)skip=t->wanted-t->position;
  for(int k=0;k<24;k++){int peak=0;for(int i=k*frames/24;i<(k+1)*frames/24;i+=4){int v=abs(pcm[i*t->channels]);if(v>peak)peak=v;}levels[k]=peak*28/32768;}
#ifdef ZOID_SMOKE_TEST
  for(int i=0;i<frames*t->channels;i+=3){int v=abs(pcm[i]);if(v>test_raw_peak)test_raw_peak=v;} /* tests: the decoded level, before EQ and boost */
#endif
  for(int k=0;k<128;k++)scope[k]=pcm[(k*frames/128)*t->channels];
  {int n=frames<512?frames:512;for(int k=0;k<n;k++)wave[k]=t->channels>1?(pcm[k*2]+pcm[k*2+1])/2:pcm[k];}
  static int lowpass,previous_bass;int bass_peak=0;
  for(int i=0;i<frames;i+=4){lowpass+=(pcm[i*t->channels]-lowpass)/8;int v=abs(lowpass);if(v>bass_peak)bass_peak=v;}
  int onset=bass_peak-previous_bass;previous_bass+=(bass_peak-previous_bass)/4;
  if(onset>300){int pulse=onset/120;if(pulse>40)pulse=40;if(pulse>beat_pulse)beat_pulse=pulse;}else if(beat_pulse>0)beat_pulse--;
  int n=frames-skip;short *src=pcm+skip*t->channels;
  if(trim_lead){if(t->position<t->index.trim_start+10ULL*t->rate && chunk_peak(src,n,t->channels)<TRIM_LEVEL){t->position+=frames;if(serial==generation)elapsed=(t->position-t->index.trim_start)/t->rate;continue;}trim_lead=0;}
  if(mix_mode==1 && !video_active && !t->live && t->position+frames+20ULL*t->rate>raw_end){if(chunk_peak(src,n,t->channels)<TRIM_LEVEL)silent_run+=n;else silent_run=0;if(silent_run>t->rate*8/10){natural_end=1;break;}}
  if(fade_at && !fading && t->position+frames>=fade_at){in=crossfade_partner(t);if(in){fading=1;fade_len=raw_end-(t->position+skip);if(!fade_len)fade_len=1;crossfade_count++;}else fade_at=0;}
  if(fading) {
   float ga=1,gb=0;
   for(int i=0;i<n;i++) {
    if(in_read>=in_have){in_have=deck_chunk(in,in_buf);in_read=0;if(in_have<0)in_have=0;}
    if(i%64==0){float g=(float)(fade_done+i)/fade_len;if(g>1)g=1;ga=cosf(g*1.5707963f);gb=sinf(g*1.5707963f);}
    int al=src[i*t->channels],ar=src[i*t->channels+t->channels-1],bl=0,br=0;
    if(in_read<in_have){bl=in_buf[in_read*2];br=in_buf[in_read*2+1];in_read++;}
    int l=(int)(al*ga+bl*gb),r=(int)(ar*ga+br*gb);
    if(l>32767)l=32767;if(l<-32768)l=-32768;if(r>32767)r=32767;if(r<-32768)r=-32768;
    mix_buf[i*2]=l;mix_buf[i*2+1]=r;
   }
   fade_done+=n;
   if(!stream_pcm(mix_buf,n,2,t->rate,generation))break;
  }else if(!stream_pcm(src,n,t->channels,t->rate,generation))break;
  t->position+=frames;if(serial==generation){elapsed=(t->position-t->index.trim_start)/t->rate;clock_ms=(unsigned int)((t->position-t->index.trim_start)*1000/t->rate);}
  if(t->position>=cut_end){natural_end=1;break;}
 }
 if(fading) {
  if(natural_end && serial==generation){int left=in_have-in_read;if(left>0){memcpy(carry_pcm,in_buf+in_read*2,left*4);carry_frames=left;snprintf(carry_path,sizeof(carry_path),"%s",in->path);}}
  else{sceKernelWaitSema(prepare_lock,1,NULL);track_close(in);in->status=0;sceKernelSignalSema(prepare_lock,1);}
 }
 track_close(t);t->status=0;
 if(natural_end && serial==generation && !error_code){elapsed=duration;return 1;}
 return 0;
}
static int audio(SceSize args,void *arg) {
 (void)args;(void)arg;
 for(int i=0;i<2;i++){tracks[i].fd=tracks[i].handle=-1;tracks[i].soft=NULL;}
 int r=sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC);if(r<0){fail(11,r);return 0;}
 r=sceUtilityLoadModule(PSP_MODULE_AV_MP3);if(r<0){fail(12,r);return 0;}
 r=sceMp3InitResource();if(r<0){fail(13,r);return 0;}
 prepare_lock=sceKernelCreateSema("prepare",0,1,1,NULL);if(prepare_lock<0){fail(17,prepare_lock);return 0;}
 SceUID worker=sceKernelCreateThread("zoid_next",prepare_ahead,0x30,0x10000,0,NULL);if(worker>=0)sceKernelStartThread(worker,0,NULL);
 audio_ready=1;unsigned int seen=0;
 while(running) {
  char path[PATHLEN];unsigned int next;int target,continuous;
  sceKernelWaitSema(command_lock,1,NULL);next=serial;target=pending_seek;continuous=pending_continuous;snprintf(path,sizeof(path),"%s",pending);sceKernelSignalSema(command_lock,1);
  if(next!=seen){
   seen=next;
   if(path[0]){
    int ended=play_track(path,seen,target,continuous);
    if(ended && serial==seen && video_active){video_audio_ended=1;finish_output(1);}
    else if(ended && serial==seen){queue_next(1,seen);if(serial!=seen){if(pending_continuous)boundary_waiting=1;else finish_output(1);continue;}finish_output(1);}
    else if(serial==seen)finish_output(0);
   }else {finish_output(0);elapsed=0;}
   if(serial==seen){state=0;beat_pulse=0;for(int i=0;i<24;i++)levels[i]=0;for(int i=0;i<128;i++)scope[i]=0;}
  }else sceKernelDelayThread(1000);
 }
 if(worker>=0)sceKernelWaitThreadEnd(worker,NULL);
 finish_output(0);for(int i=0;i<2;i++)track_close(&tracks[i]);sceKernelDeleteSema(prepare_lock);sceMp3TermResource();return 0;
}
