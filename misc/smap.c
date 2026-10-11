/* smap.c — dynamic, raw SVC only (write/open/pread/fstat/536/294). No libc deps beyond start.
 *
 * Replicates dyld's mapSplitCacheSystemWide() syscall-536 arguments:
 *   files = { main(8 mappings), .01(7 mappings), anon(fd=-1,1 mapping) }
 *   anon mapping = the DynamicRegion page at sharedRegionStart+dynamicDataOffset
 *   (0x2ac75c000 for this cache), size 0x4000, sms_file_offset = user VA of a
 *   synthesized DyldSharedCache::DynamicRegion blob ("dyld_data    v3").
 */
#include <stdint.h>

struct sf_np { uint32_t fd, count, slide; };
struct m48  { uint64_t va,size,foff,f24,f32; uint32_t f40,f44; };

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
static void phex(unsigned long long v){char b[19];b[0]=48;b[1]=120;for(int i=0;i<16;i++){int d=(int)((v>>((15-i)*4))&0xF);b[2+i]=d<10?48+d:87+d;}b[18]=0;wr(b);}
static void kv(const char*k,long v){wr(k);pnum(v);wr("\n");}
static void kvh(const char*k,unsigned long long v){wr(k);phex(v);wr("\n");}
static void z(void*p,long n){char*c=p;while(n--)c[n]=0;}

int main(int argc,char**argv){
    const char*path=argc>1?argv[1]:"/tmp/dsc/dyld_shared_cache_arm64e";
    const char*path01=argc>2?argv[2]:"/tmp/dsc/dyld_shared_cache_arm64e.01";

    /* --- DynamicRegion blob (DyldSharedCache::DynamicRegion, 0x4000) --- */
    static uint8_t dynbuf[0x4000];
    const char*mag="dyld_data    v3";
    for(int i=0;i<16;i++)dynbuf[i]=mag[i];              /* +0  magic */
    /* +16 FileIdTuple {st_dev, st_ino} via fstat */
    int fd=(int)svc6(5,(long)path,0,0,0,0,0);
    kv("fd=",fd);
    if(fd<0)return 1;
    { static uint8_t sb[256]; z(sb,sizeof sb);
      long fs=svc6(189,fd,(long)sb,0,0,0,0);           /* fstat */
      kv("fstat=",fs);
      if(fs==0){ *(uint32_t*)(dynbuf+16)=*(uint32_t*)(sb+0);   /* st_dev */
                 *(uint64_t*)(dynbuf+24)=*(uint64_t*)(sb+8);   /* st_ino */ } }
    *(uint32_t*)(dynbuf+32)=0;                          /* osCryptexPathOffset */
    *(uint32_t*)(dynbuf+36)=80;                         /* cachePathOffset */
    *(uint64_t*)(dynbuf+40)=0;                          /* paddingToAlign */
    *(uint64_t*)(dynbuf+48)=0;*(uint64_t*)(dynbuf+56)=0;/* systemWideFlags */
    *(uint64_t*)(dynbuf+64)=0;*(uint64_t*)(dynbuf+72)=0;/* processorFlags */
    { int i=0; while(path[i]&&i<0x4000-81){dynbuf[80+i]=path[i];i++;} dynbuf[80+i]=0; }

    /* --- main cache header --- */
    static uint8_t hdr[0x400];
    long nr=svc6(153,fd,(long)hdr,sizeof hdr,0,0,0);
    kv("pread=",nr);
    if(nr<0x200)return 1;
    uint32_t moff=*(uint32_t*)(hdr+0x10),mcnt=*(uint32_t*)(hdr+0x14);
    uint64_t dynoff=*(uint64_t*)(hdr+0x1F0);            /* dynamicDataOffset */
    uint64_t dynVA=0x180000000+dynoff;
    kv("mcnt=",mcnt); kvh("dynVA=",dynVA);
    static struct{uint64_t va,size,foff;uint32_t maxP,initP;}tab[16];
    long nr2=svc6(153,fd,(long)tab,32*mcnt,moff,0,0);
    kv("pread2=",nr2);
    static struct m48 m[17]; z(m,sizeof m);
    int n=mcnt<16?mcnt:16;
    for(int i=0;i<n;i++){m[i].va=tab[i].va;m[i].size=tab[i].size;m[i].foff=tab[i].foff;
        m[i].f40=tab[i].maxP;m[i].f44=tab[i].initP;
        kvh("  va=",m[i].va);kvh("   sz=",m[i].size);kvh("  maxP=",m[i].f40);kvh("  iniP=",m[i].f44);}

    /* --- .01 subcache: header + all its mappings --- */
    int fd2=(int)svc6(5,(long)path01,0,0,0,0,0);
    kv("fd2=",fd2);
    int n2=0;
    int want01=argc>4?(argv[4][0]-48):7;   /* 0..7 = cap on .01 mappings */
    if(fd2>=0&&want01>0){
        static uint8_t h2[0x400]; svc6(153,fd2,(long)h2,sizeof h2,0,0,0);
        uint32_t mo2=*(uint32_t*)(h2+0x10),mc2=*(uint32_t*)(h2+0x14);
        kv("mcnt2=",mc2);
        static struct{uint64_t va,size,foff;uint32_t maxP,initP;}t2[8];
        if(mc2>8)mc2=8; if(mc2>want01)mc2=want01;
        svc6(153,fd2,(long)t2,32*mc2,mo2,0,0);
        for(int i=0;i<mc2;i++){int j=n+i;m[j].va=t2[i].va;m[j].size=t2[i].size;
            m[j].foff=t2[i].foff;m[j].f40=t2[i].maxP;m[j].f44=t2[i].initP;
            kvh("  .01 va=",m[j].va);kvh("    sz=",m[j].size);
            kvh("    maxP=",m[j].f40);kvh("    iniP=",m[j].f44);}
        n2=mc2;
    }

    /* --- files array: {main, .01, anon(fd=-1)} --- */
    static struct sf_np files[3];
    files[0].fd=fd;files[0].count=n;files[0].slide=0;
    uint32_t fc=1, mc=n;
    int base2=n;
    if(n2>0){files[fc].fd=fd2;files[fc].count=n2;files[fc].slide=0;fc++;mc+=n2;base2+=n2;}
    /* anon dynregion entry — verbatim from dyld disasm:
       va=dynVA, size=0x4000, foff=dynbuf, f24=0,f32=0, qword@+40=0x100000001 */
    int anon=argc>3?(argv[3][0]-48):1;
    if(anon){
        m[base2].va=dynVA;m[base2].size=0x4000;
        m[base2].foff=(uint64_t)dynbuf;m[base2].f24=0;m[base2].f32=0;
        m[base2].f40=1;m[base2].f44=0x10000000;
        files[fc].fd=-1;files[fc].count=1;files[fc].slide=0; fc++; mc++; }
    kv("fc=",fc);kv("mc=",mc);

    /* clear VM_PROT_SLIDE (0x20) on all entries: we supply no slide info, so
     * a SLIDE bit would send the kernel down vm_shared_region_slide →
     * copyin(sms_slide_start=0) → EFAULT */
    for(int i=0;i<16;i++){m[i].f40&=~0x20u;m[i].f44&=~0x20u;}
    wr("READY - waiting /tmp/go\n");
    for(;;){struct{long s,ns;}ts={0,50000000};int g=(int)svc6(5,(long)"/tmp/go",0,0,0,0,0);
        if(g>=0){svc6(6,g,0,0,0,0,0);break;}svc6(334,(long)&ts,0,0,0,0,0);}
    wr("calling 536...\n");
    long r=svc6(536,fc,(long)files,mc,(long)m,0,0);
    kv("536 raw=",r);
    unsigned long long base=0; long c=svc6(294,(long)&base,0,0,0,0,0);
    kv("check_np=",c);kvh("base=",base);
    if(r==0&&c==0&&base){
        /* self-verify: base = slid VA of first mapping; dynregion at base+dynoff */
        kvh("hdr@base=",*(volatile uint64_t*)base);
        kvh("dynmagic=",*(volatile uint64_t*)(base+dynoff));
        kvh("last01=",*(volatile uint64_t*)(m[n+n2-1].va-0x180000000+base));
    }
    wr("DONE\n");
    return 0;
}
