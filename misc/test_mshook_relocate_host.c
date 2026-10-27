// D036 host-side thunk-byte verification for the exact-relocation builder in
// misc/cydiasubstrate_macos.m.  Runs on x86_64 macOS: the "targets" are
// in-image __DATA arrays holding the REAL arm64 instruction words (never
// executed on x86 — what is verified is the byte-exact thunk the provider
// emits, plus refusal/permit/callable-original behavior).
//
//   /tmp/test_reloc_host /tmp/libprov_x86.dylib
//
// Verified invariants for the IOMFB-13.4 publicSwapEnd wrapper
// {cbz x0,+0x10 | ldr x1,[x0,#0x728] | cbz x1,+8 | braaz x1}:
//   thunk[0]     = cbnz x0,+0x10 (condition inverted, skips the abs pair)
//   thunk[1..2]  = ldr x17,#8 / br x17
//   thunk[3..4]  = quad == symbol+0x10   (ORIGINAL cbz target preserved)
//   thunk[5]     = ldr verbatim
//   thunk[6]     = cbnz x1,+0x10
//   thunk[7..10] = second abs pair, quad == symbol+0x10
//   thunk[11]    = braaz x1 verbatim; NO branch-back appended (control leaves)
// plus: a plain prologue window produces verbatim copy + branch-back to
// symbol+16; an unrelocatable window refuses with callable original; wrong
// uuid/prologue/offset registrations fail -2/-1/-1; an unpermitted target
// refuses before any patch bytes are written.
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int (*permit_fn_t)(const void *, const void *, uint64_t, const uint32_t[4]);
typedef void (*hook_fn_t)(void *, void *, void **);

// Exact IOMFB-13.4 publicSwapEnd wrapper bytes (+ a ret following, unused).
static uint32_t g_swapend_window[] __attribute__((section("__DATA,mwshook"))) = {
    0xb4000080u,  // cbz  x0, +0x10
    0xf9439401u,  // ldr  x1, [x0, #0x728]
    0xb4000041u,  // cbz  w1, +8
    0xd61f083fu,  // braaz x1  (terminal, authenticated)
    0xd65f03c0u,  // ret
};

// All-verbatim window: pacibsp / sub sp / stp / add  (QC-style prologue).
static uint32_t g_prologue_window[] __attribute__((section("__DATA,mwshook"))) = {
    0xd503237fu, 0xd10243ffu, 0xa9036ffcu, 0xa90467fau, 0xd65f03c0u,
};

// Unrelocatable window: b.cond at slot 0.
static uint32_t g_bad_window[] __attribute__((section("__DATA,mwshook"))) = {
    0x54000000u, 0xd503201fu, 0xd503201fu, 0xd503201fu, 0xd65f03c0u,
};

// Indirect-call window: blrabz x0 at slot 0 — terminal class, must REFUSE
// (blr-family returns into the window). Exercises the corrected
// 0xd63f0c1f constant.
static uint32_t g_blr_window[] __attribute__((section("__DATA,mwshook"))) = {
    0xd63f0c1fu, 0xd503201fu, 0xd503201fu, 0xd503201fu, 0xd65f03c0u,
};

// Internal-target window: cbz x0,+8 — target lands inside the stolen
// window (would point into the patch stub after install) -> must REFUSE.
static uint32_t g_intwin_window[] __attribute__((section("__DATA,mwshook"))) = {
    0xb4000040u, 0xd503201fu, 0xd503201fu, 0xd503201fu, 0xd65f03c0u,
};

static int g_hits = 0;
static void replacement(void *a) { g_hits++; (void)a; }

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

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { \
        printf("FAIL %s\n", msg); fails++; } } while (0)

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s provider.dylib\n", argv[0]); return 64; }
    void *lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 65; }
    permit_fn_t permit = (permit_fn_t)dlsym(lib, "MACWSHookPermitV2");
    hook_fn_t hook = (hook_fn_t)dlsym(lib, "MSHookFunction");
    if (!permit || !hook) { fprintf(stderr, "missing exports\n"); return 66; }

    const struct mach_header_64 *hdr =
        (const struct mach_header_64 *)_dyld_get_image_header(0);
    const uint8_t *uuid = image_uuid(hdr);
    CHECK(uuid != NULL, "test image has no LC_UUID");

    uint64_t off = (uint64_t)(uintptr_t)g_swapend_window - (uint64_t)hdr;
    uint64_t poff = (uint64_t)(uintptr_t)g_prologue_window - (uint64_t)hdr;
    uint64_t boff = (uint64_t)(uintptr_t)g_bad_window - (uint64_t)hdr;
    uint64_t loff = (uint64_t)(uintptr_t)g_blr_window - (uint64_t)hdr;
    uint64_t ioff = (uint64_t)(uintptr_t)g_intwin_window - (uint64_t)hdr;
    uint32_t exp[4], pexp[4], bexp[4], lexp[4], iexp[4];
    memcpy(exp, g_swapend_window, 16);
    memcpy(pexp, g_prologue_window, 16);
    memcpy(bexp, g_bad_window, 16);
    memcpy(lexp, g_blr_window, 16);
    memcpy(iexp, g_intwin_window, 16);

    // --- negative registration -------------------------------------------
    uint8_t wrong_uuid[16]; memcpy(wrong_uuid, uuid, 16); wrong_uuid[0] ^= 0xff;
    CHECK(permit(hdr, wrong_uuid, off, exp) == -2, "wrong-uuid != -2");
    uint32_t wrong_exp[4]; memcpy(wrong_exp, exp, 16); wrong_exp[0] ^= 1;
    CHECK(permit(hdr, uuid, off, wrong_exp) == -1, "wrong-prologue != -1");
    CHECK(permit(hdr, uuid, off + 4, exp) == -1, "wrong-offset != -1");

    // Register all positive permits up-front (hooking overwrites the
    // fixture bytes, so registration must happen first).
    CHECK(permit(hdr, uuid, off, exp) == 0, "swapend permit rc != 0");
    CHECK(permit(hdr, uuid, boff, bexp) == 0, "bad-window permit rc != 0");
    CHECK(permit(hdr, uuid, poff, pexp) == 0, "prologue permit rc != 0");
    CHECK(permit(hdr, uuid, loff, lexp) == 0, "blr-window permit rc != 0");
    CHECK(permit(hdr, uuid, ioff, iexp) == 0, "intwin permit rc != 0");

    // --- refusal: permitted but unrelocatable window ---------------------
    void *orig = NULL;
    hook((void *)g_bad_window, replacement, &orig);
    CHECK(orig == (void *)g_bad_window,
          "unrelocatable window not refused/callable");

    // --- refusal: indirect call (blrabz) in window ------------------------
    orig = NULL;
    hook((void *)g_blr_window, replacement, &orig);
    CHECK(orig == (void *)g_blr_window,
          "blrabz window not refused/callable");

    // --- refusal: cbz target inside the stolen window ---------------------
    orig = NULL;
    hook((void *)g_intwin_window, replacement, &orig);
    CHECK(orig == (void *)g_intwin_window,
          "internal-window-target not refused/callable");

    // === SwapEnd wrapper window: relocation byte-exactness ================
    orig = NULL;
    hook((void *)g_swapend_window, replacement, &orig);
    CHECK(orig && orig != (void *)g_swapend_window, "swapend hook refused");
    if (orig && orig != (void *)g_swapend_window) {
        const uint32_t *t = (const uint32_t *)orig;
        const uint64_t cont = (uint64_t)(uintptr_t)g_swapend_window + 16;
        CHECK(t[0] == 0xb50000a0u, "cbz x0 not inverted to cbnz +0x14");
        CHECK(t[1] == 0x58000051u && t[2] == 0xd61f0220u,
              "abs-branch pair missing after cbz x0");
        CHECK(*(const uint64_t *)(t + 3) == cont,
              "cbz x0 target not symbol+0x10");
        CHECK(t[5] == 0xf9439401u, "ldr x1 not verbatim");
        CHECK(t[6] == 0xb50000a1u, "cbz x1 not inverted to cbnz +0x14");
        CHECK(t[7] == 0x58000051u && t[8] == 0xd61f0220u,
              "abs-branch pair missing after cbz x1");
        CHECK(*(const uint64_t *)(t + 9) == cont,
              "cbz x1 target not symbol+0x10");
        CHECK(t[11] == 0xd61f083fu, "braaz x1 not verbatim");
        CHECK(g_hits == 0, "replacement ran during install");
        // Semantic check of the skip displacement: decode imm19 of each
        // emitted cbnz and verify the landing offset is the next emitted
        // instruction, NOT inside the ldr/br/.quad payload. Catches the
        // imm19 off-by-one class of bug byte-equality alone missed.
        uint32_t imm0 = (t[0] >> 5) & 0x7ffff;
        uint32_t imm6 = (t[6] >> 5) & 0x7ffff;
        CHECK(0 + imm0 * 4 == 5 * 4, "cbnz x0 skip lands mid-payload");
        CHECK(6 * 4 + imm6 * 4 == 11 * 4, "cbnz x1 skip lands mid-payload");
        printf("INFO thunk words:");
        for (int i = 0; i < 12; i++) printf(" %08x", t[i]);
        printf("\n");
    }

    // === verbatim window: copy + branch-back ==============================
    orig = NULL;
    hook((void *)g_prologue_window, replacement, &orig);
    CHECK(orig && orig != (void *)g_prologue_window, "prologue hook refused");
    if (orig && orig != (void *)g_prologue_window) {
        const uint32_t *t = (const uint32_t *)orig;
        CHECK(!memcmp(t, g_prologue_window, 16), "verbatim copy drifted");
        CHECK(t[4] == 0x58000051u && t[5] == 0xd61f0220u,
              "branch-back pair missing");
        CHECK(*(const uint64_t *)(t + 6) ==
              (uint64_t)(uintptr_t)g_prologue_window + 16,
              "branch-back target != symbol+16");
    }

    // --- unpermitted target refuses before patching ----------------------
    static uint32_t late_window[] __attribute__((section("__DATA,mwshook"))) = {
        0xd503237fu, 0xd503201fu, 0xd503201fu, 0xd65f03c0u,
    };
    uint32_t late_before[4]; memcpy(late_before, late_window, 16);
    orig = NULL;
    hook((void *)late_window, replacement, &orig);
    CHECK(orig == (void *)late_window, "unpermitted target not callable orig");
    CHECK(!memcmp(late_before, late_window, 16),
          "unpermitted target bytes were modified");

    printf(fails ? "RESULT: %d FAILURES\n" : "RESULT: ALL PASS\n", fails);
    return fails ? 1 : 0;
}
