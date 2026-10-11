// cache_page_copy.c - diagnostic: does an ordinary file mmap of the physical
// dyld_shared_cache file survive a VM_PROT_COPY protect, in a task that keeps
// its native shared region (libc/MIG stay usable)?
//
// Maps 9 pages (0x24000) of the on-device cache file at file offset
// 0x6c188000 as MAP_PRIVATE RW, optionally applies
// vm_protect(..., VM_PROT_READ|VM_PROT_WRITE|VM_PROT_COPY), optionally
// performs exactly one private 8-byte store at the mapping start (a volatile
// uint64_t write + readback, exercising native write/COW rather than reads
// alone; MAP_PRIVATE on an O_RDONLY fd - the cache file is never written),
// then reads the first byte of each 16K page. Every page byte is logged
// explicitly so mere process survival is not taken as evidence of success.
//
// LIMITATION: intentionally a non-fixed VA in this process's OWN task -
// this is NOT claimed equivalent to the chrooted macOS dyld task's shared
// region (different task, different VM map, no shared-region teardown).
//
// Own resources only: munmap + close at the end; the cache file is opened
// O_RDONLY and never written. On vm_protect failure the process exits with a
// distinct code BEFORE touching any page.
//
// Build: clang -target arm64-apple-ios14 \
//          -isysroot ~/theos/sdks/iPhoneOS16.5.sdk -O2 -Wall -Wextra \
//          -o cache_page_copy misc/cache_page_copy.c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <mach/mach.h>

/* mach_vm_region not exported on iOS SDK - declare manually;
 * vm_region.h (via mach.h) already provides the info structs/macros */
typedef unsigned long long mach_vm_address_t;
typedef unsigned long long mach_vm_size_t;
extern kern_return_t mach_vm_region(vm_map_t, mach_vm_address_t *,
    mach_vm_size_t *, vm_region_flavor_t, vm_region_info_t,
    mach_msg_type_number_t *, mach_port_t *);

extern int csops(pid_t, unsigned int, void *, size_t);

#define MAP_LEN ((mach_vm_size_t)0x24000)
#define MAP_OFF ((off_t)0x6c188000)
#define PAGE_SZ ((mach_vm_size_t)0x4000)

static void query_region(mach_vm_address_t va, const char *tag)
{
    {
        mach_vm_address_t a = va;
        mach_vm_size_t sz = 0;
        struct vm_region_basic_info_64 info;
        mach_port_t obj = MACH_PORT_NULL;
        mach_msg_type_number_t ic = VM_REGION_BASIC_INFO_COUNT_64;
        kern_return_t kr = mach_vm_region(mach_task_self(), &a, &sz,
            VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &ic, &obj);
        if (kr == KERN_SUCCESS)
            fprintf(stderr,
                "[%s basic] 0x%llx..0x%llx prot=%x/%x off=0x%llx shared=%d\n",
                tag, a, a + sz, info.protection, info.max_protection,
                info.offset, info.shared);
        else
            fprintf(stderr, "[%s basic] kr=%d at 0x%llx\n", tag, kr, va);
        if (obj != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), obj);
    }
    {
        mach_vm_address_t a = va;
        mach_vm_size_t sz = 0;
        vm_region_extended_info_data_t info;
        mach_port_t obj = MACH_PORT_NULL;
        mach_msg_type_number_t ic = VM_REGION_EXTENDED_INFO_COUNT;
        kern_return_t kr = mach_vm_region(mach_task_self(), &a, &sz,
            VM_REGION_EXTENDED_INFO, (vm_region_info_t)&info, &ic, &obj);
        if (kr == KERN_SUCCESS)
            fprintf(stderr,
                "[%s ext] 0x%llx..0x%llx tag=%u resident=%u external=%u shadow=%u mode=%u ref=%u\n",
                tag, a, a + sz, info.user_tag, info.pages_resident,
                info.external_pager, info.shadow_depth, info.share_mode,
                info.ref_count);
        else
            fprintf(stderr, "[%s ext] kr=%d at 0x%llx\n", tag, kr, va);
        if (obj != MACH_PORT_NULL) mach_port_deallocate(mach_task_self(), obj);
    }
}

int main(int argc, char **argv)
{
    setvbuf(stderr, NULL, _IONBF, 0);

    int copy_mode = 0, write_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--copy") == 0 && !copy_mode)
            copy_mode = 1;
        else if (strcmp(argv[i], "--write") == 0 && !write_mode)
            write_mode = 1;
        else {
            fprintf(stderr, "usage: %s [--copy] [--write]\n", argv[0]);
            return 1;
        }
    }
    const char *mode = copy_mode ? (write_mode ? "copy-write" : "copy")
                                 : (write_mode ? "write" : "ordinary");

    const char *path = "/var/mnt/rootfs/private/tmp/dsc/dyld_shared_cache_arm64e";
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "[*] open %s failed errno=%d (%s)\n",
                path, errno, strerror(errno));
        return 2;
    }

    void *m = mmap(NULL, MAP_LEN, PROT_READ | PROT_WRITE, MAP_PRIVATE,
                   fd, MAP_OFF);
    if (m == MAP_FAILED) {
        fprintf(stderr, "[*] mmap failed errno=%d (%s)\n", errno, strerror(errno));
        close(fd);
        return 3;
    }

    unsigned flags = 0;
    int cs_rc = csops(getpid(), 0, &flags, sizeof(flags));
    fprintf(stderr, "[*] mode=%s pid=%d csops rc=%d flags=0x%x errno=%d\n",
            mode, getpid(), cs_rc, flags, cs_rc ? errno : 0);
    fprintf(stderr, "[*] mapping va=%p len=0x%llx file_off=0x%llx\n",
            m, (unsigned long long)MAP_LEN, (unsigned long long)MAP_OFF);

    mach_vm_address_t base = (mach_vm_address_t)m;
    query_region(base, "pre");

    if (copy_mode) {
        kern_return_t kr = vm_protect(mach_task_self(), (vm_address_t)m,
            (vm_size_t)MAP_LEN, FALSE,
            VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
        fprintf(stderr, "[*] vm_protect COPY kr=%d\n", kr);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr, "[*] protect failed - exiting WITHOUT reading pages\n");
            munmap(m, MAP_LEN);
            close(fd);
            return 4;
        }
    }

    /* same observation point in all modes: after COPY (if any), before reads */
    query_region(base, "pre-read");

    /* one private 8-byte store at mapping start + volatile readback.
     * MAP_PRIVATE on an O_RDONLY fd: COW page only, cache file untouched. */
    if (write_mode) {
        volatile uint64_t *w = (volatile uint64_t *)m;
        const uint64_t expected = 0x1122334455667788ULL;
        fprintf(stderr, "[write] storing64 va=0x%llx expected=0x%llx\n",
                base, (unsigned long long)expected);
        *w = expected;
        uint64_t got = *w;
        fprintf(stderr, "[write] readback64 va=0x%llx got=0x%llx\n",
                base, (unsigned long long)got);
        if (got != expected) {
            fprintf(stderr, "[*] readback mismatch - exiting\n");
            munmap(m, MAP_LEN);
            close(fd);
            return 5;
        }
    }

    volatile unsigned char *p = (volatile unsigned char *)m;
    for (int i = 0; i < (int)(MAP_LEN / PAGE_SZ); i++)
        fprintf(stderr, "[page %d] va=0x%llx byte0=0x%02x\n",
                i, base + (mach_vm_address_t)i * PAGE_SZ, p[i * PAGE_SZ]);

    query_region(base, "post-read");

    munmap(m, MAP_LEN);
    close(fd);
    fprintf(stderr, "[*] done mode=%s\n", mode);
    return 0;
}
