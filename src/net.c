/* Wi-Fi, DNS, TCP, HTTPS and HTTP for internet radio and online lyrics.
   Wi-Fi: the PSP's own saved networks (Settings > Network Settings, WPA2 works on 6.xx/ARK) are
   joined with sceNetApctl. Sockets are non-blocking, so a dead host times out instead of hanging.
   HTTPS uses BearSSL (third_party/bearssl, MIT), because the PSP's own SSL predates the
   elliptic-curve ciphers sites like lrclib.net require; certificates are checked against
   trust_anchors.h. The date for that check comes from the PSP's clock, or better, from the Date
   header of any web reply (a PSP whose clock was never set would reject every certificate). */
#include <pspkernel.h>
#include <psputility.h>
#include <psputility_netparam.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspnet_apctl.h>
#include <pspnet_resolver.h>
#include <pspwlan.h>
#include <psprtc.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "third_party/bearssl/inc/bearssl.h"
#include "trust_anchors.h"
#include "net.h"

volatile int net_status,net_tls_error;
char net_connected_name[64];
static SceUID net_lock=-1;
static int net_ready;
static volatile unsigned long long net_last_use;
/* Failures in a row with no byte received in between (lookups, connects, reads that timed out).
   Any byte from any connection clears it, so a playing stream keeps it at zero; at NET_FAILS_RESET
   the link is treated as dead and net_up() leaves and rejoins the Wi-Fi network. */
#define NET_FAILS_RESET 3
static volatile int net_fails;
void radio_log(const char *fmt,...);
static void dns_clear(void);

const char *net_error_text(int code) {
 switch(code) {
 case NET_E_SWITCH:return "Wi-Fi is switched off (flip the WLAN switch)";
 case NET_E_NOCONFIG:return "No saved Wi-Fi network (Settings > Network Settings)";
 case NET_E_JOIN:return "Could not join the Wi-Fi network";
 case NET_E_MODULE:return "The PSP's network modules did not load";
 case NET_E_DNS:return "Could not look up the server name";
 case NET_E_CONNECT:return "The server did not answer";
 case NET_E_TLS:return "The secure connection failed";
 case NET_E_HTTP:return "The server refused the request";
 case NET_E_TIMEOUT:return "The connection timed out";
 case NET_E_CLOSED:return "The connection closed";
 case NET_E_URL:return "That address is not a web address";
 case NET_E_MEMORY:return "Out of memory";
 case NET_E_TOOBIG:return "The reply was too large";
 case NET_E_STOPPED:return "Stopped";
 }
 return "Network error";
}
void net_setup(void){if(net_lock<0)net_lock=sceKernelCreateSema("zoid_wifi",0,1,1,NULL);}
static void lk(void){net_setup();sceKernelWaitSema(net_lock,1,NULL);}
static void ulk(void){sceKernelSignalSema(net_lock,1);}
void net_touch(void){net_last_use=sceKernelGetSystemTimeWide();}
unsigned long long net_idle_us(void){return net_last_use?sceKernelGetSystemTimeWide()-net_last_use:~0ULL;}

/* ---- Wi-Fi ---- */
/* which start-up call failed (for logs): 1 common module, 2 inet module, 3 sceNetInit, 4 inet, 5 resolver, 6 apctl */
volatile int net_fail_step;volatile unsigned int net_fail_code;volatile int net_free_kb[3]; /* largest free block before loading, after the modules, after sceNetInit */
static int net_step(int step,int r){if(r<0 && (unsigned int)r!=0x80111102){net_fail_step=step;net_fail_code=(unsigned int)r;return 0;}return 1;}
static int net_modules(void) {
 if(net_ready)return 1;
 net_free_kb[0]=sceKernelMaxFreeMemSize()/1024;
 if(!net_step(1,sceUtilityLoadModule(PSP_MODULE_NET_COMMON)) || !net_step(2,sceUtilityLoadModule(PSP_MODULE_NET_INET)))return NET_E_MODULE;
 net_free_kb[1]=sceKernelMaxFreeMemSize()/1024;
 if(!net_step(3,sceNetInit(128*1024,42,4*1024,42,4*1024)) || !net_step(4,sceNetInetInit()) || !net_step(5,sceNetResolverInit()) || !net_step(6,sceNetApctlInit(0x2000,48)))return NET_E_MODULE;
 net_ready=1;return 1;
}
int net_config_name(int config,char *out,int cap) {
 out[0]=0;if(config<1 || sceUtilityCheckNetParam(config)!=0)return 0;
 netData d;memset(&d,0,sizeof(d));
 if(sceUtilityGetNetParam(config,PSP_NETPARAM_NAME,&d)==0)snprintf(out,cap,"%s",d.asString);
 if(!out[0])snprintf(out,cap,"Connection %d",config);
 return 1;
}
int net_config_count(void){int n=0;for(int i=1;i<=10;i++)if(sceUtilityCheckNetParam(i)==0)n=i;return n;}
static int apctl_state(void){int s=0;if(sceNetApctlGetState(&s)<0)return 0;return s;}
/* Joins one saved network; up to 30 s (WPA2 key exchange and DHCP can be slow). */
static int join(int config) {
 if(sceNetApctlConnect(config)<0)return 0;
 int seen=0;unsigned long long t0=sceKernelGetSystemTimeWide();
 while(sceKernelGetSystemTimeWide()-t0<30000000ULL) {
  int s=apctl_state();
  if(s==PSP_NET_APCTL_STATE_GOT_IP)return 1;
  if(s)seen=1;else if(seen || sceKernelGetSystemTimeWide()-t0>4000000ULL)break; /* dropped back, or never started */
  sceKernelDelayThread(50000);
 }
 sceNetApctlDisconnect();sceKernelDelayThread(200000);return 0;
}
int net_up(int config) {
 lk();
 int r=net_modules();if(r<0){ulk();return r;}
 if(apctl_state()==PSP_NET_APCTL_STATE_GOT_IP) {
  if(net_fails<NET_FAILS_RESET){net_status=2;net_touch();ulk();return 1;}
  /* connected on paper, but nothing has come back for a while: leave and join again */
  radio_log("Wi-Fi: %d failures in a row with nothing received, joining again",net_fails);
  sceNetApctlDisconnect();
  unsigned long long t0=sceKernelGetSystemTimeWide();
  while(apctl_state()!=PSP_NET_APCTL_STATE_DISCONNECTED && sceKernelGetSystemTimeWide()-t0<3000000ULL)sceKernelDelayThread(50000);
  dns_clear();
 }
 net_fails=0;
 if(!sceWlanGetSwitchState()){net_status=0;ulk();return NET_E_SWITCH;}
 net_status=1;
 int n=net_config_count(),first=config>0?config:1,last=config>0?config:n;
 if(n<1)first=last=1; /* the emulator may report none; try the first slot anyway */
 int ok=0;
 for(int c=first;c<=last && !ok;c++) {
  char name[64];if(!net_config_name(c,name,sizeof(name)) && n>0)continue;
  if(join(c)){ok=1;snprintf(net_connected_name,sizeof(net_connected_name),"%s",name[0]?name:"Wi-Fi");}
 }
 net_status=ok?2:0;if(ok)net_touch();
 ulk();
 if(!ok)return n<1 && config>0?NET_E_NOCONFIG:NET_E_JOIN;
 return 1;
}
void net_down(void) {
 lk();if(net_ready && apctl_state()!=PSP_NET_APCTL_STATE_DISCONNECTED)sceNetApctlDisconnect();net_status=0;ulk();
}

/* ---- the date (for certificate checks) ---- */
static int date_days=-1,date_secs;static unsigned long long date_at;
/* days since 1970-01-01 (Howard Hinnant's days_from_civil) */
static int days_from_civil(int y,int m,int d) {
 y-=m<=2;int era=(y>=0?y:y-399)/400,yoe=y-era*400,doy=(153*(m>2?m-3:m+9)+2)/5+d-1,doe=yoe*365+yoe/4-yoe/100+doy;
 return era*146097+doe-719468;
}
/* BearSSL counts days from 1 January of year 0 */
static int bearssl_days(int y,int m,int d){return days_from_civil(y,m,d)-days_from_civil(0,1,1);}
static void learn_date(const char *v) { /* "Wed, 11 Nov 2026 17:22:16 GMT" */
 static const char *months="JanFebMarAprMayJunJulAugSepOctNovDec";
 int d,y,hh,mm,ss;char mon[4];
 const char *p=strchr(v,',');if(!p)return;
 if(sscanf(p+1," %d %3s %d %d:%d:%d",&d,mon,&y,&hh,&mm,&ss)!=6)return;
 const char *f=strstr(months,mon);if(!f || y<2020 || y>2200)return;
 date_days=bearssl_days(y,(int)(f-months)/3+1,d);date_secs=hh*3600+mm*60+ss;date_at=sceKernelGetSystemTimeWide();
}
static void now_for_certs(unsigned int *days,unsigned int *secs) {
 if(date_days>=0) {
  unsigned long long s=(unsigned long long)date_secs+(sceKernelGetSystemTimeWide()-date_at)/1000000ULL;
  *days=date_days+(unsigned int)(s/86400);*secs=(unsigned int)(s%86400);return;
 }
 u64 tick=0;sceRtcGetCurrentTick(&tick); /* microseconds since 0001-01-01, UTC */
 unsigned long long s=tick/1000000ULL;*days=(unsigned int)(s/86400)+366;*secs=(unsigned int)(s%86400);
}

/* ---- sockets ---- */
struct NetConn {
 int sock,tls,low_err;volatile int cancel; /* low_err: why the socket under TLS last failed */
 br_ssl_client_context sc;br_x509_minimal_context xc;br_sslio_context io;
 unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
};
static int is_again(int e){return e==11 || e==35 || e==0x0B;}
static int is_progress(int e){return e==119 || e==36 || e==120 || e==37 || is_again(e);}
static int is_connected(int e){return e==127 || e==56;}
static int tcp_read(NetConn *c,void *buf,int len,int timeout_ms) {
 unsigned long long t0=sceKernelGetSystemTimeWide();
 for(;;) {
  if(c->cancel)return NET_E_STOPPED;
  int r=sceNetInetRecv(c->sock,buf,len,0);
  if(r>0){net_touch();net_fails=0;return r;}
  if(r==0)return 0;
  int e=sceNetInetGetErrno();if(!is_again(e))return NET_E_CLOSED;
  if(sceKernelGetSystemTimeWide()-t0>(unsigned long long)timeout_ms*1000ULL){net_fails++;return NET_E_TIMEOUT;}
  sceKernelDelayThread(4000);
 }
}
static int tcp_write(NetConn *c,const void *buf,int len) {
 const unsigned char *p=buf;int done=0;unsigned long long t0=sceKernelGetSystemTimeWide();
 while(done<len) {
  if(c->cancel)return NET_E_STOPPED;
  int r=sceNetInetSend(c->sock,p+done,len-done,0);
  if(r>0){done+=r;t0=sceKernelGetSystemTimeWide();continue;}
  int e=sceNetInetGetErrno();if(r<0 && !is_again(e))return NET_E_CLOSED;
  if(sceKernelGetSystemTimeWide()-t0>15000000ULL)return NET_E_TIMEOUT;
  sceKernelDelayThread(4000);
 }
 return len;
}
/* A small cache, so a station that reconnects or a run of lyrics lookups skip the DNS step. */
static struct {char host[96];unsigned int ip;} dns_cache[8];static int dns_next;
static void dns_clear(void){memset(dns_cache,0,sizeof(dns_cache));}
static int parse_ip(const char *s,unsigned int *ip) {
 unsigned int a,b,c,d;char tail;if(sscanf(s,"%u.%u.%u.%u%c",&a,&b,&c,&d,&tail)!=4 || a>255 || b>255 || c>255 || d>255)return 0;
 *ip=a|(b<<8)|(c<<16)|(d<<24);return 1; /* network order on a little-endian CPU */
}
/* One DNS question (an A record) to one server over UDP, waiting at most ms. ZOID asks itself
   rather than through sceNetResolver, which can wait far past its own timeout once the link
   goes bad, and which every other network task then had to wait behind. */
static int dns_ask(unsigned int server,const char *host,unsigned int *ip,int ms) {
 unsigned char q[300];int n=12;unsigned short id=(unsigned short)(sceKernelGetSystemTimeLow()^(server>>7));
 memset(q,0,12);q[0]=id>>8;q[1]=id&255;q[2]=1;q[5]=1; /* recursion desired, one question */
 for(const char *p=host;*p;) {
  const char *dot=strchr(p,'.');int l=dot?(int)(dot-p):(int)strlen(p);
  if(l<1 || l>63 || n+l+6>(int)sizeof(q))return 0;
  q[n++]=l;memcpy(q+n,p,l);n+=l;p+=l;if(*p)p++;
 }
 q[n++]=0;q[n++]=0;q[n++]=1;q[n++]=0;q[n++]=1; /* type A, class IN */
 int s=sceNetInetSocket(AF_INET,SOCK_DGRAM,0);if(s<0)return 0;
 int one=1;sceNetInetSetsockopt(s,SOL_SOCKET,SO_NONBLOCK,&one,sizeof(one));
 struct sockaddr_in sa;memset(&sa,0,sizeof(sa));sa.sin_len=sizeof(sa);sa.sin_family=AF_INET;sa.sin_port=53<<8;sa.sin_addr.s_addr=server;
 int ok=0;unsigned long long t0=sceKernelGetSystemTimeWide();
 if((int)sceNetInetSendto(s,q,n,0,(struct sockaddr *)&sa,sizeof(sa))==n)
 while(!ok && sceKernelGetSystemTimeWide()-t0<(unsigned long long)ms*1000ULL) {
  unsigned char a[512];struct sockaddr_in from;socklen_t fl=sizeof(from);
  int r=(int)sceNetInetRecvfrom(s,a,sizeof(a),0,(struct sockaddr *)&from,&fl);
  if(r<=0){sceKernelDelayThread(10000);continue;}
  if(r<12 || a[0]!=(id>>8) || a[1]!=(id&255) || !(a[2]&0x80))continue; /* not our answer */
  if(a[3]&15)break; /* the server said no (no such name, refused) */
  int an=(a[6]<<8)|a[7],p=12;
  while(p<r && a[p]){if((a[p]&0xC0)==0xC0){p++;break;}p+=a[p]+1;}p+=5; /* past the question */
  for(int i=0;i<an && p+10<=r;i++) {
   while(p<r){if((a[p]&0xC0)==0xC0){p+=2;break;}if(!a[p]){p++;break;}p+=a[p]+1;}
   if(p+10>r)break;
   int type=(a[p]<<8)|a[p+1],len=(a[p+8]<<8)|a[p+9];p+=10;
   if(type==1 && len==4 && p+4<=r){memcpy(ip,a+p,4);ok=*ip!=0;break;} /* a CNAME is skipped: its A record follows */
   p+=len;
  }
  if(!ok)break;
 }
 sceNetInetClose(s);
 return ok;
}
static unsigned int dns_server(int which) {
 union SceNetApctlInfo info;memset(&info,0,sizeof(info));unsigned int ip=0;
 if(sceNetApctlGetInfo(which?PSP_NET_APCTL_INFO_SECDNS:PSP_NET_APCTL_INFO_PRIMDNS,&info)<0)return 0;
 info.primaryDns[15]=0;return parse_ip(info.primaryDns,&ip) && ip?ip:0;
}
static SceUID dns_lock=-1;
static int resolve(const char *host,unsigned int *ip) {
 if(parse_ip(host,ip))return 1;
 for(int i=0;i<8;i++)if(dns_cache[i].host[0] && !strcasecmp(dns_cache[i].host,host)){*ip=dns_cache[i].ip;return 1;}
 /* about 10 s at most: the first server, the second, then each once more */
 unsigned int sv[2]={dns_server(0),dns_server(1)};if(sv[1]==sv[0])sv[1]=0;
 int ok=0;
 for(int round=0;round<2 && !ok;round++)for(int k=0;k<2 && !ok;k++)if(sv[k])ok=dns_ask(sv[k],host,ip,round?3000:2000);
 if(!ok && !sv[0] && !sv[1]) { /* no server known (the emulator): the system resolver, under its own lock */
  if(dns_lock<0)dns_lock=sceKernelCreateSema("zoid_dns",0,1,1,NULL);
  int rid;static char rbuf[1024];struct in_addr a;memset(&a,0,sizeof(a));
  sceKernelWaitSema(dns_lock,1,NULL);
  ok=sceNetResolverCreate(&rid,rbuf,sizeof(rbuf))>=0;
  if(ok){ok=sceNetResolverStartNtoA(rid,host,&a,2,2)>=0 && a.s_addr;sceNetResolverDelete(rid);}
  sceKernelSignalSema(dns_lock,1);
  if(ok)*ip=a.s_addr;
 }
 if(!ok){net_fails++;radio_log("  lookup of %s failed (servers %s, %s), failure %d in a row",host,sv[0]?"1":"none",sv[1]?"2":"none",net_fails);return 0;}
 snprintf(dns_cache[dns_next].host,sizeof(dns_cache[0].host),"%s",host);dns_cache[dns_next].ip=*ip;dns_next=(dns_next+1)&7;
 return 1;
}
static void dns_forget(const char *host){for(int i=0;i<8;i++)if(!strcasecmp(dns_cache[i].host,host))dns_cache[i].host[0]=0;}

static int tls_low_read(void *ctx,unsigned char *buf,size_t len){int r=tcp_read(ctx,buf,(int)len,15000);if(r<=0)((NetConn *)ctx)->low_err=r<0?r:NET_E_CLOSED;return r>0?r:-1;}
static int tls_low_write(void *ctx,const unsigned char *buf,size_t len){int r=tcp_write(ctx,buf,(int)len);return r>0?r:-1;}
/* BearSSL needs fresh randomness for each connection: timer jitter, the clock and the Wi-Fi MAC, hashed. */
static void tls_seed(NetConn *c) {
 br_sha256_context h;br_sha256_init(&h);
 static unsigned int counter;counter++;br_sha256_update(&h,&counter,sizeof(counter));
 u64 tick=0;sceRtcGetCurrentTick(&tick);br_sha256_update(&h,&tick,sizeof(tick));
 unsigned char mac[8]={0};sceWlanGetEtherAddr(mac);br_sha256_update(&h,mac,sizeof(mac));
 for(int i=0;i<48;i++){unsigned long long t=sceKernelGetSystemTimeWide();br_sha256_update(&h,&t,sizeof(t));if(i%8==0)sceKernelDelayThread(1+(int)(t&3));}
 void *self=c;br_sha256_update(&h,&self,sizeof(self));
 unsigned char seed[32];br_sha256_out(&h,seed);br_ssl_engine_inject_entropy(&c->sc.eng,seed,sizeof(seed));
}
NetConn *net_open(const char *host,int port,int tls,int *error) {
 *error=0;
 unsigned int ip;if(!resolve(host,&ip)){*error=NET_E_DNS;return NULL;}
 NetConn *c=calloc(1,tls?sizeof(NetConn):sizeof(NetConn)-sizeof(((NetConn *)0)->iobuf));
 if(!c){*error=NET_E_MEMORY;return NULL;}
 c->tls=tls;
 c->sock=sceNetInetSocket(AF_INET,SOCK_STREAM,0);if(c->sock<0){free(c);*error=NET_E_CONNECT;return NULL;}
 int one=1;sceNetInetSetsockopt(c->sock,SOL_SOCKET,SO_NONBLOCK,&one,sizeof(one));
 struct sockaddr_in sa;memset(&sa,0,sizeof(sa));sa.sin_len=sizeof(sa);sa.sin_family=AF_INET;sa.sin_port=(unsigned short)(((port&0xff)<<8)|((port>>8)&0xff));sa.sin_addr.s_addr=ip;
 int r=sceNetInetConnect(c->sock,(struct sockaddr *)&sa,sizeof(sa)),ok=r>=0;
 if(!ok && is_progress(sceNetInetGetErrno())) {
  unsigned long long t0=sceKernelGetSystemTimeWide();
  while(!ok && sceKernelGetSystemTimeWide()-t0<10000000ULL) {
   fd_set w;FD_ZERO(&w);FD_SET(c->sock,&w);struct SceNetInetTimeval tv={0,100000};
   int n=sceNetInetSelect(c->sock+1,NULL,&w,NULL,&tv);
   if(n>0){int err=0;socklen_t el=sizeof(err);sceNetInetGetsockopt(c->sock,SOL_SOCKET,SO_ERROR,&err,&el);if(err)break;ok=1;}
   else if(n<0) { /* no select: ask connect again */
    sceKernelDelayThread(50000);r=sceNetInetConnect(c->sock,(struct sockaddr *)&sa,sizeof(sa));
    int e=sceNetInetGetErrno();if(r>=0 || is_connected(e))ok=1;else if(!is_progress(e))break;
   }
  }
 }
 if(!ok){sceNetInetClose(c->sock);free(c);dns_forget(host);net_fails++;*error=NET_E_CONNECT;return NULL;}
 net_touch();
 if(tls) {
  br_ssl_client_init_full(&c->sc,&c->xc,TAs,TAs_NUM);
  unsigned int days,secs;now_for_certs(&days,&secs);br_x509_minimal_set_time(&c->xc,days,secs);
  br_ssl_engine_set_buffer(&c->sc.eng,c->iobuf,sizeof(c->iobuf),1);
  tls_seed(c);
  if(!br_ssl_client_reset(&c->sc,host,0)){net_tls_error=br_ssl_engine_last_error(&c->sc.eng);net_close(c);*error=NET_E_TLS;return NULL;}
  br_sslio_init(&c->io,&c->sc.eng,tls_low_read,c,tls_low_write,c);
 }
 return c;
}
int net_write(NetConn *c,const void *data,int len) {
 if(!c->tls)return tcp_write(c,data,len);
 if(br_sslio_write_all(&c->io,data,len)<0 || br_sslio_flush(&c->io)<0){net_tls_error=br_ssl_engine_last_error(&c->sc.eng);return c->cancel?NET_E_STOPPED:NET_E_TLS;}
 return len;
}
int net_read(NetConn *c,void *data,int len,int timeout_ms) {
 if(!c->tls)return tcp_read(c,data,len,timeout_ms);
 int r=br_sslio_read(&c->io,data,len);
 if(r>0)return r;
 int e=br_ssl_engine_last_error(&c->sc.eng);
 if(e==BR_ERR_OK && (br_ssl_engine_current_state(&c->sc.eng)&BR_SSL_CLOSED))return 0; /* close_notify */
 net_tls_error=e;
 if(e==BR_ERR_IO)return c->cancel?NET_E_STOPPED:c->low_err<0?c->low_err:NET_E_CLOSED;
 return NET_E_TLS;
}
void net_cancel(NetConn *c){if(c)c->cancel=1;}
void net_close(NetConn *c) {
 if(!c)return;
 if(c->sock>=0)sceNetInetClose(c->sock);
 free(c);
}

/* ---- HTTP ---- */
int net_parse_url(const char *url,int *tls,char *host,int hcap,int *port,char *path,int pcap) {
 const char *p;
 if(!strncasecmp(url,"http://",7)){*tls=0;*port=80;p=url+7;}
 else if(!strncasecmp(url,"https://",8)){*tls=1;*port=443;p=url+8;}
 else return 0;
 const char *end=p;while(*end && *end!='/' && *end!='?' && *end!='#')end++;
 const char *at=p;for(const char *q=p;q<end;q++)if(*q=='@')at=q+1; /* user:pass@ is not supported, skip it */
 const char *colon=NULL;for(const char *q=at;q<end;q++)if(*q==':')colon=q;
 int hl=(int)((colon?colon:end)-at);if(hl<=0 || hl>=hcap)return 0;
 memcpy(host,at,hl);host[hl]=0;
 if(colon){*port=atoi(colon+1);if(*port<=0 || *port>65535)return 0;}
 const char *hash=strchr(end,'#');int pl=hash?(int)(hash-end):(int)strlen(end); /* the #fragment is ZOID's (a station name) */
 if(pl==0){snprintf(path,pcap,"/");return 1;}
 if(*end=='?'){if(pl+1>=pcap)return 0;path[0]='/';memcpy(path+1,end,pl);path[pl+1]=0;return 1;}
 if(pl>=pcap)return 0;
 memcpy(path,end,pl);path[pl]=0;return 1;
}
void url_escape(char *out,int cap,const char *s) {
 int n=0;static const char *hex="0123456789ABCDEF";
 for(;*s && n<cap-4;s++) {
  unsigned char c=*s;
  if(isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~')out[n++]=c;
  else{out[n++]='%';out[n++]=hex[c>>4];out[n++]=hex[c&15];}
 }
 out[n]=0;
}
static int raw_read(Http *h,void *out,int len,int timeout_ms) {
 if(h->used<h->have){int n=h->have-h->used;if(n>len)n=len;memcpy(out,h->buf+h->used,n);h->used+=n;return n;}
 return net_read(h->c,out,len,timeout_ms);
}
static int raw_getc(Http *h,int timeout_ms) {
 if(h->used>=h->have){int r=net_read(h->c,h->buf,sizeof(h->buf),timeout_ms);if(r<=0){h->last_err=r<0?r:NET_E_CLOSED;return -1;}h->have=r;h->used=0;}
 return h->buf[h->used++];
}
static void header_value(const char *line,const char *name,char *out,int cap) {
 int n=strlen(name);if(strncasecmp(line,name,n) || line[n]!=':')return;
 const char *v=line+n+1;while(*v==' ' || *v=='\t')v++;snprintf(out,cap,"%s",v);
}
/* One request: connect, send, read the status line and headers. */
static int http_once(Http *h,const char *url,const char *extra,int timeout_ms,char *location,int lcap) {
 int tls,port;char host[128],path[1024];
 if(!net_parse_url(url,&tls,host,sizeof(host),&port,path,sizeof(path)))return NET_E_URL;
 int err;h->c=net_open(host,port,tls,&err);if(!h->c)return err;
 char req[1600];
 int n=snprintf(req,sizeof(req),"GET %s HTTP/1.1\r\nHost: %s%s%.0d\r\nUser-Agent: ZOID-PSP/1.0\r\nAccept: */*\r\nConnection: close\r\n%s\r\n",
  path,host,(port!=(tls?443:80))?":":"",(port!=(tls?443:80))?port:0,extra?extra:"");
 if(n<=0 || n>=(int)sizeof(req)){net_close(h->c);h->c=NULL;return NET_E_URL;}
 int r=net_write(h->c,req,n);if(r<0){net_close(h->c);h->c=NULL;return r;}
 /* status line and headers, a line at a time */
 char line[1024];int ln=0,first=1,done=0;h->have=h->used=0;
 while(!done) {
  int c=raw_getc(h,timeout_ms);
  if(c<0){net_close(h->c);h->c=NULL;return first && h->last_err<0?h->last_err:first?NET_E_TIMEOUT:NET_E_CLOSED;} /* the real reason: timed out, closed, or TLS */
  if(c=='\r')continue;
  if(c!='\n'){if(ln<(int)sizeof(line)-1)line[ln++]=c;continue;}
  line[ln]=0;
  if(first) { /* "HTTP/1.1 200 OK" or Shoutcast's "ICY 200 OK" */
   const char *sp=strchr(line,' ');h->status=sp?atoi(sp+1):0;first=0;
   if(!h->status){net_close(h->c);h->c=NULL;return NET_E_HTTP;}
  }else if(!ln)done=1;
  else {
   char v[512];v[0]=0;
   header_value(line,"Content-Length",v,sizeof(v));if(v[0]){h->length=atoi(v);v[0]=0;}
   header_value(line,"Content-Range",v,sizeof(v));if(v[0]){const char *sl=strchr(v,'/');if(sl && sl[1]!='*')h->range_total=atoi(sl+1);v[0]=0;}
   header_value(line,"Transfer-Encoding",v,sizeof(v));if(v[0]){for(char *q=v;*q;q++)*q=tolower((unsigned char)*q);h->chunked=strstr(v,"chunked")!=NULL;v[0]=0;}
   header_value(line,"icy-metaint",v,sizeof(v));if(v[0]){h->metaint=atoi(v);v[0]=0;}
   header_value(line,"icy-br",v,sizeof(v));if(v[0]){h->bitrate=atoi(v);v[0]=0;}
   header_value(line,"Content-Type",h->content_type,sizeof(h->content_type));
   header_value(line,"icy-name",h->icy_name,sizeof(h->icy_name));
   header_value(line,"icy-genre",h->icy_genre,sizeof(h->icy_genre));
   header_value(line,"Location",location,lcap);
   header_value(line,"Date",v,sizeof(v));if(v[0])learn_date(v);
  }
  ln=0;
 }
 return 1;
}
int http_open(Http *h,const char *url,const char *extra_headers,int timeout_ms) {
 char cur[1024];snprintf(cur,sizeof(cur),"%s",url);
 for(int hop=0;hop<6;hop++) {
  memset(h,0,offsetof(Http,buf));h->length=-1;h->range_total=-1;
  char loc[1024];loc[0]=0;
  int r=http_once(h,cur,extra_headers,timeout_ms,loc,sizeof(loc));if(r<0)return r;
  snprintf(h->final_url,sizeof(h->final_url),"%s",cur);
  if(h->status>=300 && h->status<400 && loc[0]) {
   http_close(h);
   if(loc[0]=='/') { /* same server */
    int tls,port;char host[128],path[1024];net_parse_url(cur,&tls,host,sizeof(host),&port,path,sizeof(path));
    char base[300];snprintf(base,sizeof(base),"%s://%s",tls?"https":"http",host);
    if(port!=(tls?443:80))snprintf(base+strlen(base),sizeof(base)-strlen(base),":%d",port);
    snprintf(cur,sizeof(cur),"%s%s",base,loc);
   }else snprintf(cur,sizeof(cur),"%s",loc);
   continue;
  }
  return 1;
 }
 return NET_E_HTTP;
}
int http_read(Http *h,void *out,int len,int timeout_ms) {
 if(h->eof || !h->c)return 0;
 if(h->chunked) {
  if(!h->chunk_left) {
   char line[24];int n,c;
   do{n=0;while((c=raw_getc(h,timeout_ms))>=0 && c!=10)if(c!=13 && n<23)line[n++]=c;if(c<0)return NET_E_CLOSED;line[n]=0;}while(!n); /* skips the CRLF after the last chunk */
   h->chunk_left=(int)strtol(line,NULL,16);
   if(h->chunk_left<=0){h->eof=1;return 0;}
  }
  if(len>h->chunk_left)len=h->chunk_left;
  int r=raw_read(h,out,len,timeout_ms);if(r>0)h->chunk_left-=r;
  return r;
 }
 if(h->length>=0){if(!h->length){h->eof=1;return 0;}if(len>h->length)len=h->length;}
 int r=raw_read(h,out,len,timeout_ms);
 if(r>0 && h->length>0)h->length-=r;
 if(r==0)h->eof=1;
 return r;
}
void http_close(Http *h){if(h->c){net_close(h->c);h->c=NULL;}}
int http_get(const char *url,char **body,int max,int *status) {
 *body=NULL;if(status)*status=0;
 /* one per call (not static): the net, podcast and download threads may all fetch at once */
 Http *hp=malloc(sizeof(Http));if(!hp)return NET_E_MEMORY;
#define h (*hp)
 memset(&h,0,sizeof(h));
 int r=http_open(&h,url,NULL,12000);if(r<0){free(hp);return r;}
 if(status)*status=h.status;
 if(h.status!=200){http_close(&h);free(hp);return NET_E_HTTP;}
 int cap=h.length>0 && h.length<max?h.length+1:16384,n=0;char *b=malloc(cap);
 if(!b){http_close(&h);free(hp);return NET_E_MEMORY;}
 for(;;) {
  if(n+4096>cap){if(cap>=max+1){free(b);http_close(&h);free(hp);return NET_E_TOOBIG;}int nc=cap*2;if(nc>max+1)nc=max+1;char *q=realloc(b,nc);if(!q){free(b);http_close(&h);free(hp);return NET_E_MEMORY;}b=q;cap=nc;}
  int want=cap-1-n;if(want>4096)want=4096;if(want<=0){free(b);http_close(&h);free(hp);return NET_E_TOOBIG;}
  int g=http_read(&h,b+n,want,12000);
  if(g==0)break;
  if(g<0){free(b);http_close(&h);free(hp);return g;}
  n+=g;
 }
 http_close(&h);free(hp);b[n]=0;*body=b;return n;
#undef h
}
