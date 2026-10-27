// MacWSSenderObservation.c — D043/D044 dedicated record-only sender
// observation.
//
// DIAGNOSTIC / TEMPORARY SCAFFOLD.  Record-only; no return value,
// cache, audit token, rightDesc, TCC, identity or policy state is
// modified.  See MacWSSenderObservation.h for the gating and
// concurrency contracts.

#include "MacWSSenderObservation.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Byte-verified against analysis/SkyLight_22F82
 * (LC_UUID 96676A53-B1E0-3D7E-B98B-B73873CD1880):
 *   +0x318464  pacibsp / sub sp,#0x70 / stp x29,x30,[sp,#0x60] / add x29,sp,#0x60
 *   +0x3407ec  pacibsp / sub sp,#0xb0 / stp x26,x25,[sp,#0x60] / stp x24,x23,[sp,#0x70]
 *   +0x340da0  pacibsp / sub sp,#0x60 / stp x22,x21,[sp,#0x30] / stp x20,x19,[sp,#0x40]
 */
const uint32_t kMacWSSenderObsGatePrologue[4] = {
    0xd503237f, 0xd101c3ff, 0xa9067bfd, 0x910183fd };
const uint32_t kMacWSSenderObsDispPrologue[4] = {
    0xd503237f, 0xd102c3ff, 0xa90667fa, 0xa9075ff8 };
const uint32_t kMacWSSenderObsImplPrologue[4] = {
    0xd503237f, 0xd10183ff, 0xa90357f6, 0xa9044ff4 };

#ifdef MACWS_SENDER_OBS_TEST
/* Host-test path overrides (never compiled into production objects). */
const char *g_macws_sender_obs_sentinel_path = "/tmp/macws_sender_observation";
const char *g_macws_sender_obs_log_path      = "/tmp/macws_sender_observation.log";
#define SENTINEL_PATH g_macws_sender_obs_sentinel_path
#define LOG_PATH      g_macws_sender_obs_log_path
#else
#define SENTINEL_PATH "/tmp/macws_sender_observation"
#define LOG_PATH      "/tmp/macws_sender_observation.log"
#endif

static MacWSSenderObsRecord g_obs_ring[MACWS_SENDER_OBS_MAX_RECORDS];
/* Per-slot publish flag: 1 only after the record memcpy is complete
 * (release store).  Ring readers never see a half-written record. */
static _Atomic uint8_t g_obs_pub[MACWS_SENDER_OBS_MAX_RECORDS];
static _Atomic uint64_t g_obs_seq;          /* emit sequence reserve  */
static _Atomic uint64_t g_obs_site_calls[4];
static _Atomic uint64_t g_obs_dropped;
static _Atomic uint64_t g_obs_invocations;  /* chain id source        */
static _Atomic uint64_t g_obs_snaps;        /* record-capture events  */
static pthread_mutex_t g_obs_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_obs_open_once = PTHREAD_ONCE_INIT;
static int g_obs_log_fd = -1;

/* Nested-invocation TLS state: depth + current id.  Cleared when the
 * outermost observed call unwinds, so a later standalone call on the
 * same thread cannot inherit a stale pairing. */
static __thread uint64_t t_obs_inv;
static __thread uint32_t t_obs_depth;

bool macws_sender_observation_enabled(void) {
    return access(SENTINEL_PATH, F_OK) == 0;
}

uint8_t macws_sender_obs_chain_enter(uint8_t site, uint64_t *inv_out) {
    uint8_t flag = 0;
    if (t_obs_depth == 0) {
        t_obs_inv = atomic_fetch_add_explicit(&g_obs_invocations, 1,
                                              memory_order_relaxed) + 1;
        flag = (site == MACWS_SENDER_OBS_SITE_GATE)
               ? MACWS_SENDER_OBS_F_CHAIN_ROOT
               : MACWS_SENDER_OBS_F_STANDALONE;
    }
    t_obs_depth++;
    if (inv_out) *inv_out = t_obs_inv;
    return flag;
}

void macws_sender_obs_chain_exit(void) {
    if (t_obs_depth == 0) return;    /* defensive: unbalanced exit */
    if (--t_obs_depth == 0) t_obs_inv = 0;
}

/* One-time log open, bounded and type/owner/mode validated:
 *   - O_NONBLOCK makes the open itself bounded: a FIFO at LOG_PATH
 *     fails ENXIO (or opens unblocked) and a device cannot stall —
 *     the blocking variant could hang WindowServer on a planted fifo.
 *   - O_NOFOLLOW rejects a symlinked path at open.
 *   - O_CLOEXEC keeps the fd out of exec'd children.
 *   - fstat on the OPENED fd (never a path stat — no TOCTOU) requires
 *     a regular file owned by this euid.
 *   - An owned regular file carrying more than 0600 is corrected via
 *     fchmod on the validated fd (race-free; never a path chmod) —
 *     D046 permits explicit safe correction only after validation.
 *     Non-regular or foreign-owned files are rejected outright.
 * Runs at most once — a failure latches g_obs_log_fd=-1 permanently
 * (bounded; every affected emit counts as dropped).  Exactly one open
 * attempt ever, so no fd leak. */
static void macws_sender_obs_open_log(void) {
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK |
                            O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_uid != geteuid()) {
        close(fd);
        return;
    }
    if ((st.st_mode & 07777) != 0600 &&
        fchmod(fd, 0600) != 0) {     /* owned regular but over-permissive */
        close(fd);
        return;
    }
    g_obs_log_fd = fd;
}

#ifdef MACWS_SENDER_OBS_TEST
int macws_sender_obs_write_all(int fd, const void *buf, unsigned long sz)
#else
static int macws_sender_obs_write_all(int fd, const void *buf,
                                      unsigned long sz)
#endif
{
    /* Bounded retry: at most 8 write passes total; EINTR retries within
     * the budget, short writes continue the remainder, anything else
     * fails.  Returns 0 on full write, -1 on bounded failure — never
     * spins. */
    const char *p = (const char *)buf;
    unsigned long left = sz;
    int passes = 0;
    while (left > 0 && passes < 8) {
        passes++;
        ssize_t w = write(fd, p, left);
        if (w > 0) { p += w; left -= (unsigned long)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        break; /* w==0 or real error */
    }
    return left == 0 ? 0 : -1;
}

void macws_sender_obs_emit(const MacWSSenderObsRecord *tmpl) {
    if (!tmpl || tmpl->site < MACWS_SENDER_OBS_SITE_GATE ||
        tmpl->site > MACWS_SENDER_OBS_SITE_IMPL)
        return;
    int saved_errno = errno;
    atomic_fetch_add_explicit(&g_obs_site_calls[tmpl->site], 1,
                              memory_order_relaxed);
    /* Serialized emit: reserve slot, fill, publish, log — one thread at
     * a time so log writes are ordered and slot publication is atomic
     * with respect to readers of g_obs_pub. */
    pthread_mutex_lock(&g_obs_mtx);
    if (!macws_sender_observation_enabled()) {
        atomic_fetch_add_explicit(&g_obs_dropped, 1, memory_order_relaxed);
        pthread_mutex_unlock(&g_obs_mtx);
        errno = saved_errno;
        return;
    }
    uint64_t s = atomic_fetch_add_explicit(&g_obs_seq, 1,
                                           memory_order_acq_rel);
    if (s >= MACWS_SENDER_OBS_MAX_RECORDS) {
        atomic_fetch_add_explicit(&g_obs_dropped, 1, memory_order_relaxed);
        pthread_mutex_unlock(&g_obs_mtx);
        errno = saved_errno;
        return;
    }
    MacWSSenderObsRecord *rec = &g_obs_ring[s];
    memcpy(rec, tmpl, sizeof(*rec));
    rec->magic = MACWS_SENDER_OBS_RECORD_MAGIC;
    rec->version = MACWS_SENDER_OBS_RECORD_VERSION;
    rec->seq = s;
    rec->tid = (uint64_t)(uintptr_t)pthread_self();
    /* Publish complete record before the log write. */
    atomic_store_explicit(&g_obs_pub[s], 1, memory_order_release);

    pthread_once(&g_obs_open_once, macws_sender_obs_open_log);
    int fd = g_obs_log_fd;
    if (fd >= 0 &&
        macws_sender_obs_write_all(fd, rec, sizeof(*rec)) != 0) {
        atomic_fetch_add_explicit(&g_obs_dropped, 1, memory_order_relaxed);
    } else if (fd < 0) {
        atomic_fetch_add_explicit(&g_obs_dropped, 1, memory_order_relaxed);
    }
    pthread_mutex_unlock(&g_obs_mtx);
    errno = saved_errno;
}

uint64_t macws_sender_obs_seq(void) {
    return atomic_load_explicit(&g_obs_seq, memory_order_acquire);
}
uint64_t macws_sender_obs_dropped(void) {
    return atomic_load_explicit(&g_obs_dropped, memory_order_acquire);
}
uint64_t macws_sender_obs_snaps(void) {
    return atomic_load_explicit(&g_obs_snaps, memory_order_acquire);
}
uint64_t macws_sender_obs_site_calls(uint8_t site) {
    if (site > 3) return 0;
    return atomic_load_explicit(&g_obs_site_calls[site],
                                memory_order_acquire);
}
const MacWSSenderObsRecord *macws_sender_obs_ring(uint64_t *count_out) {
    /* Only fully-published slots are visible. */
    uint64_t n = 0;
    for (unsigned i = 0; i < MACWS_SENDER_OBS_MAX_RECORDS; i++) {
        if (atomic_load_explicit(&g_obs_pub[i], memory_order_acquire))
            n++;
    }
    if (count_out) *count_out = n;
    return g_obs_ring;
}

/* ---------------- wrapper invoke functions (D046) ------------------
 * These are the real forwarding bodies the mac_hooks.m replacement
 * functions call — kept here so the host test can exercise the exact
 * wrapper path, not just the emitter.
 *
 * errno envelope (identical at all three boundaries):
 *   1. capture incoming errno BEFORE any helper (chain/TLS/capture/
 *      env hook) can touch it;
 *   2. record captures + env hooks run — they may clobber errno;
 *   3. restore incoming errno IMMEDIATELY before the original, so the
 *      original sees exactly what its caller saw;
 *   4. capture the original's errno right after it returns;
 *   5. emit/chain_exit/diag run — clobbering errno freely;
 *   6. restore the ORIGINAL's errno at return — the caller observes
 *      precisely what the unwrapped call would have produced.
 *
 * Live disarm: record-path captures (32B token copy, desc copy, lazy
 * reads, chain ids) only run while the dedicated sentinel exists —
 * checked once at wrapper entry; g_obs_snaps proves it stays flat
 * after disarm.  The gate's env-provided scalar reads (cred ptr/size/
 * euid/pid for the runtime-diagnostics CGXSYNTH line) are governed by
 * that separate sentinel inside the caller's post_orig hook and are
 * not record snapshots.  Emit is still invoked when disarmed so the
 * call is counted; it drops without storing or writing. */

int macws_sender_obs_gate_invoke(MacWSSenderObsGateFn orig,
                                 void *a0, void *a1,
                                 const MacWSSenderObsGateEnv *env) {
    int in_errno = errno;
    int armed = macws_sender_observation_enabled();
    MacWSSenderObsRecord rec = {0};
    rec.site = MACWS_SENDER_OBS_SITE_GATE;
    int entered = 0;
    const void *cred = NULL;
    uint32_t csz = 0, aeuid = 0, apid = 0;
    if (env && env->audit_slot && *env->audit_slot) {
        /* audit_slot points at the caller's void** variable which holds
         * the gCurMsgAudit cell address — double-deref reaches the
         * container, the same read the gate itself performs. */
        cred = **env->audit_slot;
        if (cred) {
            csz = *(volatile uint32_t *)((const char *)cred + 4);
            if (csz >= 0x34) {                /* scalars for the caller's
                                                 own diag line — not a
                                                 record token snapshot */
                aeuid = *(volatile uint32_t *)((const char *)cred + 0x18);
                apid  = *(volatile uint32_t *)((const char *)cred + 0x28);
            }
        }
    }
    if (armed) {
        uint64_t inv = 0;
        rec.flags |= macws_sender_obs_chain_enter(
            MACWS_SENDER_OBS_SITE_GATE, &inv);
        rec.invocation = inv;
        entered = 1;
        if (cred) {                            /* pre-orig verified read */
            rec.audit_ptr = (uint64_t)(uintptr_t)cred;
            rec.audit_size = csz;
            rec.flags |= MACWS_SENDER_OBS_F_AUDIT;
            if (csz >= 0x34)
                memcpy(rec.token, (const char *)cred + 0x14, 32);
        }
        if (env && env->right_desc && *env->right_desc) {
            memcpy(rec.desc, *env->right_desc, 40);
            rec.flags |= MACWS_SENDER_OBS_F_DESC;
        }
        atomic_fetch_add_explicit(&g_obs_snaps, 1, memory_order_relaxed);
    }
    unsigned pre_tok =
        (env && env->pre_orig) ? env->pre_orig(env->ctx) : 0;
    errno = in_errno;
    int r = orig(a0, a1);
    int call_errno = errno;
    rec.rc = r;
    if (env && env->post_orig) {
        /* Scalar view for the caller's own diagnostics line — captured
         * pre-orig in the verified-readable window; no token bytes are
         * handed out. */
        if (!rec.audit_ptr) rec.audit_ptr = (uint64_t)(uintptr_t)cred;
        if (!rec.audit_size) rec.audit_size = csz;
        env->post_orig(env->ctx, &rec, aeuid, apid, pre_tok);
    }
    macws_sender_obs_emit(&rec);
    if (entered) macws_sender_obs_chain_exit();
    errno = call_errno;
    return r;
}

int macws_sender_obs_disp_invoke(MacWSSenderObsDispFn orig,
                                 void *audit, void *desc, void *impl) {
    int in_errno = errno;
    int armed = macws_sender_observation_enabled();
    MacWSSenderObsRecord rec = {0};
    rec.site = MACWS_SENDER_OBS_SITE_DISP;
    int entered = 0;
    if (armed) {
        uint64_t inv = 0;
        rec.flags |= macws_sender_obs_chain_enter(
            MACWS_SENDER_OBS_SITE_DISP, &inv);
        rec.invocation = inv;
        entered = 1;
        rec.arg_ptr = (uint64_t)(uintptr_t)impl; /* which impl pointer */
        if (audit) memcpy(rec.token, audit, 32); /* caller-stack copy  */
        if (desc)  { memcpy(rec.desc, desc, 40);
                     rec.flags |= MACWS_SENDER_OBS_F_DESC; }
        atomic_fetch_add_explicit(&g_obs_snaps, 1, memory_order_relaxed);
    }
    errno = in_errno;
    int r = orig(audit, desc, impl);
    int call_errno = errno;
    rec.rc = r;
    macws_sender_obs_emit(&rec);
    if (entered) macws_sender_obs_chain_exit();
    errno = call_errno;
    return r;
}

int macws_sender_obs_impl_invoke(MacWSSenderObsImplFn orig,
                                 void *audit, void *service,
                                 volatile uint64_t **lazy_cell_pp) {
    int in_errno = errno;
    int armed = macws_sender_observation_enabled();
    MacWSSenderObsRecord rec = {0};
    rec.site = MACWS_SENDER_OBS_SITE_IMPL;
    int entered = 0;
    uint64_t pre = 0;
    if (armed) {
        uint64_t inv = 0;
        rec.flags |= macws_sender_obs_chain_enter(
            MACWS_SENDER_OBS_SITE_IMPL, &inv);
        rec.invocation = inv;
        entered = 1;
        rec.arg_ptr = (uint64_t)(uintptr_t)service;
        if (audit) memcpy(rec.token, audit, 32);
        if (lazy_cell_pp && *lazy_cell_pp) pre = **lazy_cell_pp;
        atomic_fetch_add_explicit(&g_obs_snaps, 1, memory_order_relaxed);
    }
    errno = in_errno;
    int r = orig(audit, service);
    int call_errno = errno;
    rec.rc = r;
    if (entered) {
        uint64_t post =
            (lazy_cell_pp && *lazy_cell_pp) ? **lazy_cell_pp : 0;
        rec.lazy_pre = pre;
        rec.lazy_post = post;
        if (pre)  rec.flags |= MACWS_SENDER_OBS_F_LAZY_PRE;
        if (post) rec.flags |= MACWS_SENDER_OBS_F_LAZY_POST;
    }
    macws_sender_obs_emit(&rec);
    if (entered) macws_sender_obs_chain_exit();
    errno = call_errno;
    return r;
}
