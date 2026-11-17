/* zoidvol.prx: a tiny kernel module that lets ZOID read the system volume, which Sony keeps to
   kernel mode (sceImposeGetParam), read the system buttons (the music-note button switches themes),
   and undo the mute that button also makes. Custom firmware (PRO/ME/ARK) lets a user app load it.
   The impose functions are found at run time through SystemControl, trying the NIDs of 6.6x, 5.xx
   and 1.50, so an unknown firmware just reports "unknown" instead of failing to load. sceImposeSetParam
   is only looked up under the NID that pairs with the GetParam NID found (none known for 5.xx). */
#include <pspsdk.h>
#include <pspkernel.h>
#include <pspctrl.h>

PSP_MODULE_INFO("zoidvol",0x1006,1,0);
PSP_NO_CREATE_MAIN_THREAD();

unsigned int sctrlHENFindFunction(const char *module,const char *library,unsigned int nid);
static int (*get_param)(int);
static int (*set_param)(int,int);

/* volume 0-30 in the low byte, 0x100 when muted; -1 when it can't be read */
int zoidvol_get(void) {
 int k1=pspSdkSetK1(0),v=-1;
 if(get_param){int vol=get_param(0x1),mute=get_param(0x8);if(vol>=0 && vol<=30)v=vol|(mute>0?0x100:0);}
 pspSdkSetK1(k1);
 return v;
}

/* every button, the system ones too (the note button, Home, the volume keys, Screen), which user
   mode never sees: read here in kernel mode. 0 when the pad can't be read. */
unsigned int zoidvol_buttons(void) {
 int k1=pspSdkSetK1(0);SceCtrlData pad;unsigned int b=0;
 if(sceCtrlPeekBufferPositive(&pad,1)>0)b=pad.Buttons;
 pspSdkSetK1(k1);
 return b;
}

/* turns the system mute off (the note button turns it on as well as switching ZOID's theme).
   0 done, 1 it wasn't muted, -1 it can't be done on this firmware */
int zoidvol_unmute(void) {
 int k1=pspSdkSetK1(0),r=-1;
 if(get_param && set_param){if(get_param(0x8)>0)r=set_param(0x8,0)<0?-1:0;else r=1;}
 pspSdkSetK1(k1);
 return r;
}

int module_start(SceSize args,void *argp) {
 (void)args;(void)argp;
 /* GetParam and SetParam NIDs, pairwise: 6.6x, 5.xx (SetParam unknown), 1.50 */
 static const unsigned int get_nids[]={0xDC3BECFF,0x4B02F047,0x531C9778},set_nids[]={0x3C318569,0,0x810FB7FB};
 for(int i=0;i<3 && !get_param;i++) {
  unsigned int f=sctrlHENFindFunction("sceImpose_Driver","sceImpose_driver",get_nids[i]);
  if(f>=0x88000000 && f<0x8A000000) {
   get_param=(int (*)(int))f;
   if(set_nids[i]){unsigned int s=sctrlHENFindFunction("sceImpose_Driver","sceImpose_driver",set_nids[i]);if(s>=0x88000000 && s<0x8A000000)set_param=(int (*)(int,int))s;}
  }
 }
 return 0;
}
int module_stop(SceSize args,void *argp){(void)args;(void)argp;return 0;}
