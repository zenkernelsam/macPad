// vadiff.c — read child VA via task_for_pid+mach_vm_read_overwrite and diff
// against a file range. Diagnostic only.
// usage: vadiff <pid> <vaddr_hex> <len> <file> <fileoff_hex>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>
kern_return_t mach_vm_read_overwrite(vm_map_t, mach_vm_address_t, mach_vm_size_t,
                                     mach_vm_address_t, mach_vm_size_t *);

int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: %s pid vaddr len file fileoff\n", argv[0]); return 1; }
    pid_t pid = atoi(argv[1]);
    vm_address_t va = strtoull(argv[2], 0, 16);
    vm_size_t n = (vm_size_t)strtoull(argv[3], 0, 0);
    const char *fp = argv[4];
    uint64_t fo = strtoull(argv[5], 0, 16);

    task_t tp = 0;
    kern_return_t kr = task_for_pid(mach_task_self(), pid, &tp);
    fprintf(stderr, "task_for_pid kr=%#x port=%u\n", kr, tp);
    if (kr) return 1;

    char *buf = malloc(n);
    mach_vm_size_t out = 0;
    kr = mach_vm_read_overwrite(tp, va, n, (mach_vm_address_t)buf, &out);
    fprintf(stderr, "read kr=%#x out=%llu\n", kr, (unsigned long long)out);
    if (kr) return 1;

    FILE *f = fopen(fp, "rb");
    if (!f) { perror("fopen"); return 1; }
    char *want = malloc(n);
    fseek(f, (long)fo, SEEK_SET);
    size_t got = fread(want, 1, n, f);
    fclose(f);
    fprintf(stderr, "file read %zu\n", got);

    size_t lim = out < got ? out : got, nd = 0;
    for (size_t i = 0; i < lim; i++)
        if (buf[i] != want[i]) {
            if (nd < 24)
                fprintf(stderr, "  +%#zx got %02x want %02x\n", i,
                        (unsigned char)buf[i], (unsigned char)want[i]);
            nd++;
        }
    fprintf(stderr, "diff bytes: %zu (of %zu)\n", nd, lim);
    return 0;
}
