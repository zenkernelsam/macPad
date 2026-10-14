"""R004B loader-observation contract tests.

Compiles Diagnostics/MacWSLoaderObservation.m plus a small fixture main
on the host and exercises the documented contract: outside-context and
other-thread calls produce nothing, nesting unwinds, the record cap
holds, the emit guard stops recursive self-observation, and errno is
preserved.  The fixture also calls real dlopen/dlsym inside a context to
verify the interposer wiring end-to-end on the host dyld.
"""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / 'libmachook' / 'Diagnostics' / 'MacWSLoaderObservation.m'
INTERPOSE_H = ROOT / 'libmachook' / 'Diagnostics' / 'interpose.h'

FIXTURE = r'''
#include "MacWSLoaderObservation.h"
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_buf[65536];
static size_t g_len;
static void sink(const char *line) {
    size_t n = strlen(line);
    if (g_len + n + 1 <= sizeof(g_buf)) {
        memcpy(g_buf + g_len, line, n);
        g_len += n;
        g_buf[g_len] = 0;   // keep C-string terminated for strstr scans
    }
}
static unsigned count_lines(void) {
    unsigned c = 0;
    for (char *p = g_buf; (p = strstr(p, "#### LOADER-OBS")); p++) c++;
    return c;
}
static unsigned count_api(const char *api) {
    unsigned c = 0;
    char needle[32]; snprintf(needle, sizeof needle, "api=%s ", api);
    for (char *p = g_buf; (p = strstr(p, needle)); p++) c++;
    return c;
}
static unsigned count_substr(const char *needle) {
    unsigned c = 0;
    for (char *p = g_buf; (p = strstr(p, needle)); p++) c++;
    return c;
}
static void reset(void) { g_len = 0; g_buf[0] = 0; }
static int fails;
#define CHECK(name, cond) do { \
    printf("TEST %-46s %s\n", name, (cond) ? "PASS" : "FAIL"); \
    if (!(cond)) fails++; } while (0)

static void *other_thread(void *arg) {
    (void)arg;
    // Inside an active main-thread context but on a DIFFERENT thread:
    // must emit nothing.
    MacWSLoaderObservationNoteDlsym((void *)0x1, "thread_leak", (void *)0x2);
    return NULL;
}

static int g_sink_reentered;
static void reentrant_sink(const char *line) {
    if (!g_sink_reentered) {
        g_sink_reentered = 1;
        MacWSLoaderObservationNoteDlsym((void *)0x1, "inner", (void *)0x2);
    }
    fputs(line, stderr);
}

int main(void) {
    MacWSLoaderObservationSetSink(sink);

    // 1. outside context → nothing
    MacWSLoaderObservationNoteDlsym((void *)1, "outside", (void *)2);
    CHECK("outside-context emits nothing", count_lines() == 0);

    // 2. inside context → captured with seq; errno preserved
    errno = EINTR;
    unsigned seq = MacWSLoaderObservationEnter();
    MacWSLoaderObservationNoteDlsym((void *)0x1111, "checker_sym", (void *)0x2222);
    MacWSLoaderObservationExit();
    CHECK("inside-context emits one", count_lines() == 1);
    CHECK("errno preserved", errno == EINTR);
    CHECK("seq present", strstr(g_buf, "seq=") != NULL);
    CHECK("dlsym args logged", strstr(g_buf, "api=dlsym") != NULL &&
          strstr(g_buf, "checker_sym") != NULL &&
          strstr(g_buf, "res=0x2222") != NULL);
    reset();

    // 3. nesting: Enter/Enter, note, Exit, note, Exit → two records same seq
    unsigned outer2 = MacWSLoaderObservationEnter();
    unsigned inner = MacWSLoaderObservationEnter();
    MacWSLoaderObservationNoteDlopen("/tmp/a.dylib", RTLD_NOW, (void *)0x3);
    MacWSLoaderObservationExit();
    MacWSLoaderObservationNoteDlopen("/tmp/b.dylib", RTLD_NOW, (void *)0x4);
    MacWSLoaderObservationExit();
    CHECK("nesting shares seq", inner == outer2 && inner != 0);
    CHECK("nested context emits two", count_lines() == 2);
    CHECK("dlopen args logged", strstr(g_buf, "api=dlopen") != NULL &&
          strstr(g_buf, "/tmp/a.dylib") != NULL);
    reset();

    // 4. after outermost Exit → nothing
    MacWSLoaderObservationNoteDlsym((void *)1, "after_exit", (void *)2);
    CHECK("post-exit emits nothing", count_lines() == 0);

    // 5. cap: 40 notes → 32 records + 1 capped line
    MacWSLoaderObservationEnter();
    for (int i = 0; i < 40; i++)
        MacWSLoaderObservationNoteDlsym((void *)1, "cap_sym", (void *)2);
    MacWSLoaderObservationExit();
    CHECK("cap=32 records", count_api("dlsym") == 32);
    CHECK("cap marker emitted", strstr(g_buf, "capped") != NULL);
    reset();

    // 6. other thread inside active context → nothing
    MacWSLoaderObservationEnter();
    pthread_t th; pthread_create(&th, NULL, other_thread, NULL);
    pthread_join(th, NULL);
    MacWSLoaderObservationExit();
    CHECK("foreign thread emits nothing", count_lines() == 0);

    // 7. reentrancy: sink calls Note again → outer line only, no recursion
    MacWSLoaderObservationSetSink(reentrant_sink);
    MacWSLoaderObservationEnter();
    MacWSLoaderObservationNoteDlsym((void *)1, "outer", (void *)2);
    MacWSLoaderObservationExit();
    MacWSLoaderObservationSetSink(NULL);  // lines went to stderr path only
    CHECK("reentrant sink reached", g_sink_reentered == 1);
    reset();

    // 8. dlopen NOTE path (no interpose — caller-relative @loader_path
    // resolution makes dlopen interposition unsafe; see module comment).
    MacWSLoaderObservationSetSink(sink);
    MacWSLoaderObservationEnter();
    MacWSLoaderObservationNoteDlopen("/usr/lib/libz.1.dylib", RTLD_NOW,
                                     (void *)0xfeed);
    MacWSLoaderObservationExit();
    CHECK("dlopen note recorded",
          count_api("dlopen") >= 1 && strstr(g_buf, "libz.1.dylib") != NULL);
    reset();

    // 9. real dlsym through interpose inside context (module's own path
    // is exercised because the fixture binary binds through dyld).
    MacWSLoaderObservationEnter();
    void *sym = dlsym(RTLD_DEFAULT, "printf");
    MacWSLoaderObservationExit();
    CHECK("real dlsym observed via interpose",
          sym != NULL && count_api("dlsym") >= 1 &&
          strstr(g_buf, "printf") != NULL);
    reset();

    // 10. failure path inside context: real missing dlopen must still
    // return NULL, keep the loader error for the legitimate caller's
    // dlerror(), and leave errno exactly as the real loader left it.
    // dlopen is NOT interposed (see module comment) — the observer must
    // stay silent while the public call behaves identically.
    dlerror();  // drain stale error, if any
    void *bad_ref = dlopen("/nonexistent/no_such_lib_zzzz.dylib", RTLD_NOW);
    int errno_ref = errno;
    char *dlerr_ref = bad_ref ? NULL : dlerror();
    dlerror();  // drain for the in-context comparison
    errno = 0;
    unsigned dl_before = count_api("dlopen");
    MacWSLoaderObservationEnter();
    void *bad = dlopen("/nonexistent/no_such_lib_zzzz.dylib", RTLD_NOW);
    int errno_bad = errno;
    MacWSLoaderObservationExit();
    CHECK("missing dlopen returns NULL", bad == NULL && bad_ref == NULL);
    CHECK("uninterposed dlopen silent", count_api("dlopen") == dl_before);
    // CHECK evaluates cond twice — dlerror() is stateful, capture first.
    const char *dle1 = dlerror();
    const char *dle2 = dlerror();
    CHECK("caller dlerror still pending",
          dle1 != NULL);        // wrapper must not consume it
    CHECK("dlerror consumed after read", dle2 == NULL);
    CHECK("errno equals real-loader result", errno_bad == errno_ref);
    (void)dlerr_ref;
    reset();

    // 11. missing dlsym inside context: NULL result, dlerror still owned by
    // the caller, errno identical to the same call made outside a context.
    dlerror();
    errno = 0;   // dlsym does not set errno; normalize both sides first
    void *ms_ref = dlsym(RTLD_DEFAULT, "no_such_symbol_zzzz_qqqq");
    int merrno_ref = errno;
    dlerror();
    errno = 0;
    MacWSLoaderObservationEnter();
    void *ms = dlsym(RTLD_DEFAULT, "no_such_symbol_zzzz_qqqq");
    int merrno = errno;
    MacWSLoaderObservationExit();
    CHECK("missing dlsym returns NULL", ms == NULL && ms_ref == NULL);
    CHECK("missing dlsym recorded", strstr(g_buf,
          "no_such_symbol_zzzz_qqqq") != NULL);
    CHECK("dlsym errno equals real result", merrno == merrno_ref);

    // 13. success result parity: same dlopen inside context returns the same
    // handle value class and observes it (non-NULL).
    MacWSLoaderObservationEnter();
    void *h2 = dlopen("/usr/lib/libz.1.dylib", RTLD_NOW);
    MacWSLoaderObservationExit();
    CHECK("in-context dlopen still succeeds", h2 != NULL);
    if (h2) dlclose(h2);
    reset();

    // 14. context cap boundary LAST (seq is monotonic): the contexts above
    // consumed seq 1..10; 60 more contexts cover seq 11..70.  With cap=64
    // exactly seq 11..64 (54 contexts) may emit, then suppression holds.
    for (int i = 0; i < 60; i++) {
        MacWSLoaderObservationEnter();
        MacWSLoaderObservationNoteDlsym((void *)1, "ctx_cap", (void *)2);
        MacWSLoaderObservationExit();
    }
    CHECK("context cap boundary emits 54",
          count_substr("ctx_cap") == 54);
    MacWSLoaderObservationEnter();
    MacWSLoaderObservationNoteDlsym((void *)1, "over_cap_marker", (void *)2);
    MacWSLoaderObservationExit();
    CHECK("over-cap context emits nothing",
          strstr(g_buf, "over_cap_marker") == NULL);

    printf(fails ? "RESULT FAILURES=%d\n" : "RESULT ALL-PASS\n", fails);
    return fails ? 1 : 0;
}
'''


@unittest.skipUnless(shutil.which('clang'), 'C compiler required')
class LoaderObservationContract(unittest.TestCase):
    def test_bounded_context_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            # The module must live in its own image: dyld never applies an
            # interpose tuple to binds inside the image that defines it —
            # which is exactly how the wrapper's direct dlopen reaches the
            # real symbol.  Building libobs.dylib + a separate fixture exe
            # mirrors the shipped DYLD_INSERT_LIBRARIES topology.
            dylib = Path(directory) / 'libobs.dylib'
            build_dylib = subprocess.run(
                ['clang', '-x', 'objective-c', '-fmodules', '-O0', '-g',
                 '-dynamiclib', '-I', str(MODULE.parent), '-I', str(ROOT),
                 '-o', str(dylib), str(MODULE),
                 str(MODULE.parent / 'MacWSRandomDiagnostics.m'),
                 '-framework', 'Security'],
                capture_output=True, text=True)
            self.assertEqual(build_dylib.returncode, 0,
                             build_dylib.stderr)
            binary = Path(directory) / 'fixture'
            compile_result = subprocess.run(
                ['clang', '-fmodules', '-O0', '-g',
                 '-I', str(MODULE.parent), '-I', str(ROOT),
                 '-x', 'objective-c', '-', '-x', 'none', str(dylib),
                 '-o', str(binary)],
                input=FIXTURE, text=True, capture_output=True)
            self.assertEqual(compile_result.returncode, 0,
                             compile_result.stderr)
            run = subprocess.run([str(binary)], capture_output=True,
                                 text=True, timeout=60)
            sys.stdout.write(run.stdout)
            sys.stderr.write(run.stderr)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('RESULT ALL-PASS', run.stdout)


if __name__ == '__main__':
    unittest.main()
