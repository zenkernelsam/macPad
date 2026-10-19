/*
 * D141/D143/D150b deterministic host-side tests for the isolated svctime
 * prototype. D150b adds: T15 gate-retry (I1), T16 CAP-boundary + flood
 * (I2/D157), T17 multi-producer overlap churn (I3), fixed T13, extended
 * mach-leg legality tests (I5). All adversarial windows are
 * barrier/hook-driven — zero sleeps.
 */

#define D141_TEST_HOOKS
#include "d141_svctime.h"

#include <errno.h>
#include <fcntl.h>
#include <mach/message.h>           /* MACH_MSG_SUCCESS, MACH_RCV_TIMED_OUT */
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures, checks;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { failures++; printf("FAIL %s: %s\n", __func__, msg); } \
} while (0)

static const char *SENT = "/tmp/d141_svctime_test.sentinel";

static uintptr_t orig_42(void *a)   { (void)a; return 42; }
static uintptr_t orig_err(void *a)  { (void)a; errno = ENOSPC; return (uintptr_t)-1; }
static uintptr_t orig_touch(void *a){ (*(int *)a)++; return 7; }

static void arm(void) {
    int fd = open(SENT, O_CREAT | O_WRONLY, 0600); close(fd);
    (void)d141_gate_init(SENT);
}

/* ---- T1 ordering + correlation ------------------------------------ */
static void t1(void) {
    d141_reset_for_test(); unlink(SENT);
    int fd = open(SENT, O_CREAT | O_WRONLY, 0600); close(fd);

    unsigned char uuid[16] = {0x96,0x67,0x6a,0x53,0xb1,0xe0,0x3d,0x7e,
                              0xb9,0x8b,0xb7,0x38,0x73,0xcd,0x18,0x80};
    CHECK(d141_set_generation(22527, uuid) == 0, "set_generation rc");
    CHECK(d141_gate_init(SENT) == 1, "armed by sentinel");

    const uint64_t MSG = 0x74a7u * 1000u + 27;
    d141_mach_call(MSG, 0x3000903);
    int touched = 0;
    CHECK(d141_wrap_pass(orig_touch, &touched) == 7, "pass orig return");
    CHECK(touched == 1, "orig ran inside pass");
    d141_emit(D141K_DISPATCH_BEGIN, MSG, 0);
    d141_emit(D141K_HANDLER_BEGIN,  MSG, 0);
    d141_emit(D141K_IMPL_INVOKE,    MSG, 0);
    d141_emit(D141K_REPLY_ENQUEUE,  MSG, 0);
    d141_emit(D141K_FLUSH_BEGIN,    MSG, 0);
    d141_mach_ret(MSG, 0);

    d141_record out[64];
    ssize_t n = d141_drain(out, 64);
    CHECK(n == 9, "nine records drained");
    static const uint32_t want[9] = {
        D141K_MACH_CALL, D141K_PASS_ENTER, D141K_PASS_EXIT,
        D141K_DISPATCH_BEGIN, D141K_HANDLER_BEGIN, D141K_IMPL_INVOKE,
        D141K_REPLY_ENQUEUE, D141K_FLUSH_BEGIN, D141K_MACH_RET };
    for (ssize_t i = 0; i < n && i < 9; i++) {
        CHECK(out[i].kind == want[i], "edge order");
        CHECK(out[i].magic == D141_SVC_MAGIC, "magic");
        CHECK(out[i].pid == 22527, "pid identity");
        CHECK(i == 0 || out[i].seq > out[i-1].seq, "seq monotonic");
        CHECK(i == 0 || out[i].ts_ns >= out[i-1].ts_ns, "ts monotonic");
        CHECK(out[i].tid == out[0].tid, "same measured tid");
    }
    for (ssize_t i = 0; i < n; i++)
        CHECK(out[i].msgid == (out[i].kind == D141K_PASS_ENTER ||
                               out[i].kind == D141K_PASS_EXIT ? 0 : MSG),
              "msgid correlation");
    uint64_t lo; memcpy(&lo, uuid, 8);
    CHECK(out[0].gen == (((uint64_t)22527u << 32) ^ lo), "generation tag");
    /* I4 contract route: PASS_EXIT.aux == 0 — handled-count derivable by
     * counting per-tid DISPATCH/HANDLER/IMPL edges inside the bracket. */
    CHECK(out[2].aux == 0, "PASS_EXIT aux 0 (contract route)");
    CHECK(d141_dropped() == 0 && d141_emitted() == 9, "no drops");
    unlink(SENT);
}

/* ---- T2 saturation + drop accounting ------------------------------- */
static void t2(void) {
    d141_reset_for_test(); unlink(SENT); arm();
    const int N = D141_RING_CAP + 40;
    for (int i = 0; i < N; i++) d141_emit(D141K_DISPATCH_BEGIN, i, 0);
    CHECK(d141_emitted() == D141_RING_CAP, "ring capped");
    CHECK(d141_dropped() == 40, "exact drop count");
    d141_record out[D141_RING_CAP];
    CHECK(d141_drain(out, D141_RING_CAP) == (ssize_t)D141_RING_CAP,
          "drain returns all committed");
    CHECK(d141_drain(out, 8) == 0, "empty drain");
    unlink(SENT);
}

/* ---- T3 errno + return preservation -------------------------------- */
static void t3(void) {
    d141_reset_for_test(); unlink(SENT); arm();
    errno = EDOM;
    d141_emit(D141K_DISPATCH_BEGIN, 1, 0);
    CHECK(errno == EDOM, "errno preserved across emit");
    errno = 0;
    CHECK(d141_wrap_entry(D141K_HANDLER_BEGIN, 1, 0, orig_err, NULL)
            == (uintptr_t)-1, "orig return verbatim");
    CHECK(errno == ENOSPC, "orig errno visible to caller");
    CHECK(d141_wrap_entry(D141K_HANDLER_BEGIN, 2, 0, orig_42, NULL) == 42,
          "pass-through value");
    unlink(SENT);
}

/* ---- T4 nested pass / reentrancy ------------------------------------ */
static uintptr_t nested_inner(void *a) {
    return d141_wrap_pass(orig_42, a);
}
static void t4(void) {
    d141_reset_for_test(); unlink(SENT); arm();
    CHECK(d141_wrap_pass(nested_inner, NULL) == 42, "nested return");
    d141_record out[8];
    ssize_t n = d141_drain(out, 8);
    CHECK(n == 4, "four bracket records");
    CHECK(out[0].kind == D141K_PASS_ENTER && out[1].kind == D141K_PASS_ENTER
        && out[2].kind == D141K_PASS_EXIT && out[3].kind == D141K_PASS_EXIT,
          "nested order LIFO-valid");
    for (int i = 1; i < 4; i++)
        CHECK(out[i].seq > out[i-1].seq, "nested seqs distinct");
    unlink(SENT);
}

/* ---- T5 disarmed: zero output --------------------------------------- */
static void t5(void) {
    d141_reset_for_test(); unlink(SENT);
    CHECK(d141_gate_init(SENT) == 0, "disarmed without sentinel");
    for (int i = 0; i < 100; i++) d141_emit(D141K_DISPATCH_BEGIN, i, 0);
    d141_record out[8];
    CHECK(d141_drain(out, 8) == 0, "zero records while disarmed");
    CHECK(d141_dropped() == 0 && d141_emitted() == 0, "zero accounting");
    int touched = 0;
    CHECK(d141_wrap_pass(orig_touch, &touched) == 7 && touched == 1,
          "orig still runs disarmed");
}

/* ---- T6 post-hoc arm refused ---------------------------------------- */
static void t6(void) {
    d141_reset_for_test(); unlink(SENT);
    CHECK(d141_gate_init(SENT) == 0, "init disarmed");
    int fd = open(SENT, O_CREAT | O_WRONLY, 0600); close(fd);
    CHECK(d141_gate_init(SENT) == 0, "second init does not re-arm");
    CHECK(d141_armed() == 0, "armed stays 0 after sentinel appears");
    d141_emit(D141K_DISPATCH_BEGIN, 1, 0);
    d141_record out[4];
    CHECK(d141_drain(out, 4) == 0, "no records from post-hoc arm");
    unsigned char u[16] = {0};
    CHECK(d141_set_generation(1, u) == -1, "generation locked at init");
    unlink(SENT);
}

/* ---- shared barrier-driven pause machinery --------------------------- */
static _Atomic int hook_gate, hook_entered;

static void pause_hook(void) {
    atomic_store(&hook_entered, 1);
    while (!atomic_load_explicit(&hook_gate, memory_order_acquire))
        sched_yield();
}

static void *paused_producer(void *a) {
    (void)a;
    d141_emit(D141K_DISPATCH_BEGIN, 0xDEADBEEF, 0);
    return NULL;
}

/* ---- T9 barrier-paused claim: no skip-past ---------------------------- */
static void t9(void) {
    d141_reset_for_test(); unlink(SENT); arm();
    atomic_store(&hook_gate, 0); atomic_store(&hook_entered, 0);
    d141_emit(D141K_DISPATCH_BEGIN, 0xAA, 0);
    d141_hook_post_claim = pause_hook;
    pthread_t th;
    pthread_create(&th, NULL, paused_producer, NULL);
    while (!atomic_load_explicit(&hook_entered, memory_order_acquire))
        sched_yield();
    d141_record out[8];
    ssize_t n = d141_drain(out, 8);
    CHECK(n == 1, "drain stops at uncommitted slot, yields only prefix");
    CHECK(out[0].msgid == 0xAA, "prefix record identity");
    CHECK(d141_uncommitted() == 1, "open slot counted");
    d141_hook_post_claim = NULL;
    d141_emit(D141K_DISPATCH_BEGIN, 0xBB, 0);
    d141_record mid[8];
    CHECK(d141_drain(mid, 8) == 0,
          "no skip-past: later record unreachable until open slot commits");
    atomic_store_explicit(&hook_gate, 1, memory_order_release);
    pthread_join(th, NULL);
    d141_record after[8];
    ssize_t m = d141_drain(after, 8);
    CHECK(m == 2, "post-commit drain yields paused record + follower");
    CHECK(after[0].msgid == 0xDEADBEEF && after[1].msgid == 0xBB,
          "claim order preserved at drain");
    CHECK(d141_dropped() == 0, "nothing dropped");
    unlink(SENT);
}

/* ---- T10 consumer lease — concurrent drain rejection ------------------ */
static ssize_t drain_result;
static void *foreign_drainer(void *a) {
    drain_result = d141_drain((d141_record *)a, 8);
    return NULL;
}
static void t10(void) {
    d141_reset_for_test(); unlink(SENT); arm();
    d141_emit(D141K_DISPATCH_BEGIN, 1, 0);
    d141_record out[8];
    CHECK(d141_drain(out, 8) == 1, "owner drain ok");
    d141_record fb[8];
    drain_result = 999;
    pthread_t th;
    pthread_create(&th, NULL, foreign_drainer, fb);
    pthread_join(th, NULL);
    CHECK(drain_result == -1, "foreign consumer refused");
    CHECK(d141_drain_refused() == 1, "refusal counted");
    d141_emit(D141K_DISPATCH_BEGIN, 2, 0);
    CHECK(d141_drain(out, 8) == 1 && out[0].msgid == 2, "owner unaffected");
    unlink(SENT);
}

/* ---- T11/T7 unique-msgid conservation under MPSC ---------------------- */
#define TP_THREADS 4
#define TP_EACH    200
static void *tp_worker(void *a) {
    uintptr_t base = (uintptr_t)a;
    for (int i = 0; i < TP_EACH; i++)
        d141_emit(D141K_DISPATCH_BEGIN, base + i + 1, 0);
    return NULL;
}
static void t11(void) {
    d141_reset_for_test(); unlink(SENT); arm();
    pthread_t th[TP_THREADS];
    for (uintptr_t i = 0; i < TP_THREADS; i++)
        pthread_create(&th[i], NULL, tp_worker, (void *)(i * TP_EACH));
    for (int i = 0; i < TP_THREADS; i++) pthread_join(th[i], NULL);
    const int TOTAL = TP_THREADS * TP_EACH;
    CHECK(d141_emitted() + d141_dropped() == TOTAL,
          "conservation: emitted+dropped == attempted");
    d141_record *out = malloc(sizeof(d141_record) * TOTAL);
    size_t total = 0; ssize_t n;
    while ((n = d141_drain(out + total, TOTAL - total)) > 0) total += n;
    CHECK(total == d141_emitted(), "drained == emitted");
    unsigned char *seen = calloc(TOTAL + 1, 1);
    int dup = 0, oob = 0;
    for (size_t i = 0; i < total; i++) {
        uint64_t m = out[i].msgid;
        if (m == 0 || m > (uint64_t)TOTAL) { oob = 1; break; }
        if (seen[m]) dup = 1; else seen[m] = 1;
    }
    CHECK(!dup && !oob, "unique in-range msgids — no torn/duplicate slots");
    CHECK(d141_uncommitted() == 0, "no skipped slots after quiesce");
    free(seen); free(out);
    unlink(SENT);
}
static void t7(void) { t11(); }   /* retained identical coverage */

/* ---- T12 wraparound with open slot ------------------------------------- */
static void t12(void) {
    d141_reset_for_test(); unlink(SENT); arm();
    atomic_store(&hook_gate, 0); atomic_store(&hook_entered, 0);
    for (unsigned i = 0; i < D141_RING_CAP - 1; i++)
        d141_emit(D141K_DISPATCH_BEGIN, i + 1, 0);
    d141_hook_post_claim = pause_hook;
    pthread_t th;
    pthread_create(&th, NULL, paused_producer, NULL);
    while (!atomic_load_explicit(&hook_entered, memory_order_acquire))
        sched_yield();
    d141_record *out = malloc(sizeof(d141_record) * D141_RING_CAP);
    ssize_t n = d141_drain(out, D141_RING_CAP);
    CHECK(n == (ssize_t)(D141_RING_CAP - 1), "drains all but open slot");
    d141_hook_post_claim = NULL;
    d141_emit(D141K_DISPATCH_BEGIN, 0xFF, 0);
    CHECK(d141_dropped() == 0 && d141_emitted() == (uint64_t)D141_RING_CAP,
          "emit past open slot claims consumed slot 0; paused not committed");
    atomic_store_explicit(&hook_gate, 1, memory_order_release);
    pthread_join(th, NULL);
    d141_record tail_out[4];
    ssize_t m = d141_drain(tail_out, 4);
    CHECK(m == 2 && tail_out[0].msgid == 0xDEADBEEF &&
          tail_out[1].msgid == 0xFF,
          "claim order preserved across the reclaimed wraparound slot");
    free(out);
    unlink(SENT);
}

/* ---- T13 concurrent gen-vs-gate (fixed, non-flaky) --------------------- */
static _Atomic int gen_rc, gate_rc;
static _Atomic int go;
static unsigned char t13_uuid[16] = {0x96,0x67,0x6a,0x53,0xb1,0xe0,0x3d,0x7e,
                                     0xb9,0x8b,0xb7,0x38,0x73,0xcd,0x18,0x80};
static void *t13_gen(void *a) {
    (void)a;
    while (!atomic_load(&go)) sched_yield();
    gen_rc = d141_set_generation(22527, t13_uuid);
    return NULL;
}
static void *t13_gate(void *a) {
    (void)a;
    while (!atomic_load(&go)) sched_yield();
    gate_rc = d141_gate_init(SENT);
    return NULL;
}
static void t13(void) {
    d141_reset_for_test(); unlink(SENT);
    int fd = open(SENT, O_CREAT | O_WRONLY, 0600); close(fd);
    atomic_store(&go, 0); atomic_store(&gen_rc, -9); atomic_store(&gate_rc, -9);
    pthread_t g1, g2;
    pthread_create(&g1, NULL, t13_gen, NULL);
    pthread_create(&g2, NULL, t13_gate, NULL);
    atomic_store(&go, 1);
    pthread_join(g1, NULL); pthread_join(g2, NULL);
    /* I1: gate_rc is deterministic 1 either way — if gen won UNINIT the
     * gate re-CASes after rollback; if gate won first it armed directly. */
    CHECK(gate_rc == 1, "gate armed deterministically (I1 retry)");
    CHECK(gen_rc == 0 || gen_rc == -1, "gen coherent winner/loser");
    d141_emit(D141K_DISPATCH_BEGIN, 5, 0);
    d141_record out[4];
    CHECK(d141_drain(out, 4) == 1, "record present");
    uint64_t lo; memcpy(&lo, t13_uuid, 8);
    uint64_t want = ((uint64_t)22527u << 32) ^ lo;
    if (gen_rc == 0) {
        CHECK(out[0].gen == want && out[0].pid == 22527,
              "gen won — records carry it coherently");
    } else {
        /* C3 reverse order: zeroed pid/gen (forbidden in prod wiring,
         * asserted observable here — real check, not tautology). */
        CHECK(out[0].gen == 0 && out[0].pid == 0,
              "gen lost — records carry zeroed pid/gen per C3");
    }
    unlink(SENT);
}

/* ---- T14 concurrent gate_init all observe final ------------------------ */
static _Atomic int gate_results[8];
static void *t14_gate(void *a) {
    uintptr_t i = (uintptr_t)a;
    while (!atomic_load(&go)) sched_yield();
    atomic_store(&gate_results[i], d141_gate_init(SENT));
    return NULL;
}
static void t14(void) {
    d141_reset_for_test(); unlink(SENT);
    int fd = open(SENT, O_CREAT | O_WRONLY, 0600); close(fd);
    atomic_store(&go, 0);
    for (int i = 0; i < 8; i++) atomic_store(&gate_results[i], -9);
    pthread_t th[8];
    for (uintptr_t i = 0; i < 8; i++)
        pthread_create(&th[i], NULL, t14_gate, (void *)i);
    atomic_store(&go, 1);
    for (int i = 0; i < 8; i++) pthread_join(th[i], NULL);
    for (int i = 0; i < 8; i++)
        CHECK(atomic_load(&gate_results[i]) == 1,
              "every concurrent gate caller observes the final decision");
    CHECK(d141_armed() == 1, "armed final");
    unlink(SENT);
}

/* ---- I1 T15: gate retry deterministic schedule ------------------------- */
static _Atomic int gen_hook_in, gen_hook_rel;
static const unsigned char *t15_uuidp;
static void gen_pause(void) {
    atomic_store(&gen_hook_in, 1);
    while (!atomic_load_explicit(&gen_hook_rel, memory_order_acquire))
        sched_yield();
}
static void *t15_gen(void *a) {
    (void)a;
    gen_rc = d141_set_generation(22527, t15_uuidp);
    return NULL;
}
static void *t15_gate(void *a) {
    (void)a;
    gate_rc = d141_gate_init(SENT);
    return NULL;
}
static void t15(void) {
    d141_reset_for_test(); unlink(SENT);
    int fd = open(SENT, O_CREAT | O_WRONLY, 0600); close(fd);
    unsigned char uuid[16] = {0x96,0x67,0x6a,0x53,0xb1,0xe0,0x3d,0x7e,
                              0xb9,0x8b,0xb7,0x38,0x73,0xcd,0x18,0x80};
    t15_uuidp = uuid;
    atomic_store(&gen_hook_in, 0); atomic_store(&gen_hook_rel, 0);
    atomic_store(&gen_rc, -9); atomic_store(&gate_rc, -9);
    /* G parks inside set_generation holding INITIALIZING */
    d141_hook_gen_init = gen_pause;
    pthread_t G, T;
    pthread_create(&G, NULL, t15_gen, NULL);
    while (!atomic_load_explicit(&gen_hook_in, memory_order_acquire))
        sched_yield();              /* G holds INITIALIZING, parked        */
    /* T calls gate_init: must CAS-lose (state INITIALIZING), enter the
     * retry path, then win the re-CAS after G's UNINIT rollback. */
    pthread_create(&T, NULL, t15_gate, NULL);
    while (d141_gate_spun() == 0 &&
           atomic_load(&gate_rc) == -9)
        sched_yield();              /* T entered the losing-CAS path      */
    CHECK(d141_gate_spun() >= 1, "gate entered losing-CAS retry path");
    CHECK(atomic_load(&gate_rc) == -9, "gate still pending while G parked");
    atomic_store_explicit(&gen_hook_rel, 1, memory_order_release);
    pthread_join(G, NULL); pthread_join(T, NULL);
    CHECK(atomic_load(&gen_rc) == 0, "set_generation won UNINIT window");
    CHECK(atomic_load(&gate_rc) == 1, "gate re-CASed to ARMED after rollback");
    d141_emit(D141K_DISPATCH_BEGIN, 9, 0);
    d141_record out[4];
    CHECK(d141_drain(out, 4) == 1, "record");
    uint64_t lo; memcpy(&lo, uuid, 8);
    CHECK(out[0].gen == (((uint64_t)22527u << 32) ^ lo) &&
          out[0].pid == 22527, "gen+pid coherent in record");
    unlink(SENT);
}

/* ---- I2 T16: CAP-boundary refusal + saturation flood -------------------
 * Exact schedule per the D150b spec (CAP=256, rule c − tail ≥ CAP → drop).
 * The post-claim hook is GLOBAL — it is cleared once P is provably parked
 * (before any fill emit) per the D159 sequencing fix.
 */
static void t16(void) {
    d141_reset_for_test(); unlink(SENT); arm();
    atomic_store(&hook_gate, 0); atomic_store(&hook_entered, 0);

    /* 1. emit 255 committed (claims 0..254, hook NULL) */
    for (unsigned i = 0; i < D141_RING_CAP - 1; i++)
        d141_emit(D141K_DISPATCH_BEGIN, i + 1, 0);
    CHECK(d141_emitted() == D141_RING_CAP - 1, "255 committed");
    CHECK(d141_head() == D141_RING_CAP - 1, "head=255");

    /* 2. arm hook; P claims index 255 and parks inside post_claim */
    d141_hook_post_claim = pause_hook;
    pthread_t P;
    pthread_create(&P, NULL, paused_producer, NULL);
    while (!atomic_load_explicit(&hook_entered, memory_order_acquire))
        sched_yield();              /* P claimed 255, parked pre-commit    */

    /* 3. clear the GLOBAL hook — P stays blocked inside its entered call;
     *    later claimants must not touch pause_hook (D159 ordering). */
    d141_hook_post_claim = NULL;

    /* 4. drain → exactly 255 committed, tail stops at the open slot */
    d141_record *out = malloc(sizeof(d141_record) * D141_RING_CAP * 2);
    ssize_t n = d141_drain(out, D141_RING_CAP * 2);
    CHECK(n == (ssize_t)(D141_RING_CAP - 1), "mid-park drain == 255");
    CHECK(d141_uncommitted() >= 1, "open slot counted");
    CHECK(d141_tail() == D141_RING_CAP - 1, "tail=255 held at open slot");

    /* 5. emit 255 → claims 256..510 land (c−tail = 1..255 < CAP) */
    for (unsigned i = 0; i < D141_RING_CAP - 1; i++)
        d141_emit(D141K_DISPATCH_BEGIN, 1000 + i, 0);
    CHECK(d141_emitted() == 510 && d141_head() == 511,
          "255 followers claimed behind the open slot");
    CHECK(d141_dropped() == 0, "no drops yet");

    /* 6. emit once → claim attempt 511: c−tail = 511−255 = 256 ≥ CAP */
    d141_emit(D141K_DISPATCH_BEGIN, 0xFEED, 0);
    CHECK(d141_dropped() == 1, "claim 511 refused by capacity");
    CHECK(d141_head() == 511, "refusal does not consume a claim index");

    /* 7. saturation flood (D145 A2): M=CAP attempts, all refused */
    const int M = (int)D141_RING_CAP;
    for (int i = 0; i < M; i++)
        d141_emit(D141K_DISPATCH_BEGIN, 0xF000 + i, 0);
    CHECK(d141_dropped() == 1 + M, "flood: every attempt dropped");
    CHECK(d141_emitted() == 510 && d141_head() == 511,
          "refusals never advance head");
    CHECK(d141_drain(out, 8) == 0, "second mid-park drain returns 0");

    /* 8. release P → commits index 255 */
    atomic_store_explicit(&hook_gate, 1, memory_order_release);
    pthread_join(P, NULL);
    CHECK(d141_emitted() == 511, "paused record committed");

    /* 9. drain → 256 records [DEADBEEF, 255 followers in claim order] */
    ssize_t m = d141_drain(out, D141_RING_CAP * 2);
    CHECK(m == (ssize_t)D141_RING_CAP, "final drain == 256");
    CHECK(out[0].msgid == 0xDEADBEEF, "paused record drains first");
    for (unsigned i = 1; i < (unsigned)m; i++)
        CHECK(out[i].msgid == 1000 + i - 1, "followers in claim order");

    /* conservation: attempts = 255 + 1(parked) + 255 + 1 + M = 512+M */
    CHECK(d141_emitted() == 511 && d141_dropped() == 1 + M &&
          d141_emitted() + d141_dropped() == 512 + M,
          "conservation emitted+dropped == attempts");
    free(out);
    unlink(SENT);
}

/* ---- I3 T17: deterministic multi-producer + overlapping drain ----------
 * Single consumer = main thread (lease). Producers gate on a drain_started
 * latch so emission overlaps live draining. A producer parked post-claim
 * proves the committed-prefix-only drain under overlap.
 */
#define T17_P 4
#define T17_K 300
#define T17_PREFIX 8
static _Atomic int t17_drain_started, t17_done;
static _Atomic uint64_t t17_tids[T17_P + 2];

typedef struct { uintptr_t base; int slot; } t17_arg;
static t17_arg t17_args[T17_P];

static void *t17_producer(void *a) {
    t17_arg *t = a;
    uint64_t tid = 0; pthread_threadid_np(NULL, &tid);
    atomic_store(&t17_tids[t->slot], tid);
    while (!atomic_load_explicit(&t17_drain_started, memory_order_acquire))
        sched_yield();              /* emit only once the drain is live   */
    for (int i = 0; i < T17_K; i++) {
        uint64_t m = t->base + i;
        d141_emit(D141K_DISPATCH_BEGIN, m, m ^ 0x5A5A00ull);
    }
    atomic_fetch_add_explicit(&t17_done, 1, memory_order_release);
    return NULL;
}

static _Atomic int t17_park_in, t17_park_rel;
static void t17_pause(void) {
    atomic_store(&t17_park_in, 1);
    while (!atomic_load_explicit(&t17_park_rel, memory_order_acquire))
        sched_yield();
}
static void *t17_parker(void *a) {
    uint64_t tid = 0; pthread_threadid_np(NULL, &tid);
    atomic_store(&t17_tids[T17_P], tid);
    (void)a;
    d141_emit(D141K_DISPATCH_BEGIN, 0xCAFE, 0xCAFE ^ 0x5A5A00ull);
    return NULL;
}

static void t17(void) {
    d141_reset_for_test(); unlink(SENT);
    /* C3 order: generation BEFORE the gate decision. */
    unsigned char uuid[16] = {1,2,3,4,5,6,7,8};
    CHECK(d141_set_generation(4242, uuid) == 0, "gen set pre-gate");
    int fd = open(SENT, O_CREAT | O_WRONLY, 0600); close(fd);
    CHECK(d141_gate_init(SENT) == 1, "armed");
    uint64_t lo; memcpy(&lo, uuid, 8);
    const uint64_t want_gen = ((uint64_t)4242u << 32) ^ lo;
    atomic_store(&t17_drain_started, 0); atomic_store(&t17_done, 0);
    atomic_store(&t17_park_in, 0); atomic_store(&t17_park_rel, 0);
    for (int i = 0; i < T17_P + 2; i++) atomic_store(&t17_tids[i], 0);
    uint64_t main_tid = 0; pthread_threadid_np(NULL, &main_tid);
    atomic_store(&t17_tids[T17_P + 1], main_tid);

    /* Phase A: committed prefix, then a producer parked post-claim while
     * the owner drains — drain overlaps the live claim. */
    for (int i = 0; i < T17_PREFIX; i++)
        d141_emit(D141K_DISPATCH_BEGIN, 0xE000 + i, (0xE000 + i) ^ 0x5A5A00ull);
    d141_hook_post_claim = t17_pause;
    pthread_t P1;
    pthread_create(&P1, NULL, t17_parker, NULL);
    while (!atomic_load_explicit(&t17_park_in, memory_order_acquire))
        sched_yield();              /* P1 holds claim index 8, uncommitted */

    const int TOTAL = T17_P * T17_K;
    const size_t ATTEMPTS = TOTAL + T17_PREFIX + 1;
    d141_record *out = malloc(sizeof(d141_record) * (ATTEMPTS + 8));
    size_t drained = 0;
    ssize_t n = d141_drain(out, ATTEMPTS + 8);   /* main claims lease here */
    CHECK(n == T17_PREFIX, "parked-prefix drain yields committed prefix");
    for (ssize_t i = 0; i < n; i++)
        CHECK(out[i].msgid == (uint64_t)(0xE000 + i), "prefix order");
    drained += (size_t)n;
    CHECK(d141_uncommitted() >= 1, "consumer observed live uncommitted");
    CHECK(d141_tail() == T17_PREFIX, "tail held at P1's open slot");

    /* Phase B: release P1, start churn producers gated on drain_started,
     * then drain continuously while they emit. */
    atomic_store_explicit(&t17_park_rel, 1, memory_order_release);
    d141_hook_post_claim = NULL;

    for (int i = 0; i < T17_P; i++)
        t17_args[i] = (t17_arg){ .base = (uintptr_t)i * T17_K + 1, .slot = i };
    pthread_t prod[T17_P];
    for (int i = 0; i < T17_P; i++)
        pthread_create(&prod[i], NULL, t17_producer, &t17_args[i]);

    atomic_store_explicit(&t17_drain_started, 1, memory_order_release);
    while (atomic_load_explicit(&t17_done, memory_order_acquire) < T17_P) {
        ssize_t r = d141_drain(out + drained, ATTEMPTS + 8 - drained);
        if (r > 0) drained += (size_t)r;
        /* producers still live → keep draining: real producer/consumer
         * overlap, not serialized phases */
    }
    pthread_join(P1, NULL);
    for (int i = 0; i < T17_P; i++) pthread_join(prod[i], NULL);
    ssize_t r;
    while ((r = d141_drain(out + drained, ATTEMPTS + 8 - drained)) > 0)
        drained += (size_t)r;

    /* Per-record validation: magic, aux == f(msgid), gen uniform,
     * tid ∈ {producers, parked, main}, unique msgids, no corruption. */
    unsigned char *seen = calloc(0xF000, 1);
    int bad = 0, dup = 0;
    int seen_cafe = 0, seen_prefix = 0, seen_churn = 0;
    for (size_t i = 0; i < drained; i++) {
        d141_record *e = &out[i];
        uint64_t m = e->msgid;
        if (e->magic != D141_SVC_MAGIC) { bad++; continue; }
        if (e->aux != (m ^ 0x5A5A00ull)) bad++;
        if (e->gen != want_gen) bad++;
        int tid_ok = 0;
        for (int j = 0; j < T17_P + 2; j++)
            if (atomic_load(&t17_tids[j]) == e->tid) tid_ok = 1;
        if (!tid_ok) { bad++;
            printf("  t17 bad tid rec[%zu] msgid=%llx tid=%llu\n",
                   i, (unsigned long long)m, (unsigned long long)e->tid); }
        if (e->aux != (m ^ 0x5A5A00ull))
            printf("  t17 bad aux rec[%zu] msgid=%llx aux=%llx\n",
                   i, (unsigned long long)m, (unsigned long long)e->aux);
        if (e->gen != want_gen)
            printf("  t17 bad gen rec[%zu] gen=%llx\n",
                   i, (unsigned long long)e->gen);
        if (m >= 0xE000 && m < 0xE000 + T17_PREFIX) seen_prefix++;
        else if (m == 0xCAFE) seen_cafe++;
        else if (m >= 1 && m <= (uint64_t)TOTAL) seen_churn++;
        if (m < 0xF000) { if (seen[m]) { dup++;
                printf("  t17 dup msgid=%llx at %zu\n",
                       (unsigned long long)m, i); } else seen[m] = 1; }
    }
    printf("  t17 drained=%zu bad=%d dup=%d prefix=%d cafe=%d churn=%d "
           "dropped=%llu emitted=%llu\n", drained, bad, dup, seen_prefix,
           seen_cafe, seen_churn,
           (unsigned long long)d141_dropped(),
           (unsigned long long)d141_emitted());
    CHECK(!bad && !dup, "per-record magic/aux/gen/tid/unique-msgid valid");
    CHECK(seen_prefix == T17_PREFIX && seen_cafe == 1 &&
          seen_churn + d141_dropped() == TOTAL,
          "all record classes accounted exactly once");
    CHECK(d141_emitted() + d141_dropped() == ATTEMPTS,
          "conservation: emitted+dropped == attempts");
    CHECK(drained + d141_dropped() == ATTEMPTS,
          "drained+dropped == attempts (nothing lost, nothing extra)");
    CHECK(d141_emitted() == drained, "every committed record delivered");
    free(seen); free(out);
    unlink(SENT);
}

/* ---- I5 mach-leg legality ---------------------------------------------- */
/* decoder-side pairing for the tests (NOT a candidate API): a RET only
 * pairs when it reports success — non-success RETs (timeout, too_large)
 * consume nothing and leave the send CALL unmatched. Pair = nearest
 * UNMATCHED same-tid CALL (LIFO unwind for async reentrancy). */
static ssize_t pair_for(d141_record *recs, ssize_t ret_idx,
                        const unsigned char *matched) {
    if (recs[ret_idx].aux != MACH_MSG_SUCCESS)
        return -1;                  /* non-success RET pairs to nothing   */
    for (ssize_t i = ret_idx - 1; i >= 0; i--)
        if (recs[i].kind == D141K_MACH_CALL &&
            recs[i].tid == recs[ret_idx].tid && !matched[i])
            return i;
    return -1;
}

static void t18(void) {
    d141_reset_for_test(); unlink(SENT); arm();
    d141_record out[16];

    /* distinct-id join: send 0x74a7, reply recv 0x750b */
    d141_mach_call(0x74a7, 0x3000903);
    d141_mach_ret(0x750b, MACH_MSG_SUCCESS);
    /* timeout: RET with msgid 0 + MACH_RCV_TIMED_OUT is a leg, not a pair;
     * the send CALL(0x8000) must remain unmatched for a later RET.
     * Constant per vendored osfmk/mach/message.h:1231 (0x10004003). */
    d141_mach_call(0x8000, 0x3000903);
    d141_mach_ret(0, MACH_RCV_TIMED_OUT);
    /* delayed successful RET with a distinct id → pairs to the CALL that
     * the timeout left unmatched */
    d141_mach_ret(0x9000, MACH_MSG_SUCCESS);
    /* async interleave: CALL(A) CALL(B) RET(B) RET(A) — nearest-unmatched
     * pairing (signal-handler reentrancy) must not FIFO-match */
    d141_mach_call(0xAAAA, 0x1);
    d141_mach_call(0xBBBB, 0x2);
    d141_mach_ret (0xBBBB, MACH_MSG_SUCCESS);
    d141_mach_ret (0xAAAA, MACH_MSG_SUCCESS);

    ssize_t n = d141_drain(out, 16);
    CHECK(n == 9, "nine leg records");

    /* CALL aux == option verbatim; RET aux == kr verbatim (I5/D156) */
    CHECK(out[0].aux == 0x3000903 && out[1].aux == MACH_MSG_SUCCESS,
          "aux verbatim");
    CHECK(out[3].aux == MACH_RCV_TIMED_OUT, "timeout RET aux=kr verbatim");

    /* distinct-id join: records carry raw ids; decoder pairs by tid+order,
     * never by equality of msgid (send id ≠ reply id) */
    CHECK(out[0].msgid == 0x74a7 && out[1].msgid == 0x750b, "raw ids");
    unsigned char matched[16] = {0};
    ssize_t p = pair_for(out, 1, matched);
    CHECK(p == 0, "reply pairs to its CALL by tid");
    if (p >= 0) matched[p] = 1;

    /* timeout exclusion (D161/D4): the TIMED_OUT RET pairs to nothing —
     * kr != SUCCESS ⇒ −1 — and CALL(0x8000) stays unmatched */
    CHECK(out[3].msgid == 0 && out[3].aux == MACH_RCV_TIMED_OUT,
          "timeout leg shape");
    CHECK(pair_for(out, 3, matched) == -1, "timeout RET pairs to -1");
    CHECK(!matched[2], "CALL(0x8000) left unmatched by timeout");
    /* the delayed successful RET pairs to that still-unmatched CALL */
    p = pair_for(out, 4, matched);
    CHECK(p == 2, "delayed success RET pairs to CALL(0x8000)");
    if (p >= 0) matched[p] = 1;

    /* async interleave: RET(B) pairs to CALL(B) — LIFO-ish nearest match */
    p = pair_for(out, 7, matched);
    CHECK(p == 6, "RET(B) → CALL(B) nearest unmatched");
    if (p >= 0) matched[p] = 1;
    p = pair_for(out, 8, matched);
    CHECK(p == 5, "RET(A) → CALL(A) after B unwinds");

    /* per-leg errno preservation (D7/I5): each leg, not just emit */
    errno = EDOM;
    d141_mach_call(1, 0);
    CHECK(errno == EDOM, "errno preserved across mach_call");
    errno = EPIPE;
    d141_mach_ret(1, 0);
    CHECK(errno == EPIPE, "errno preserved across mach_ret");
    unlink(SENT);
}

/* ---- D182 t19: UUID accessor U1/U2/U3/U4/U6/U7 ------------------------
 * U1 verbatim round-trip, U2 unset, U3 record low64 binding, U4 reset,
 * U6 second-set refusal, U7 errno preservation on both paths. */
static void t19(void) {
    static const unsigned char UUID[16] =
        {0x96,0x67,0x6a,0x53,0xb1,0xe0,0x3d,0x7e,
         0xb9,0x8b,0xb7,0x38,0x73,0xcd,0x18,0x80};
    unsigned char out[16];

    /* U2: before any set → -1, out zeroed */
    d141_reset_for_test(); unlink(SENT);
    memset(out, 0xAA, 16);
    CHECK(d141_get_uuid(out) == -1, "U2 unset → -1");
    { int z = 1; for (int i = 0; i < 16; i++) z &= (out[i] == 0);
      CHECK(z, "U2 out zeroed"); }

    /* U1: verbatim 16-byte round trip incl. high64 */
    CHECK(d141_set_generation(22527, UUID) == 0, "set rc");
    memset(out, 0, 16);
    errno = EBADF;
    CHECK(d141_get_uuid(out) == 0, "U1 get rc");
    CHECK(memcmp(out, UUID, 16) == 0, "U1 all 16 bytes verbatim");
    CHECK(errno == EBADF, "U7 errno preserved (valid path)");

    /* U6a (D183/D184): second set BEFORE gate_init — the UNINIT rollback
     * must not let it re-win the window; the first-set latch refuses it */
    static const unsigned char OTHER[16] = {9,9,9,9,9,9,9,9,
                                            8,8,8,8,8,8,8,8};
    CHECK(d141_set_generation(1, OTHER) == -1,
          "U6 pre-gate second set refused by latch");
    memset(out, 0, 16);
    CHECK(d141_get_uuid(out) == 0 && memcmp(out, UUID, 16) == 0,
          "U6 uuid unchanged by refused pre-gate set");

    /* U3: drained record (gen,pid) recovers lo64 == accessor lo64 —
     * the exact relation the D175 decoder enforces. */
    arm();
    d141_emit(D141K_DISPATCH_BEGIN, 1, 0);
    d141_record rec[1];
    CHECK(d141_drain(rec, 1) == 1, "U3 record drained");
    uint64_t rec_lo = ((uint64_t)((uint32_t)(rec[0].gen >> 32) ^
                                  (uint32_t)rec[0].pid) << 32)
                      | (rec[0].gen & 0xffffffffull);
    uint64_t acc_lo; memcpy(&acc_lo, out, 8);
    CHECK(rec_lo == acc_lo, "U3 record lo64 == accessor lo64");

    /* U6b: post-gate second set refused; uuid untouched */
    CHECK(d141_set_generation(1, OTHER) == -1, "U6 post-gate refused");
    memset(out, 0, 16);
    CHECK(d141_get_uuid(out) == 0 && memcmp(out, UUID, 16) == 0,
          "U6 uuid unchanged post-gate");

    /* U4 + U7b: reset clears validity; invalid path preserves errno */
    d141_reset_for_test();
    memset(out, 0xAA, 16);
    errno = EDOM;
    CHECK(d141_get_uuid(out) == -1, "U4 post-reset → -1");
    { int z = 1; for (int i = 0; i < 16; i++) z &= (out[i] == 0);
      CHECK(z, "U4 out zeroed"); }
    CHECK(errno == EDOM, "U7 errno preserved (invalid path)");
    unlink(SENT);
}

/* ---- D182 t20 / U5: publication gate, deterministic -------------------
 * gen_pause parks the writer inside INITIALIZING while g_uuid is being
 * written — a mid-window reader must observe INVALID (flag still 0),
 * never torn bytes; post-release the read is verbatim. No sleeps. */
static void t20(void) {
    d141_reset_for_test(); unlink(SENT);
    static const unsigned char UUID[16] =
        {0x96,0x67,0x6a,0x53,0xb1,0xe0,0x3d,0x7e,
         0xb9,0x8b,0xb7,0x38,0x73,0xcd,0x18,0x80};
    t15_uuidp = UUID;
    atomic_store(&gen_hook_in, 0); atomic_store(&gen_hook_rel, 0);
    atomic_store(&gen_rc, -9);
    d141_hook_gen_init = gen_pause;
    pthread_t G;
    pthread_create(&G, NULL, t15_gen, NULL);
    while (!atomic_load_explicit(&gen_hook_in, memory_order_acquire))
        sched_yield();              /* writer parked inside INITIALIZING */
    unsigned char out[16]; memset(out, 0xAA, 16);
    CHECK(d141_get_uuid(out) == -1, "U5 mid-window get → -1");
    { int z = 1; for (int i = 0; i < 16; i++) z &= (out[i] == 0);
      CHECK(z, "U5 mid-window out zeroed"); }
    atomic_store_explicit(&gen_hook_rel, 1, memory_order_release);
    pthread_join(G, NULL);
    CHECK(atomic_load(&gen_rc) == 0, "U5 set completed");
    memset(out, 0, 16);
    CHECK(d141_get_uuid(out) == 0 && memcmp(out, UUID, 16) == 0,
          "U5 post-release verbatim");
    unlink(SENT);
}

/* ---- D184 t21: concurrent second-set while first writer parked --------
 * Deterministic: first writer holds INITIALIZING in hook_gen_init; a
 * concurrent set loses the CAS → −1 without touching uuid; after release
 * the first tuple publishes intact, and a post-publish pre-gate set hits
 * the first-set latch. No sleeps — existing barriers only. */
static const unsigned char *t21_uuidp2;
static _Atomic int t21_rc2;
static void *t21_gen2(void *a) {
    (void)a;
    t21_rc2 = d141_set_generation(999, t21_uuidp2);
    return NULL;
}
static void t21(void) {
    d141_reset_for_test(); unlink(SENT);
    static const unsigned char UUID[16] =
        {0x96,0x67,0x6a,0x53,0xb1,0xe0,0x3d,0x7e,
         0xb9,0x8b,0xb7,0x38,0x73,0xcd,0x18,0x80};
    static const unsigned char OTHER[16] = {9,9,9,9,9,9,9,9,
                                            8,8,8,8,8,8,8,8};
    t15_uuidp = UUID; t21_uuidp2 = OTHER;
    atomic_store(&gen_hook_in, 0); atomic_store(&gen_hook_rel, 0);
    atomic_store(&gen_rc, -9); atomic_store(&t21_rc2, -9);
    d141_hook_gen_init = gen_pause;
    pthread_t G, S;
    pthread_create(&G, NULL, t15_gen, NULL);
    while (!atomic_load_explicit(&gen_hook_in, memory_order_acquire))
        sched_yield();              /* first writer holds INITIALIZING    */
    pthread_create(&S, NULL, t21_gen2, NULL);
    pthread_join(S, NULL);
    CHECK(atomic_load(&t21_rc2) == -1,
          "concurrent second set lost CAS while INITIALIZING");
    unsigned char out[16]; memset(out, 0xAA, 16);
    CHECK(d141_get_uuid(out) == -1, "still unpublished mid-window");
    atomic_store_explicit(&gen_hook_rel, 1, memory_order_release);
    pthread_join(G, NULL);
    CHECK(atomic_load(&gen_rc) == 0, "first writer published");
    memset(out, 0, 16);
    CHECK(d141_get_uuid(out) == 0 && memcmp(out, UUID, 16) == 0,
          "uuid verbatim after concurrent refusal");
    /* post-publish, pre-gate: the new latch refuses the window re-entry */
    CHECK(d141_set_generation(999, OTHER) == -1,
          "post-publish pre-gate set refused by latch");
    memset(out, 0, 16);
    CHECK(d141_get_uuid(out) == 0 && memcmp(out, UUID, 16) == 0,
          "uuid intact after latch refusal");
    unlink(SENT);
}

int main(void) {
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "t1",  t1  }, { "t2",  t2  }, { "t3",  t3  }, { "t4",  t4  },
        { "t5",  t5  }, { "t6",  t6  }, { "t7",  t7  }, { "t9",  t9  },
        { "t10", t10 }, { "t11", t11 }, { "t12", t12 }, { "t13", t13 },
        { "t14", t14 }, { "t15", t15 }, { "t16", t16 }, { "t17", t17 },
        { "t18", t18 }, { "t19", t19 }, { "t20", t20 }, { "t21", t21 },
    };
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = checks;
        tests[i].fn();
        printf("%-4s %3d checks\n", tests[i].name, checks - before);
    }
    printf("%s: %d checks, %d failures\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}

