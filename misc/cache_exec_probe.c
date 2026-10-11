// cache_exec_probe: decisive single-variable test for cache page exec validation.
//   open cache -> F_ADDFILESIGS_RETURN -> mmap RX -> [data read] -> jump into page
// run under run_dbg_hold_v2 to capture the exec fault:
//   pc==target && code0==0x32 => kernel rejected exec pagein (cs fail)
//   pc elsewhere / SIGILL     => page was entered executable (validation passed)
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef F_ADDFILESIGS_RETURN
#define F_ADDFILESIGS_RETURN 97
#endif

typedef struct fsig {
    off_t   fs_file_start;
    void   *fs_blob_start;
    size_t  fs_blob_size;
    size_t  fs_fsignatures_size;
    char    fs_cdhash[20];
    int     fs_hash_type;
} fsig_t;

int main(int argc, char **argv)
{
    const char *path   = argc > 1 ? argv[1] : "/var/mnt/rootfs/macdsc/dyld_shared_cache_arm64e";
    off_t       sigoff = argc > 2 ? strtoull(argv[2], 0, 0) : 0xa160c000;
    size_t      sigsz  = argc > 3 ? strtoull(argv[3], 0, 0) : 0x50c000;
    off_t       pageoff= argc > 4 ? strtoull(argv[4], 0, 0) : 0x47c000;   /* 16k aligned */
    int         mode   = argc > 5 ? atoi(argv[5]) : 0;   /* 0=exec fresh, 1=read then exec */

    int fd = open(path, O_RDONLY);
    if (fd < 0) { dprintf(2, "CEP open errno=%d\n", errno); return 1; }

    fsig_t fs; memset(&fs, 0, sizeof(fs));
    fs.fs_file_start = 0;
    fs.fs_blob_start = (void *)(uintptr_t)sigoff;
    fs.fs_blob_size  = sigsz;
    int r = fcntl(fd, F_ADDFILESIGS_RETURN, &fs);
    dprintf(2, "CEP fcntl r=%d errno=%d\n", r, r < 0 ? errno : 0);

    void *p = mmap(NULL, 0x4000, PROT_READ | PROT_EXEC, MAP_PRIVATE, fd, pageoff);
    dprintf(2, "CEP mmap_rx p=%p errno=%d\n", p, p == MAP_FAILED ? errno : 0);
    if (p == MAP_FAILED) return 1;

    volatile unsigned char b;
    if (mode == 1) {
        b = *((volatile unsigned char *)p + 0xdc9c);
        dprintf(2, "CEP dataread byte=0x%02x\n", b);
    }

    void (*f)(void) = (void (*)(void))((char *)p + 0xdc9c);
    dprintf(2, "CEP jump target=%p\n", (void *)f);
    f();
    dprintf(2, "CEP returned unexpectedly\n");
    return 0;
}
