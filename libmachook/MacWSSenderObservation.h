// MacWSSenderObservation.h — D043/D044 dedicated record-only sender
// observation.
//
// DIAGNOSTIC / TEMPORARY SCAFFOLD — not a production feature and not a
// fix.  Records bounded evidence around the WindowServer event-post
// gate chain (CGXSenderCanSynthesizeEvents ->
// WSAuditTokenHoldsCachedProcessRight -> audit_token_check_tcc_access)
// so the producing layer of the rc=1002 deny can be identified.
// Record-only: no return value, cache, audit token, rightDesc, TCC
// state, identity or policy is ever modified.
//
// Gating contract (all must hold, else nothing installs/records):
//   1. the injecting process is the real WindowServer executable
//      (checked by the caller in mac_hooks.m)
//   2. dedicated sentinel /tmp/macws_sender_observation exists in the
//      chroot at install time AND at each record (live disarm).  This is
//      deliberately NOT the macws_runtime_diagnostics umbrella, which
//      also activates an alternate display producer — an unconfounded
//      switch.
//   3. per-site MACWSHookPermitV2 success (live SkyLight LC_UUID +
//      offset + prologue-words verified by the provider, fail closed).
//
// Concurrency contract (D044):
//   - Emits serialize on one mutex; a record's slot publish flag is set
//     release-after-memcpy, so readers never observe a half-written
//     record.
//   - The log fd opens at most once (pthread_once) with a BOUNDED open:
//     O_NONBLOCK so a FIFO/device at the path cannot stall the call,
//     O_NOFOLLOW | O_CLOEXEC, mode 0600.  The opened fd is then
//     fstat-validated: regular file AND owned by this euid AND not
//     over-permissive (an owned regular file with more than 0600 is
//     corrected in place via fchmod on the validated fd — race-free,
//     never a path chmod).  Any failure latches fd=-1; emits count as
//     dropped — bounded, no unbounded retry, no fd leak.
//   - Writes use a bounded retry loop (EINTR/short-write, <=8 passes);
//     failure counts as dropped, never spins.
//   - Wrappers prove an errno envelope across the WHOLE call: incoming
//     errno is captured before any helper (chain/TLS/capture/hook),
//     restored immediately before the original runs, and the original's
//     own errno is what the caller observes at return.
//   - Live disarm: while the sentinel is absent a wrapper performs no
//     record capture (no token/desc/lazy reads — g_obs_snaps stays
//     flat); the original still runs with the same errno envelope and
//     the call is counted via emit's disarmed-drop path.
//   - Nested gate->dispatcher->impl chains share one TLS invocation id
//     assigned at the outermost observed entry and cleared on unwind to
//     depth 0 — a standalone dispatcher/impl call gets its own id
//     flagged "standalone" instead of being paired to a stale gate.
//
// Expected prologues below are byte-verified against
// analysis/SkyLight_22F82 (LC_UUID 96676A53-B1E0-3D7E-B98B-B73873CD1880)
// and are the single source of truth for both the install path and the
// host-side pin test.

#ifndef MACWS_SENDER_OBSERVATION_H
#define MACWS_SENDER_OBSERVATION_H

#include <stdint.h>
#include <stdbool.h>

/* Record sites */
enum {
    MACWS_SENDER_OBS_SITE_GATE = 1,   /* CGXSenderCanSynthesizeEvents   */
    MACWS_SENDER_OBS_SITE_DISP = 2,   /* WSAuditTokenHoldsCachedProcessRight */
    MACWS_SENDER_OBS_SITE_IMPL = 3,   /* audit_token_check_tcc_access   */
};

/* Record flag bits */
#define MACWS_SENDER_OBS_F_AUDIT       0x01 /* container present     */
#define MACWS_SENDER_OBS_F_LAZY_PRE    0x02 /* lazyChecker set pre   */
#define MACWS_SENDER_OBS_F_LAZY_POST   0x04 /* lazyChecker set post  */
#define MACWS_SENDER_OBS_F_DESC        0x08 /* rightDesc captured    */
#define MACWS_SENDER_OBS_F_CHAIN_ROOT  0x10 /* outermost observed call */
#define MACWS_SENDER_OBS_F_STANDALONE  0x20 /* invoked outside a gate
                                               chain (own id)        */

/* Fixed-size raw record — 144 bytes, no pointers into WS memory kept. */
typedef struct {
    uint32_t magic;        /* 'MSOB' = 0x4d534f42                    */
    uint16_t version;      /* 2                                      */
    uint8_t  site;         /* MACWS_SENDER_OBS_SITE_*                */
    uint8_t  flags;        /* MACWS_SENDER_OBS_F_*                   */
    uint64_t seq;          /* global emit sequence (ring index)      */
    uint64_t tid;          /* pthread_self correlation id            */
    uint64_t invocation;   /* TLS nested-invocation id (0 = unset)   */
    int32_t  rc;           /* original function's return, unmodified */
    uint32_t audit_size;   /* gCurMsgAudit container size field      */
    uint64_t audit_ptr;    /* gCurMsgAudit container pointer value   */
    uint64_t lazy_pre;     /* lazyChecker cell before original       */
    uint64_t lazy_post;    /* lazyChecker cell after original        */
    uint64_t arg_ptr;      /* site arg identity (desc ptr / service) */
    uint8_t  token[32];    /* audit token bytes, verbatim            */
    uint8_t  desc[40];     /* rightDesc snapshot                     */
} MacWSSenderObsRecord;

#define MACWS_SENDER_OBS_RECORD_MAGIC 0x4d534f42u /* 'MSOB' */
#define MACWS_SENDER_OBS_RECORD_VERSION 2u
#define MACWS_SENDER_OBS_MAX_RECORDS 256u
_Static_assert(sizeof(MacWSSenderObsRecord) == 144,
               "record ABI must stay a fixed 144 bytes");

/* SkyLight-22F82 image-relative offsets (va - 0x1850e5000).  D044
 * correction: the D043 draft dropped a nibble on all three globals
 * (caught by independent frontend review); values below verified by
 * direct subtraction — 0x1d3dcb6c8-0x1850e5000 = 0x4ECE66C8,
 * 0x1d8ba61b8-0x1850e5000 = 0x53AC11B8, 0x1d8ba6b50-0x1850e5000 =
 * 0x53AC1B50. */
#define MACWS_SENDER_OBS_OFF_GATE  0x318464ull
#define MACWS_SENDER_OBS_OFF_DISP  0x3407ecull
#define MACWS_SENDER_OBS_OFF_IMPL  0x340da0ull
#define MACWS_SENDER_OBS_OFF_AUDIT_SLOT   0x53AC11B8ull /* 0x1d8ba61b8 */
#define MACWS_SENDER_OBS_OFF_RIGHT_DESC   0x4ECE66C8ull /* 0x1d3dcb6c8 */
#define MACWS_SENDER_OBS_OFF_LAZY_CHECKER 0x53AC1B50ull /* 0x1d8ba6b50 */

extern const uint32_t kMacWSSenderObsGatePrologue[4];  /* +0x318464 */
extern const uint32_t kMacWSSenderObsDispPrologue[4];  /* +0x3407ec */
extern const uint32_t kMacWSSenderObsImplPrologue[4];  /* +0x340da0 */

/* Sentinel existence check — dedicated toggle, uncached so removing the
 * file disarms on the very next event. */
bool macws_sender_observation_enabled(void);

/* Nested-invocation correlation.  Call chain_enter at the top of each
 * wrapper (before the original) and chain_exit before returning.  The
 * outermost observed entry assigns a new invocation id and returns
 * MACWS_SENDER_OBS_F_CHAIN_ROOT (gate site) or _F_STANDALONE (a
 * dispatcher/impl call seen outside any observed chain); nested entries
 * inherit the current id with no flag. */
uint8_t macws_sender_obs_chain_enter(uint8_t site, uint64_t *inv_out);
void    macws_sender_obs_chain_exit(void);

/* Original-function signatures (RE-confirmed against SkyLight-22F82
 * disasm; see .c wrapper comments). */
typedef int (*MacWSSenderObsGateFn)(void *, void *);
typedef int (*MacWSSenderObsDispFn)(void *, void *, void *);
typedef int (*MacWSSenderObsImplFn)(void *, void *);

/* Gate invocation environment — pointers to the caller's WS-local
 * variables (resolved at install time; a NULL variable skips that
 * capture) plus optional around-orig hooks that run inside the errno
 * envelope regardless of sender-observation arming (the loader-
 * observation window and the runtime-diagnostics text line live in
 * mac_hooks.m). */
typedef struct {
    void ***audit_slot;         /* &gCurMsgAudit holder var          */
    const uint8_t **right_desc; /* &desc holder var                  */
    unsigned (*pre_orig)(void *ctx);   /* runs before orig, post-restore */
    void (*post_orig)(void *ctx, const MacWSSenderObsRecord *rec,
                      uint32_t audit_euid, uint32_t audit_pid,
                      unsigned pre_tok);
    void *ctx;
} MacWSSenderObsGateEnv;

/* Wrapper-equivalent invoke functions — the real forwarding logic,
 * host-testable.  Each performs: capture incoming errno -> (armed?
 * chain_enter + bounded captures) -> env hooks -> restore incoming
 * errno -> orig -> capture orig errno -> emit (counts even disarmed)
 * -> chain_exit -> restore orig errno -> return orig's rc verbatim.
 * When the sentinel is absent no record capture runs at all. */
int macws_sender_obs_gate_invoke(MacWSSenderObsGateFn orig,
                                 void *a0, void *a1,
                                 const MacWSSenderObsGateEnv *env);
int macws_sender_obs_disp_invoke(MacWSSenderObsDispFn orig,
                                 void *audit, void *desc, void *impl);
int macws_sender_obs_impl_invoke(MacWSSenderObsImplFn orig,
                                 void *audit, void *service,
                                 volatile uint64_t **lazy_cell_pp);

/* Emit one bounded record.  Counts the site call unconditionally,
 * stores+publishes while the sequence budget remains, then only bumps
 * the dropped counter.  Serialized on one mutex; save/restore errno
 * internally.  Never dereferences WS memory — callers pass
 * already-captured bytes. */
void macws_sender_obs_emit(const MacWSSenderObsRecord *tmpl);

/* Test/inspection counters (read-only). */
uint64_t macws_sender_obs_seq(void);
uint64_t macws_sender_obs_dropped(void);
uint64_t macws_sender_obs_snaps(void); /* record-capture events (armed only) */
uint64_t macws_sender_obs_site_calls(uint8_t site);
const MacWSSenderObsRecord *macws_sender_obs_ring(uint64_t *count_out);

#ifdef MACWS_SENDER_OBS_TEST
/* Host-test seam — bounded writer, exported only in test builds. */
int macws_sender_obs_write_all(int fd, const void *buf, unsigned long sz);
#endif

#endif /* MACWS_SENDER_OBSERVATION_H */
