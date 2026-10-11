// exec_fault_test.c — mmap a cache file page PROT_EXEC and force an exec
// fault by branching into it. Tells us whether the kernel rejects the *file*
// page (CS problem) — vs the anon-shadow theory. Diagnostic only.
// usage: exec_fault_test <file> <fileoff_hex>   (default first cache page)
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <setjmp.h>
#include <libkern/OSCacheControl.h>
#include <errno.h>

static sigjmp_buf jb;
static void h(int s, siginfo_t *si, void *c) {
    fprintf(stderr, "[sig] %d code=%d addr=%p\n", s, si->si_code, si->si_addr);
    siglongjmp(jb, 1);
}

int main(int argc, char **argv) {
    const char *fp = argc > 1 ? argv[1] : "/var/mnt/rootfs/macdsc/dyld_shared_cache_arm64e";
    uint64_t fo = argc > 2 ? strtoull(argv[2], 0, 16) : 0;
    int fd = open(fp, O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }
    /* try dyld's recipe: MAP_TPRO makes iOS allow PROT_EXEC on trusted file */
    void *p = mmap(NULL, 0x4000, PROT_READ | PROT_EXEC,
                   MAP_FILE | MAP_PRIVATE | MAP_TPRO, fd, fo);
    fprintf(stderr, "[*] mmap TPRO -> %p errno=%d\n", p, errno);
    if (p == MAP_FAILED) {
        p = mmap(NULL, 0x4000, PROT_READ | PROT_EXEC, MAP_FILE | MAP_PRIVATE, fd, fo);
        fprintf(stderr, "[*] mmap plain -> %p errno=%d\n", p, errno);
    }
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    fprintf(stderr, "[*] mapped %s off %#llx -> %p\n", fp, (unsigned long long)fo, p);
    /* touch for read first (validates page at read-fault) */
    fprintf(stderr, "[*] read byte: %02x\n", ((volatile unsigned char *)p)[0]);

    struct sigaction sa = {0};
    sa.sa_sigaction = h; sa.sa_flags = SA_SIGINFO;
    sigaction(SIGBUS, &sa, 0); sigaction(SIGSEGV, &sa, 0);
    sigaction(SIGILL, &sa, 0); sigaction(SIGTRAP, &sa, 0);

    if (sigsetjmp(jb, 1) == 0) {
        /* force an instruction-fetch fault on the page */
        void (*f)(void) = (void (*)(void))((char *)p + 0x10);
        sys_icache_invalidate(p, 0x4000);
        f();
        fprintf(stderr, "[*] EXEC SURVIVED (returned!)\n");
    } else {
        fprintf(stderr, "[*] exec fault caught by handler\n");
    }
    return 0;
}
