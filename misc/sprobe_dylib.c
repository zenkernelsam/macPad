/*
 * sprobe_dylib.c — shared-region syscall probe as a DYLD_INSERT_LIBRARIES dylib.
 * Raw SVC only — no libc dependency beyond open/read/write (which exist in
 * the on-disk libSystem stub). Returns raw -errno values.
 */
typedef long ssize_t;

typedef unsigned long long u64; typedef unsigned int u32; typedef int i32;

struct shared_file_np { i32 sf_fd; u32 sf_mappings_count; u32 sf_slide; };
struct sfm_slide { u64 sms_address, sms_size, sms_file_offset,
                   sms_slide_size, sms_slide_start; i32 sms_max_prot, sms_init_prot; };

static inline long svc(int n, long a0, long a1, long a2, long a3, long a4, long a5)
{
    register long x0 asm("x0") = a0;
    register long x1 asm("x1") = a1;
    register long x2 asm("x2") = a2;
    register long x3 asm("x3") = a3;
    register long x4 asm("x4") = a4;
    register long x5 asm("x5") = a5;
    register int  x16 asm("x16") = n;
    long err;
    asm volatile("svc #0x80\n\tcset %w[err], cs"
                 : "+r"(x0), [err]"=r"(err)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x16)
                 : "memory", "cc");
    return err ? -x0 : x0;
}
#define SYS_exit 1
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_check_np 294
#define SYS_map_slide 536

static int s_len(const char*s){int n=0;while(s[n])n++;return n;}
static void w(const char*s){svc(SYS_write,2,(long)s,s_len(s),0,0,0);}
static void puth(u64 v){char b[19];b[0]='0';b[1]='x';for(int i=0;i<16;i++){int d=(v>>((15-i)*4))&0xF;b[2+i]=d<10?'0'+d:'a'+d-10;}b[18]=0;w(b);}
static void putd(long v){char b[24];int i=24,neg=0;if(v<0){neg=1;v=-v;}b[--i]=0;do{b[--i]='0'+v%10;v/=10;}while(v);if(neg)b[--i]='-';w(&b[i]);}
static void kv(const char*k,long v){w(k);putd(v);w("\n");}
static void kvh(const char*k,u64 v){w(k);puth(v);w("\n");}
static u64 rd64(void*p,u32 o){return *(u64*)((char*)p+o);}
static u32 rd32(void*p,u32 o){return*(u32*)((char*)p+o);}

static char hdr[0x10000] __attribute__((aligned(16)));
static char dynregion[0x4000] __attribute__((aligned(16)));
static struct shared_file_np files[4];
static struct sfm_slide maps[64];

__attribute__((constructor))
static void sprobe_ctor(void)
{
    w("[sprobe] === start ===\n");
    u64 base=0;
    long r = svc(SYS_check_np,(long)&base,0,0,0,0,0);
    kv("[sprobe] A check_np ret = ", r);
    kvh("[sprobe] A base         = ", base);
    if (r==0 && base){ w("[sprobe]   magic: "); svc(SYS_write,2,(long)base,16,0,0,0); w("\n"); }

    long fd = svc(SYS_open,(long)"/System/Library/dyld/dyld_shared_cache_arm64e",0,0,0,0,0);
    kv("[sprobe] open fd        = ", fd);
    if (fd<0){ w("[sprobe] no cache file\n"); svc(SYS_exit,42,0,0,0,0,0); }
    long nr = svc(SYS_read,fd,(long)hdr,sizeof(hdr),0,0,0);
    kv("[sprobe] read hdr       = ", nr);
    w("[sprobe] magic          = "); svc(SYS_write,2,(long)hdr,16,0,0,0); w("\n");
    u32 mapOff=rd32(hdr,0x10), mapCnt=rd32(hdr,0x14);
    kv("[sprobe] mapCnt         = ", mapCnt);

    files[0].sf_fd=(i32)fd; files[0].sf_mappings_count=mapCnt; files[0].sf_slide=0;
    u32 rc = mapCnt<48?mapCnt:48;
    for(u32 i=0;i<rc;i++){
        char *e=hdr+mapOff+(u64)i*32;   /* dyld_cache_mapping_info = 32B */
        maps[i].sms_address=rd64(e,0); maps[i].sms_size=rd64(e,8);
        maps[i].sms_file_offset=rd64(e,0x10);
        maps[i].sms_slide_start=0; maps[i].sms_slide_size=0;
        maps[i].sms_max_prot=(i32)rd32(e,0x18); maps[i].sms_init_prot=(i32)rd32(e,0x1c);
        kvh("[sprobe]   map addr  = ", maps[i].sms_address);
    }

    long mr = svc(SYS_map_slide,1,(long)files,mapCnt,(long)maps,0,0);
    kv("[sprobe] B main-only    = ", mr);
    if (mr!=0){
        files[1].sf_fd=-1; files[1].sf_mappings_count=1; files[1].sf_slide=0;
        maps[rc].sms_address=0x1FA000000ULL; maps[rc].sms_size=0x4000;
        maps[rc].sms_file_offset=(u64)dynregion;
        maps[rc].sms_slide_size=0; maps[rc].sms_slide_start=0;
        maps[rc].sms_max_prot=1; maps[rc].sms_init_prot=1;
        mr = svc(SYS_map_slide,2,(long)files,mapCnt+1,(long)maps,0,0);
        kv("[sprobe] C +dyn reloc = ", mr);
        /* D: detach then retry main-only — CAREFUL: unmaps our shared region.
           do it LAST since it also unmaps live code of iOS-inherited pages */
        long dr = svc(SYS_check_np,0,0,0,0,0,0);
        kv("[sprobe] D detach ret = ", dr);
        mr = svc(SYS_map_slide,1,(long)files,mapCnt,(long)maps,0,0);
        kv("[sprobe] D retry main = ", mr);
        /* E: dynregion at original OOB VA (control) */
        maps[rc].sms_address=0x2AC75C000ULL;
        mr = svc(SYS_map_slide,2,(long)files,mapCnt+1,(long)maps,0,0);
        kv("[sprobe] E dyn orig   = ", mr);
    }
    if (mr==0){
        u64 b2=0; long r2=svc(SYS_check_np,(long)&b2,0,0,0,0,0);
        kv("[sprobe] post check   = ", r2); kvh("[sprobe] post base    = ", b2);
        if(b2){w("[sprobe] post magic  = ");svc(SYS_write,2,(long)b2,16,0,0,0);w("\n");}
    }
    w("[sprobe] === done ===\n");
    svc(SYS_exit,99,0,0,0,0,0);
}
