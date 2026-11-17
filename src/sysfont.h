/* Letters the theme fonts lack (accents, Cyrillic, Greek, Japanese, Chinese, Korean) come from the
   fonts every PSP has in flash0:/font: ltn0 (Latin), jpn0 (kana, kanji, Greek, Cyrillic) and kr0
   (Hangul). They are Sony's PGF bitmap fonts: one size, 4 bits per pixel, run-length coded.
   A font is opened the first time a letter needs it; only its two lookup tables are kept in
   memory, and each glyph is read from flash when first drawn. Drawn glyphs live in a 4-bit
   512x512 texture (361 cells), reused least recently drawn first.
   Text is UTF-8. A byte that does not start valid UTF-8 is taken as Latin-1, so older
   Latin-1 file names still show their accents. */

/* ---- UTF-8 ---- */
static int utf8_put(char *b,unsigned int cp) {
 if(cp<0x80){b[0]=cp;return 1;}
 if(cp<0x800){b[0]=0xC0|(cp>>6);b[1]=0x80|(cp&63);return 2;}
 if(cp<0x10000){b[0]=0xE0|(cp>>12);b[1]=0x80|((cp>>6)&63);b[2]=0x80|(cp&63);return 3;}
 b[0]=0xF0|(cp>>18);b[1]=0x80|((cp>>12)&63);b[2]=0x80|((cp>>6)&63);b[3]=0x80|(cp&63);return 4;
}
/* The letter at s[*i]; *i moves past it. */
static unsigned int utf8_at(const char *s,int *i) {
 const unsigned char *p=(const unsigned char *)s+*i;unsigned int c=p[0];
 if(c<0x80){(*i)++;return c;}
 int len=c>=0xF0 && c<0xF5?4:c>=0xE0?3:c>=0xC2 && c<0xE0?2:0;
 for(int k=1;k<len;k++)if((p[k]&0xC0)!=0x80){len=0;break;}
 if(!len){(*i)++;return c;} /* not UTF-8: Latin-1 */
 unsigned int cp=len==2?c&31:len==3?c&15:c&7;
 for(int k=1;k<len;k++)cp=(cp<<6)|(p[k]&63);
 *i+=len;return cp;
}

/* ---- PGF fonts ---- */
typedef struct {
 const char *path;int tried,ok,fd;
 int first,last,map_len,map_bpe,ptr_len,ptr_bpe,max_w,max_h,data_off,size;
 unsigned char *map,*ptr;int adv_n,adv[256];
} SysFont;
enum {SF_LATIN,SF_JAPANESE,SF_KOREAN,SF_N};
static SysFont sysfonts[SF_N]={{"flash0:/font/ltn0.pgf"},{"flash0:/font/jpn0.pgf"},{"flash0:/font/kr0.pgf"}};
static int pgf_bits(const unsigned char *b,unsigned int pos,int n) {
 int v=0;for(int i=0;i<n;i++,pos++)v|=((b[pos>>3]>>(pos&7))&1)<<i;return v;
}
static int pgf_i32(const unsigned char *b){return b[0]|(b[1]<<8)|(b[2]<<16)|(b[3]<<24);}
static int pgf_read(SysFont *f,int off,void *buf,int n) {
 for(int tries=0;tries<2;tries++) {
  if(f->fd<0)f->fd=sceIoOpen(f->path,PSP_O_RDONLY,0);
  if(f->fd<0)return -1;
  if(sceIoLseek32(f->fd,off,PSP_SEEK_SET)==off){int got=sceIoRead(f->fd,buf,n);if(got>=0)return got;}
  sceIoClose(f->fd);f->fd=-1; /* a stale handle (after sleep): open again once */
 }
 return -1;
}
static int pgf_open(SysFont *f) {
 if(f->tried)return f->ok;
 f->tried=1;f->fd=-1;
 unsigned char h[0x1A0];
 int got=pgf_read(f,0,h,sizeof(h));
 if(got<0x188 || memcmp(h+4,"PGF0",4))return 0;
 int hdr=h[2]|(h[3]<<8),rev=pgf_i32(h+8);
 f->map_len=pgf_i32(h+0x10);f->ptr_len=pgf_i32(h+0x14);f->map_bpe=pgf_i32(h+0x18);f->ptr_bpe=pgf_i32(h+0x1C);
 f->first=h[0xB6]|(h[0xB7]<<8);f->last=h[0xB8]|(h[0xB9]<<8);f->max_w=h[0xFC]|(h[0xFD]<<8);f->max_h=h[0xFE]|(h[0xFF]<<8);
 int tab[4];for(int i=0;i<4;i++)tab[i]=h[0x102+i];
 int shadow_len=pgf_i32(h+0x16C),shadow_bpe=pgf_i32(h+0x170);
 if(h[0x22]!=4 || f->map_bpe<1 || f->map_bpe>24 || f->ptr_bpe<1 || f->ptr_bpe>24 || f->map_len<=0 || f->ptr_len<=0 || f->max_w>64 || f->max_h>64)return 0;
 /* tables after the header: dimensions, x and y adjust, advance (pairs of 32-bit values each) */
 int off=hdr+(tab[0]+tab[1]+tab[2])*8;
 unsigned char *adv=malloc(tab[3]*8+1);if(!adv)return 0;
 if(pgf_read(f,off,adv,tab[3]*8)!=tab[3]*8){free(adv);return 0;}
 f->adv_n=tab[3];for(int i=0;i<tab[3];i++)f->adv[i]=pgf_i32(adv+i*8);free(adv);
 off+=tab[3]*8+((shadow_len*shadow_bpe+31)&~31)/8;
 if(rev==3 && hdr>=0x19C)off+=((pgf_i32(h+0x18C)&0xFFFF)+(pgf_i32(h+0x194)&0xFFFF))*4; /* charmap compression tables */
 int map_bytes=(int)((((long long)f->map_len*f->map_bpe+31)&~31)/8),ptr_bytes=(int)((((long long)f->ptr_len*f->ptr_bpe+31)&~31)/8);
 f->map=malloc(map_bytes);f->ptr=malloc(ptr_bytes);
 if(!f->map || !f->ptr || pgf_read(f,off,f->map,map_bytes)!=map_bytes || pgf_read(f,off+map_bytes,f->ptr,ptr_bytes)!=ptr_bytes){free(f->map);free(f->ptr);f->map=f->ptr=NULL;return 0;}
 f->data_off=off+map_bytes+ptr_bytes;
 f->size=sceIoLseek32(f->fd,0,PSP_SEEK_END);
 f->ok=1;return 1;
}
static int pgf_index(SysFont *f,unsigned int cp) {
 if((int)cp<f->first || (int)cp>f->last)return -1;
 int i=cp-f->first;if(i>=f->map_len)return -1;
 int g=pgf_bits(f->map,(unsigned int)i*f->map_bpe,f->map_bpe);return g<f->ptr_len?g:-1;
}
typedef struct {int w,h,left,top,adv;} PgfGlyph; /* adv in 1/64 pixel */
/* Decodes glyph g into pix (w*h bytes, 0-15). Returns 0 when it cannot be read. */
static int pgf_glyph(SysFont *f,int g,PgfGlyph *o,unsigned char *pix,int pix_cap) {
 static unsigned char b[1024];
 int off=f->data_off+pgf_bits(f->ptr,(unsigned int)g*f->ptr_bpe,f->ptr_bpe)*4;
 if(off<f->data_off || off>=f->size)return 0;
 int got=pgf_read(f,off,b,sizeof(b));if(got<16)return 0;
 unsigned int p=14,end=got*8;
 o->w=pgf_bits(b,p,7);p+=7;o->h=pgf_bits(b,p,7);p+=7;
 o->left=pgf_bits(b,p,7);p+=7;if(o->left>=64)o->left-=128;
 o->top=pgf_bits(b,p,7);p+=7;if(o->top>=64)o->top-=128;
 int flags=pgf_bits(b,p,6);p+=6;
 p+=7+9; /* shadow flags and id */
 for(int t=0;t<3;t++)p+=(flags&(4<<t))?8:64; /* dimensions, x and y adjust: a table index or two values */
 if(flags&0x20){int a=pgf_bits(b,p,8);p+=8;o->adv=a<f->adv_n?f->adv[a]:0;}else{o->adv=pgf_bits(b,p,32);p+=64;}
 int n=o->w*o->h;if(o->w>f->max_w+2 || o->h>f->max_h+2 || n>pix_cap)return 0;
 /* nibble runs: c<8 repeats the next value c+1 times; c>=8 is 16-c literal values */
 static unsigned char lin[64*64];int k=0;
 while(k<n && p+8<=end) {
  int c=pgf_bits(b,p,4);p+=4;
  if(c<8){int v=pgf_bits(b,p,4);p+=4;for(int i=0;i<=c && k<n;i++)lin[k++]=v;}
  else for(int i=0;i<16-c && k<n && p+4<=end;i++){lin[k++]=pgf_bits(b,p,4);p+=4;}
 }
 while(k<n)lin[k++]=0;
 for(int i=0;i<n;i++){int x,y;if((flags&3)==1){x=i%o->w;y=i/o->w;}else{x=i/o->h;y=i%o->h;}pix[y*o->w+x]=lin[i];}
 return 1;
}

/* ---- glyph cache: a 4-bit texture of 26x26 cells ---- */
#define GC_CELL 26
#define GC_PER 19
#define GC_N (GC_PER*GC_PER)
static unsigned char gcache[512*512/2] __attribute__((aligned(16)));
static unsigned int gclut[16] __attribute__((aligned(16)));
typedef struct {unsigned int cp;int next,font;unsigned int last;signed char left,top;unsigned char w,h;short adv;} GCell;
static GCell gcells[GC_N];static int gc_used,gc_head[256],gc_ready;
static unsigned int gfx_frame; /* frames drawn; a cell drawn this frame is never reused this frame */
static float sys_cap=14; /* height of 'H' in the PSP font, which text sizes are matched to */
static void gc_init(void) {
 for(int i=0;i<256;i++)gc_head[i]=-1;
 for(int i=0;i<16;i++)gclut[i]=((unsigned int)(i*17)<<24)|0xffffff;
 sceKernelDcacheWritebackRange(gclut,sizeof(gclut));gc_ready=1;
}
/* which font to try first: Hangul from kr0, CJK and kana from jpn0, the rest from ltn0 */
static int sys_order(unsigned int cp,int k) {
 static const int hangul[3]={SF_KOREAN,SF_JAPANESE,SF_LATIN},cjk[3]={SF_JAPANESE,SF_KOREAN,SF_LATIN},latin[3]={SF_LATIN,SF_JAPANESE,SF_KOREAN};
 int is_hangul=(cp>=0x1100 && cp<0x1200) || (cp>=0x3130 && cp<0x3190) || (cp>=0xAC00 && cp<0xD7B0);
 int is_cjk=cp>=0x2E80 && !is_hangul;
 return is_hangul?hangul[k]:is_cjk?cjk[k]:latin[k];
}
/* Greek with tonos or dialytika -> the plain letter (0 when cp is not one) */
static unsigned int greek_base(unsigned int cp) {
 static const unsigned short from[]={0x386,0x388,0x389,0x38A,0x38C,0x38E,0x38F,0x390,0x3AA,0x3AB,0x3AC,0x3AD,0x3AE,0x3AF,0x3B0,0x3CA,0x3CB,0x3CC,0x3CD,0x3CE};
 static const unsigned short to[]  ={0x391,0x395,0x397,0x399,0x39F,0x3A5,0x3A9,0x3B9,0x399,0x3A5,0x3B1,0x3B5,0x3B7,0x3B9,0x3C5,0x3B9,0x3C5,0x3BF,0x3C5,0x3C9};
 for(unsigned int i=0;i<sizeof(from)/sizeof(from[0]);i++)if(from[i]==cp)return to[i];
 return 0;
}
/* The cache cell for cp, decoding it on first use. NULL when no PSP font has it (or the cache
   is full of letters already drawn this frame). */
static GCell *sys_glyph(unsigned int cp) {
 if(!gc_ready)gc_init();
 int bucket=cp&255;
 for(int i=gc_head[bucket];i>=0;i=gcells[i].next)if(gcells[i].cp==cp){gcells[i].last=gfx_frame;return gcells[i].font<0?NULL:&gcells[i];}
 /* a free cell, else the one drawn longest ago */
 int c=-1;
 if(gc_used<GC_N)c=gc_used++;
 else {
  unsigned int best=~0u;for(int i=0;i<GC_N;i++)if(gcells[i].last!=gfx_frame && gcells[i].last<best){best=gcells[i].last;c=i;}
  if(c<0)return NULL;
  int *link=&gc_head[gcells[c].cp&255];while(*link>=0 && *link!=c)link=&gcells[*link].next;if(*link==c)*link=gcells[c].next;
 }
 GCell *e=&gcells[c];e->cp=cp;e->last=gfx_frame;e->font=-1;e->w=e->h=0;e->left=e->top=0;e->adv=0;
 e->next=gc_head[bucket];gc_head[bucket]=c;
 static unsigned char pix[64*64];PgfGlyph g;
 unsigned int want[2]={cp,greek_base(cp)}; /* an accented Greek letter no font has: its plain letter */
 for(int k=0;k<6 && e->font<0;k++) {
  unsigned int c2=want[k/3];if(!c2)break;
  int fi=sys_order(c2,k%3);SysFont *f=&sysfonts[fi];
  if(!pgf_open(f))continue;
  int gi=pgf_index(f,c2);if(gi<0 || !pgf_glyph(f,gi,&g,pix,sizeof(pix)))continue;
  e->font=fi;e->w=g.w>GC_CELL-2?GC_CELL-2:g.w;e->h=g.h>GC_CELL-2?GC_CELL-2:g.h;e->left=g.left;e->top=g.top;e->adv=g.adv;
  /* Greek and Cyrillic in these fonts are full width (made to sit beside Japanese); set them as tight as Latin */
  if(c2>=0x370 && c2<0x530 && g.adv>(g.w+2)*64){e->left=0;e->adv=(g.w+1)*64;}
  /* the glyph goes in one pixel from the cell's corner, so filtering never picks up a neighbor */
  int cx=(c%GC_PER)*GC_CELL,cy=(c/GC_PER)*GC_CELL;
  for(int y=0;y<GC_CELL;y++)memset(gcache+((cy+y)*512+cx)/2,0,GC_CELL/2);
  for(int y=0;y<e->h;y++)for(int x=0;x<e->w;x++){int px=cx+1+x,py=cy+1+y;unsigned char *d=gcache+(py*512+px)/2,v=pix[y*g.w+x];*d=(px&1)?(*d&0x0F)|(v<<4):(*d&0xF0)|v;}
  sceKernelDcacheWritebackRange(gcache+(cy*512)/2,GC_CELL*256);
 }
 return e->font<0?NULL:e;
}
static int sys_cap_done;
static void sys_cap_measure(void) {
 sys_cap_done=1;SysFont *f=&sysfonts[SF_LATIN];
 if(pgf_open(f)){int gi=pgf_index(f,'H');PgfGlyph g;static unsigned char pix[64*64];if(gi>=0 && pgf_glyph(f,gi,&g,pix,sizeof(pix)) && g.top>4)sys_cap=g.top;}
}
/* Binds the glyph cache as the texture (call gc_unbind after drawing with it). */
static void gc_bind(void) {
 sceGuClutMode(GU_PSM_8888,0,0x0f,0);sceGuClutLoad(2,gclut);
 sceGuTexMode(GU_PSM_T4,0,0,0);sceGuTexImage(0,512,512,512,gcache);sceGuTexFlush();
}
