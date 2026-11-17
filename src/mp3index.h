/* Each decoder owns its index and read cache so preparing the next track
   cannot overwrite the playing track's seek table. */
#define INDEX_STRIDE 32
#define INDEX_SIZE 65536
typedef struct {
 int offsets[INDEX_SIZE],frames,rate,samples,end,trim_start,trim_end,tag_frames,exact,has_toc,toc_base;
 unsigned char toc[100];
 char path[PATHLEN];
 unsigned char scan[65536];int scan_base,scan_length;
} Mp3Index;
static int header_at(Mp3Index *ix,int fd,int pos,unsigned char h[4]) {
 if(pos<ix->scan_base || pos+4>ix->scan_base+ix->scan_length) {
  if(sceIoLseek32(fd,pos,PSP_SEEK_SET)<0)return 0;
  ix->scan_base=pos;ix->scan_length=sceIoRead(fd,ix->scan,sizeof(ix->scan));
 }
 if(ix->scan_length<4 || pos+4>ix->scan_base+ix->scan_length)return 0;
 memcpy(h,ix->scan+pos-ix->scan_base,4);return 1;
}
static int frame_size(const unsigned char h[4],int *rate,int *samples) {
 static const int rates[]={44100,48000,32000};
 static const int v1[]={0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0};
 static const int v2[]={0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0};
 int version=(h[1]>>3)&3,layer=(h[1]>>1)&3,b=(h[2]>>4)&15,r=(h[2]>>2)&3;
 if(h[0]!=255 || (h[1]&224)!=224 || version==1 || layer!=1 || !b || b==15 || r==3)return 0;
 *rate=rates[r]/(version==3?1:version==2?2:4);*samples=version==3?1152:576;
 return (version==3?144000:72000)*(version==3?v1[b]:v2[b])/(*rate)+((h[2]>>1)&1);
}
static int xing_metadata(Mp3Index *ix,int fd,int pos,int size,const unsigned char h[4]) {
 unsigned char b[2048];if(size>(int)sizeof(b))return 0;
 if(sceIoLseek32(fd,pos,PSP_SEEK_SET)<0 || sceIoRead(fd,b,size)!=size)return 0;
 int version=(h[1]>>3)&3,mono=(h[3]>>6)==3;
 int off=4+((h[1]&1)?0:2)+(version==3?(mono?17:32):(mono?9:17));
 if(off+12>size || (memcmp(b+off,"Xing",4) && memcmp(b+off,"Info",4)))return 0;
 int flags=b[off+7];off+=8;
 if(!(flags&1))return 1;
 ix->tag_frames=((unsigned int)b[off]<<24)|(b[off+1]<<16)|(b[off+2]<<8)|b[off+3];off+=4;
 if(flags&2)off+=4;
 if(flags&4){if(off+100>size)return 1;memcpy(ix->toc,b+off,100);ix->has_toc=1;ix->toc_base=pos;off+=100;}
 if(flags&8)off+=4;
 /* LAME/Lavc gapless extension. Decoder synthesis latency is 528+1 samples.
    Format reference: CC0 minimp3_ex.h mp3dec_check_vbrtag. */
 if(off+36<=size && (!memcmp(b+off,"LAME",4) || !memcmp(b+off,"Lavc",4))) {
  int delay=(b[off+21]<<4)|(b[off+22]>>4),padding=((b[off+22]&15)<<8)|b[off+23];
  ix->trim_start=delay+529;ix->trim_end=padding>529?padding-529:0;
 }
 return 1;
}
/* First position at or after pos holding a frame whose successor is also a valid frame. */
static int find_frame(Mp3Index *ix,int fd,int pos,int end,int *size) {
 for(int limit=pos+262144;pos+4<=end && pos<limit;pos++) {
  unsigned char h[4],n[4];int rate,samples,r,s;
  if(!header_at(ix,fd,pos,h))return -1;
  int sz=frame_size(h,&rate,&samples);if(!sz || pos+sz>end)continue;
  if(pos+sz+4<end && (!header_at(ix,fd,pos+sz,n) || !frame_size(n,&r,&s) || r!=rate))continue;
  *size=sz;return pos;
 }
 return -1;
}
static int index_mp3(Mp3Index *ix,int fd,int start,int end,const char *path,unsigned int generation,int background) {
 if(ix->end==end && strcmp(ix->path,path)==0)return 1;
 /* Only the start of the file is read, so a song can begin at once. Length and gapless
    trim come from the Xing/Info header; without one, a CBR estimate is used. */
 ix->path[0]=0;ix->end=ix->frames=0;ix->scan_base=-1;ix->scan_length=0;ix->trim_start=ix->trim_end=ix->tag_frames=ix->has_toc=0;
 unsigned char h[4];int size,pos=find_frame(ix,fd,start,end,&size);
 if(pos<0 || !header_at(ix,fd,pos,h))return 0;
 if(xing_metadata(ix,fd,pos,size,h)){pos=find_frame(ix,fd,pos+size,end,&size);if(pos<0 || !header_at(ix,fd,pos,h))return 0;}
 if(!frame_size(h,&ix->rate,&ix->samples))return 0;
 ix->offsets[0]=pos;
 if(ix->tag_frames>0){ix->frames=ix->tag_frames;ix->exact=1;}
 else{ix->frames=(end-pos)/size;if(ix->frames<1)ix->frames=1;ix->exact=0;ix->has_toc=0;ix->trim_start=ix->trim_end=0;}
 if(!running || (!background && serial!=generation))return 0;
 if((unsigned long long)ix->frames*ix->samples<=(unsigned int)(ix->trim_start+ix->trim_end))ix->trim_start=ix->trim_end=0;
 ix->end=end;snprintf(ix->path,sizeof(ix->path),"%s",path);return 1;
}
