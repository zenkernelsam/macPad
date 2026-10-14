// macPad diagnostic scaffold: a macOS-platform CydiaSubstrate.dylib for the
// Ventura chroot rootfs.
//
// libmachook links MSHookFunction/MSHookMessageEx/MSGetImageByName/
// MSFindSymbol through @rpath/CydiaSubstrate.framework/CydiaSubstrate.
// The iOS ElleKit CydiaSubstrate on this device is built for platform iOS
// and dyld rejects it inside a macOS process ("incompatible platform
// (have 'iOS', need 'macOS')"). The author's original 13.4 rootfs shipped
// a macOS-built copy we do not have; this scaffold re-creates the ABI
// surface so the authentic-dyld CLI milestone can proceed.
//
// Status: SCAFFOLD, not a fix.
//   * MSGetImageByName: real implementation (dyld image-name walk).
//   * MSFindSymbol: real implementation — LC_DYLD_INFO export-trie walk
//     (dyld-4 shared-cache images carry no usable LC_SYMTAB), with LC_SYMTAB
//     fallback and strict bounds checks.
//   * MSHookFunction: REAL minimal trampoline implementation
//     (2026-10-15, R001→R002 repair). 16-byte absolute-branch patch
//     (ldr x17,#8 / br x17 / .quad) over the first 4 instructions plus a
//     thunk holding the stolen bytes and a branch back to symbol+16.
//     FAILS CLOSED (*result=NULL, target untouched) if any stolen
//     instruction is PC-relative — those need real relocation fixups.
//   * MSHookMessageEx: NO-OP stub (method-swizzle not implemented).

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/nlist.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <objc/message.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

const struct mach_header *MSGetImageByName(const char *file) {
    if (!file) return NULL;
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        const char *name = _dyld_get_image_name(i);
        if (name && strstr(name, file))
            return _dyld_get_image_header(i);
    }
    return NULL;
}

// Read a dyld-format uleb128. Returns false if the buffer ends early.
static bool read_uleb(const uint8_t **p, const uint8_t *end, uint64_t *out) {
    uint64_t result = 0;
    int shift = 0;
    while (*p < end) {
        uint8_t b = **p;
        (*p)++;
        result |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) {
            *out = result;
            return true;
        }
        shift += 7;
        if (shift > 63) return false;
    }
    return false;
}

// Recursive export-trie lookup. `acc` carries the matched name prefix.
// Child offsets are relative to the trie ROOT (`base`), not the parent
// node. Returns the image-offset of the exported symbol or NULL.
static const uint8_t *trie_find_addr(const uint8_t *base,
                                     const uint8_t *node,
                                     const uint8_t *trie_end,
                                     const char *name,
                                     char *acc, size_t acc_len,
                                     size_t acc_cap, int depth) {
    if (depth > 128 || !node || node >= trie_end) return NULL;
    const uint8_t *p = node;
    uint64_t size;
    if (!read_uleb(&p, trie_end, &size)) return NULL;
    if (size) {
        const uint8_t *end = p + size;
        if (end > trie_end) return NULL;
        uint64_t flags, addr;
        if (!read_uleb(&p, end, &flags)) return NULL;
        if (!read_uleb(&p, end, &addr)) return NULL;
        if (acc_len && !strcmp(acc, name))
            return (const uint8_t *)(uintptr_t)addr;
    }
    if (p >= trie_end) return NULL;
    uint8_t nchildren = *p++;
    for (uint8_t c = 0; c < nchildren; c++) {
        const uint8_t *label = p;
        while (p < trie_end && *p) p++;
        if (p >= trie_end) return NULL;
        size_t llen = (size_t)(p - label);
        p++;
        uint64_t off;
        if (!read_uleb(&p, trie_end, &off)) return NULL;
        if (acc_len + llen >= acc_cap) continue;
        if (strncmp(name + acc_len, (const char *)label, llen)) continue;
        memcpy(acc + acc_len, label, llen);
        acc[acc_len + llen] = '\0';
        const uint8_t *r = trie_find_addr(
            base, base + off, trie_end, name,
            acc, acc_len + llen, acc_cap, depth + 1);
        acc[acc_len] = '\0';
        if (r) return r;
    }
    return NULL;
}

static const char *image_path_for_header(const void *header) {
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++)
        if (_dyld_get_image_header(i) == header)
            return _dyld_get_image_name(i);
    return NULL;
}

static void *find_sym_in_image(const struct mach_header_64 *mh,
                               const char *name) {
    if (!mh || !name || !*name) return NULL;
    if (mh->magic != MH_MAGIC_64) return NULL;
    if (mh->ncmds == 0 || mh->ncmds > 4096) return NULL;

    // Preferred path: let dyld itself resolve the export. dlopen with
    // RTLD_NOLOAD returns the already-loaded image without side effects,
    // and dlsym walks its real export trie inside libdyld.
    const char *path = image_path_for_header(mh);
    if (path) {
        void *handle = dlopen(path, RTLD_NOLOAD | RTLD_LAZY);
        if (handle) {
            void *sym = dlsym(handle, name + (name[0] == '_' ? 1 : 0));
            if (sym) return sym;
            sym = dlsym(handle, name);
            if (sym) return sym;
        }
    }

    uintptr_t slide = 0;
    uintptr_t le_lo = 0, le_hi = 0;
    const struct dyld_info_command *dyldinfo = NULL;
    const struct linkedit_data_command *exports_trie = NULL;
    const struct symtab_command *symtab = NULL;
    const uint8_t *lc = (const uint8_t *)(mh + 1);
    const uint8_t *lc_end = lc + mh->sizeofcmds;
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (lc + sizeof(struct load_command) > lc_end) return NULL;
        const struct load_command *cmd = (const struct load_command *)lc;
        if (cmd->cmdsize < sizeof(struct load_command) ||
            lc + cmd->cmdsize > lc_end)
            return NULL;
        if (cmd->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg =
                (const struct segment_command_64 *)lc;
            uintptr_t seg_slide = (uintptr_t)mh - (uintptr_t)seg->vmaddr;
            if (!strncmp(seg->segname, "__TEXT", 7))
                slide = seg_slide;
            if (!strncmp(seg->segname, "__LINKEDIT", 11)) {
                le_lo = (uintptr_t)seg->vmaddr + seg_slide;
                le_hi = le_lo + (uintptr_t)seg->vmsize;
            }
        } else if (cmd->cmd == LC_DYLD_INFO ||
                   cmd->cmd == LC_DYLD_INFO_ONLY) {
            dyldinfo = (const struct dyld_info_command *)lc;
        } else if (cmd->cmd == LC_DYLD_EXPORTS_TRIE) {
            exports_trie = (const struct linkedit_data_command *)lc;
        } else if (cmd->cmd == LC_SYMTAB) {
            symtab = (const struct symtab_command *)lc;
        }
        lc += cmd->cmdsize;
    }

    // Prefer the export trie — shared-cache images have no usable symtab.
    uint32_t trie_off = exports_trie ? exports_trie->dataoff
                                     : (dyldinfo ? dyldinfo->export_off : 0);
    uint32_t trie_size = exports_trie ? exports_trie->datasize
                                      : (dyldinfo ? dyldinfo->export_size : 0);
    if (trie_off && trie_size) {
        const uint8_t *trie = (const uint8_t *)(slide + trie_off);
        const uint8_t *trie_end = trie + trie_size;
        // Only walk when the whole region lies inside the image's own
        // mapped __LINKEDIT; anything else is a malformed foreign header.
        if (le_lo && (uintptr_t)trie >= le_lo &&
            (uintptr_t)trie_end <= le_hi && trie_end > trie) {
            char acc[1024];
            acc[0] = '\0';
            const uint8_t *off = trie_find_addr(
                trie, trie, trie_end, name, acc, 0, sizeof(acc), 0);
            if (off)
                return (void *)((uintptr_t)mh + (uintptr_t)off);
        }
    }

    // LC_SYMTAB fallback (non-cache images).
    if (symtab) {
        const struct nlist_64 *nl =
            (const struct nlist_64 *)(slide + symtab->symoff);
        const char *strtab = (const char *)(slide + symtab->stroff);
        const char *str_end = strtab + symtab->strsize;
        if (!le_lo || (uintptr_t)nl < le_lo ||
            (uintptr_t)(nl + symtab->nsyms) > le_hi ||
            (uintptr_t)strtab < le_lo || (uintptr_t)str_end > le_hi)
            return NULL;
        for (uint32_t i = 0; i < symtab->nsyms; i++) {
            const char *sname = strtab + nl[i].n_un.n_strx;
            if (sname < strtab || sname >= str_end) continue;
            if (!strcmp(sname, name) && (nl[i].n_type & N_TYPE) == N_SECT)
                return (void *)(slide + nl[i].n_value);
        }
    }
    return NULL;
}

void *MSFindSymbol(const void *image, const char *name) {
    if (image)
        return find_sym_in_image((const struct mach_header_64 *)image, name);
    if (!name || !*name) return NULL;
    void *global = dlsym(RTLD_DEFAULT,
                         name + (name[0] == '_' ? 1 : 0));
    if (!global && name[0] == '_')
        global = dlsym(RTLD_DEFAULT, name);
    if (global) return global;
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        void *sym = find_sym_in_image(
            (const struct mach_header_64 *)_dyld_get_image_header(i), name);
        if (sym) return sym;
    }
    return NULL;
}

// Real minimal trampoline hook (arm64/arm64e).
//
// Patch written over symbol[0..16):
//     ldr  x17, #8          ; x17 = *(pc+8) = replace
//     br   x17
//     .quad replace
// Orig-callable thunk (fresh RX page):
//     <stolen 16 bytes verbatim>
//     ldr  x17, #8
//     br   x17
//     .quad symbol+16
//
// Fails closed (target untouched, *result=NULL) when any of the four
// stolen instructions is PC-relative — verbatim copies would compute the
// wrong address from the thunk. Verified safe for
// CGXSenderCanSynthesizeEvents whose window is
// pacibsp/sub/stp/add (SkyLight 22F82 +0x318464).
static int macws_insn_pc_relative(uint32_t i) {
    if ((i & 0x7c000000) == 0x14000000) return 1;   // b / bl
    if ((i & 0xff000010) == 0x54000000) return 1;   // b.cond
    if ((i & 0x7e000000) == 0x34000000) return 1;   // cbz / cbnz
    if ((i & 0x7e000000) == 0x36000000) return 1;   // tbz / tbnz
    if ((i & 0x9f000000) == 0x90000000) return 1;   // adrp
    if ((i & 0x9f000000) == 0x10000000) return 1;   // adr
    if ((i & 0x3b000000) == 0x18000000) return 1;   // ldr/prfm literal
    return 0;
}

extern void sys_icache_invalidate(void *addr, size_t len);

// arm64e callers may hand us PAC-signed pointers; a signed address stored
// in the .quad slot or dereferenced as code crashes. Strip both sides.
#if __arm64e__
#include <ptrauth.h>
#define MACWS_PTRSTRIP(p) ptrauth_strip((p), ptrauth_key_asia)
#else
#define MACWS_PTRSTRIP(p) (p)
#endif

void MSHookFunction(void *symbol, void *replace, void **result) {
    if (result) *result = NULL;
    if (!symbol || !replace) return;
    symbol = MACWS_PTRSTRIP(symbol);
    replace = MACWS_PTRSTRIP(replace);
    const uint32_t *src = (const uint32_t *)symbol;
    for (int i = 0; i < 4; i++)
        if (macws_insn_pc_relative(src[i])) return;   // fail closed

    // Thunk: stolen bytes + branch back to symbol+16.
    uint8_t *thunk = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
    if (thunk == MAP_FAILED) return;
    uint32_t *tp = (uint32_t *)thunk;
    memcpy(tp, src, 16);
    tp[4] = 0x58000051;                              // ldr x17, #8
    tp[5] = 0xd61f0220;                              // br x17
    *(uint64_t *)(tp + 6) = (uint64_t)((char *)symbol + 16);
    if (mprotect(thunk, 4096, PROT_READ | PROT_EXEC) != 0) {
        munmap(thunk, 4096);
        return;
    }
    sys_icache_invalidate(thunk, 32);

    // Patch: absolute branch to replace over symbol[0..16).
    vm_address_t page = (vm_address_t)symbol & ~0x3fffULL;
    vm_size_t span = ((vm_address_t)symbol + 16 - page + 0x3fff) & ~0x3fffULL;
    if (vm_protect(mach_task_self(), page, span, FALSE,
                   VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY)
            != KERN_SUCCESS &&
        vm_protect(mach_task_self(), page, span, FALSE,
                   VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS) {
        munmap(thunk, 4096);
        return;
    }
    uint32_t *dp = (uint32_t *)symbol;
    dp[0] = 0x58000051;                              // ldr x17, #8
    dp[1] = 0xd61f0220;                              // br x17
    *(uint64_t *)(dp + 2) = (uint64_t)replace;
    vm_protect(mach_task_self(), page, span, FALSE,
               VM_PROT_READ | VM_PROT_EXECUTE);
    sys_icache_invalidate(symbol, 16);
#if __arm64e__
    // Callers invoke *result via blraaz — authenticate with key IA and
    // zero discriminator. Sign the thunk the same way or the indirect
    // call faults at authentication (verified: probe emits blraaz x8).
    if (result)
        *result = ptrauth_sign_unauthenticated(thunk, ptrauth_key_asia, 0);
#else
    if (result) *result = thunk;
#endif
}

void MSHookMessageEx(Class _class, SEL sel, IMP imp, IMP *result) {
    Method m = class_getInstanceMethod(_class, sel);
    if (result) *result = m ? method_getImplementation(m) : NULL;
    (void)imp;
}
