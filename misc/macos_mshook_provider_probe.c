// macos_mshook_provider_probe — R001 provider verification tool.
// Resolves the LIVE MSHookFunction provider (dladdr), dumps its entry
// bytes, installs a hook on a local toy function, and verifies:
//   (a) target bytes changed (16-byte absolute branch),
//   (b) calling target() reaches the replacement,
//   (c) calling the returned orig thunk reaches the real target.
// Fail-closed expectations: provider that no-ops leaves bytes unchanged
// and VERDICT=NO-OP; a working provider yields VERDICT=INSTALLED.
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
#if __arm64e__
#include <ptrauth.h>
#define STRIP(p) ptrauth_strip((p), ptrauth_key_asia)
#else
#define STRIP(p) (p)
#endif

typedef void (*MSHookFn)(void *, void *, void **);

static int g_replace_called = 0;
static int (*g_orig)(void) = NULL;

// toy_target must occupy >=16 bytes of non-PC-relative instructions —
// the hook steals the first four insns and refuses PC-relative ones.
// volatile arithmetic keeps the compiler from shrinking it.
__attribute__((noinline)) static int toy_target(void) {
    volatile int a = 40, b = 1, c = 1;
    return a + b + c;
}
__attribute__((noinline)) static int toy_replace(void) {
    g_replace_called = 1;
    return 43;
}
__attribute__((noinline)) static int toy_replace_via_orig(void) {
    g_replace_called = 1;
    return g_orig ? g_orig() + 1 : -1;
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

    printf("toy_target=%p toy_replace=%p\n",
           (void *)toy_target, (void *)toy_replace);
    void *toy = STRIP((void *)toy_target);
    dump_bytes("target-before", toy, 16);

    int use_orig_path = (argc > 1 && !strcmp(argv[1], "orig"));
    void *rep = use_orig_path ? (void *)toy_replace_via_orig
                              : (void *)toy_replace;
    void *orig = NULL;
    hook(toy, rep, &orig);
    dump_bytes("target-after ", toy, 16);
    printf("orig=%p\n", orig);

    int before = toy_target();   // dead value, then:
    int via_hook = toy_target(); // should hit replace
    printf("target()=%d replace_called=%d\n", via_hook, g_replace_called);

    int via_orig = -1;
    if (orig) {
        g_orig = (int (*)(void))orig;
        via_orig = g_orig();
    }
    printf("orig()=%d\n", via_orig);

    if (via_hook == 43 && g_replace_called && via_orig == 42)
        printf("VERDICT=INSTALLED\n");
    else if (via_hook == 42 && !g_replace_called)
        printf("VERDICT=NO-OP\n");
    else
        printf("VERDICT=PARTIAL(hook=%d,called=%d,orig=%d)\n",
               via_hook, g_replace_called, via_orig);
    (void)before;
    return 0;
}
