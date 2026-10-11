#include <stdint.h>

#ifndef CACHE_PAGE_LENGTH
#define CACHE_PAGE_LENGTH 0x4000
#endif

static inline long svc6(int n, long a, long b, long c, long d, long e, long f)
{
    register long x0 asm("x0") = a, x1 asm("x1") = b, x2 asm("x2") = c;
    register long x3 asm("x3") = d, x4 asm("x4") = e, x5 asm("x5") = f;
    register int x16 asm("x16") = n;
    long error;
    asm volatile("svc #0x80\n\tcset %w[error], cs" : "+r"(x0), [error] "=r"(error)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x16) : "memory", "cc");
    return error ? -x0 : x0;
}

static void out(const char *text)
{
    int n = 0;
    while (text[n]) ++n;
    svc6(4, 2, (long)text, n, 0, 0, 0);
}

static void hex(uint64_t value)
{
    char text[19] = "0x0000000000000000";
    for (int i = 17; i >= 2; --i) {
        int d = value & 15;
        text[i] = "0123456789abcdef"[d];
        value >>= 4;
    }
    svc6(4, 2, (long)text, 18, 0, 0, 0);
}

int main(void)
{
    long fd = svc6(5, (long)"/var/mnt/rootfs/private/tmp/dsc/dyld_shared_cache_arm64e", 0, 0, 0, 0, 0);
    out("[fixed] open="); hex(fd); out("\n");
    if (fd < 0) return 1;
    long cleared = svc6(294, 0, 0, 0, 0, 0, 0);
    out("[fixed] check_np(NULL)="); hex(cleared); out("\n");
    if (cleared < 0) return 1;
#ifdef CACHE_MAP_PREFIX
    const struct { uint64_t address, size, offset; int prot; } prefix[] = {
        {0x180000000, 0x67f5c000, 0, 5},
        {0x1e7f5c000, 0x1e90000, 0x67f5c000, 1},
        {0x1ebdec000, 0x239c000, 0x69dec000, 3},
    };
    for (unsigned i = 0; i < sizeof(prefix) / sizeof(prefix[0]); ++i) {
        long result = svc6(197, prefix[i].address, prefix[i].size, prefix[i].prot,
                           0x40012, fd, prefix[i].offset);
        out("[fixed] prefix="); hex(result); out("\n");
        if (result != (long)prefix[i].address) svc6(1, 3, 0, 0, 0, 0, 0);
    }
#endif
    long mapped = svc6(197, 0x1ee188000, CACHE_PAGE_LENGTH, 3, 0x40012, fd, 0x6c188000);
    out("[fixed] mmap="); hex(mapped); out("\n");
    if (mapped != 0x1ee188000) svc6(1, 2, 0, 0, 0, 0, 0);
#ifdef CACHE_STOP_BEFORE_READ
    svc6(37, svc6(20, 0, 0, 0, 0, 0, 0), 17, 0, 0, 0, 0);
#endif
    out("[fixed] reading\n");
    unsigned char byte = *(volatile unsigned char *)0x1ee188000;
    out("[fixed] value="); hex(byte); out("\n");
    svc6(1, 0, 0, 0, 0, 0, 0);
    return 0;
}
