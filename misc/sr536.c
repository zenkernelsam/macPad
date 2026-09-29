// sr536.c — direct __shared_region_map_and_slide_2_np caller.
// Replicates dyld's exact syscall shape so we can bisect the kernel
// rejection without rebuilding dyld.
// usage: sr536 <cachefile> [mode]
//   mode: full (default) = 8 entries verbatim + dyn entry
//         m6      = first 6 entries only
//         noexec  = entry[0] prot r-x -> r--
//         slide=N = files[0].slide window = N (hex)
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>

struct sf_np { uint32_t fd, mappings_count, slide; };           // 12B
struct map48 {                                                    // 48B
    uint64_t va, size, foff, f24, f32;
    uint32_t f40, f44;
};

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/var/mnt/rootfs/macdsc/dyld_shared_cache_arm64e";
    const char *mode = argc > 2 ? argv[2] : "full";
    uint64_t slide = argc > 3 ? strtoull(argv[3], 0, 0) : 0;

    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }

    // read header mapping table: dyld_cache_mapping_info 32B records
    uint8_t hdr[0x400];
    pread(fd, hdr, sizeof(hdr), 0);
    uint32_t moff = *(uint32_t *)(hdr + 0x10), mcnt = *(uint32_t *)(hdr + 0x14);
    struct { uint64_t va, size, foff; uint32_t maxP, initP; } tab[16];
    pread(fd, tab, 32 * mcnt, moff);

    int n = mcnt;
    if (!strcmp(mode, "m6")) n = 6;
    if (!strcmp(mode, "m1")) n = 1;

    struct map48 m[16];
    memset(m, 0, sizeof(m));
    for (int i = 0; i < n; i++) {
        m[i].va = tab[i].va; m[i].size = tab[i].size; m[i].foff = tab[i].foff;
        m[i].f40 = tab[i].maxP; m[i].f44 = tab[i].initP;
        if (!strcmp(mode, "noexec") && i == 0) { m[i].f40 = 1; m[i].f44 = 1; }
    }
    // dynamic region entry (files[1], count=1, anon-ish flags like dyld's)
    // dyld's dyn entry: va=dynVA, size, foff=0, +24=dynRegion, +40/+44 flags 0x100000001
    m[n].va = 0x2ac75c000; m[n].size = 0x4000; m[n].foff = 0;
    m[n].f24 = 0; m[n].f32 = 0; m[n].f40 = 0x100000001; m[n].f44 = 0x100000001;

    struct sf_np files[2];
    files[0].fd = fd; files[0].mappings_count = n; files[0].slide = slide;
    files[1].fd = -1; files[1].mappings_count = 1; files[1].slide = 0;

    fprintf(stderr, "[sr536] calling 536 files=%d maps=%d slide_win=%llx\n", 2, n + 1, (unsigned long long)slide);
    errno = 0;
    long r = syscall(536, 2, files, n + 1, m);
    fprintf(stderr, "syscall(536) r=%ld errno=%d(%s)\n", r, errno, strerror(errno));
    return 0;
}
