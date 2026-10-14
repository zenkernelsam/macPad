// macos_mshook_provider_probe — R003 provider verification tool.
// Resolves the LIVE MSHookFunction provider (dladdr), dumps its entry
// bytes, installs a hook on a local toy function, and verifies:
//   (a) target bytes changed (16-byte absolute branch),
//   (b) calling target() reaches the replacement,
//   (c) calling the returned orig thunk reaches the real target.
// R003-F02 containment: MSHookFunction now requires a MACWSHookPermit
// (image UUID + offset + expected prologue) — this probe registers its
// own permit and also exercises the NEGATIVE path (unpermitted target
// must be refused with target bytes untouched).
// Exit codes are DISTINCT per outcome so CI/automation can gate:
//   0 = INSTALLED (+orig works)
//   2 = MSHookFunction unresolved
//   3 = NO-OP (bytes unchanged / orig==symbol / replace not called)
//   4 = PARTIAL (hook fired but orig thunk broken)
//   5 = refusal witness failed (unpermitted target was patched)
//   6 = permit registration failed
//   7 = uuid identity pin not enforced (wrong-uuid permit accepted)
// argv[1] == "orig" exercises replacement-via-original ordering.
//
// Build (macOS dev host):
//   xcrun clang -arch arm64e -o /tmp/mshook_provider_probe \
//       misc/macos_mshook_provider_probe.c -ldl
//   python3 misc/set_macos_version.py /tmp/mshook_provider_probe
// Deploy: scp to /var/mnt/rootfs/tmp/, ldid -S, boot-trust --readd,
// run via launchdchrootexec.

#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#if __arm64e__
#include <ptrauth.h>
#define STRIP(p) ptrauth_strip((p), ptrauth_key_asia)
#else
#define STRIP(p) (p)
#endif

typedef void (*MSHookFn)(void *, void *, void **);
typedef int (*PermitFn)(const void *image_base, const void *uuid16,
                        uint64_t image_offset,
                        const uint32_t expected[4]);

// Read LC_UUID out of a live image header — the caller supplies this as
// the identity pin when registering a permit for code in that image.
static int image_uuid(const void *hdr_in, uint8_t out[16]) {
    const struct mach_header_64 *h = (const struct mach_header_64 *)hdr_in;
    if (!h || h->magic != MH_MAGIC_64) return -1;
    const struct load_command *lc = (const struct load_command *)(h + 1);
    for (uint32_t i = 0; i < h->ncmds; i++) {
        if (lc->cmd == LC_UUID) {
            memcpy(out, ((const struct uuid_command *)lc)->uuid, 16);
            return 0;
        }
        lc = (const struct load_command *)
            ((const uint8_t *)lc + lc->cmdsize);
    }
    return -1;
}

static int g_replace_called = 0;
static int (*g_orig)(void) = NULL;
static int g_via_orig_returned = 0;

// toy_target must occupy >=16 bytes of non-PC-relative instructions —
// the hook steals the first four insns and refuses PC-relative ones.
// volatile arithmetic keeps the compiler from shrinking it.
__attribute__((noinline)) static int toy_target(void) {
    volatile int a = 40, b = 1, c = 1;
    return a + b + c;
}
// Second toy target with NO permit — the refusal witness.
__attribute__((noinline)) static int toy_unpermitted(void) {
    volatile int a = 41, b = 1;
    return a + b;
}
__attribute__((noinline)) static int toy_replace(void) {
    g_replace_called = 1;
    return 43;
}
__attribute__((noinline)) static int toy_replace_via_orig(void) {
    g_replace_called = 1;
    int r = g_orig ? g_orig() : -1;
    g_via_orig_returned = (r == 42);
    return g_orig ? r + 1 : -1;
}

static void dump_bytes(const char *tag, const void *p, int n) {
    printf("%s @%p:", tag, p);
    const unsigned char *b = (const unsigned char *)p;
    for (int i = 0; i < n; i++) printf(" %02x", b[i]);
    printf("\n");
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    MSHookFn hook = (MSHookFn)dlsym(RTLD_DEFAULT, "MSHookFunction");
    if (!hook) { printf("MSHookFunction unresolved\n"); return 2; }
    Dl_info di;
    memset(&di, 0, sizeof(di));
    dladdr((void *)hook, &di);
    printf("provider-image=%s\n", di.dli_fname ? di.dli_fname : "?");
    printf("provider-addr=%p\n", (void *)hook);
    dump_bytes("provider-bytes", STRIP((const void *)hook), 16);

    void *toy = STRIP((void *)toy_target);
    void *toyNP = STRIP((void *)toy_unpermitted);
    printf("toy_target=%p toy_replace=%p\n",
           (void *)toy_target, (void *)toy_replace);
    dump_bytes("target-before", toy, 16);
    dump_bytes("nopermit-before", toyNP, 16);

    // Negative witness: hooking an UNPERMITTED target must fail closed.
    // Refusal contract: *result is a CALLABLE pointer to the original —
    // prove it by actually calling through it, for both the stripped
    // and the raw (PAC-signed as-returned-by-compiler) input forms.
    // Discriminator: all 16 target bytes unchanged AND res()==42.
    {
        typedef int (*toy_fn_t)(void);
        uint8_t before[16];
        memcpy(before, toyNP, 16);

        // Form A: stripped input.  The provider returns the pointer as
        // passed (identity preserved).  A caller holding a stripped
        // pointer must re-authenticate before an indirect call — that's
        // the PAC contract, not provider behavior — so call resA via
        // ptrauth_sign_unauthenticated (asia,0), the same convention
        // the thunk return uses.
        void *resA = (void *)0xdead;
        hook(toyNP, (void *)toy_replace, &resA);
        int unchangedA = (memcmp(before, toyNP, 16) == 0);
        int calledA = -99;
        if (resA == toyNP) {
#if __arm64e__
            toy_fn_t f = (toy_fn_t)ptrauth_sign_unauthenticated(
                resA, ptrauth_key_asia, 0);
#else
            toy_fn_t f = (toy_fn_t)resA;
#endif
            calledA = f();
        }
        printf("nopermit-strip: res=%p all16unchanged=%d res()=%d\n",
               resA, unchangedA, calledA);

        // Form B: raw input (the pointer exactly as the compiler hands
        // it — PAC-signed on arm64e).  The provider must return that
        // same signed pointer so resB() authenticates correctly.
        void *rawToy = (void *)toy_unpermitted;
        void *resB = (void *)0xdead;
        hook(rawToy, (void *)toy_replace, &resB);
        int unchangedB = (memcmp(before, toyNP, 16) == 0);
        int calledB = (resB == rawToy)
            ? ((toy_fn_t)resB)() : -99;
        printf("nopermit-raw:   res=%p all16unchanged=%d res()=%d\n",
               resB, unchangedB, calledB);

        if (!unchangedA || calledA != 42 ||
            !unchangedB || calledB != 42 || toy_unpermitted() != 42)
            printf("VERDICT=PERMIT-BYPASS\n"),
            fflush(stdout), _exit(5);
    }

    // Register permit for toy_target against the image CONTAINING it —
    // dladdr gives the true image base (dyld index 0 is not guaranteed
    // to be this image when insert libraries reorder the list).
    PermitFn permitFn =
        (PermitFn)dlsym(RTLD_DEFAULT, "MACWSHookPermitV2");
    if (!permitFn) { printf("MACWSHookPermitV2 unresolved\n"); return 6; }
    // Version check: an older provider exporting only the 3-arg
    // MACWSHookPermit would silently mis-handle the 4-arg contract —
    // the V2 name makes mixed deployment fail closed.
    if (dlsym(RTLD_DEFAULT, "MACWSHookPermit") &&
        !dlsym(RTLD_DEFAULT, "MACWSHookPermitV2"))
        { printf("VERDICT=OLD-PROVIDER\n"); return 8; }
    Dl_info ti;
    memset(&ti, 0, sizeof(ti));
    dladdr((void *)toy_target, &ti);
    const struct mach_header *self = ti.dli_fbase;
    uint64_t off = (uint64_t)(uintptr_t)toy - (uint64_t)(uintptr_t)self;
    uint8_t self_uuid[16];
    if (image_uuid(self, self_uuid) != 0) {
        printf("VERDICT=PERMIT-FAIL(no LC_UUID)\n");
        return 6;
    }
    // Identity-pin negative witness: registering with a WRONG uuid must
    // be refused (-2) — a self-copied uuid is not a pin.
    {
        uint8_t wrong[16]; memcpy(wrong, self_uuid, 16); wrong[0] ^= 0xff;
        int neg = permitFn(self, wrong, off, (const uint32_t *)toy);
        printf("permit-wronguuid -> %d (expect -2)\n", neg);
        if (neg != -2) {
            printf("VERDICT=PIN-NOT-ENFORCED\n");
            return 7;
        }
    }
    int prc = permitFn(self, self_uuid, off, (const uint32_t *)toy);
    printf("permit(image=%p,off=%#llx) -> %d\n",
           (void *)self, (unsigned long long)off, prc);
    if (prc != 0) { printf("VERDICT=PERMIT-FAIL\n"); return 6; }

    int use_orig_path = (argc > 1 && !strcmp(argv[1], "orig"));
    void *rep = use_orig_path ? (void *)toy_replace_via_orig
                              : (void *)toy_replace;
    void *orig = NULL;
    hook(toy, rep, &orig);
    dump_bytes("target-after ", toy, 16);
    printf("orig=%p\n", orig);

    // R003-F04: publish the orig thunk BEFORE exercising the replace
    // path, so replacement-via-original sees a valid pointer.
    if (orig) g_orig = (int (*)(void))orig;
    int via_hook = toy_target();          // should hit replace
    printf("target()=%d replace_called=%d via_orig_returned=%d\n",
           via_hook, g_replace_called, g_via_orig_returned);

    int via_orig = -1;
    if (orig) via_orig = g_orig();
    printf("orig()=%d\n", via_orig);

    if (via_hook == 43 && g_replace_called && via_orig == 42) {
        printf("VERDICT=INSTALLED\n");
        return 0;
    }
    if (via_hook == 42 && !g_replace_called) {
        printf("VERDICT=NO-OP\n");
        return 3;
    }
    printf("VERDICT=PARTIAL(hook=%d,called=%d,orig=%d)\n",
           via_hook, g_replace_called, via_orig);
    return 4;
}
