#ifndef NET_H
#define NET_H
/* Wi-Fi, DNS, TCP, HTTPS (BearSSL) and a small HTTP client. See net.c.
   Everything here blocks, so it is only called from worker threads (zoid_net, zoid_radio). */

/* errors (negative); net_error_text() says them in words */
enum {NET_E_SWITCH=-1001,NET_E_NOCONFIG=-1002,NET_E_JOIN=-1003,NET_E_MODULE=-1004,NET_E_DNS=-1005,NET_E_CONNECT=-1006,
 NET_E_TLS=-1007,NET_E_HTTP=-1008,NET_E_TIMEOUT=-1009,NET_E_CLOSED=-1010,NET_E_URL=-1011,NET_E_MEMORY=-1012,NET_E_TOOBIG=-1013,
 NET_E_STOPPED=-1014};
const char *net_error_text(int code);

/* Wi-Fi. config: the PSP's saved network (1 = the first under Network Settings), 0 = the first that works. */
extern volatile int net_status;      /* 0 off, 1 connecting, 2 connected */
extern volatile int net_fail_step;extern volatile unsigned int net_fail_code; extern volatile int net_free_kb[3]; /* which start-up call failed, and its code */
extern volatile int net_tls_error;   /* the last BearSSL error, for logs */
extern char net_connected_name[64];  /* the network ZOID joined */
void net_setup(void);                /* once at startup, before any thread uses the network */
int net_up(int config);              /* 1 when connected, else a NET_E_* code */
void net_down(void);
void net_touch(void);                /* marks the network as in use (the idle timer starts again) */
unsigned long long net_idle_us(void);
int net_config_count(void);          /* saved networks on this PSP (indexes 1..n are valid) */
int net_config_name(int config,char *out,int cap); /* 1 when that slot holds a network */

/* One TCP connection, with TLS for https. Reads and writes wait at most timeout_ms. */
typedef struct NetConn NetConn;
NetConn *net_open(const char *host,int port,int tls,int *error);
int net_write(NetConn *c,const void *data,int len);           /* len, or a NET_E_* code */
int net_read(NetConn *c,void *data,int len,int timeout_ms);   /* bytes, 0 at the end, NET_E_TIMEOUT when nothing came */
void net_close(NetConn *c);
void net_cancel(NetConn *c);  /* from another thread: the next read or write gives up */

/* HTTP GET. Redirects are followed; chunked bodies are decoded. */
typedef struct {
 NetConn *c;int status,length,chunked,chunk_left,metaint,bitrate,eof;
 int range_total; /* the whole file's size from a 206 reply's Content-Range, or -1 */
 char content_type[64],icy_name[96],icy_genre[64],final_url[1024];
 int last_err; /* why the last header read failed */
 unsigned char buf[2048];int have,used;
} Http;
int http_open(Http *h,const char *url,const char *extra_headers,int timeout_ms); /* 1, or a NET_E_* code; h->status holds the HTTP status */
int http_read(Http *h,void *out,int len,int timeout_ms);  /* body bytes; 0 at the end */
void http_close(Http *h);
/* The whole body (NUL-terminated, malloc'd) of a 200 reply, at most max bytes. Returns its length or a NET_E_* code. */
int http_get(const char *url,char **body,int max,int *status);

/* helpers */
void url_escape(char *out,int cap,const char *s);   /* for query strings */
int net_parse_url(const char *url,int *tls,char *host,int hcap,int *port,char *path,int pcap);
#endif
