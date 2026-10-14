// MacWSLoaderObservation — bounded, thread-local loader-call witness.
//
// Purpose (R004B): the CGXSYNTH gate lazily resolves a checker through an
// unknown path.  Public ABI records of dlopen/dlsym issued *while the real
// gate executes* may name that checker without any new private offsets.
// Observation is strictly scoped to a thread-local context entered around
// the already-validated original-gate call in mac_hooks.m.
//
// Contract:
//  - record-only; every API returns the real result unchanged;
//  - errno is preserved across every emit;
//  - dlerror is never called here (it consumes pending loader errors);
//  - reentrant loader calls made *by this file's own logging* are skipped;
//  - per-context records are capped; string arguments are truncated.

@import Darwin;

#import "MacWSLoaderObservation.h"
#import "interpose.h"

#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdarg.h>

#define MACWS_OBS_RECORD_CAP 32u
#define MACWS_OBS_SYM_LEN 96
#define MACWS_OBS_PATH_LEN 160

static __thread int g_obs_depth;
static __thread int g_obs_emitting;
static __thread unsigned g_obs_count;
static __thread unsigned g_obs_seq;
static _Atomic unsigned g_obs_seq_counter;
typedef void (*MacWSObsSink)(const char *);
static _Atomic(MacWSObsSink) g_obs_sink;

unsigned MacWSLoaderObservationEnter(void) {
    if (g_obs_depth++ == 0) {
        g_obs_seq = atomic_fetch_add_explicit(&g_obs_seq_counter, 1,
                                              memory_order_relaxed) + 1;
        g_obs_count = 0;
    }
    return g_obs_seq;
}

void MacWSLoaderObservationExit(void) {
    if (g_obs_depth > 0) --g_obs_depth;
}

static void MacWSLoaderObservationEmit(const char *fmt, ...) {
    if (g_obs_emitting) return;  // logging must not observe itself
    g_obs_emitting = 1;
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    void (*sink)(const char *) =
        atomic_load_explicit(&g_obs_sink, memory_order_relaxed);
    if (sink) {
        sink(line);
    } else {
        fputs(line, stderr);
        fflush(stderr);
    }
    g_obs_emitting = 0;
}

#define MACWS_OBS_CONTEXT_CAP 64u

static void MacWSLoaderObservationNote(const char *fmt, ...) {
    if (g_obs_depth <= 0) return;
    // Order matters: check the emit guard and the caps BEFORE consuming a
    // record slot — a reentrant/overflow note must be free.
    if (g_obs_emitting) return;
    if (g_obs_seq > MACWS_OBS_CONTEXT_CAP) return;  // bounded gate contexts
    int saved = errno;
    if (g_obs_count >= MACWS_OBS_RECORD_CAP) {
        if (g_obs_count == MACWS_OBS_RECORD_CAP) {
            g_obs_count++;
            MacWSLoaderObservationEmit(
                "#### LOADER-OBS seq=%u pid=%d capped\n",
                g_obs_seq, getpid());
        }
        errno = saved;
        return;
    }
    g_obs_count++;
    char record[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(record, sizeof(record), fmt, ap);
    va_end(ap);
    MacWSLoaderObservationEmit(
        "#### LOADER-OBS seq=%u pid=%d tid=%llu %s\n",
        g_obs_seq, getpid(),
        (unsigned long long)pthread_mach_thread_np(pthread_self()),
        record);
    errno = saved;
}

void MacWSLoaderObservationNoteDlsym(void *handle, const char *symbol,
                                     void *result) {
    MacWSLoaderObservationNote("api=dlsym handle=%p sym=%.*s res=%p",
                               handle, MACWS_OBS_SYM_LEN,
                               symbol ? symbol : "(null)", result);
}

void MacWSLoaderObservationNoteDlopen(const char *path, int mode,
                                      void *result) {
    MacWSLoaderObservationNote("api=dlopen mode=%#x path=%.*s res=%p",
                               mode, MACWS_OBS_PATH_LEN,
                               path ? path : "(null)", result);
}

void MacWSLoaderObservationSetSink(void (*sink)(const char *)) {
    atomic_store_explicit(&g_obs_sink, (MacWSObsSink)sink, memory_order_relaxed);
}

// NOTE (2026-10-14, runtime-confirmed): there is intentionally NO dlopen
// interposer here.  dyld resolves `@loader_path` relative to the image that
// *calls* dlopen; an interposed shim makes libmachook the caller, so
// launchservicesd's `dlopen("@loader_path/launchservicesd.dylib")` resolved
// under /usr/local/lib instead of /System/Library/CoreServices and the
// whole gui-start pipeline failed (artifacts/r004b-gui-start.json).
// Observing the RESULT forces an extra frame — a transparent forward is
// impossible through the public ABI.  The pre-existing dlsym interposer in
// MacWSRandomDiagnostics.m is kept as-is; dlsym handles are NOT universally
// caller-independent either (RTLD_NEXT/RTLD_SELF walk the caller's image
// list), so no general safety claim is made — only that its interposer
// predates this observer and unchanged behavior is test-covered.
// NoteDlopen remains available for direct callers that can note without
// interposing.

