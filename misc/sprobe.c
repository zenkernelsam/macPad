/*
 * sprobe.c — freestanding diagnostic for shared_region_map_and_slide_2_np (BSD #536)
 * on iOS 16.3 kernel, run inside the macOS chroot via launchdchrootexec.
 * Static/-nostdlib: no dyld dependency, raw SVC only.
 *
 * Stages:
 *   A) shared_region_check_np(&base) -> region state
 *   B) map_and_slide files_count=1 (main cache only, 8 in-bounds mappings)
 *   C) map_and_slide files_count=2 (main + fd=-1 dynregion at relocated addr)
 *   D) check_np(NULL) dealloc, then retry B
 *   E) retry with dynregion at ORIGINAL 0x2ac75c000 (out-of-bounds proof)
 *
 * Errno meaning (xnu-8792.81.2 vm_unix.c):
 *   22 EINVAL: no shared region / bad args / cs coverage fail
 *    1 EPERM : root_dir mismatch / fd unreadable / wrong mount / AMFI
 *   14 EFAULT: KERN_INVALID_ADDRESS (outside shared region)
 *   12 ENOMEM: KERN_NO_SPACE
 *    0      : success
 */

typedef unsigned long long u64;
typedef unsigned int u32;
typedef int i32;

#define O_RDONLY 0x0000

struct shared_file_np {
    i32  sf_fd;
    u32  sf_mappings_count;
    u32  sf_slide;
};

struct sfm_slide {
    u64 sms_address;
    u64 sms_size;
    u64 sms_file_offset;
    u64 sms_slide_size;
    u64 sms_slide_start;
    i32 sms_max_prot;
    i32 sms_init_prot;
};

#define VM_PROT_SLIDE 0x20

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
    return err ? -x0 : x0;   /* negative -> -errno */
}

#define SYS_exit        1
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_check_np    294    /* iOS 16.3: shared_region_check_np (xnu-8792 master:445) */
#define SYS_map_slide   536

static long sys_write(int fd, const void *p, long n) { return svc(SYS_write, fd, (long)p, n, 0, 0, 0); }
static long sys_open(const char *p, int fl)          { return svc(SYS_open, (long)p, fl, 0, 0, 0, 0); }
static long sys_read(int fd, void *p, long n)        { return svc(SYS_read, (int)fd, (long)p, n, 0, 0, 0); }
static long sys_close(int fd)                        { return svc(SYS_close, fd, 0, 0, 0, 0, 0); }
static void sys_exit(int c)                          { svc(SYS_exit, c, 0, 0, 0, 0, 0); __builtin_unreachable(); }

static int s_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void puts_(const char *s) { sys_write(1, s, s_len(s)); }

static void put_hex(u64 v)
{
    char b[19];
    b[0] = '0'; b[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int d = (int)(v >> ((15 - i) * 4)) & 0xF;
        b[2 + i] = d < 10 ? '0' + d : 'a' + d - 10;
    }
    b[18] = 0;
    puts_(b);
}

static void put_dec(long v)
{
    char b[24]; int i = 24;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    b[--i] = 0;
    do { b[--i] = '0' + (v % 10); v /= 10; } while (v);
    if (neg) b[--i] = '-';
    puts_(&b[i]);
}

static void kv(const char *k, long v) { puts_(k); put_dec(v); puts_("\n"); }
static void kvh(const char *k, u64 v) { puts_(k); put_hex(v); puts_("\n"); }

static u64 rd64(const void *p, u32 off) { return *(u64 *)((char *)p + off); }
static u32 rd32(const void *p, u32 off) { return *(u32 *)((char *)p + off); }

static const char *cache_paths[] = {
    "/System/Volumes/Preboot/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e",
    "/System/Library/dyld/dyld_shared_cache_arm64e",
    "/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64e",
    0
};

static char hdr[0x10000] __attribute__((aligned(16)));
static char dynregion[0x4000] __attribute__((aligned(16)));

static struct shared_file_np files[4];
static struct sfm_slide maps[64];

static long try_map(u32 nfiles, u32 nmaps)
{
    return svc(SYS_map_slide, nfiles, (long)files, nmaps, (long)maps, 0, 0);
}

void start(void)
{
    puts_("== sprobe ==\n");

    /* ---- A: region state ---- */
    u64 base = 0;
    long r = svc(SYS_check_np, (long)&base, 0, 0, 0, 0, 0);
    kv("check_np ret = ", r);
    kvh("check_np bas = ", base);

    /* ---- open cache ---- */
    long fd = -1;
    const char *used = 0;
    for (int i = 0; cache_paths[i]; i++) {
        fd = sys_open(cache_paths[i], O_RDONLY);
        if (fd >= 0) { used = cache_paths[i]; break; }
    }
    kv("cache fd     = ", fd);
    if (used) { puts_("cache path   = "); puts_(used); puts_("\n"); }
    if (fd < 0)
        sys_exit(2);

    long nr = sys_read((int)fd, hdr, sizeof(hdr));
    kv("header read  = ", nr);
    if (nr < 0x1000)
        sys_exit(3);

    puts_("magic        = ");
    sys_write(1, hdr, 16);
    puts_("\n");
    u32 mapOff   = rd32(hdr, 0x10);
    u32 mapCnt   = rd32(hdr, 0x14);
    u32 fmtVer   = rd32(hdr, 0xDC);
    kvh("mappingOff   = ", mapOff);
    kv("mappingCnt   = ", mapCnt);
    kv("formatVer    = ", fmtVer);

    /* dyld_cache_mapping_and_slide_info = 64B stride */
    if (mapOff + (u64)mapCnt * 64 > (u64)nr) {
        puts_("mapping table beyond buffer\n");
        sys_exit(4);
    }

    files[0].sf_fd = (i32)fd;
    files[0].sf_mappings_count = mapCnt;
    files[0].sf_slide = 0;

    u32 realCnt = mapCnt < 48 ? mapCnt : 48;
    for (u32 i = 0; i < realCnt; i++) {
        char *e = hdr + mapOff + (u64)i * 64;
        maps[i].sms_address     = rd64(e, 0x00);
        maps[i].sms_size        = rd64(e, 0x08);
        maps[i].sms_file_offset = rd64(e, 0x10);
        maps[i].sms_slide_start = rd64(e, 0x18);
        maps[i].sms_slide_size  = rd64(e, 0x20);
        u64 maxp = rd64(e, 0x30), initp = rd64(e, 0x38);
        if (maps[i].sms_slide_size)
            maxp |= VM_PROT_SLIDE;
        maps[i].sms_max_prot  = (i32)maxp;
        maps[i].sms_init_prot = (i32)initp;
        kvh("  map addr   = ", maps[i].sms_address);
        kvh("     size    = ", maps[i].sms_size);
        kv("     prots   = ", (((u64)(u32)maps[i].sms_max_prot) << 16) | (u32)maps[i].sms_init_prot);
    }

    /* ---- B: main cache only ---- */
    long mr = try_map(1, mapCnt);
    kv("B main-only  = ", mr);

    if (mr != 0) {
        /* ---- C: main + relocated dynregion entry ---- */
        files[1].sf_fd = -1;
        files[1].sf_mappings_count = 1;
        files[1].sf_slide = 0;
        maps[realCnt].sms_address     = 0x1FA000000ULL;   /* relocated, in-bounds */
        maps[realCnt].sms_size        = 0x4000;
        maps[realCnt].sms_file_offset = (u64)dynregion;
        maps[realCnt].sms_slide_size  = 0;
        maps[realCnt].sms_slide_start = 0;
        maps[realCnt].sms_max_prot    = 1;
        maps[realCnt].sms_init_prot   = 1;
        mr = try_map(2, mapCnt + 1);
        kv("C +dynreloc  = ", mr);

        /* ---- D: dealloc existing region, retry main-only ---- */
        long dr = svc(SYS_check_np, 0, 0, 0, 0, 0, 0);
        kv("D check_np(0)= ", dr);
        mr = try_map(1, mapCnt);
        kv("D retry main = ", mr);

        /* ---- E: dynregion at ORIGINAL out-of-bounds address ---- */
        maps[realCnt].sms_address = 0x2AC75C000ULL;
        mr = try_map(2, mapCnt + 1);
        kv("E dynorig    = ", mr);
    }

    if (mr == 0) {
        puts_("SUCCESS - cache mapped\n");
        u64 b2 = 0;
        long r2 = svc(SYS_check_np, (long)&b2, 0, 0, 0, 0, 0);
        kv("check_np2    = ", r2);
        kvh("check_np2bas = ", b2);
    }

    sys_close((int)fd);
    sys_exit(0);
}
