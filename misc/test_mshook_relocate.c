// D037 arm64/arm64e LIVE relocation contract test for the exact-relocation
// thunk in misc/cydiasubstrate_macos.m.  Runs on the device (chroot or
// native arm64e); exercises the REAL instruction words of the IOMFB 13.4
// publicSwapEnd wrapper, emitted verbatim via .long so nothing depends on
// assembler decode:
//
//   b4000080  cbz  x0, +0x10          ; fb == NULL   -> continuation
//   f9439401  ldr  x1, [x0, #0x728]   ; impl pointer
//   b4000041  cbz  x1, +8             ; impl == NULL -> continuation
//   d61f083f  braaz x1                ; authenticated tail-call (arm64e)
//
// On the plain arm64 build the terminal word is br x1 (0xd61f0020) — same
// shape, unauthenticated.  The tail pointer stored at fb+0x728 is signed
// IA+0 via ptrauth_sign_unauthenticated on arm64e — the exact signature
// class braaz authenticates.  tail_target verifies x0 arrived intact
// (register preservation through the relocated window).
//
//   ./test_mshook_relocate <provider.dylib>
// exit 0 ALL PASS; 1 failures; 2 missing exports; 3 arm64e required but
// binary is arm64 (PAC path not exercised).
//
// Refusal fixtures: b.cond window, blrabz window, internal-window cbz,
// unpermitted target — each must refuse with callable original + untouched
// bytes.
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if __arm64e__
#include <ptrauth.h>
#endif

typedef int (*permit_fn_t)(const void *, const void *, uint64_t,
                           const uint32_t[4]);
typedef void (*hook_fn_t)(void *, void *, void **);

// arm64e function addresses are IA+0-signed at capture; strip before using
// them as data pointers or in image-offset arithmetic. Calls keep the
// signed value (the CPU authenticates them at the call site).
#if __arm64e__
#define FPTR(p) ptrauth_strip((void *)(p), ptrauth_key_asia)
#else
#define FPTR(p) ((void *)(p))
#endif

static int g_tail_calls = 0;
static int g_hooked_calls = 0;
static void *g_tail_arg = NULL;
static void (*g_orig)(void *);

static void tail_target(void *fb) {
    g_tail_calls++;
    g_tail_arg = fb;
}

// Window = exact publicSwapEnd bytes.  The .long continuation at S+0x10
// (ret) is the un-stolen original path — replaying it must return to the
// thunk's caller.
__attribute__((naked)) static void swapend_shim(void *fb) {
    __asm__ volatile(
        ".long 0xb4000080\n"                    // cbz x0, +0x10
        ".long 0xf9439401\n"                    // ldr x1, [x0, #0x728]
        ".long 0xb4000041\n"                    // cbz x1, +8
#if __arm64e__
        ".long 0xd61f083f\n"                    // braaz x1
#else
        ".long 0xd61f0020\n"                    // br x1
#endif
        ".long 0xd65f03c0\n"                    // ret (continuation)
        ::: "x1", "memory");
}

// Refusal fixtures — naked functions whose windows must NOT install.
__attribute__((naked)) static void win_bcond(void) {
    __asm__ volatile(
        "b.eq 1f\n mov x0, #1\n mov x0, #2\n mov x0, #3\n 1: ret\n");
}
__attribute__((naked)) static void win_blrabz(void) {
    __asm__ volatile(
#if __arm64e__
        ".long 0xd63f0c1f\n"                    // blrabz x0
#else
        ".long 0xd63f0000\n"                    // blr x0
#endif
        " mov x0, #1\n mov x0, #2\n mov x0, #3\n ret\n");
}
__attribute__((naked)) static void win_intarget(void) {
    __asm__ volatile(
        "cbz x0, .+8\n"                          // target inside window
        " mov x0, #1\n mov x0, #2\n mov x0, #3\n ret\n");
}
__attribute__((naked)) static void win_nopermit(void) {
    __asm__ volatile(
        "mov x0, #7\n mov x0, #7\n mov x0, #7\n mov x0, #7\n ret\n");
}

static void replacement(void *fb) {
    g_hooked_calls++;
    g_orig(fb);                       // also proves orig is callable here
}

static const uint8_t *image_uuid(const struct mach_header_64 *h) {
    const uint8_t *p = (const uint8_t *)(h + 1);
    for (uint32_t i = 0; i < h->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)p;
        if (lc->cmd == LC_UUID)
            return ((const struct uuid_command *)lc)->uuid;
        p += lc->cmdsize;
    }
    return NULL;
}

static const struct mach_header_64 *self_header(void) {
    Dl_info ti; memset(&ti, 0, sizeof(ti));
    dladdr(FPTR(swapend_shim), &ti);
    return (const struct mach_header_64 *)ti.dli_fbase;
}

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
        printf("FAIL %s\n", msg); fails++; } } while (0)
#define STEP(n) fprintf(stderr, "step %d\n", n)

// fb layout for the shim: impl pointer at +0x728.
struct fake_fb { char pad[0x728]; void *impl; };

int main(int argc, char **argv) {
    setbuf(stdout, NULL);
    if (argc < 2) { fprintf(stderr, "usage: %s provider.dylib\n", argv[0]); return 64; }
    void *lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 65; }
    permit_fn_t permit = (permit_fn_t)dlsym(lib, "MACWSHookPermitV2");
    hook_fn_t hook = (hook_fn_t)dlsym(lib, "MSHookFunction");
    if (!permit || !hook) { fprintf(stderr, "missing exports\n"); return 2; }

#if __arm64e__
    printf("arch=arm64e (PAC path active)\n");
#else
    printf("arch=arm64 (br tail — PAC not exercised)\n");
#endif

    STEP(1);
    const struct mach_header_64 *hdr = self_header();
    STEP(11);
    const uint8_t *uuid = image_uuid(hdr);
    STEP(12);
    CHECK(uuid != NULL, "no LC_UUID on self image");
    fprintf(stderr, "hdr=%p shim=%p uuid0=%02x magic=%08x ncmds=%u\n",
            hdr, (void *)swapend_shim, uuid ? uuid[0] : 0,
            hdr->magic, hdr->ncmds);

    // --- negative registration -------------------------------------------
    uint64_t off = (uint64_t)(uintptr_t)FPTR(swapend_shim) - (uint64_t)hdr;
    uint32_t exp[4]; memcpy(exp, FPTR(swapend_shim), 16);
    uint8_t wrong_uuid[16]; memcpy(wrong_uuid, uuid, 16); wrong_uuid[0] ^= 0xff;
    STEP(121);
    CHECK(permit(hdr, wrong_uuid, off, exp) == -2, "wrong-uuid != -2");
    STEP(13);
    uint32_t wrong_exp[4]; memcpy(wrong_exp, exp, 16); wrong_exp[0] ^= 1;
    CHECK(permit(hdr, uuid, off, wrong_exp) == -1, "wrong-prologue != -1");
    STEP(14);
    CHECK(permit(hdr, uuid, off + 4, exp) == -1, "wrong-offset != -1");

    // --- positive permits (register before hooks rewrite bytes) ----------
    uint64_t o_bad = (uint64_t)(uintptr_t)FPTR(win_bcond) - (uint64_t)hdr;
    uint64_t o_blr = (uint64_t)(uintptr_t)FPTR(win_blrabz) - (uint64_t)hdr;
    uint64_t o_int = (uint64_t)(uintptr_t)FPTR(win_intarget) - (uint64_t)hdr;
    STEP(2);
    CHECK(permit(hdr, uuid, off, exp) == 0, "shim permit rc != 0");
    CHECK(permit(hdr, uuid, o_bad, (const uint32_t *)FPTR(win_bcond)) == 0,
          "bcond permit rc != 0");
    CHECK(permit(hdr, uuid, o_blr, (const uint32_t *)FPTR(win_blrabz)) == 0,
          "blr permit rc != 0");
    CHECK(permit(hdr, uuid, o_int, (const uint32_t *)FPTR(win_intarget)) == 0,
          "intarget permit rc != 0");

    STEP(3);
    // --- refusal matrix ---------------------------------------------------
    uint8_t before[16]; void *orig = NULL;
    memcpy(before, FPTR(win_bcond), 16);
    hook(FPTR(win_bcond), replacement, &orig);
    CHECK(FPTR(orig) == FPTR(win_bcond) &&
          !memcmp(before, FPTR(win_bcond), 16),
          "b.cond window not refused-clean");

    memcpy(before, FPTR(win_blrabz), 16);
    orig = NULL;
    hook(FPTR(win_blrabz), replacement, &orig);
    CHECK(FPTR(orig) == FPTR(win_blrabz) &&
          !memcmp(before, FPTR(win_blrabz), 16),
          "blr-family window not refused-clean");

    memcpy(before, FPTR(win_intarget), 16);
    orig = NULL;
    hook(FPTR(win_intarget), replacement, &orig);
    CHECK(FPTR(orig) == FPTR(win_intarget) &&
          !memcmp(before, FPTR(win_intarget), 16),
          "internal-window target not refused-clean");

    memcpy(before, FPTR(win_nopermit), 16);
    orig = NULL;
    hook(FPTR(win_nopermit), replacement, &orig);
    CHECK(FPTR(orig) == FPTR(win_nopermit) &&
          !memcmp(before, FPTR(win_nopermit), 16),
          "unpermitted target not refused-clean");

    STEP(4);
    // === live hook on the real window =====================================
    uint8_t shim_before[16];
    memcpy(shim_before, FPTR(swapend_shim), 16);
    printf("shim-before:"); for (int i = 0; i < 16; i++)
        printf(" %08x", ((const uint32_t *)shim_before)[i]); printf("\n");

    STEP(5);
    g_orig = NULL;
    hook(FPTR(swapend_shim), replacement, (void **)&g_orig);
    STEP(6);
    CHECK(g_orig && FPTR(g_orig) != FPTR(swapend_shim), "shim hook refused");

    const uint32_t *patched = (const uint32_t *)FPTR(swapend_shim);
    printf("shim-after :"); for (int i = 0; i < 4; i++)
        printf(" %08x", patched[i]); printf("\n");
    CHECK(patched[0] == 0x58000051u && patched[1] == 0xd61f0220u,
          "installed stub is not the expected ldr/br pair");
    CHECK(memcmp(shim_before, patched, 16) != 0,
          "shim bytes unchanged after install");

    struct fake_fb *fb = calloc(1, sizeof(*fb));
    struct fake_fb *fb_null_impl = calloc(1, sizeof(*fb));
#if __arm64e__
    fb->impl = ptrauth_sign_unauthenticated(
        FPTR(tail_target), ptrauth_key_asia, 0);
#else
    fb->impl = (void *)tail_target;
#endif
    printf("signed-impl=%p (stored at fb+0x728)\n", fb->impl);

    // Case 1: fb == NULL — first cbz taken -> continuation ret, no tail.
    STEP(7);
    swapend_shim(NULL);
    STEP(8);
    CHECK(g_hooked_calls == 1, "replacement not reached (null fb)");
    CHECK(g_tail_calls == 0, "tail ran on null fb");

    // Case 2: impl == NULL — second cbz taken -> ret, no tail.
    swapend_shim(fb_null_impl);
    CHECK(g_hooked_calls == 2, "replacement not reached (null impl)");
    CHECK(g_tail_calls == 0, "tail ran on null impl");

    // Case 3: signed impl — braaz authenticates and tail-calls it.
    swapend_shim(fb);
    CHECK(g_hooked_calls == 3, "replacement not reached (signed impl)");
    CHECK(g_tail_calls == 1, "tail did not run once (signed impl)");
    CHECK(g_tail_arg == fb, "x0 arg not preserved into tail target");

    // Case 4: callable original — direct thunk replay, all three paths.
    g_orig(NULL);              CHECK(g_tail_calls == 1, "orig null-fb tailed");
    g_orig(fb_null_impl);      CHECK(g_tail_calls == 1, "orig null-impl tailed");
    g_orig(fb);
    CHECK(g_tail_calls == 2, "orig signed-impl path failed");
    CHECK(g_tail_arg == fb, "orig path arg not preserved");
    CHECK(g_hooked_calls == 3,
          "replacement fired during orig() replay");

    printf("hooked=%d tail=%d\n", g_hooked_calls, g_tail_calls);
    printf(fails ? "RESULT: %d FAILURES\n" : "RESULT: ALL PASS\n", fails);
    return fails ? 1 : 0;
}
