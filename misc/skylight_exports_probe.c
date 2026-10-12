// Enumerate export-trie symbols of an already-loaded (or dlopen-able) image.
// Usage: skylight_exports_probe <dyld-image-substring> <name-substring> [name-substring...]
// Prints every exported symbol whose name contains any of the substrings.
// Read-only diagnostic — no interposes, no writes.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

static int read_uleb(const uint8_t **pp, const uint8_t *end, uint64_t *out) {
    uint64_t v = 0; unsigned shift = 0;
    while (*pp < end) {
        uint8_t b = **pp; (*pp)++;
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) { *out = v; return 1; }
        shift += 7; if (shift > 63) return 0;
    }
    return 0;
}

static int want(const char *name, char **filters, int nf) {
    for (int i = 0; i < nf; i++)
        if (strstr(name, filters[i])) return 1;
    return 0;
}

static void trie_dump(const uint8_t *base, const uint8_t *node,
                      const uint8_t *trie_end, char *acc, size_t acc_len,
                      size_t acc_cap, int depth,
                      char **filters, int nf, unsigned long *count) {
    if (depth > 128 || !node || node >= trie_end) return;
    const uint8_t *p = node;
    uint64_t size;
    if (!read_uleb(&p, trie_end, &size)) return;
    if (size) {
        const uint8_t *end = p + size;
        if (end > trie_end) return;
        uint64_t flags, addr;
        if (!read_uleb(&p, end, &flags)) return;
        if (!read_uleb(&p, end, &addr)) return;
        if (acc_len && want(acc, filters, nf)) {
            printf("EXPORT %s @%llx\n", acc, (unsigned long long)addr);
            (*count)++;
        }
    }
    if (p >= trie_end) return;
    uint8_t nchildren = *p++;
    for (uint8_t c = 0; c < nchildren; c++) {
        const uint8_t *label = p;
        while (p < trie_end && *p) p++;
        if (p >= trie_end) return;
        size_t llen = (size_t)(p - label);
        p++;
        uint64_t off;
        if (!read_uleb(&p, trie_end, &off)) return;
        if (acc_len + llen >= acc_cap) continue;
        memcpy(acc + acc_len, label, llen);
        acc[acc_len + llen] = '\0';
        trie_dump(base, base + off, trie_end, acc, acc_len + llen,
                  acc_cap, depth + 1, filters, nf, count);
        acc[acc_len] = '\0';
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <image-substring> <filter> [filter...]\n", argv[0]);
        return 2;
    }
    const char *img_match = argv[1];
    uint32_t count = _dyld_image_count();
    const struct mach_header_64 *mh = NULL;
    const char *found_path = NULL;
    for (uint32_t i = 0; i < count; i++) {
        const char *name = _dyld_get_image_name(i);
        if (name && strstr(name, img_match)) {
            mh = (const struct mach_header_64 *)_dyld_get_image_header(i);
            found_path = name;
            break;
        }
    }
    if (!mh) {
        void *h = dlopen(img_match, RTLD_LAZY | RTLD_LOCAL);
        if (h) {
            for (uint32_t i = 0; i < _dyld_image_count(); i++) {
                const char *name = _dyld_get_image_name(i);
                if (name && strstr(name, img_match)) {
                    mh = (const struct mach_header_64 *)_dyld_get_image_header(i);
                    found_path = name;
                    break;
                }
            }
        }
    }
    if (!mh) { fprintf(stderr, "image not found: %s\n", img_match); return 1; }
    fprintf(stderr, "image: %s @%p\n", found_path, mh);

    uintptr_t slide = 0, le_lo = 0, le_hi = 0;
    const struct dyld_info_command *dyldinfo = NULL;
    const struct linkedit_data_command *exports = NULL;
    const uint8_t *lc = (const uint8_t *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *cmd = (const struct load_command *)lc;
        if (cmd->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const void *)lc;
            if (!strncmp(seg->segname, "__TEXT", 7))
                slide = (uintptr_t)mh - seg->vmaddr;
            if (!strncmp(seg->segname, "__LINKEDIT", 11)) {
                le_lo = seg->vmaddr + slide;
                le_hi = le_lo + seg->vmsize;
            }
        } else if (cmd->cmd == LC_DYLD_INFO || cmd->cmd == LC_DYLD_INFO_ONLY)
            dyldinfo = (const void *)lc;
        else if (cmd->cmd == LC_DYLD_EXPORTS_TRIE)
            exports = (const void *)lc;
        lc += cmd->cmdsize;
    }
    uint32_t t_off = exports ? exports->dataoff : (dyldinfo ? dyldinfo->export_off : 0);
    uint32_t t_sz  = exports ? exports->datasize : (dyldinfo ? dyldinfo->export_size : 0);
    if (!t_off || !t_sz) { fprintf(stderr, "no export trie\n"); return 1; }
    uintptr_t t_addr = 0;
    if (le_lo) {
        uintptr_t foff_in_le = (uintptr_t)t_off - ((uintptr_t)le_lo - slide - 0) /* unused */;
        (void)foff_in_le;
        // __LINKEDIT file offsets map linearly: le_vaddr + (t_off - le_fileoff)
        // but simpler: dyld cache images map LINKEDIT at (le_lo - le_fileoff + t_off)? Compute via fileoff:
    }
    // Correct mapping: file offset t_off lives in __LINKEDIT: addr = le_vmaddr_start + slide + (t_off - le_fileoff)
    // le_lo already includes slide. Need le_fileoff:
    lc = (const uint8_t *)(mh + 1);
    uintptr_t le_fileoff = 0;
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *cmd = (const struct load_command *)lc;
        if (cmd->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const void *)lc;
            if (!strncmp(seg->segname, "__LINKEDIT", 11)) {
                le_fileoff = seg->fileoff;
                le_lo = seg->vmaddr + slide;
                le_hi = le_lo + seg->vmsize;
            }
        }
        lc += cmd->cmdsize;
    }
    t_addr = le_lo + ((uintptr_t)t_off - le_fileoff);
    const uint8_t *base = (const uint8_t *)t_addr;
    const uint8_t *end = base + t_sz;
    char acc[1024]; acc[0] = 0;
    unsigned long n = 0;
    trie_dump(base, base, end, acc, 0, sizeof(acc), 0, &argv[2], argc - 2, &n);
    fprintf(stderr, "matched %lu exports\n", n);
    return 0;
}
