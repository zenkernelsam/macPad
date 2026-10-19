/*
 * D141/D143/D150b — isolated record-only SkyLight service-timing
 * prototype. Diagnostic-only; never linked into production libmachook.
 *
 * Models the D140 service-half record layer. D150b consolidated the
 * D145/D149–D159 acceptance packet; contract lines D1–D10 below are the
 * binding invariants.
 *
 * Ring protocol — MPSC producers, single consumer:
 *   - producers CAS-claim a head index; head − tail >= CAP → drop+count.
 *     The claimant exclusively owns slot[claim % CAP] until the release
 *     commit of its seq tag. (A3)
 *   - the single consumer drains only the contiguous committed prefix:
 *     stops at the first claimed-but-uncommitted slot and never advances
 *     tail past it — a slot can never be reused while its producer may
 *     still be writing. (D141 skip-past race closed structurally.)
 *   - consumer lease: first drain caller claims a process-lifetime lease
 *     by tid; foreign threads return -1. MPMC is not claimed. (D5)
 *
 * Init lifecycle: UNINIT → INITIALIZING → ARMED | DISARMED. Exactly one
 * thread probes the sentinel. Concurrent gate callers re-attempt the
 * UNINIT→INITIALIZING CAS on every UNINIT observation (a set_generation
 * window) and spin (bounded) across INITIALIZING — nobody observes a
 * false stable "disarmed" while a decision is pending. Generation setup
 * is accepted only in UNINIT, serialized before the gate decision. (C3)
 */

#ifndef D141_SVCTIME_H
#define D141_SVCTIME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>

#define D141_SVC_MAGIC    0x53564331u /* 'SVC1' */
#define D141_SVC_VERSION  3u          /* D150b revision */
#define D141_RING_CAP     256u        /* bounded: producers drop, never block */

enum d141_kind {
    D141K_PASS_ENTER      = 1,  /* CGXRunOneServicesPass entry  (0x33C63C) */
    D141K_PASS_EXIT       = 2,  /* CGXRunOneServicesPass return          */
    D141K_DISPATCH_BEGIN  = 3,  /* _post_port_data entry        (0x338E48) */
    D141K_HANDLER_BEGIN   = 4,  /* rendezvousHandler entry      (0x1064A8) */
    D141K_IMPL_INVOKE     = 5,  /* _CGXHandleMessage entry      (0x1064E0) */
    D141K_REPLY_ENQUEUE   = 6,  /* CGXPostReplyMessage entry    (0x33C3A4) */
    D141K_FLUSH_BEGIN     = 7,  /* _push_out_message entry      (0x33C4FC) */
    D141K_MACH_CALL       = 8,  /* mach_msg* call leg  (existing interpose)*/
    D141K_MACH_RET        = 9,  /* mach_msg* return leg                  */
};

typedef struct {
    uint64_t magic;   /* D141_SVC_MAGIC                                */
    uint64_t seq;     /* attempt identifier — NOT claim index (D1)     */
    uint64_t ts_ns;   /* CLOCK_MONOTONIC nanoseconds                   */
    uint64_t tid;     /* pthread_threadid_np — measured, never assumed */
    int32_t  pid;
    uint32_t kind;    /* enum d141_kind                                */
    uint64_t msgid;   /* raw msgh_id / in-flight id — never translated */
    uint64_t aux;     /* kind-specific, verbatim (see leg contract)    */
    uint64_t gen;     /* generation tag = (pid<<32) ^ uuid_low64       */
} d141_record;        /* 64 bytes fixed — memcpy-able, no pointers     */

/* ---- contract lines (binding; restated in README) ------------------- */
/*
 * D1  drain order == claim order. record.seq is an ATTEMPT identifier
 *     allocated before the ring claim — distinct from the slot-internal
 *     seq (claim index + 1, ring bookkeeping never exposed to decoders).
 *     record.seq gaps denote attempts that never reached the ring
 *     (bounded drop or dead-mid-emit) — never committed-record loss — and
 *     seq may be non-monotonic across producers; pairers must buffer
 *     reorder.
 * C3  under documented order (set_generation BEFORE gate_init), no
 *     record emits before generation publishes; reverse order yields
 *     zeroed pid/gen — forbidden in production wiring.
 * A3  only the head-CAS winner may write a slot: s->rec / s->seq stores
 *     belong solely to the thread holding that claim.
 * D5  the consumer lease is process-lifetime and never released — a dead
 *     consumer thread permanently disables draining (records accumulate/
 *     drop; bounded, by design). No d141_drain from signal or other async
 *     context even on the owner tid — same-tid reentry is unserialized.
 *     Symmetric producer bound: a producer that dies or stalls holding a
 *     claim permanently head-of-line-blocks the ring at the CAP bound —
 *     every subsequent emit drops counted; the ring never corrupts and
 *     g_dropped is the honest record of the stall.
 * D8  the armed timing window is exclusive with the MACWS_MACH_MSG_ALL
 *     umbrella trace; enforcement is integration-layer responsibility;
 *     the prototype flags the obligation.
 * D9  emit→push worklist: armed acquire check, errno save, g_seq
 *     fetch_add, clock_gettime(MONOTONIC), pthread_threadid_np, two
 *     relaxed loads (pid, gen), g_head acquire load, bounded claim loop
 *     (g_tail acquire load + weak CAS per iteration), optional
 *     D141_TEST_HOOKS callback (test builds only), fixed 64-byte copy,
 *     release seq commit, one g_emitted increment (plus g_dropped on the
 *     refused path), errno restore — no lock, write(2), allocation, or
 *     blocking call.
 * D10 in-candidate: emit/push/drain never call mach* or any wrapped
 *     function. Integration constraint: the same property must hold
 *     inside the production hook path.
 */

/* Mach-leg contract (I5, per D151 source audit + D155/D156 corrections) —
 *
 *   CALL leg: msgid = send-header msgh_id when (option & MACH_SEND_MSG),
 *             else 0. For mach_msg2 the send id comes from the packed
 *             register ABI (id = hi32(arg4), bits = lo32(arg2)).
 *             aux = option VERBATIM. Send fields MUST be captured
 *             pre-call — mach_msg shares one buffer, so post-call
 *             message->msgh_id is the RECEIVED id.
 *   RET leg:  msgid = recv-header id iff kr == MACH_MSG_SUCCESS &&
 *             (option & MACH_RCV_MSG) — read `message` for mach_msg
 *             (overwritten), `receive_buffer` for overwrite, `data` for
 *             msg2 — else 0. aux = kr VERBATIM. Option bits are NOT
 *             carried on RET: they stay verbatim on the paired CALL's
 *             aux and are recovered via the same-tid pairing rule.
 *   MACH_RCV_TOO_LARGE: msgid = 0 (conservative — only msgh_size is
 *             defined; no header validity claimed without an xnu cite).
 *   Pairing:  nearest unmatched same-tid CALL — required for ASYNC
 *             reentrancy (signal-handler mach calls); ordinary trap
 *             nesting is impossible (kernel trap, no user code in-window).
 *   Reply correlation id−100 is decoder-side; records carry raw ids.
 *   Timing legs must NOT inherit the trace-only 40000–41000 filter.
 *   Integration note: all three real wrappers (mach_msg,
 *   mach_msg_overwrite, mach_msg2_internal) can host both legs — verified
 *   at the record API; no integration edits in this task.
 */

/*
 * Init-time-only gate. First caller probes the sentinel and publishes
 * ARMED/DISARMED; a caller that loses the UNINIT CAS re-attempts it on
 * each UNINIT observation (covering a set_generation window) and spins
 * across INITIALIZING until a final state lands.
 *   returns 1 armed, 0 disarmed, -1 pathological unsettled (observable;
 *   callers treat as disarmed).
 */
int      d141_gate_init(const char *sentinel_path);
int      d141_armed(void);

/* Generation tag — accepted only while UNINIT; serialized before the
 * gate decision. Post-init calls return -1. */
int      d141_set_generation(int32_t pid, const unsigned char uuid[16]);

/* Emit one record. Silent when disarmed. errno preserved. Nonblocking:
 * full ring → drop + counter. */
void     d141_emit(uint32_t kind, uint64_t msgid, uint64_t aux);

/* Entry-wrapper brackets — orig's return/errno pass through untouched. */
uintptr_t d141_wrap_entry(uint32_t kind, uint64_t msgid, uint64_t aux,
                          uintptr_t (*fn)(void *), void *arg);
/* PASS_ENTER/PASS_EXIT bracket. PASS_EXIT carries aux=0: the per-pass
 * handled count is NOT carried on the record (I4 contract route) — it is
 * derivable by counting DISPATCH/HANDLER/IMPL edges between ENTER/EXIT on
 * the same tid; decoder rule documented in README. */
uintptr_t d141_wrap_pass (uintptr_t (*fn)(void *), void *arg);

/* Mach interpose legs — aux verbatim: option on CALL, kr on RET. */
void     d141_mach_call(uint64_t msgid, uint64_t option);
void     d141_mach_ret (uint64_t msgid, uint64_t kr);

/*
 * Single-consumer drain — contiguous committed prefix only; foreign
 * threads get -1 (lease enforced). Returns count copied.
 */
ssize_t  d141_drain(d141_record *out, size_t max);

uint64_t d141_dropped(void);        /* records refused by a full ring    */
uint64_t d141_uncommitted(void);    /* drains stopped on an open slot    */
uint64_t d141_emitted(void);        /* records committed to the ring     */
uint64_t d141_drain_refused(void);  /* drains rejected by consumer lease */
uint64_t d141_gate_spun(void);      /* gate calls that entered the retry
                                       path (losing-CAS observability)   */
uint64_t d141_head(void);           /* producer claim index (observability) */
uint64_t d141_tail(void);           /* consumer index    (observability) */

/* Test-only reset + deterministic hooks (documented; not production).   */
void     d141_reset_for_test(void);
#ifdef D141_TEST_HOOKS
extern void (*d141_hook_post_claim)(void); /* after head claim, pre-commit */
extern void (*d141_hook_gen_init)(void);   /* inside set_generation while
                                              holding INITIALIZING         */
#endif

#endif /* D141_SVCTIME_H */
