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
//   * MSFindSymbol: real implementation — LC_DYLD_INFO/LC_DYLD_EXPORTS_TRIE
//     walk, with an LC_SYMTAB fallback for local (non-exported) symbols.
//     Both paths convert the linker's FILE offset through the live
//     __LINKEDIT mapping (`slide + LINKEDIT.vmaddr + (off - fileoff)`).
//     That distinction is load-bearing for dyld shared-cache images, where
//     `slide + fileoff` addresses unrelated memory; it is also what lets
//     local N_SECT symbols (e.g. SkyLight's _audit_token_check_tcc_access)
//     resolve at all.
//   * MSHookFunction: minimal trampoline implementation PLUS an
//     explicit permit whitelist (R003-F02 containment).  A patch is
//     installed only for targets pre-registered via MACWSHookPermit()
//     with (EXPECTED image LC_UUID, image-relative offset, expected
//     16-byte prologue); registration fails when the live UUID does not
//     match the supplied pin.  Refused or failed calls return the
//     original callable symbol pointer (the exact no-op-scaffold
//     contract) and log a marker, so the ~70 unvetted MSHookFunction
//     call sites in libmachook keep their previous pass-through
//     behavior.  This is NOT presented as a general substrate repair:
//     no relocation fixups, no thread-quiescence protocol, atomicity is
//     limited to caller-chosen quiescent scope (ctor/add-image time).
//   * MSHookMessageEx: real exact-class method swizzle via libobjc's
//     class_replaceMethod; returns the previously invoked IMP. Methods that
//     are only inherited are overridden on the requested class, so a
//     superclass implementation is never mutated. Missing class/selector/
//     replacement fails closed and installs nothing. No thread-quiescence
//     protocol beyond libobjc's own method-cache flush. See the
//     implementation for the precise contract and its limits.

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/nlist.h>
#include <mach-o/loader.h>
#include <mach/mach.h>
#include <objc/message.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

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

// Convert a Mach-O LINKER FILE offset (LC_SYMTAB symoff/stroff, export-trie
// dataoff, ...) to the live address inside this image's mapped __LINKEDIT.
//
// dyld maps a segment at `slide + segment.vmaddr`, while the linker stores
// every file-backed offset relative to the segment's `fileoff`. Those two
// coordinate systems coincide only when `vmaddr - fileoff` is constant
// across segments, which is not true for images inside a dyld shared
// cache. Using `slide + fileoff` there reads unrelated cache bytes, so a
// local symbol resolves to NULL even though the Mach-O data is present.
// Everything reachable through this helper is bounded by `filesize` (not
// `vmsize`): the tail of LINKEDIT is zero-fill and must never be walked.
static const uint8_t *linkedit_file_ptr(
        const struct segment_command_64 *linkedit, uintptr_t slide,
        uint64_t file_offset, uint64_t size) {
    if (!linkedit || !size) return NULL;
    if (file_offset < linkedit->fileoff) return NULL;
    uint64_t delta = file_offset - linkedit->fileoff;
    if (delta > linkedit->filesize ||
        size > (uint64_t)linkedit->filesize - delta)
        return NULL;
    return (const uint8_t *)(slide + (uintptr_t)linkedit->vmaddr + delta);
}

static void *find_sym_in_image(const struct mach_header_64 *mh,
                               const char *name) {
    if (!mh || !name || !*name) return NULL;
    if (mh->magic != MH_MAGIC_64) return NULL;
    if (mh->ncmds == 0 || mh->ncmds > 4096) return NULL;

    // Preferred path: let dyld itself resolve the export. dlopen with
    // RTLD_NOLOAD returns the already-loaded image without side effects,
    // and dlsym walks its real export trie inside libdyld. Local symbols
    // are not exported, so they fall through to the manual walk below.
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

    uintptr_t text_vmaddr = 0;
    int have_text = 0;
    const struct segment_command_64 *linkedit = NULL;
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
            // segname is a fixed 16-byte, NUL-padded field; comparing all
            // 16 bytes keeps "__TEXT" from also matching "__TEXT_EXEC".
            if (!have_text && !strncmp(seg->segname, "__TEXT", 16)) {
                text_vmaddr = (uintptr_t)seg->vmaddr;
                have_text = 1;
            } else if (!linkedit &&
                       !strncmp(seg->segname, "__LINKEDIT", 16)) {
                linkedit = seg;
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

    // The slide is defined by __TEXT only. Any image without a real
    // __LINKEDIT (or whose header is not mapped at/above its vmaddr) fails
    // closed rather than guessing an address.
    if (!have_text || text_vmaddr > (uintptr_t)mh) return NULL;
    uintptr_t slide = (uintptr_t)mh - text_vmaddr;
    if (!linkedit) return NULL;

    // Export trie first — it covers exported symbols of cache images.
    uint32_t trie_off = exports_trie ? exports_trie->dataoff
                                     : (dyldinfo ? dyldinfo->export_off : 0);
    uint32_t trie_size = exports_trie ? exports_trie->datasize
                                      : (dyldinfo ? dyldinfo->export_size : 0);
    const uint8_t *trie = linkedit_file_ptr(
        linkedit, slide, trie_off, trie_size);
    if (trie) {
        const uint8_t *trie_end = trie + trie_size;
        char acc[1024];
        acc[0] = '\0';
        const uint8_t *off = trie_find_addr(
            trie, trie, trie_end, name, acc, 0, sizeof(acc), 0);
        if (off)
            return (void *)((uintptr_t)mh + (uintptr_t)off);
    }

    // LC_SYMTAB fallback: the only path to local (non-exported) symbols.
    // N_TYPE == N_SECT accepts both external and local defined symbols;
    // the reference provider likewise does not require N_EXT.
    if (symtab && symtab->nsyms) {
        const struct nlist_64 *nl = (const struct nlist_64 *)
            linkedit_file_ptr(linkedit, slide, symtab->symoff,
                              (uint64_t)symtab->nsyms * sizeof(*nl));
        const char *strtab = (const char *)
            linkedit_file_ptr(linkedit, slide, symtab->stroff,
                              symtab->strsize);
        if (!nl || !strtab) return NULL;
        // Linked images store the symbol's UNSLID vmaddr in n_value; the
        // __TEXT slide is therefore the correct runtime adjustment.
        for (uint32_t i = 0; i < symtab->nsyms; i++) {
            uint32_t strx = nl[i].n_un.n_strx;
            if (strx == 0 || strx >= symtab->strsize) continue;
            const char *sname = strtab + strx;
            size_t remain = (size_t)symtab->strsize - strx;
            if (!memchr(sname, '\0', remain)) continue;
            if (!strcmp(sname, name) &&
                (nl[i].n_type & N_TYPE) == N_SECT)
                return (void *)(slide + (uintptr_t)nl[i].n_value);
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

// Terminal register transfers inside the stolen window. These are copied
// verbatim into the thunk (they are position-independent) and suppress the
// appended branch-back — control never falls through them. The blr/blra*
// call variants are additionally distinguished at relocation time and
// refused, since they return into the window and have not been exercised.
static int macws_insn_terminal(uint32_t i) {
    if ((i & 0xfffffc1f) == 0xd65f0000) return 1;   // ret Xn
    if (i == 0xd65f0bff || i == 0xd65f0fff) return 1; // retaa / retab
    if ((i & 0xfffffc1f) == 0xd61f0000) return 1;   // br Xn
    if ((i & 0xfffffc1f) == 0xd63f0000) return 1;   // blr Xn
    // Authenticated register branches (arm64e), zero-discriminator forms:
    // mask drops the Rn field (bits [9:5]); op4=11111 selects *Z encoding.
    if ((i & 0xfffffc1f) == 0xd61f081f) return 1;   // braaz Xn
    if ((i & 0xfffffc1f) == 0xd61f0c1f) return 1;   // brabz Xn
    if ((i & 0xfffffc1f) == 0xd63f081f) return 1;   // blraaz Xn
    if ((i & 0xfffffc1f) == 0xd63f0c1f) return 1;   // blrabz Xn
    if (i == 0xd69f03e0) return 1;                  // eret
    return 0;
}

// ── R003-F02 permit whitelist ─────────────────────────────────────
// MSHookFunction only patches targets a callsite has registered with
// exact (expected image UUID, image-relative offset, expected 16-byte
// prologue).  The caller supplies the KNOWN-BUILD uuid it derived the
// offset from; registration fails (-2) when the live image's LC_UUID
// differs, so an offset can never be applied against an unverified
// binary.  Call sites do the RE validation; the provider re-checks the
// identity and bytes at patch time, so version drift or a wrong offset
// fails closed.
#define MACWS_MAX_HOOK_PERMITS 32
struct macws_hook_permit {
    uint8_t  uuid[16];        // expected LC_UUID identity pin
    uint64_t image_offset;    // symbol - image base
    uint32_t expected[4];     // first 4 instructions at symbol
};
static struct macws_hook_permit g_permits[MACWS_MAX_HOOK_PERMITS];
static int g_permit_count;

static const uint8_t *macws_lc_uuid(const struct mach_header_64 *h) {
    if (!h || h->magic != MH_MAGIC_64) return NULL;
    const struct load_command *lc = (const void *)(h + 1);
    for (uint32_t i = 0; i < h->ncmds; i++) {
        if (lc->cmd == LC_UUID)
            return ((const struct uuid_command *)lc)->uuid;
        lc = (const void *)((const char *)lc + lc->cmdsize);
    }
    return NULL;
}

// Register a permitted hook target. `image_base` is the load address of
// the image (mach_header); `expected_uuid16` is the known-build identity
// pin the offset was derived from.  Returns 0 armed, -1 bad args / byte
// mismatch, -2 live-UUID-vs-pin mismatch, -3 no LC_UUID, -4 table full.
// NOTE: versioned name — the 4-arg permit contract.  Callers resolve
// this exact symbol by dlsym; an older provider exposing only the
// 3-arg MACWSHookPermit is detected as absent and fails closed, so a
// mixed-deployed intermediate build cannot be silently misused.
int MACWSHookPermitV2(const void *image_base, const void *expected_uuid16,
                      uint64_t image_offset, const uint32_t expected[4]) {
    if (!image_base || !expected_uuid16 || !expected) return -1;
    if (g_permit_count >= MACWS_MAX_HOOK_PERMITS) return -4;
    const uint8_t *live_uuid =
        macws_lc_uuid((const struct mach_header_64 *)image_base);
    if (!live_uuid) return -3;
    if (memcmp(live_uuid, expected_uuid16, 16) != 0) return -2;
    // Registration-time prologue check: a wrong offset fails HERE rather
    // than silently arming a permit that would corrupt some other code.
    if (memcmp((const uint8_t *)image_base + image_offset,
               expected, 16) != 0) return -1;
    struct macws_hook_permit *p = &g_permits[g_permit_count++];
    memcpy(p->uuid, expected_uuid16, 16);
    p->image_offset = image_offset;
    memcpy(p->expected, expected, 16);
    return 0;
}

static const struct macws_hook_permit *macws_permit_for(
        const void *symbol, const struct mach_header_64 **out_hdr) {
    // Find the image that contains symbol by walking dyld's image list
    // and comparing against each image's load range is unreliable for
    // shared-cache images (no per-image VM extent query), so match on
    // (uuid, offset): compute offset against every loaded image and a
    // match requires both uuid equality AND expected bytes present.
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        const struct mach_header_64 *h =
            (const struct mach_header_64 *)_dyld_get_image_header(i);
        if (!h || (uintptr_t)symbol < (uintptr_t)h) continue;
        uint64_t off = (uintptr_t)symbol - (uintptr_t)h;
        if (off >= 0x2000000) continue;          // implausible offset
        for (int p = 0; p < g_permit_count; p++) {
            if (g_permits[p].image_offset != off) continue;
            // verify uuid matches this image
            const struct load_command *lc = (const void *)(h + 1);
            const uint8_t *uuid = NULL;
            for (uint32_t c = 0; c < h->ncmds; c++) {
                if (lc->cmd == LC_UUID) {
                    uuid = ((const struct uuid_command *)lc)->uuid;
                    break;
                }
                lc = (const void *)((const char *)lc + lc->cmdsize);
            }
            if (uuid && !memcmp(uuid, g_permits[p].uuid, 16)) {
                *out_hdr = h;
                return &g_permits[p];
            }
        }
    }
    return NULL;
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

static void macws_hook_log(const char *msg) {
    // Direct write(): fprintf may be filtered by the host process.
    if (msg) write(2, msg, strlen(msg));
}

void MSHookFunction(void *symbol, void *replace, void **result) {
    // Refusal contract: preserve the original scaffold behavior — the
    // caller receives a callable pointer to the unpatched symbol (the
    // PAC-signed pointer as passed in, not the stripped form) so legacy
    // call sites that unconditionally invoke *result keep working. A
    // refused hook is still a no-op; it is just no longer silent.
    void *const orig_symbol = symbol;
    if (result) *result = NULL;
    if (!symbol || !replace) return;
    symbol = MACWS_PTRSTRIP(symbol);
    replace = MACWS_PTRSTRIP(replace);
    const uint32_t *src = (const uint32_t *)symbol;

#define MACWS_REFUSE(why) do {                                          \
        Dl_info _macws_di;                                              \
        if (dladdr(symbol, &_macws_di) && _macws_di.dli_fbase) {        \
            char _macws_buf[384];                                       \
            snprintf(_macws_buf, sizeof(_macws_buf),                    \
                "MACWSHook: REFUSED " why " target=%p image=%s+%#llx " \
                "sym=%s\n",                                             \
                orig_symbol,                                            \
                _macws_di.dli_fname ? _macws_di.dli_fname : "?",        \
                (unsigned long long)((uintptr_t)symbol -                \
                    (uintptr_t)_macws_di.dli_fbase),                    \
                _macws_di.dli_sname ? _macws_di.dli_sname : "?");       \
            macws_hook_log(_macws_buf);                                 \
        } else {                                                        \
            char _macws_buf[160];                                       \
            snprintf(_macws_buf, sizeof(_macws_buf),                    \
                "MACWSHook: REFUSED " why " target=%p\n",               \
                orig_symbol);                                           \
            macws_hook_log(_macws_buf);                                 \
        }                                                               \
        if (result) *result = orig_symbol;                              \
        return;                                                         \
    } while (0)

    // R003-F02 containment: only whitelisted (image-uuid, offset,
    // expected-prologue) targets may be patched. Unregistered call
    // sites fail closed, preserving prior no-op behavior for unvetted
    // sites.
    const struct mach_header_64 *hdr = NULL;
    const struct macws_hook_permit *permit =
        macws_permit_for(symbol, &hdr);
    if (!permit) MACWS_REFUSE("no permit for target");
    if (memcmp(src, permit->expected, 16) != 0)
        MACWS_REFUSE("prologue drift");

    // Exact-relocation thunk. Each stolen instruction is translated to
    // position-independent thunk code:
    //   * position-independent insns copy verbatim;
    //   * cbz/cbnz become an inverted-condition skip over an absolute
    //     ldr+br pair carrying the ORIGINAL absolute target;
    //   * terminal register branches/returns copy verbatim and suppress
    //     the appended branch-back (control never falls through);
    //   * blr-family insns (calls that return into the window) and every
    //     other PC-relative class (b/bl/b.cond/tbz/adr/adrp/ldr-lit) are
    //     refused — mis-retargeting them would silently corrupt the window.
    // Every failure path from here on must still hand back a callable
    // original — the refusal contract applies to install failures too
    // (a NULL result would crash callers that invoke *result).
    uint8_t *thunk = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANON, -1, 0);
    if (thunk == MAP_FAILED)
        MACWS_REFUSE("thunk mmap failed");
    uint32_t *tp = (uint32_t *)thunk;
    const uint64_t sym_pc = (uint64_t)(uintptr_t)symbol;
    int control_leaves = 0;
    for (int i = 0; i < 4 && !control_leaves; i++) {
        const uint32_t insn = src[i];
        const uint64_t pc = sym_pc + (uint64_t)i * 4;
        if ((insn & 0x7e000000u) == 0x34000000u) {   // cbz / cbnz
            const int32_t imm19 = ((int32_t)insn << 8) >> 13;
            const uint64_t target = pc + (int64_t)imm19 * 4;
            // A target inside the stolen window would land inside the
            // patch stub once symbol[0..16) is overwritten — refuse
            // rather than silently re-routing it.
            if (target >= sym_pc && target < sym_pc + 16) {
                munmap(thunk, 4096);
                MACWS_REFUSE("cbz/cbnz target inside stolen window");
            }
            // invert condition; imm19=5 skips ldr+br+.quad (4 slots) and
            // lands on the next emitted instruction
            *tp++ = ((insn ^ (1u << 24)) & ~(0x7ffffu << 5)) | (5u << 5);
            *tp++ = 0x58000051;                      // ldr x17, #8
            *tp++ = 0xd61f0220;                      // br x17
            *(uint64_t *)tp = target; tp += 2;
        } else if (macws_insn_terminal(insn)) {
            if ((insn & 0xfffffc1f) == 0xd63f0000 ||   // blr Xn
                (insn & 0xfffffc1f) == 0xd63f081f ||   // blraaz Xn
                (insn & 0xfffffc1f) == 0xd63f0c1f) {   // blrabz Xn
                munmap(thunk, 4096);
                MACWS_REFUSE("indirect call in stolen window");
            }
            *tp++ = insn;                            // verbatim
            control_leaves = 1;
        } else if (macws_insn_pc_relative(insn)) {
            munmap(thunk, 4096);
            MACWS_REFUSE("unrelocatable PC-relative in stolen window");
        } else {
            *tp++ = insn;                            // verbatim
        }
    }
    if (!control_leaves) {
        *tp++ = 0x58000051;                          // ldr x17, #8
        *tp++ = 0xd61f0220;                          // br x17
        *(uint64_t *)tp = sym_pc + 16; tp += 2;
    }
    const size_t thunk_used = (size_t)((char *)tp - (char *)thunk);
    if (mprotect(thunk, 4096, PROT_READ | PROT_EXEC) != 0) {
        munmap(thunk, 4096);
        MACWS_REFUSE("thunk mprotect RX failed");
    }
    sys_icache_invalidate(thunk, thunk_used);

#ifdef MACWS_RELOC_TESTONLY
    // Test-only thunk-inspection build: return the raw thunk before the
    // target patch so host tests can verify emitted bytes without needing
    // writable code pages. Never defined in production builds.
    if (result) *result = thunk;
    return;
#endif

    // Patch: absolute branch to replace over symbol[0..16).
    // Caller responsibility: invoke at a quiescent scope (ctor /
    // add-image callback before the target runs). No thread
    // quiescence protocol is provided — documented limitation.
    uint32_t stolen[4];
    memcpy(stolen, src, 16);                          // for rollback
    vm_address_t page = (vm_address_t)symbol & ~0x3fffULL;
    vm_size_t span = ((vm_address_t)symbol + 16 - page + 0x3fff) & ~0x3fffULL;
    if (vm_protect(mach_task_self(), page, span, FALSE,
                   VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY)
            != KERN_SUCCESS &&
        vm_protect(mach_task_self(), page, span, FALSE,
                   VM_PROT_READ | VM_PROT_WRITE) != KERN_SUCCESS) {
        munmap(thunk, 4096);
        MACWS_REFUSE("target vm_protect RW failed");
    }
    uint32_t *dp = (uint32_t *)symbol;
    dp[0] = 0x58000051;                              // ldr x17, #8
    dp[1] = 0xd61f0220;                              // br x17
    *(uint64_t *)(dp + 2) = (uint64_t)replace;
    if (vm_protect(mach_task_self(), page, span, FALSE,
                   VM_PROT_READ | VM_PROT_EXECUTE) != KERN_SUCCESS) {
        // Roll back the patch bytes so the target is not left
        // half-written and unexecutable. The page is already RW, so the
        // byte restore always lands; then restore the RX protection the
        // page had on entry. If the RX restore itself fails the code
        // page is left writable-and-non-executable — every subsequent
        // call into it would fault — so fail loudly instead of
        // pretending the target is restored.
        memcpy(dp, stolen, 16);
        sys_icache_invalidate(symbol, 16);
        if (vm_protect(mach_task_self(), page, span, FALSE,
                       VM_PROT_READ | VM_PROT_EXECUTE) != KERN_SUCCESS) {
            macws_hook_log("MACWSHook: FATAL rx-restore failed; "
                           "code page left RW\n");
        }
        munmap(thunk, 4096);
        // Target bytes restored — hand back the callable original so a
        // caller invoking *result still reaches the real function.
        MACWS_REFUSE("target rx restore after patch");
    }
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

// Exact-class method swizzle with the classic substrate callable contract.
//
//   * Lookup uses class_getInstanceMethod, so a metaclass argument hooks
//     class methods as well as an ordinary class hooks instance methods.
//   * class_replaceMethod installs the replacement on the REQUESTED class.
//     When the selector is only inherited, libobjc adds an override to that
//     class instead of mutating the superclass; instances of the superclass
//     therefore keep their original behavior.
//   * The previously invoked IMP is published through *result BEFORE the
//     replacement is installed, so a replacement that forwards through
//     *result never observes an uninitialized dependency.
//   * Missing class/selector/replacement fails closed: nothing is added
//     with a fabricated type encoding and *result stays NULL. This is the
//     method-level equivalent of MSHookFunction's refusal contract.
//
// Limits, stated rather than hidden:
//   * There is no target-identity permit here. Objective-C methods carry no
//     stable prologue to pin, so callers remain responsible for verifying
//     the class name and framework version they hook.
//   * libobjc performs its own method-cache flush inside
//     class_replaceMethod; this provider adds no thread-quiescence protocol
//     for calls already in flight (same limitation as MSHookFunction).
//   * The replacement must have a compatible ABI; type encodings are
//     validated for presence, not for signature equality.
void MSHookMessageEx(Class _class, SEL sel, IMP imp, IMP *result) {
    IMP original = NULL;
    const char *types = NULL;
    if (_class && sel) {
        Method m = class_getInstanceMethod(_class, sel);
        if (m) {
            original = method_getImplementation(m);
            types = method_getTypeEncoding(m);
        }
    }
    if (result) *result = original;
    if (!_class || !sel || !imp || !original || !types) {
        char _macws_buf[288];
        snprintf(_macws_buf, sizeof(_macws_buf),
            "MACWSHook: MSG-REFUSED %s class=%s sel=%s imp=%p\n",
            (!_class || !sel || !imp)
                ? "bad args" : "missing method/encoding",
            _class ? class_getName(_class) : "(null)",
            sel ? sel_getName(sel) : "(null)", (void *)imp);
        macws_hook_log(_macws_buf);
        return;
    }
    class_replaceMethod(_class, sel, imp, types);
}
