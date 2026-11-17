/* Books: page pictures. See page.h.
   JPEGs are decoded already reduced (libjpeg can skip to 1/2, 1/4 or 1/8 size almost for free),
   then every page goes through one area-averaging scaler that reads the source a row at a time,
   so a full-size page is never held in memory. Each output pixel is the exact average of the
   source pixels under it: linework stays crisp and screentones don't shimmer into moire, which
   is what letting the GPU shrink a big page by a fraction would do.
   Only the Books loader thread calls page_decode (its state is static). */
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <setjmp.h>
#include <jpeglib.h>
#include <png.h>
#include "page.h"

#define MAX_SRC 16384
#define MAX_OUT_H 8192

/* ---- the scaler ----
   Widths in "units": source pixel i covers [i*tw,(i+1)*tw), output pixel x covers [x*sw,(x+1)*sw).
   Scaling only ever goes down (tw<=sw), so a source pixel touches one output pixel or two. */
static struct {
 int sw,sh,tw,th,ch;
 unsigned short *cx,*cw;      /* per source column: its first output column and the weight there */
 unsigned int *hrow,*acc0,*acc1;
 unsigned long long inv_h,inv_v;
 int y0,j;
 unsigned char *out;          /* tw*th*ch */
} R;

static void rs_free(void){free(R.cx);free(R.cw);free(R.hrow);free(R.acc0);free(R.acc1);free(R.out);memset(&R,0,sizeof(R));}
static int rs_init(int sw,int sh,int tw,int th,int ch) {
 rs_free();
 if(tw>sw)tw=sw;if(th>sh)th=sh;if(tw<1)tw=1;if(th<1)th=1;
 R.sw=sw;R.sh=sh;R.tw=tw;R.th=th;R.ch=ch;
 R.cx=malloc(sw*sizeof(unsigned short));R.cw=malloc(sw*sizeof(unsigned short));
 R.hrow=malloc(tw*ch*4);R.acc0=calloc(tw*ch,4);R.acc1=calloc(tw*ch,4);R.out=malloc(tw*th*ch);
 if(!R.cx || !R.cw || !R.hrow || !R.acc0 || !R.acc1 || !R.out){rs_free();return 0;}
 for(int i=0;i<sw;i++) {
  int a=i*tw,x0=a/sw,end=a+tw,edge=(x0+1)*sw;
  R.cx[i]=x0;R.cw[i]=end<=edge?tw:edge-a;
 }
 R.inv_h=(256ULL<<32)/sw;                 /* hrow -> 8.8 fixed point average across */
 R.inv_v=((1ULL<<32)+(unsigned)(sh*256)-1)/(unsigned)(sh*256); /* acc -> 0..255 */
 return 1;
}
static void rs_emit(void) {
 if(R.y0>=R.th)return;
 unsigned char *o=R.out+R.y0*R.tw*R.ch;int n=R.tw*R.ch;
 unsigned int half=R.sh*128;
 for(int k=0;k<n;k++){unsigned int v=(unsigned int)(((unsigned long long)(R.acc0[k]+half)*R.inv_v)>>32);o[k]=v>255?255:v;}
}
static void rs_row(const unsigned char *row) {
 int tw=R.tw,n=tw*R.ch;
 memset(R.hrow,0,n*4);
 if(R.ch==1) {
  for(int i=0;i<R.sw;i++){unsigned int v=row[i],x=R.cx[i],w=R.cw[i];R.hrow[x]+=v*w;if(w<(unsigned)tw)R.hrow[x+1]+=v*(tw-w);}
 }else {
  for(int i=0;i<R.sw;i++){const unsigned char *p=row+i*3;unsigned int x=R.cx[i]*3,w=R.cw[i],w1=tw-w;
   R.hrow[x]+=p[0]*w;R.hrow[x+1]+=p[1]*w;R.hrow[x+2]+=p[2]*w;
   if(w1){R.hrow[x+3]+=p[0]*w1;R.hrow[x+4]+=p[1]*w1;R.hrow[x+5]+=p[2]*w1;}}
 }
 for(int k=0;k<n;k++)R.hrow[k]=(unsigned int)(((unsigned long long)R.hrow[k]*R.inv_h)>>32);
 int a=R.j*R.th,y0=a/R.sh,end=a+R.th,edge=(y0+1)*R.sh,w0=end<=edge?R.th:edge-a,w1=R.th-w0;
 while(R.y0<y0){rs_emit();unsigned int *t=R.acc0;R.acc0=R.acc1;R.acc1=t;memset(R.acc1,0,n*4);R.y0++;}
 for(int k=0;k<n;k++)R.acc0[k]+=R.hrow[k]*w0;
 if(w1)for(int k=0;k<n;k++)R.acc1[k]+=R.hrow[k]*w1;
 R.j++;
}
static void rs_finish(void){rs_emit();}

static void fit_size(int sw,int sh,int tw,int fit_h,int *ow,int *oh) {
 float s=tw/(float)sw;
 if(fit_h>0 && sh*s>fit_h)s=fit_h/(float)sh;
 if(s>1)s=1;
 if(sh*s>MAX_OUT_H)s=MAX_OUT_H/(float)sh;
 *ow=(int)(sw*s+0.5f);*oh=(int)(sh*s+0.5f);
 if(*ow<1)*ow=1;if(*oh<1)*oh=1;if(*ow>sw)*ow=sw;if(*oh>sh)*oh=sh;
}

/* R.out -> a GPU-ready picture: 8-bit gray when the page has (almost) no color */
static int rs_store(PageImg *p) {
 int w=R.tw,h=R.th,stride=(w+15)&~15,gray=R.ch==1;
 if(!gray) {
  int colored=0,total=w*h;const unsigned char *s=R.out;
  for(int k=0;k<total;k++,s+=3){int rg=s[0]-s[1],bg=s[2]-s[1];if(rg>24 || rg<-24 || bg>24 || bg<-24)colored++;}
  gray=colored*200<total; /* under 0.5%: a black-and-white scan saved in color */
 }
 int bpp=gray?1:2,size=stride*h*bpp;
 unsigned char *px=memalign(16,size);if(!px)return 0;
 for(int y=0;y<h;y++) {
  const unsigned char *s=R.out+y*w*R.ch;
  if(gray) {
   unsigned char *d=px+y*stride;
   if(R.ch==1)memcpy(d,s,w);
   else for(int x=0;x<w;x++,s+=3)d[x]=(unsigned char)((s[0]*77+s[1]*150+s[2]*29+128)>>8);
   memset(d+w,255,stride-w);
  }else {
   unsigned short *d=(unsigned short *)px+y*stride;
   for(int x=0;x<w;x++,s+=3)d[x]=(s[0]>>3)|((s[1]>>2)<<5)|((s[2]>>3)<<11);
   for(int x=w;x<stride;x++)d[x]=0xffff;
  }
 }
 sceKernelDcacheWritebackRange(px,size);
 p->w=w;p->h=h;p->stride=stride;p->fmt=gray?PG_GRAY:PG_RGB565;p->px=px;
 return 1;
}

/* ---- JPEG ---- */
struct jfail {struct jpeg_error_mgr pub;jmp_buf jump;};
static void jfail_exit(j_common_ptr c){longjmp(((struct jfail *)c->err)->jump,1);}
static void jquiet(j_common_ptr c){(void)c;}
static struct jpeg_decompress_struct jci;static struct jfail jerr;static unsigned char *jrow;
static int dec_jpeg(const unsigned char *data,int len,int tw,int fit_h,PageImg *out,volatile int *abort) {
 jrow=NULL;
 jci.err=jpeg_std_error(&jerr.pub);jerr.pub.error_exit=jfail_exit;jerr.pub.output_message=jquiet;
 if(setjmp(jerr.jump)){jpeg_destroy_decompress(&jci);free(jrow);jrow=NULL;rs_free();return 0;}
 jpeg_create_decompress(&jci);jpeg_mem_src(&jci,(unsigned char *)data,len);
 if(jpeg_read_header(&jci,TRUE)!=JPEG_HEADER_OK){jpeg_destroy_decompress(&jci);return 0;}
 int sw=jci.image_width,sh=jci.image_height,ow,oh;
 if(sw<1 || sh<1 || sw>MAX_SRC || sh>MAX_SRC || (jci.num_components!=1 && jci.num_components!=3)){jpeg_destroy_decompress(&jci);return 0;}
 fit_size(sw,sh,tw,fit_h,&ow,&oh);
 int d=8;while(d>1 && ((sw+d-1)/d<ow || (sh+d-1)/d<oh))d/=2;
 jci.scale_num=1;jci.scale_denom=d;
 jci.out_color_space=jci.num_components==1?JCS_GRAYSCALE:JCS_RGB;
 jci.dct_method=JDCT_ISLOW;  /* the exact transform: the scaler averages afterwards anyway */
 jpeg_start_decompress(&jci);
 int ch=jci.output_components,dw=jci.output_width,dh=jci.output_height;
 jrow=malloc(dw*ch);
 if(!jrow || !rs_init(dw,dh,ow,oh,ch)){jpeg_destroy_decompress(&jci);free(jrow);jrow=NULL;rs_free();return 0;}
 int cut=0;
 while(jci.output_scanline<jci.output_height) {
  if(abort && *abort){cut=1;break;}
  JSAMPROW r=jrow;jpeg_read_scanlines(&jci,&r,1);rs_row(jrow);
 }
 if(cut)jpeg_abort_decompress(&jci);else jpeg_finish_decompress(&jci);
 jpeg_destroy_decompress(&jci);free(jrow);jrow=NULL;
 if(cut){rs_free();return -1;}
 rs_finish();
 int ok=rs_store(out);out->src_w=sw;out->src_h=sh;rs_free();return ok;
}

/* ---- PNG ---- */
typedef struct {const unsigned char *p;int len,pos;} PngMem;
static void png_mem(png_structp png,png_bytep o,png_size_t n){PngMem *m=png_get_io_ptr(png);if(m->pos+(int)n>m->len)png_error(png,"short");memcpy(o,m->p+m->pos,n);m->pos+=n;}
static void png_quiet(png_structp png,png_const_charp msg){(void)png;(void)msg;}
static void png_fail(png_structp png,png_const_charp msg){(void)msg;png_longjmp(png,1);}
static png_structp pp;static png_infop pi;static unsigned char *prow,*pfull;static png_bytep *prows;
static void png_cleanup(void){png_destroy_read_struct(&pp,&pi,NULL);pp=NULL;pi=NULL;free(prow);free(pfull);free(prows);prow=pfull=NULL;prows=NULL;}
static int dec_png(const unsigned char *data,int len,int tw,int fit_h,PageImg *out,volatile int *abort) {
 PngMem m={data,len,0};prow=pfull=NULL;prows=NULL;
 pp=png_create_read_struct(PNG_LIBPNG_VER_STRING,NULL,png_fail,png_quiet);if(!pp)return 0;
 pi=png_create_info_struct(pp);if(!pi){png_cleanup();return 0;}
 if(setjmp(png_jmpbuf(pp))){png_cleanup();rs_free();return 0;}
 png_set_read_fn(pp,&m,png_mem);png_read_info(pp,pi);
 int sw=png_get_image_width(pp,pi),sh=png_get_image_height(pp,pi),ow,oh;
 if(sw<1 || sh<1 || sw>MAX_SRC || sh>MAX_SRC){png_cleanup();return 0;}
 png_set_expand(pp);png_set_strip_16(pp);png_set_strip_alpha(pp);
 int passes=png_set_interlace_handling(pp);
 png_read_update_info(pp,pi);
 int ch=png_get_channels(pp,pi);if(ch!=1 && ch!=3){png_cleanup();return 0;}
 fit_size(sw,sh,tw,fit_h,&ow,&oh);
 if(!rs_init(sw,sh,ow,oh,ch)){png_cleanup();return 0;}
 int rb=png_get_rowbytes(pp,pi),cut=0;
 if(passes>1) { /* interlaced: the whole image first, then the scaler */
  if((long long)rb*sh>24*1024*1024){png_cleanup();rs_free();return 0;}
  pfull=malloc((size_t)rb*sh);prows=malloc(sh*sizeof(png_bytep));
  if(!pfull || !prows){png_cleanup();rs_free();return 0;}
  for(int y=0;y<sh;y++)prows[y]=pfull+(size_t)y*rb;
  png_read_image(pp,prows);
  for(int y=0;y<sh;y++){if(abort && *abort){cut=1;break;}rs_row(prows[y]);}
 }else {
  prow=malloc(rb);if(!prow){png_cleanup();rs_free();return 0;}
  for(int y=0;y<sh;y++){if(abort && *abort){cut=1;break;}png_read_row(pp,prow,NULL);rs_row(prow);}
 }
 png_cleanup();
 if(cut){rs_free();return -1;}
 rs_finish();
 int ok=rs_store(out);out->src_w=sw;out->src_h=sh;rs_free();return ok;
}

int page_decode(const unsigned char *data,int len,int target_w,int fit_h,PageImg *out,volatile int *abort) {
 memset(out,0,sizeof(*out));
 if(!data || len<8 || target_w<1)return 0;
 if(data[0]==0xFF && data[1]==0xD8)return dec_jpeg(data,len,target_w,fit_h,out,abort);
 if(!png_sig_cmp((png_const_bytep)data,0,8))return dec_png(data,len,target_w,fit_h,out,abort);
 return 0;
}
void page_free(PageImg *p){free(p->px);memset(p,0,sizeof(*p));}
