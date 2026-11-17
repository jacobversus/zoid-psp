	.set noreorder

#include "pspstub.s"

	STUB_START "zoidvol",0x40090000,0x00030005
	STUB_FUNC  0xCA854673,zoidvol_get
	STUB_FUNC  0x7B318D0C,zoidvol_buttons
	STUB_FUNC  0xEB57AD61,zoidvol_unmute
	STUB_END
