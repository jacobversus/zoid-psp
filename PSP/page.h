/* Books: one page turned into a picture at the size it is shown (see page.c). */
#ifndef ZOID_PAGE_H
#define ZOID_PAGE_H

enum {PG_GRAY=1,PG_RGB565=2};
typedef struct {
 int w,h;          /* the picture */
 int stride;       /* pixels per row in memory (a multiple of 16) */
 int fmt;          /* PG_GRAY: 1 byte a pixel; PG_RGB565: 2 bytes, the GPU's 5650 order */
 int src_w,src_h;  /* the image file's own size */
 unsigned char *px;/* 16-byte aligned, written back from the CPU cache, ready for the GPU */
} PageImg;

/* Decodes a JPEG or PNG and scales it down to target_w wide (never up), keeping its shape.
   With fit_h > 0 it fits inside target_w x fit_h instead. Black-and-white pages, even ones
   saved in color, come out as PG_GRAY. Scaling averages every source pixel into the output,
   which keeps screentones from turning into moire.
   Returns 1, 0 on failure, or -1 when *abort turned nonzero part way. */
int page_decode(const unsigned char *data,int len,int target_w,int fit_h,PageImg *out,volatile int *abort);
void page_free(PageImg *p);

#endif
