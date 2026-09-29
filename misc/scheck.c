/* scheck.c — dynamic shared_region_check_np(294) ONLY. No map calls.
 * Run inside chroot WITHOUT DYLD_SHARED_CACHE_DIR: dyld finds no cache,
 * skips 536 entirely -> region bound but stays empty -> scheck reports it.
 * Prints "scheck ret=<n> base=<hex>":
 *   ret=12 region exists but empty / ret=0 populated (base set) / ret=22 none */
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
int main(void)
{
    unsigned long long base = 0;
    long r = syscall(294, &base);
    printf("scheck ret=%ld base=0x%llx\n", r, base);
    return 0;
}
