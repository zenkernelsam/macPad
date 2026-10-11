/* mregions2.c — find REAL mapped content (nonzero pages + Mach-O headers)
 * in [0x100000000,0x300000000) at _start under macOS dyld in the chroot.
 * Oracle: mprotect(p,16k,RW); ENOMEM=hole; else read *(u32*)p directly.
 * Reports ranges containing nonzero data + any 0xfeedfacf Mach-O headers
 * (dyld image, leftovers, commpage...).
 *
 * Build: clang -target arm64e-apple-macosx14 -isysroot $SDK -nostdlib \
 *          -fno-stack-protector misc/mpriv.c -lSystem -Wl,-e,__start
 */
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
static void phex(unsigned long long v){char b[19];b[0]='0';b[1]='x';
    for(int i=0;i<16;i++){int d=(v>>((15-i)*4))&0xF;b[2+i]=d<10?'0'+d:'a'-10+d;}b[18]=0;wr(b);}

void _start(void){
    wr("\n[SCAN2] content-scan 0x100000000..0x300000000\n");
    unsigned long long rstart=0; int inrange=0;
    for (unsigned long long p=0x100000000ULL; p<0x300000000ULL; p+=0x4000){
        long m = svc6(74,(long)p,0x4000,3,0,0,0);       /* mprotect RW */
        int has = 0;
        if (m==0){
            /* readable now — look for nonzero first words + mach-o magic */
            volatile unsigned int *q=(volatile unsigned int*)p;
            unsigned int w0=q[0];
            if (w0==0xfeedfacf){ wr("  *MACH-O* @");phex(p);wr("\n"); }
            if (w0 || q[1] || q[2] || q[3]) has=1;
        }
        if (has && !inrange){rstart=p;inrange=1;}
        if (!has && inrange){wr("  content [");phex(rstart);wr(" .. ");
            phex(p);wr(")\n");inrange=0;}
    }
    if (inrange){wr("  content [");phex(rstart);wr(" .. end)\n");}
    wr("[DONE]\n");
    svc6(1,0,0,0,0,0,0);
}
