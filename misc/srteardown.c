/* srteardown.c — does shared_region_check_np(NULL) remove the permanent
 * [0x180000000,0x280000000) submap entry enough for user FIXED mmap?
 *
 * Sequence (all raw svc, no libSystem):
 *   1. check_np(&base)          -> expect ret=12 ENOMEM, base=0
 *   2. check_np(NULL)           -> teardown; expect ret=0
 *   3. mmap FIXED anon @0x180000000  -> succeeds iff permanent entry gone
 *   4. mmap FIXED anon @0x2ac75c000  -> gap zone: DEALLOC_GAP guard -> SIGKILL
 *      (if reached, report; death itself is the datum)
 *   5. non-FIXED hint mmap @0x2ac75c000 then FIXED over it -> gap-fill trick
 *
 * Build: clang -target arm64e-apple-macosx14 -isysroot <iOSSDK> -nostdlib
 *        -fno-stack-protector misc/srteardown.c -lSystem -Wl,-e,__start
 */
#include <stdint.h>
#include <signal.h>

static inline long svc6(int n,long a,long b,long c,long d,long e,long f){
    register long x0 asm("x0")=a,x1 asm("x1")=b,x2 asm("x2")=c,
                  x3 asm("x3")=d,x4 asm("x4")=e,x5 asm("x5")=f;
    register int  x16 asm("x16")=n; long err;
    asm volatile("svc #0x80\n\tcset %w[err],cs":"+r"(x0),[err]"=r"(err)
                 :"r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5),"r"(x16):"memory","cc");
    return err?-x0:x0;
}
static int sl(const char*s){int n=0;while(s[n])n++;return n;}
static void wr(const char*s){svc6(4,1,(long)s,sl(s),0,0,0);}
static void phex(unsigned long long v){char b[19];b[0]='0';b[1]='x';
    for(int i=0;i<16;i++){int d=(v>>((15-i)*4))&0xF;b[2+i]=d<10?'0'+d:'a'-10+d;}b[18]=0;wr(b);}
static void pd(long v){ if(v<0){wr("-");v=-v;} char b[21];int i=20;b[i]=0;
    do{b[--i]='0'+v%10;v/=10;}while(v&&i); wr(b+i); }

#ifdef SRTEARDOWN_STOPS
static void stop_phase(void){
    long pid=svc6(20,0,0,0,0,0,0);
    svc6(37,pid,SIGSTOP,0,0,0,0);
}
#endif

void _start(void){
    wr("\n[TD] shared_region teardown test\n");
    unsigned long long base=0xdeadbeef;
    long r = svc6(294,(long)&base,0,0,0,0,0);      /* check_np(&base) */
    wr("  check_np(&base) ret="); pd(r); wr(" base="); phex(base); wr("\n");
#ifdef SRTEARDOWN_STOPS
    wr("[TD] stop before teardown\n");
    stop_phase();
#endif

    r = svc6(294,0,0,0,0,0,0);                      /* check_np(NULL) */
    wr("  check_np(NULL)  ret="); pd(r); wr("\n");
#ifdef SRTEARDOWN_STOPS
    wr("[TD] stop after teardown\n");
    stop_phase();
#endif

    /* 3. FIXED anon mmap into ex-permanent range */
    r = svc6(197,0x180000000,0x4000,3,0x1012,-1,0); /* PROT_RW MAP_FIXED|ANON */
    wr("  mmap FIXED @0x180000000 -> "); phex(r); wr("\n");

    /* 4. gap-fill trick: non-fixed hint mmap at high VA, then FIXED over it */
    r = svc6(197,0x2ac75c000,0x4000,3,0x1002,-1,0); /* MAP_PRIVATE|ANON (hint) */
    wr("  mmap hint  @0x2ac75c000 -> "); phex(r); wr("\n");
    r = svc6(197,0x2ac75c000,0x4000,3,0x1012,-1,0); /* now FIXED over it */
    wr("  mmap FIXED @0x2ac75c000 -> "); phex(r); wr("\n");

    /* 5. re-check: region now empty? */
    base=0xdeadbeef;
    r = svc6(294,(long)&base,0,0,0,0,0);
    wr("  check_np(&base) ret="); pd(r); wr(" base="); phex(base); wr("\n");
    wr("[DONE]\n");
    svc6(1,0,0,0,0,0,0);
}
