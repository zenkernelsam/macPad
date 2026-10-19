/*
 * D141/D143/D150b — isolated record-only SkyLight service-timing
 * prototype. See d141_svctime.h for the binding contract. Diagnostic-only;
 * host-side harness. D9 worklist is honored literally in d141_emit/push.
 */

#include "d141_svctime.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum d141_init_state { UNINIT = 0, INITIALIZING = 1, ARMED = 2, DISARMED = 3 };

typedef struct {
    _Atomic uint64_t seq;   /* claim index + 1 once the record is committed */
    d141_record      rec;
} d141_slot;

static d141_slot        g_ring[D141_RING_CAP];
static _Atomic uint64_t g_head;     /* next claim index (producers)        */
static _Atomic uint64_t g_tail;     /* next consume index (single consumer)*/
static _Atomic uint64_t g_seq;      /* attempt identifier (pre-claim)      */
static _Atomic uint64_t g_dropped;
static _Atomic uint64_t g_uncommitted;
static _Atomic uint64_t g_emitted;
static _Atomic uint64_t g_drain_refused;
static _Atomic uint64_t g_gate_spun;
static _Atomic int      g_state;    /* enum d141_init_state                */
static _Atomic uint64_t g_consumer; /* owner tid; 0 = unclaimed            */
static _Atomic uint64_t g_gen;
static _Atomic int      g_pid;

#ifdef D141_TEST_HOOKS
void (*d141_hook_post_claim)(void) = NULL;
void (*d141_hook_gen_init)(void) = NULL;
#endif

static uint64_t d141_now_ns(void) {
    /* CLOCK_MONOTONIC — the pinned D107/D109 pace-record domain. */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint64_t d141_tid(void) {
    uint64_t t = 0;
    pthread_threadid_np(NULL, &t);
    return t;
}

int d141_set_generation(int32_t pid, const unsigned char uuid[16]) {
    /* Accepted only in UNINIT; holds INITIALIZING across the stores so the
     * publication is serialized before any gate decision (C3). */
    int expected = UNINIT;
    if (!atomic_compare_exchange_strong_explicit(&g_state, &expected,
            INITIALIZING, memory_order_acq_rel, memory_order_acquire))
        return -1;                  /* decision or another setup in flight */
#ifdef D141_TEST_HOOKS
    if (d141_hook_gen_init) d141_hook_gen_init();
#endif
    uint64_t lo = 0;
    memcpy(&lo, uuid, 8);
    atomic_store_explicit(&g_pid, pid, memory_order_relaxed);
    atomic_store_explicit(&g_gen,
        ((uint64_t)(uint32_t)pid << 32) ^ lo, memory_order_relaxed);
    atomic_store_explicit(&g_state, UNINIT, memory_order_release);
    return 0;
}

int d141_gate_init(const char *sentinel_path) {
    unsigned spins = 0;
    int spun = 0;
    for (;;) {
        int s = atomic_load_explicit(&g_state, memory_order_acquire);
        if (s == ARMED)    return 1;
        if (s == DISARMED) return 0;
        if (s == UNINIT) {
            /* Re-attempt the claim on every UNINIT observation — a
             * set_generation window must not permanently fail the first
             * gate call (I1). */
            int expected = UNINIT;
            if (atomic_compare_exchange_strong_explicit(&g_state,
                    &expected, INITIALIZING,
                    memory_order_acq_rel, memory_order_acquire)) {
                int armed = (sentinel_path &&
                             access(sentinel_path, F_OK) == 0);
                atomic_store_explicit(&g_state,
                    armed ? ARMED : DISARMED, memory_order_release);
                return armed;
            }
            /* lost the retry CAS — fall into the bounded spin */
        }
        /* INITIALIZING observed (or lost CAS): wait for the final state. */
        if (!spun) {
            spun = 1;
            atomic_fetch_add_explicit(&g_gate_spun, 1,
                memory_order_relaxed);
        }
        if (++spins > 40000000u)
            return -1;              /* pathological: decision never landed */
    }
}

int d141_armed(void) {
    return atomic_load_explicit(&g_state, memory_order_acquire) == ARMED;
}

static void d141_push(const d141_record *r) {
    uint64_t c = atomic_load_explicit(&g_head, memory_order_acquire);
    for (;;) {
        if (c - atomic_load_explicit(&g_tail, memory_order_acquire)
                >= D141_RING_CAP) {
            atomic_fetch_add_explicit(&g_dropped, 1, memory_order_relaxed);
            return;                 /* bounded, nonblocking: drop + count  */
        }
        if (atomic_compare_exchange_weak_explicit(&g_head, &c, c + 1,
                memory_order_acq_rel, memory_order_acquire))
            break;
    }
#ifdef D141_TEST_HOOKS
    if (d141_hook_post_claim) d141_hook_post_claim();
#endif
    /* A3: only this thread (the head-CAS winner) may write the slot. */
    d141_slot *s = &g_ring[c % D141_RING_CAP];
    s->rec = *r;                    /* fixed 64-byte copy — nonblocking    */
    atomic_store_explicit(&s->seq, c + 1, memory_order_release); /* commit */
    atomic_fetch_add_explicit(&g_emitted, 1, memory_order_relaxed);
}

void d141_emit(uint32_t kind, uint64_t msgid, uint64_t aux) {
    if (!d141_armed())
        return;                     /* disarmed: zero writes, zero drops   */
    int saved = errno;
    d141_record r = {
        .magic = D141_SVC_MAGIC,
        .seq   = atomic_fetch_add_explicit(&g_seq, 1, memory_order_relaxed),
        .ts_ns = d141_now_ns(),
        .tid   = d141_tid(),
        .pid   = atomic_load_explicit(&g_pid, memory_order_relaxed),
        .kind  = kind,
        .msgid = msgid,
        .aux   = aux,
        .gen   = atomic_load_explicit(&g_gen, memory_order_relaxed),
    };
    d141_push(&r);
    errno = saved;
}

uintptr_t d141_wrap_entry(uint32_t kind, uint64_t msgid, uint64_t aux,
                          uintptr_t (*fn)(void *), void *arg) {
    d141_emit(kind, msgid, aux);
    return fn(arg);                 /* no state is held across the call    */
}

uintptr_t d141_wrap_pass(uintptr_t (*fn)(void *), void *arg) {
    d141_emit(D141K_PASS_ENTER, 0, 0);
    uintptr_t r = fn(arg);          /* enter/exit bracket; orig untouched  */
    /* I4 contract route: handled-count is derived decoder-side by counting
     * DISPATCH/HANDLER/IMPL edges between ENTER and EXIT on the same tid —
     * aux stays 0 rather than carrying a pre-call-fixed value. */
    d141_emit(D141K_PASS_EXIT, 0, 0);
    return r;
}

void d141_mach_call(uint64_t msgid, uint64_t option) {
    int saved = errno;              /* per-leg errno preservation (I5/D7)  */
    d141_emit(D141K_MACH_CALL, msgid, option);
    errno = saved;
}

void d141_mach_ret(uint64_t msgid, uint64_t kr) {
    int saved = errno;
    d141_emit(D141K_MACH_RET, msgid, kr);
    errno = saved;
}

ssize_t d141_drain(d141_record *out, size_t max) {
    /* D5: single-consumer lease, process-lifetime, never released. */
    uint64_t tid = d141_tid();
    uint64_t owner = 0;
    if (!atomic_compare_exchange_strong_explicit(&g_consumer, &owner, tid,
            memory_order_acq_rel, memory_order_acquire) &&
        owner != tid) {
        atomic_fetch_add_explicit(&g_drain_refused, 1, memory_order_relaxed);
        return -1;
    }
    /* Contiguous committed prefix only — stop at the first
     * claimed-but-uncommitted slot so a producer can never publish into a
     * slot already reclaimed. Bounded: ≤ CAP slot probes per call. */
    uint64_t tail = atomic_load_explicit(&g_tail, memory_order_acquire);
    const uint64_t head = atomic_load_explicit(&g_head, memory_order_acquire);
    uint64_t limit = head - tail;
    if (limit > D141_RING_CAP) limit = D141_RING_CAP;
    size_t n = 0;
    uint64_t i = 0;
    for (; n < max && i < limit; i++) {
        d141_slot *s = &g_ring[(tail + i) % D141_RING_CAP];
        if (atomic_load_explicit(&s->seq, memory_order_acquire)
                != tail + i + 1) {
            atomic_fetch_add_explicit(&g_uncommitted, 1,
                memory_order_relaxed);
            break;                  /* stop — never skip past             */
        }
        out[n++] = s->rec;
    }
    atomic_store_explicit(&g_tail, tail + i, memory_order_release);
    return (ssize_t)n;
}

uint64_t d141_dropped(void) { return atomic_load_explicit(&g_dropped,
        memory_order_relaxed); }
uint64_t d141_uncommitted(void) { return atomic_load_explicit(&g_uncommitted,
        memory_order_relaxed); }
uint64_t d141_emitted(void) { return atomic_load_explicit(&g_emitted,
        memory_order_relaxed); }
uint64_t d141_drain_refused(void) { return atomic_load_explicit(
        &g_drain_refused, memory_order_relaxed); }
uint64_t d141_gate_spun(void) { return atomic_load_explicit(&g_gate_spun,
        memory_order_relaxed); }
uint64_t d141_head(void) { return atomic_load_explicit(&g_head,
        memory_order_acquire); }
uint64_t d141_tail(void) { return atomic_load_explicit(&g_tail,
        memory_order_acquire); }

void d141_reset_for_test(void) {
    memset(g_ring, 0, sizeof(g_ring));
    atomic_store(&g_head, 0);  atomic_store(&g_tail, 0);
    atomic_store(&g_seq, 0);   atomic_store(&g_dropped, 0);
    atomic_store(&g_uncommitted, 0); atomic_store(&g_emitted, 0);
    atomic_store(&g_drain_refused, 0); atomic_store(&g_gate_spun, 0);
    atomic_store(&g_state, UNINIT); atomic_store(&g_consumer, 0);
    atomic_store(&g_gen, 0);   atomic_store(&g_pid, 0);
#ifdef D141_TEST_HOOKS
    d141_hook_post_claim = NULL;
    d141_hook_gen_init = NULL;
#endif
}
