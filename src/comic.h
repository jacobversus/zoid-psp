/* Books: a comic or manga volume is a .cbz or .zip file (a zip of images) or a folder of images.
   comic.c lists its pages in reading order and reads one page's file into memory;
   page.c turns those bytes into a picture at the size the screen needs. */
#ifndef ZOID_COMIC_H
#define ZOID_COMIC_H

typedef struct {
 unsigned int off;   /* zip: where the entry's local header starts */
 unsigned int csize; /* zip: stored size */
 unsigned int size;  /* the image file's own size */
 unsigned int method; /* zip: 0 stored, 8 deflated */
 unsigned int name;   /* offset into Book.names */
} BookPage;

typedef struct {
 char path[512];
 int is_zip;
 int fd;            /* the open .cbz (zip only), or -1 */
 int n;             /* pages, in reading order */
 BookPage *pages;
 char *names;       /* zip: entry names; folder: file names */
 int names_len;
} Book;

/* Opens a .cbz or a folder of images. 0 when it holds no pages it can show. */
int book_open(Book *b,const char *path);
void book_close(Book *b);
/* After the PSP wakes from sleep, open files no longer read: this opens the .cbz again. */
int book_reopen(Book *b);
/* The page's whole image file, malloc'd (the caller frees it). NULL on any error,
   including a damaged zip entry or one bigger than max_bytes. */
unsigned char *book_read(Book *b,int page,int *len,int max_bytes);
const char *book_page_name(const Book *b,int page);
/* 1 for .jpg/.jpeg/.png names (case does not matter) */
int book_image_name(const char *name);
/* 1 for .cbz and .zip names: a whole book in one file */
int book_archive_name(const char *name);
/* "file 2" before "file 10"; case does not matter */
int book_natural_compare(const char *a,const char *b);

/* Names beyond plain ASCII ("Ranma ½", "❤"): a real PSP can't always open a file or folder by such a
   long name, but every name on the stick also has a short 8.3 one ("RANMA~1") that always opens.
   book_dread reads the next entry of folder `dir` (open as d): name = what to open it by (its long
   name when that opens, else its short one), show = its long name for the screen, with a letter the
   PSP gave as '?' turned into a square. Returns what sceIoDread does (>0 while there are entries). */
#include <pspiofilemgr.h>
int book_dread(int d,const char *dir,SceIoDirent *e,char *name,int ncap,char *show,int scap);
/* After book_dread: the entry's short 8.3 name ("" when the PSP gave none), whose extension can be
   read even when the long name's can't, and whether its long name has letters beyond plain ASCII */
extern char book_short_name[16];
extern int book_odd_name;
/* 1 when dir/name opens (a folder with sceIoDopen) or is found (a file, with sceIoGetstat) */
int book_entry_opens(const char *dir,const char *name,int isdir);
/* 1 when the entry book_dread just read is a .cbz/.zip book, by its long name or its short one */
int book_entry_archive(const char *show);
/* A folder's zoid-title.txt (first line, UTF-8): the name to show for a folder whose own name had to be
   made plain for the PSP ("Ranma 1-2" shows as "Ranma 1/2"; see fix-names.ps1). 0 when it has none. */
int book_folder_title(const char *dir,char *out,int cap);
/* A path as it should read on screen: folders by their zoid-title.txt, short names as their long ones.
   A '/' inside a title comes back as BOOK_SLASH, so the path still splits at its folders; turn it back
   into '/' (book_unslash) only when the words are drawn. */
#define BOOK_SLASH '\x01'
void book_show_path(const char *path,char *out,int cap);
static inline void book_unslash(char *s){for(;*s;s++)if(*s==BOOK_SLASH)*s='/';}
/* When set (a file path), odd names and how each was opened are written there, for checking on a PSP */
extern char book_names_log[512];

#endif
