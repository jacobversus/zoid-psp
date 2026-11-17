/* ---- volume bar ----
   The system volume is kernel-only, so zoidvol.prx (a tiny kernel module beside the EBOOT, loaded
   at start on custom firmware) reads it. When the level or mute changes, a bar shows for 1.5 s on
   every screen, videos included. Without the module (stock firmware, emulator) there is no bar. */
int zoidvol_get(void); /* zoidvol/zoidvol.S: volume 0-30, 0x100 when muted, -1 unknown */
unsigned int zoidvol_buttons(void); /* every button, the system ones too (the note button) */
int zoidvol_unmute(void);          /* the system mute off: 0 done, 1 not muted, -1 not on this firmware */
static int vol_ok,vol_loaded,vol_last=-1;static unsigned long long vol_until,vol_polled;
static void volume_init(const char *prx) {
 SceUID m=pspSdkLoadStartModule(prx,PSP_MEMORY_PARTITION_KERNEL);
 checkpoint("volume module",m);
 vol_loaded=m>=0;
 if(m>=0){int v=zoidvol_get();vol_ok=v>=0;vol_last=v;checkpoint("volume reading",v);}
}
/* 1 when the bar just appeared or changed (screens that draw only on change redraw then) */
static int volume_poll(void) {
 if(!vol_ok || now_ms-vol_polled<100)return 0;
 vol_polled=now_ms;int v=zoidvol_get();
 if(v<0 || v==vol_last)return 0;
 vol_last=v;vol_until=now_ms+1500;return 1;
}
static int vol_test; /* tests show the bar without the module (emulators can't load kernel modules) */
static int volume_showing(void){return (vol_ok || vol_test) && now_ms<vol_until;}
static void draw_volume(void) {
 if(!volume_showing())return;
 int level=vol_last&0xff,muted=(vol_last&0x100)!=0;
 float w=232,h=30,x=240-w/2,y=10;
 rect(x,y,w,h,C(0x101010,230));frame_rect(x,y,w,h,1,C(0xffffff,40)); /* square corners throughout */
 icon(IC_NOTE,x+18,y+h/2,16,C(muted?0x888888:T->accent,255));
 text_vc(x+32,y+h/2,muted?"Muted":"Volume",C(0xffffff,220),F_SMALL);
 /* 15 blocks, one per two steps of the PSP's 0-30 scale */
 float bx=x+86,bw=6,gap=2; /* whole pixels: crisp, even edges */
 for(int i=0;i<15;i++){int on=!muted && level>=(i+1)*2-1;rect(bx+i*(bw+gap),y+8,bw,h-16,on?C(T->accent,255):C(0xffffff,45));}
 char n[8];snprintf(n,sizeof(n),"%d",muted?0:level);text_right(x+w-10,y+7,n,C(0xffffff,220),F_SMALL);
}
