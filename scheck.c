/* scheck.c — dynamic, direct SVC shared_region_check_np(294) ONLY.
 * Prints "scheck ret=<n> base=<hex>":
 *   ret=12 region exists but empty / ret=0 populated / ret=22 none */
#include <stdio.h>
int main(void)
{
    unsigned long long base = 0;
    register unsigned long long x0 asm("x0") = (unsigned long long)&base;
    register int x16 asm("x16") = 294;
    long err;
    asm volatile("svc #0x80\n\tcset %w[err], cs"
                 : "+r"(x0), [err]"=r"(err) : "r"(x16) : "memory","cc");
    long r = err ? -x0 : x0;
    printf("scheck ret=%ld base=0x%llx\n", r, base);
    return 0;
}
