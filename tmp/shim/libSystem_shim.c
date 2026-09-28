/*
 * Minimal self-contained /usr/lib/libSystem.B.dylib shim so that the macOS
 * /bin/echo can be linked from disk by dyld in the chroot (no shared cache).
 * Exports exactly the 10 symbols echo imports:
 *   err, exit, fflush, getenv, mbtowc, putchar, putwchar, strlen,
 *   __stdoutp (data), __mb_cur_max (data)
 * Uses raw arm64 Darwin syscalls only (no libSystem dependency).
 */
#include <stddef.h>
#include <stdint.h>

typedef struct __sFILE FILE;

/* exported data symbols — FILE* is opaque; we encode fd directly (NULL=0=stdin) */
FILE *__stdoutp = (FILE *)1;
int   __mb_cur_max = 1;

__attribute__((always_inline))
static inline long _sys3(long n, long a, long b, long c) {
    register long x16 __asm__("x16") = n;
    register long x0  __asm__("x0")  = a;
    register long x1  __asm__("x1")  = b;
    register long x2  __asm__("x2")  = c;
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16), "r"(x1), "r"(x2) : "memory", "cc");
    return x0;
}

static void _raw_exit(int code) {
    register long x16 __asm__("x16") = 1; /* SYS_exit */
    register long x0  __asm__("x0")  = code;
    __asm__ volatile("svc #0x80" :: "r"(x16), "r"(x0) : "memory");
    for (;;) {}
}

static long _raw_write(int fd, const void *buf, unsigned long n) {
    return _sys3(4 /* SYS_write */, fd, (long)buf, (long)n);
}

unsigned long strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (unsigned long)(p - s);
}

int putchar(int c) {
    unsigned char b = (unsigned char)c;
    _raw_write(1, &b, 1);
    return c;
}

int putwchar(int c) {
    unsigned int u = (unsigned int)c;
    unsigned char b[4];
    int n;
    if (u < 0x80)         { b[0]=u; n=1; }
    else if (u < 0x800)   { b[0]=(0xC0|(u>>6));  b[1]=(0x80|(u&0x3F)); n=2; }
    else if (u < 0x10000) { b[0]=(0xE0|(u>>12)); b[1]=(0x80|((u>>6)&0x3F)); b[2]=(0x80|(u&0x3F)); n=3; }
    else                  { b[0]=(0xF0|(u>>18)); b[1]=(0x80|((u>>12)&0x3F)); b[2]=(0x80|((u>>6)&0x3F)); b[3]=(0x80|(u&0x3F)); n=4; }
    _raw_write(1, b, (unsigned long)n);
    return c;
}

int fflush(FILE *f) { (void)f; return 0; }

/* reference a symbol from our libdyld shim so the linker records an
 * LC_LOAD_DYLIB on /usr/lib/system/libdyld.dylib (dyld needs it present). */
extern void *dyldshim_anchor;
char *getenv(const char *name) { (void)name; return (char *)dyldshim_anchor; }

int mbtowc(int *pwc, const char *s, size_t n) {
    if (s == 0) return 0;
    if (n == 0) return -1;
    unsigned char c = (unsigned char)s[0];
    if (pwc) *pwc = (int)c;
    return c ? 1 : 0;
}

void exit(int code) { _raw_exit(code); }
void _exit(int code) { _raw_exit(code); }

void err(int eval, const char *fmt, ...) {
    (void)fmt;
    _raw_exit(eval);
}

/* ===== added for /bin/cat (arm64 slice imports ___error) ===== */
int *__error(void) {
    long t;
    __asm__ volatile("mrs %0, TPIDRRO_EL0" : "=r"(t));
    return (int *)(void *)t;
}
char *strerror(int e) { (void)e; return (char *)"err"; }
const char *strerror_r(int e, char *buf, size_t n) { (void)e;(void)buf;(void)n; return "err"; }

/* ===== added for /bin/sh (11 imports) ===== */
FILE *__stderrp = (FILE *)2;         /* ___stderrp */
unsigned long long __stack_chk_guard = 0;   /* ___stack_chk_guard */
void __stack_chk_fail(void) { _raw_write(2, "stack check fail\n", 17); _raw_exit(134); }

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

#ifdef SHIM_DEBUG
#define DBG(c) do{char _c=(c);_raw_write(2,&_c,1);}while(0)
#else
#define DBG(c) do{}while(0)
#endif
static void _putstr(const char *s) { unsigned long n = strlen(s); _raw_write(2, s, n); }
static void _putnum(long v) {
    char b[24]; int i = 0; unsigned long u;
    if (v < 0) { b[i++] = '-'; u = (unsigned long)(-v); } else { u = (unsigned long)v; }
    char t[24]; int j = 0;
    if (u == 0) t[j++] = '0';
    while (u) { t[j++] = (char)('0' + (u % 10)); u /= 10; }
    while (j) b[i++] = t[--j];
    _raw_write(2, b, (unsigned long)i);
}

int fprintf(FILE *f, const char *fmt, ...) {
    (void)f;
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { _raw_write(2, p, 1); continue; }
        p++;
        if (*p == 's')      { const char *s = __builtin_va_arg(ap, const char *); _putstr(s ? s : "(null)"); }
        else if (*p == 'd' || *p == 'i') { _putnum(__builtin_va_arg(ap, int)); }
        else if (*p == 'u') { _putnum((long)__builtin_va_arg(ap, unsigned int)); }
        else if (*p == 'c') { char c = (char)__builtin_va_arg(ap, int); _raw_write(2, &c, 1); }
        else if (*p == '%') { _raw_write(2, "%", 1); }
        else if (*p == 0) break;
    }
    __builtin_va_end(ap);
    return 0;
}

int fputc(int c, FILE *f) { (void)f; unsigned char b = (unsigned char)c; _raw_write(2, &b, 1); return c; }
int fputs(const char *s, FILE *f) { (void)f; _putstr(s); return 0; }

long readlink(const char *path, char *buf, unsigned long n) {
    register long x16 __asm__("x16") = 58;               /* SYS_readlink */
    register long x0 __asm__("x0") = (long)path;
    register long x1 __asm__("x1") = (long)buf;
    register long x2 __asm__("x2") = (long)n;
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16), "r"(x1), "r"(x2) : "memory", "cc");
    return x0;
}

extern char **environ;
int execv(const char *path, char *const argv[]) {
    register long x16 __asm__("x16") = 59;               /* SYS_execve */
    register long x0 __asm__("x0") = (long)path;
    register long x1 __asm__("x1") = (long)argv;
    register long x2 __asm__("x2") = (long)environ;
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16), "r"(x1), "r"(x2) : "memory", "cc");
    return (int)x0;
}

/* ===== added for /bin/cat (41 imports) ===== */
FILE *__stdinp = (FILE *)0;               /* ___stdinp */
int  __mb_cur_max_alias = 1;
int  __maskrune(int c, int f) { (void)f; return (c >= ' ' && c < 127) ? 1 : 0; }   /* ___maskrune */
unsigned char _DefaultRuneLocale[512] = {0};   /* __DefaultRuneLocale (data 桩) */
int  optind = 1;                           /* _optind */
char *optarg = 0;

unsigned long __dummy_rune = 0;            /* __DefaultRuneLocale 实体占位 */

/* --- syscalls --- */
static inline long _s1(long n,long a){ register long x16 __asm__("x16")=n; register long x0 __asm__("x0")=a; __asm__ volatile("svc #0x80":"+r"(x0):"r"(x16):"memory","cc"); return x0; }
static inline long _s2(long n,long a,long b){ register long x16 __asm__("x16")=n; register long x0 __asm__("x0")=a; register long x1 __asm__("x1")=b; __asm__ volatile("svc #0x80":"+r"(x0):"r"(x16),"r"(x1):"memory","cc"); return x0; }
static inline long _s3(long n,long a,long b){ register long x16 __asm__("x16")=n; register long x0 __asm__("x0")=a; register long x1 __asm__("x1")=b; __asm__ volatile("svc #0x80":"+r"(x0):"r"(x16),"r"(x1):"memory","cc"); return x0; }
static inline long _s4(long n,long a,long b,long c){ register long x16 __asm__("x16")=n; register long x0 __asm__("x0")=a; register long x1 __asm__("x1")=b; register long x2 __asm__("x2")=c; __asm__ volatile("svc #0x80":"+r"(x0):"r"(x16),"r"(x1),"r"(x2):"memory","cc"); return x0; }
static inline long _s5(long n,long a,long b,long c,long d){ register long x16 __asm__("x16")=n; register long x0 __asm__("x0")=a; register long x1 __asm__("x1")=b; register long x2 __asm__("x2")=c; register long x3 __asm__("x3")=d; __asm__ volatile("svc #0x80":"+r"(x0):"r"(x16),"r"(x1),"r"(x2),"r"(x3):"memory","cc"); return x0; }
static inline long _s6(long n,long a,long b,long c,long d,long e){ register long x16 __asm__("x16")=n; register long x0 __asm__("x0")=a; register long x1 __asm__("x1")=b; register long x2 __asm__("x2")=c; register long x3 __asm__("x3")=d; register long x4 __asm__("x4")=e; __asm__ volatile("svc #0x80":"+r"(x0):"r"(x16),"r"(x1),"r"(x2),"r"(x3),"r"(x4):"memory","cc"); return x0; }

int open(const char *p, int flags, int mode) { DBG('O'); int r = (int)_s4(5, (long)p, flags, mode); DBG(r<0?'o':'P'); return r; }
int close(int fd) { return (int)_s3(6, fd, 0); }
long read(int fd, void *buf, unsigned long n) { DBG('r'); long r = _s4(3, fd, (long)buf, (long)n); DBG('0'+(r<0?'-'-48:(r>9?9:r))); return r; }
int fcntl(int fd, int cmd, long arg) { DBG('F'); return (int)_s4(92, fd, cmd, arg); }
int fstat(int fd, void *st) {                                  /* SYS_fstat=339 → INODE64 layout on arm64 */
    DBG('S');
    register long x16 __asm__("x16") = 339;
    register long x0 __asm__("x0") = (long)fd;
    register long x1 __asm__("x1") = (long)st;
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16), "r"(x1) : "memory", "cc");
    return (int)x0;
}
void putc_dbg(char c){_raw_write(2,&c,1);}
int *__error_alias = 0;

/* --- stdio 最小实现（FILE* 按 fileno 指向真实 fd；NULL→stdin/stdout）--- */
static int _fdof(FILE *f, int dflt) { long v = (long)f; return (v >= 0 && v < 1024) ? (int)v : dflt; }
int fwrite(const void *ptr, unsigned long sz, unsigned long n, FILE *f) { DBG('W'); unsigned long t = sz*n; _raw_write(_fdof(f,1), ptr, t); return (int)n; }
unsigned long fread(void *ptr, unsigned long sz, unsigned long n, FILE *f) { DBG('R'); long r = _s4(3, _fdof(f,0), (long)ptr, (long)(sz*n)); return r > 0 ? (unsigned long)(r / (sz?sz:1)) : 0; }
int getc(FILE *f) { DBG('G'); unsigned char c; long r = _s4(3, _fdof(f,0), (long)&c, 1); return r == 1 ? (int)c : -1; }
int ungetc(int c, FILE *f) { (void)f; return c; }
int feof(FILE *f) { DBG('e'); return 0; }
int ferror(FILE *f) { DBG('!'); return 0; }
int fileno(FILE *f) { return (int)(long)f; }
void clearerr(FILE *f) { (void)f; }
int fclose(FILE *f) { DBG('c'); _s3(6,_fdof(f,0),0); return 0; }
FILE *fdopen(int fd, const char *m) { (void)m; DBG('d'); return (FILE *)(long)fd; }
void setbuf(FILE *f, char *b) { (void)f; (void)b; DBG('b'); }
int setvbuf(FILE *f, char *b, int m, unsigned long s) { (void)f;(void)b;(void)m;(void)s; return 0; }
char *setlocale(int c, const char *l) { (void)c; (void)l; return (char *)"C"; }
long sysconf(int n) { (void)n; return 4096; }

/* --- $INODE64 aliases (cat imports _fstat$INODE64; date imports _stat/_stat$INODE64) --- */
int fstat(int fd, void *st);              /* fwd decl of stub below */
int fstat_INODE64(int fd, void *st) __asm__("_fstat$INODE64");
int fstat_INODE64(int fd, void *st) { return fstat(fd, st); }
int stat64_sys(const char *p, void *st) {
    register long x16 __asm__("x16") = 338;               /* SYS_stat (arm64 = INODE64 layout) */
    register long x0 __asm__("x0") = (long)p;
    register long x1 __asm__("x1") = (long)st;
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16), "r"(x1) : "memory", "cc");
    return (int)x0;
}
int stat_fn(const char *p, void *st) __asm__("_stat");
int stat_fn(const char *p, void *st) { return (int)stat64_sys(p, st); }
int stat_INODE64(const char *p, void *st) __asm__("_stat$INODE64");
int stat_INODE64(const char *p, void *st) { return (int)stat64_sys(p, st); }
int lstat_INODE64(const char *p, void *st) __asm__("_lstat$INODE64");
int lstat_INODE64(const char *p, void *st) {
    register long x16 __asm__("x16") = 340;               /* SYS_lstat */
    register long x0 __asm__("x0") = (long)p;
    register long x1 __asm__("x1") = (long)st;
    __asm__ volatile("svc #0x80" : "+r"(x0) : "r"(x16), "r"(x1) : "memory", "cc");
    return (int)x0;
}

/* --- 简单内存分配（mmap bump）--- */
static char *_heap = 0; static unsigned long _heap_left = 0;
void *malloc(unsigned long n) {
    if (n == 0) n = 16;
    n = (n + 15) & ~15UL;
    if (n > _heap_left) {
        unsigned long want = 1UL << 22;                     /* 4MB 一块 */
        if (want < n) want = (n + 0xFFFF) & ~0xFFFFUL;
        long p = _s4(197 /*SYS_mmap*/, 0, (long)want, 3 /*PROT_RW*/);   /* 注意：真实 mmap 需更多参数 */
        /* 用最简单的匿名映射：这里退化为静态大 buffer */
        static char pool[1 << 20]; static unsigned long used = 0;
        if (used + n > sizeof(pool)) return 0;
        char *r = pool + used; used += n; (void)p; return r;
    }
    char *r = _heap; _heap += n; _heap_left -= n; return r;
}
void *calloc(unsigned long n, unsigned long sz) { unsigned long t = n*sz; char *p = (char *)malloc(t); for (unsigned long i=0;i<t;i++) p[i]=0; return p; }
void free(void *p) { (void)p; }
void *realloc(void *p, unsigned long n) { (void)p; return malloc(n); }
unsigned long strlen_probe(void) { return 0; }


/* --- 桩：cat -l 的网络路径、realpath 等 --- */
int socket(int d, int t, int p) { (void)d;(void)t;(void)p; return -1; }
int connect(int fd, const void *a, unsigned int l) { (void)fd;(void)a;(void)l; return -1; }
int shutdown(int fd, int h) { (void)fd;(void)h; return -1; }
int getaddrinfo(const char *n, const char *s, const void *h, void **r) { (void)n;(void)s;(void)h;(void)r; return -1; }
void freeaddrinfo(void *r) { (void)r; }
const char *gai_strerror(int e) { (void)e; return "err"; }
char *realpath(const char *p, char *out) { (void)out; return (char *)p; }
extern char *realpath_extsn(const char *p, char *out) __asm__("_realpath$DARWIN_EXTSN");
char *realpath_extsn(const char *p, char *out) { (void)out; return (char *)p; }

/* ===== getopt（cat 用到）===== */
static const char *_optp = 0; static int _optsub = 1;
int getopt(int argc, char *const argv[], const char *optstring) {
    if (_optsub == 1) {
        if (optind >= argc || !argv[optind] || argv[optind][0] != '-' || argv[optind][1] == 0) return -1;
        if (argv[optind][1] == '-' && argv[optind][2] == 0) { optind++; return -1; }
        _optp = argv[optind] + 1;
    }
    int c = (unsigned char)*_optp;
    _optp++;
    if (*_optp == 0) { optind++; _optsub = 1; } else { _optsub = 0; }
    const char *q = optstring;
    while (*q) { if (*q == c) break; q++; }
    if (!*q) return '?';
    if (q[1] == ':') {                       /* 需要参数 */
        if (_optsub == 0) { optarg = (char *)_optp; _optsub = 1; optind++; }
        else if (optind < argc) { optarg = argv[optind++]; }
        else return ':';
    }
    return c;
}

/* ===== malloc_type 系列（cat 用）===== */
void *malloc_type_malloc(unsigned long n, unsigned int t) { (void)t; return malloc(n); }
void *malloc_type_calloc(unsigned long n, unsigned long sz, unsigned int t) { (void)t; return calloc(n, sz); }
void *malloc_type_realloc(void *p, unsigned long n, unsigned int t) { (void)t; return realloc(p, n); }
void  malloc_type_free(void *p, unsigned int t) { (void)t; free(p); }
void *malloc_zone_malloc(void *z, unsigned long n) { (void)z; return malloc(n); }

void warn(const char *fmt, ...) { (void)fmt; _raw_write(2, "warn\n", 5); }
void warnx(const char *fmt, ...) { (void)fmt; _raw_write(2, "warnx\n", 6); }

long write(int fd, const void *buf, unsigned long n) { DBG('w'); return _s4(4, fd, (long)buf, (long)n); }

/* =====================================================================
 * added for /bin/date (23 missing) + shared easy ones used by /bin/ls
 * ===================================================================== */
void abort(void) { _raw_write(2,"abort\n",6); _raw_exit(134); }
void errx(int eval, const char *fmt, ...) { (void)fmt; _raw_write(2,"errx\n",5); _raw_exit(eval); }

int atoi(const char *s) {
    int v=0,neg=0;
    while(*s==' '||*s=='\t')s++;
    if(*s=='-'){neg=1;s++;}else if(*s=='+')s++;
    while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}
    return neg?-v:v;
}
long long strtoq(const char *s, char **end, int base) {
    (void)base;
    long long v=0; int neg=0;
    while(*s==' '||*s=='\t')s++;
    if(*s=='-'){neg=1;s++;}
    while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}
    if(end)*end=(char*)s;
    return neg?-v:v;
}
unsigned long strtoul(const char *s, char **end, int base) {
    unsigned long v=0;
    while(*s==' '||*s=='\t')s++;
    if(*s=='0'&&(s[1]=='x'||s[1]=='X')&&(base==0||base==16)){s+=2;base=16;}
    if(base==0)base=10;
    for(;;){int d;if(*s>='0'&&*s<='9')d=*s-'0';else if(*s>='a'&&*s<='f')d=*s-'a'+10;else if(*s>='A'&&*s<='F')d=*s-'A'+10;else break;if(d>=base)break;v=v*base+d;s++;}
    if(end)*end=(char*)s;
    return v;
}
char *strcpy(char *d, const char *s){char*r=d;while((*d++=*s++));return r;}
char *strdup(const char *s){unsigned long n=strlen(s)+1;char*p=(char*)malloc(n);for(unsigned long i=0;i<n;i++)p[i]=s[i];return p;}
int strncasecmp(const char *a,const char *b,unsigned long n){
    while(n--&&*a){int ca=*a,cb=*b;if(ca>='A'&&ca<='Z')ca+=32;if(cb>='A'&&cb<='Z')cb+=32;if(ca!=cb)return ca-cb;a++;b++;}
    return 0;
}
int strcasecmp(const char *a,const char *b){
    while(*a){int ca=*a,cb=*b;if(ca>='A'&&ca<='Z')ca+=32;if(cb>='A'&&cb<='Z')cb+=32;if(ca!=cb)return ca-cb;a++;b++;}
    return *b?-*b:0;
}
unsigned long strspn(const char *s,const char *set){
    unsigned long n=0;
    for(;*s;s++){const char*q=set;while(*q&&*q!=*s)q++;if(!*q)break;n++;}
    return n;
}
unsigned long strcspn(const char *s,const char *set){
    unsigned long n=0;
    for(;*s;s++){const char*q=set;while(*q&&*q!=*s)q++;if(*q)break;n++;}
    return n;
}
char *strchr(const char *s,int c){for(;*s;s++)if(*s==(char)c)return(char*)s;return(c==0)?(char*)s:0;}
char *strstr(const char *h,const char *nd){
    unsigned long nl=strlen(nd);if(!nl)return(char*)h;
    for(;*h;h++){unsigned long i;for(i=0;i<nl&&h[i]==nd[i];i++);if(i==nl)return(char*)h;}
    return 0;
}
char *stpncpy(char *d,const char *s,unsigned long n){char*r=d;while(n--&&*s)*d++=*s++;while((long)n-- >0)*d++=0;return r;}
void *memchr(const void *p,int c,unsigned long n){const unsigned char*u=(const unsigned char*)p;while(n--){if(*u==(unsigned char)c)return(void*)u;u++;}return 0;}
void *memcpy(void *d,const void *s,unsigned long n){char*dd=(char*)d;const char*ss=(const char*)s;while(n--)*dd++=*ss++;return d;}
void *memmove(void *d,const void *s,unsigned long n){
    char*dd=(char*)d;const char*ss=(const char*)s;
    if(dd<ss){while(n--)*dd++=*ss++;}else{dd+=n;ss+=n;while(n--)*--dd=*--ss;}
    return d;
}
void *memset(void *p,int c,unsigned long n){unsigned char*u=(unsigned char*)p;while(n--)*u++=(unsigned char)c;return p;}
void bzero(void *p,unsigned long n){memset(p,0,n);}
int strcoll(const char *a,const char *b){return strcmp(a,b);}
int tolower_(int c) __asm__("___tolower");
int tolower_(int c){return(c>='A'&&c<='Z')?c+32:c;}
/* --- printf family into buffer / fd --- */
static int _fmtcore(char *out, unsigned long cap, int fd, const char *fmt, __builtin_va_list ap) {
    unsigned long o = 0;
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { if(out){if(o<cap-1)out[o]=*p;}else _raw_write(fd,p,1); o++; continue; }
        p++;
        int lflag=0,zflag=0;
        while (*p=='-'||*p=='+'||*p==' '||*p=='0'||(*p>='1'&&*p<='9')) p++;  /* skip flags/width */
        if (*p=='.'){p++;while(*p>='0'&&*p<='9')p++;}
        while (*p=='l'||*p=='z'||*p=='t'){if(*p=='l')lflag++;p++;}
        char tmp[64]; unsigned long tn=0; long long sv=0; unsigned long long uv=0; int isnum=1;
        switch(*p){
        case 's': { const char *s=__builtin_va_arg(ap,const char*); if(!s)s="(null)";unsigned long n=strlen(s);
            for(unsigned long i=0;i<n;i++){if(out){if(o<cap-1)out[o]=s[i];}else _raw_write(fd,s+i,1);o++;}
            continue; }
        case 'c': { char c=(char)__builtin_va_arg(ap,int);if(out){if(o<cap-1)out[o]=c;}else _raw_write(fd,&c,1);o++;continue; }
        case 'd': case 'i': sv=lflag?__builtin_va_arg(ap,long long):(long long)__builtin_va_arg(ap,int);
            uv=(unsigned long long)sv; if(sv<0){tmp[tn++]='-';uv=(unsigned long long)(-sv);} break;
        case 'u': uv=lflag?__builtin_va_arg(ap,unsigned long long):(unsigned long long)__builtin_va_arg(ap,unsigned int); break;
        case 'x': case 'X': uv=lflag?__builtin_va_arg(ap,unsigned long long):(unsigned long long)__builtin_va_arg(ap,unsigned int);{int base=16;int hexa=(*p=='X');
            char tb[24];int j=0;do{int d=uv%base;tb[j++]=d<10?'0'+d:(hexa?'A':'a')+d-10;uv/=base;}while(uv);
            while(j)tmp[tn++]=tb[--j];} isnum=2; break;
        case 'p': uv=(unsigned long long)__builtin_va_arg(ap,void*);{tmp[tn++]='0';tmp[tn++]='x';char tb[24];int j=0;do{int d=uv%16;tb[j++]="0123456789abcdef"[d];uv/=16;}while(uv);while(j)tmp[tn++]=tb[--j];}isnum=2;break;
        case '%': if(out){if(o<cap-1)out[o]='%';}else _raw_write(fd,"%",1);o++;continue;
        default: isnum=0; if(out){if(o<cap-1)out[o]=*p;}else _raw_write(fd,p,1);o++;continue;
        }
        if(isnum==1){char tb[24];int j=0;if(uv==0)tb[j++]='0';while(uv){tb[j++]='0'+(uv%10);uv/=10;}while(j)tmp[tn++]=tb[--j];}
        for(unsigned long i=0;i<tn;i++){if(out){if(o<cap-1)out[o]=tmp[i];}else _raw_write(fd,tmp+i,1);o++;}
    }
    if(out)out[o<cap?o:cap-1]=0;
    return (int)o;
}
int snprintf(char *buf, unsigned long n, const char *fmt, ...) {
    __builtin_va_list ap;__builtin_va_start(ap,fmt);
    int r=_fmtcore(buf,n,0,fmt,ap);__builtin_va_end(ap);return r;
}
int vsnprintf(char *buf, unsigned long n, const char *fmt, __builtin_va_list ap) {
    return _fmtcore(buf,n,0,fmt,ap);
}
int asprintf(char **out, const char *fmt, ...) {
    __builtin_va_list ap;__builtin_va_start(ap,fmt);
    char tmp[1024]; int r=_fmtcore(tmp,sizeof(tmp),0,fmt,ap);__builtin_va_end(ap);
    *out=(char*)malloc((unsigned long)r+1);memcpy(*out,tmp,(unsigned long)r+1);
    return r;
}
int printf(const char *fmt, ...) {
    __builtin_va_list ap;__builtin_va_start(ap,fmt);
    int r=_fmtcore(0,0,1,fmt,ap);__builtin_va_end(ap);return r;
}
int puts(const char *s){_raw_write(1,s,strlen(s));_raw_write(1,"\n",1);return 0;}
int pututxline(void *u){(void)u;return 0;}
void syslog_extsn(int pri,const char*m,...) __asm__("_syslog$DARWIN_EXTSN");
void syslog_extsn(int pri,const char*m,...){(void)pri;(void)m;}

/* --- time: UTC only --- */
struct tm_g {int tm_sec,tm_min,tm_hour,tm_mday,tm_mon,tm_year,tm_wday,tm_yday,tm_isdst;long tm_gmtoff;char*tm_zone;};
static void civil_from_days(long z,int*y,int*m,int*d){
    z+=719468;long era=(z>=0?z:z-146096)/146097;unsigned doe=(unsigned)(z-era*146097);
    unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365;long y2=(long)yoe+era*400;
    unsigned doy=doe-(365*yoe+yoe/4-yoe/100);unsigned mp=(5*doy+2)/153;
    *d=(int)(doy-(153*mp+2)/5+1);*m=(int)(mp<10?mp+3:mp-9);*y=(int)(y2+(*m<=2));
}
static long days_from_civil(int y,unsigned m,unsigned d){
    y-=m<=2;long era=(y>=0?y:y-399)/400;unsigned yoe=(unsigned)(y-era*400);
    unsigned doy=(153*(m>2?m-3:m+9)+2)/5+d-1;unsigned doe=yoe*365+yoe/4-yoe/100+doy;
    return era*146097+(long)doe-719468;
}
int gettimeofday(void *tv, void *tz){(void)tz;return(int)_s3(116,(long)tv,0);}
int clock_gettime(int c, void *ts){(void)c;
    struct{long tv_sec,tv_usec;}tv;gettimeofday(&tv,0);
    struct{long tv_sec;long tv_nsec;}*t=(void*)ts;t->tv_sec=tv.tv_sec;t->tv_nsec=tv.tv_usec*1000;return 0;}
int clock_settime(int c, const void *ts){(void)c;(void)ts;return -1;}
long time_(long *t) __asm__("_time");
long time_(long *t){struct{long s,u;}tv;gettimeofday(&tv,0);if(t)*t=tv.s;return tv.s;}

void *localtime(const long *tp){
    static struct tm_g t;
    long days=(*tp)/86400, rem=(*tp)%86400; if(rem<0){rem+=86400;days--;}
    int y,m,d;civil_from_days(days,&y,&m,&d);
    t.tm_sec=(int)(rem%60);t.tm_min=(int)((rem/60)%60);t.tm_hour=(int)(rem/3600);
    t.tm_mday=d;t.tm_mon=m-1;t.tm_year=y-1900;
    t.tm_wday=(int)((days%7+11)%7);t.tm_yday=(int)(days-days_from_civil(y,1,1));
    t.tm_isdst=0;t.tm_gmtoff=0;static char z[]="UTC";t.tm_zone=z;
    return &t;
}
long mktime(void *tp){
    struct tm_g*t=(struct tm_g*)tp;
    return days_from_civil(t->tm_year+1900,(unsigned)t->tm_mon+1,(unsigned)t->tm_mday)*86400
          +(long)t->tm_hour*3600+(long)t->tm_min*60+t->tm_sec;
}
static const char*DYS[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
static const char*MOS[]={"January","February","March","April","May","June","July","August","September","October","November","December"};
unsigned long strftime(char *b,unsigned long cap,const char *f,const void *tp){
    const struct tm_g*t=(const struct tm_g*)tp;unsigned long o=0;
    for(;*f&&o<cap-1;f++){
        if(*f!='%'){b[o++]=*f;continue;}f++;
        char num[16];int nn;
        switch(*f){
        case'Y':{int y=t->tm_year+1900;nn=snprintf(num,16,"%d",y);break;}
        case'y':nn=snprintf(num,16,"%02d",(t->tm_year+1900)%100);break;
        case'm':nn=snprintf(num,16,"%02d",t->tm_mon+1);break;
        case'd':nn=snprintf(num,16,"%02d",t->tm_mday);break;
        case'e':nn=snprintf(num,16,"%2d",t->tm_mday);break;
        case'H':nn=snprintf(num,16,"%02d",t->tm_hour);break;
        case'I':{int h=t->tm_hour%12;if(!h)h=12;nn=snprintf(num,16,"%02d",h);break;}
        case'M':nn=snprintf(num,16,"%02d",t->tm_min);break;
        case'S':nn=snprintf(num,16,"%02d",t->tm_sec);break;
        case'j':nn=snprintf(num,16,"%03d",t->tm_yday+1);break;
        case'u':{int w=t->tm_wday;if(!w)w=7;nn=snprintf(num,16,"%d",w);break;}
        case'w':nn=snprintf(num,16,"%d",t->tm_wday);break;
        case'p':nn=snprintf(num,16,"%s",t->tm_hour<12?"AM":"PM");break;
        case'a':nn=snprintf(num,16,"%.3s",DYS[t->tm_wday%7]);break;
        case'A':nn=snprintf(num,16,"%s",DYS[t->tm_wday%7]);break;
        case'b':case'h':nn=snprintf(num,16,"%.3s",MOS[t->tm_mon%12]);break;
        case'B':nn=snprintf(num,16,"%s",MOS[t->tm_mon%12]);break;
        case'F':nn=snprintf(num,16,"%04d-%02d-%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday);break;
        case'T':nn=snprintf(num,16,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);break;
        case'R':nn=snprintf(num,16,"%02d:%02d",t->tm_hour,t->tm_min);break;
        case'D':case'x':nn=snprintf(num,16,"%02d/%02d/%02d",t->tm_mon+1,t->tm_mday,(t->tm_year+1900)%100);break;
        case'X':nn=snprintf(num,16,"%02d:%02d:%02d",t->tm_hour,t->tm_min,t->tm_sec);break;
        case'c':nn=snprintf(num,16,"%.3s %.3s %02d %02d:%02d:%02d %04d",DYS[t->tm_wday%7],MOS[t->tm_mon%12],t->tm_mday,t->tm_hour,t->tm_min,t->tm_sec,t->tm_year+1900);break;
        case'z':nn=snprintf(num,16,"+0000");break;
        case'Z':nn=snprintf(num,16,"UTC");break;
        case'n':num[0]='\n';num[1]=0;nn=1;break;
        case't':num[0]='\t';num[1]=0;nn=1;break;
        case'%':num[0]='%';num[1]=0;nn=1;break;
        default:num[0]='%';num[1]=*f;num[2]=0;nn=2;break;
        }
        for(int i=0;i<nn&&o<cap-1;i++)b[o++]=num[i];
    }
    b[o]=0;return o;
}
char *strptime(const char *s,const char *f,void *tp){
    struct tm_g*t=(struct tm_g*)tp;
    while(*f&&*s){
        if(*f!='%'){if(*f++!=*s++)return 0;continue;}f++;
        int v=0,nd=0;const char*q=s;while(*q>='0'&&*q<='9'&&nd<9){v=v*10+(*q-'0');q++;nd++;}
        switch(*f){
        case'Y':t->tm_year=v-1900;s=q;break;
        case'm':t->tm_mon=v-1;s=q;break;
        case'd':t->tm_mday=v;s=q;break;
        case'H':t->tm_hour=v;s=q;break;
        case'M':t->tm_min=v;s=q;break;
        case'S':t->tm_sec=v;s=q;break;
        default:return(char*)s;
        }
        f++;
    }
    return (char*)s;
}

/* --- misc one-liners (date+ls shared) --- */
int compat_mode_(const char *a,const char *b) __asm__("_compat_mode");
int compat_mode_(const char *a,const char *b){(void)a;(void)b;return 0;}
char *getlogin(void){static char n[]="root";return n;}
int setenv(const char *n,const char *v,int ow){(void)n;(void)v;(void)ow;return 0;}
int unsetenv(const char *n){(void)n;return 0;}
int isatty(int fd){int t[8];long r=_s4(54,fd,0x40487413,(long)t);return(int)(r==0);}
int kill_fn(int pid,int sig) __asm__("_kill");
int kill_fn(int pid,int sig){return(int)_s3(37,pid,sig);}
int getpid(void){return(int)_s1(20,0);}
int getppid(void){return(int)_s1(39,0);}
unsigned int getuid(void){return(unsigned int)_s1(24,0);}
unsigned int geteuid(void){return(unsigned int)_s1(25,0);}
unsigned int getgid(void){return(unsigned int)_s1(47,0);}
unsigned int getegid(void){return(unsigned int)_s1(43,0);}
int ioctl(int fd,unsigned long req,...){
    __builtin_va_list ap;__builtin_va_start(ap,req);long arg=__builtin_va_arg(ap,long);__builtin_va_end(ap);
    return(int)_s4(54,fd,(long)req,arg);
}
int chdir(const char *p){return(int)_s2(12,(long)p,0);}
int umask_(int m) __asm__("_umask");
int umask_(int m){return(int)_s2(60,m,0);}
int dup2(int a,int b){return(int)_s3(90,a,b);}
int pipe_(int*fds) __asm__("_pipe");
int pipe_(int*fds){return(int)_s3(42,(long)fds,0);}
int fork(void){return(int)_s1(2,0);}
int vfork(void){return(int)_s1(66,0);}
long waitpid(int p,int*st,int o){return _s5(7,p,(long)st,o,0);}
int execve(const char*p,char*const a[],char*const e[]){return(int)_s4(59,(long)p,(long)a,(long)e);}

char *nl_langinfo(int i){(void)i;static char e[]="";return e;}
int sysctlbyname(const char*n,void*o,unsigned long*ol,void*nn,unsigned long nl){(void)n;(void)o;(void)ol;(void)nn;(void)nl;return -1;}
int wcwidth(int c){(void)c;return 1;}
unsigned long mbrtowc(int*pwc,const char*s,unsigned long n,void*st){(void)st;
    if(!s)return 0;if(!n)return(unsigned long)-2;if(pwc)*pwc=(unsigned char)s[0];return s[0]?1:0;}
char *user_from_uid(unsigned int u,int n){(void)u;(void)n;static char r[]="root";return r;}
char *group_from_gid(unsigned int g,int n){(void)g;(void)n;static char r[]="wheel";return r;}
int getbsize(int*h,long*b){if(h)*h=0;if(b)*b=512;return 0;}
int getxattr(const char*p,const char*n,void*v,unsigned long s,unsigned int o,int x){(void)p;(void)n;(void)v;(void)s;(void)o;(void)x;return -1;}
long listxattr(const char*p,char*l,unsigned long s,int o){(void)p;(void)l;(void)s;(void)o;return -1;}
void uuid_unparse_upper(const unsigned char*u,char*o){
    static const char h[]="0123456789ABCDEF";int j=0;
    for(int i=0;i<16;i++){if(i==4||i==6||i==8||i==10)o[j++]='-';o[j++]=h[u[i]>>4];o[j++]=h[u[i]&15];}
    o[j]=0;
}
void strmode(int m,char*b){(void)m;strcpy(b,"----------");}
char *realpath_chk(const char*p,char*o,unsigned long n) __asm__("___realpath_chk");
char *realpath_chk(const char*p,char*o,unsigned long n){(void)n;if(o)strcpy(o,p);return o?o:(char*)p;}
void __assert_rtn(const char*f,const char*file,int l,const char*e){
    fprintf(0,"assert: %s %s %d %s\n",f?f:"",file?file:"",l,e?e:"");_raw_exit(134);
}
unsigned long __strlcat_chk(char*d,const char*s,unsigned long ds,unsigned long dl) __asm__("___strlcat_chk");
unsigned long __strlcat_chk(char*d,const char*s,unsigned long ds,unsigned long dl){
    (void)dl;
    unsigned long dn=0;while(dn<ds&&d[dn])dn++;
    unsigned long sn=strlen(s);
    if(dn>=ds){_raw_exit(134);}
    unsigned long room=ds-dn-1,copy=sn<room?sn:room;
    for(unsigned long i=0;i<copy;i++)d[dn+i]=s[i];
    d[dn+copy]=0;
    return dn+sn;
}
unsigned long __strlcpy_chk(char*d,const char*s,unsigned long ds,unsigned long dl) __asm__("___strlcpy_chk");
unsigned long __strlcpy_chk(char*d,const char*s,unsigned long ds,unsigned long dl){
    (void)dl;unsigned long sn=strlen(s);if(!ds)return sn;
    unsigned long copy=sn<ds-1?sn:ds-1;for(unsigned long i=0;i<copy;i++)d[i]=s[i];d[copy]=0;return sn;
}
char *__stpcpy_chk(char*d,const char*s,unsigned long dl) __asm__("___stpcpy_chk");
char *__stpcpy_chk(char*d,const char*s,unsigned long dl){(void)dl;while((*d++=*s++));return d-1;}

/* acl stubs (ls -e path; harmless NULLs) */
void *acl_get_qualifier(void*e){(void)e;return 0;}
int acl_free(void*p){(void)p;return 0;}
int acl_get_entry(void*a,int i,void**e){(void)a;(void)i;(void)e;return 0;}
int acl_get_flag_np(void*f,int fl){(void)f;(void)fl;return 0;}
int acl_get_flagset_np(void*e,void**f){(void)e;(void)f;return -1;}
int acl_get_perm_np(void*p,int pm){(void)p;(void)pm;return 0;}
int acl_get_permset(void*e,void**p){(void)e;(void)p;return -1;}
int acl_get_tag_type(void*e,int*t){(void)e;(void)t;return -1;}
void *acl_get_link_np(const char*p,int t){(void)p;(void)t;return 0;}
int mbr_identifier_translate(const void*a,char*b,unsigned long n){(void)a;(void)b;(void)n;return -1;}

/* signal: minimal — sigaction syscall w/o tramp is fragile; record & ignore */
void *signal(int sig,void*h){(void)sig;(void)h;return h;}
int sigaction(int sig,const void*a,void*o){(void)sig;(void)a;(void)o;return 0;}
int sigprocmask(int h,const void*s,void*o){(void)h;(void)s;(void)o;return 0;}
int sigsetmask(int m){return m;}
int sigsuspend(const void*m){(void)m;return -1;}
int raise(int s){return(int)_s3(37,getpid(),s);}
int killpg(int p,int s){return(int)_s3(37,-p,s);}

/* getopt_long → minimal pass-through to getopt-ish parse */
int getopt_long(int argc,char*const argv[],const char*os,const void*lo,int*li){
    (void)lo;(void)li;
    return getopt(argc,argv,os);
}

/* fts stubs — ls will show nothing but stays alive */
void *fts_open(char*const*a,int o,void*c){(void)a;(void)o;(void)c;return 0;}
void *fts_read(void*f){(void)f;return 0;}
void *fts_children(void*f,int o){(void)f;(void)o;return 0;}
int fts_close(void*f){(void)f;return 0;}
int fts_set(void*f,void*e,int o){(void)f;(void)e;(void)o;return 0;}
void *reallocf(void*p,unsigned long n){return realloc(p,n);}
void *humanize_number_stub(char*b,unsigned long l,long long n,const char*u,int s,int f){(void)u;(void)s;(void)f;snprintf(b,l,"%lld",n);return b;}

