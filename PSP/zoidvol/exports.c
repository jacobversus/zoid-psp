#include <pspmoduleexport.h>
#define NULL ((void *) 0)

extern int module_start;
extern int module_stop;
extern int module_info;
static const unsigned int __syslib_exports[6] __attribute__((section(".rodata.sceResident"))) = {
	0xD632ACDB,
	0xCEE8593C,
	0xF01D73A7,
	(unsigned int) &module_start,
	(unsigned int) &module_stop,
	(unsigned int) &module_info,
};

extern int zoidvol_get;
extern int zoidvol_buttons;
extern int zoidvol_unmute;
static const unsigned int __zoidvol_exports[6] __attribute__((section(".rodata.sceResident"))) = {
	0xCA854673,
	0x7B318D0C,
	0xEB57AD61,
	(unsigned int) &zoidvol_get,
	(unsigned int) &zoidvol_buttons,
	(unsigned int) &zoidvol_unmute,
};

const struct _PspLibraryEntry __library_exports[2] __attribute__((section(".lib.ent"), used)) = {
	{ NULL, 0x0000, 0x8000, 4, 1, 2, (unsigned int *) &__syslib_exports },
	{ "zoidvol", 0x0000, 0x4001, 4, 0, 3, (unsigned int *) &__zoidvol_exports },
};
