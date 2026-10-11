/* otest.c — raw SVC: open dir + openat + fstatat diagnostics, no libc. */
#include <stdint.h>

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
static void pnum(long v){char b[24];int i=24,ng=0;if(v<0){ng=1;v=-v;}b[--i]=0;do{b[--i]=48+v%10;v/=10;}while(v);if(ng)b[--i]=45;wr(&b[i]);}
static void kv(const char*k,long v){wr(k);pnum(v);wr("\n");}

int main(int argc,char**argv){
    const char*dir = argc>1?argv[1]:"/tmp/dsc";
    const char*nm  = argc>2?argv[2]:"dyld_shared_cache_arm64e";
    long dfd = svc6(5,(long)dir,0,0,0,0,0);          /* open(dir,0) */
    kv("open dir=",dfd);
    static uint8_t sb[256];
    long fs = svc6(469,(int)dfd,(long)nm,(long)sb,0,0,0); /* fstatat */
    kv("fstatat=",fs);
    long cfd = svc6(463,(int)dfd,(long)nm,0,0,0,0);  /* openat(dfd,name,0) */
    kv("openat=",cfd);
    long afd = svc6(5,(long)"/tmp/dsc/dyld_shared_cache_arm64e",0,0,0,0,0);
    kv("open abs /tmp/dsc=",afd);
    long mfd = svc6(5,(long)"/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e",0,0,0,0,0);
    kv("open abs cryptex=",mfd);
    long dfd2 = svc6(5,(long)"/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld",0,0,0,0,0);
    kv("open dir cryptex=",dfd2);
    return 0;
}
