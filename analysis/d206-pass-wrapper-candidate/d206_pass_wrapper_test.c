/* D206 host tests — typed int(int,double) PASS wrapper contract.
 * No hooks/permits/WS/sender; originals are plain stubs. */

#include "d206_pass_wrapper.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { failures++; printf("FAIL %s: %s\n", __func__, msg); } } \
    while (0)

#define SENT  "/tmp/d206_pass_wrapper_test.sentinel"
static const int32_t PID = 0xD206;
static const uint8_t U16[16] = { 0x11,0x22,0x33,0x44, 0x55,0x66,0x77,0x88,
                                 0xAA,0xBB,0xCC,0xDD, 0xEE,0xFF,0x00,0x11 };
static uint64_t GEN;

static void fresh(void) {
    d141_reset_for_test();
    int fd = open(SENT, O_CREAT | O_WRONLY, 0600); close(fd);
    CHECK(d141_set_generation(PID, U16) == 0, "generation published");
    CHECK(d141_gate_init(SENT) == 1, "armed");
}

/* stub originals — capture args verbatim, count calls */
static int   s_calls, s_arg_int;
static uint64_t s_arg_dbl;
static int   s_errno_seen;
static int stub_zero(int x, double d) {
    memcpy(&s_arg_dbl, &d, 8);
    s_arg_int = x;
    s_errno_seen = errno;
    s_calls++;
    errno = 0;                       /* leaves 0 */
    return 0;
}
static int stub_ret(int x, double d) {
    memcpy(&s_arg_dbl, &d, 8);
    s_arg_int = x;
    s_errno_seen = errno;
    s_calls++;
    errno = 42;                      /* distinct orig errno            */
    return -7;
}
static int stub_read_errno(int x, double d) {
    (void)x; (void)d;
    s_calls++;
    return errno;                    /* returns what it was entered with */
}

/* ---- w1: full bracket, arg/return/errno/call-count verbatim --------- */
static void w1(void) {
    fresh();
    s_calls = 0;
    errno = 9;                       /* caller's errno                 */
    double d = -0.0;                 /* sign-bit set, exponent zero    */
    int r = d206_wrap_pass(stub_ret, -12345, d);
    CHECK(r == -7, "return int unchanged");
    CHECK(errno == 42, "orig's errno reaches caller, not emit's");
    CHECK(s_calls == 1, "orig called exactly once");
    CHECK(s_arg_int == -12345, "int arg verbatim");
    uint64_t bits = 0x8000000000000000ull;   /* -0.0 bit pattern */
    CHECK(s_arg_dbl == bits, "-0.0 object bits verbatim");
    CHECK(s_errno_seen == 9, "caller's errno reaches orig (emit safe)");
    d141_record out[8];
    ssize_t n = d141_drain(out, 8);
    CHECK(n == 2, "ENTER+EXIT drained");
    uint64_t tid = 0; pthread_threadid_np(NULL, &tid);
    CHECK(out[0].kind == D141K_PASS_ENTER &&
          out[1].kind == D141K_PASS_EXIT, "bracket order");
    CHECK(out[0].tid == tid && out[1].tid == tid,
          "same measured tid on both legs");
    CHECK(out[0].seq + 1 == out[1].seq, "legs adjacent in claim order");
    CHECK(out[0].ts_ns <= out[1].ts_ns, "monotonic ts");
    CHECK(out[0].gen == GEN && out[0].pid == PID,
          "metadata from set_generation");
    unlink(SENT);
    d141_reset_for_test();
}

/* ---- w2: finite double + zero return -------------------------------- */
static void w2(void) {
    fresh();
    s_calls = 0;
    int r = d206_wrap_pass(stub_zero, 7, 3.14159);
    CHECK(r == 0, "zero return verbatim");
    double expect = 3.14159; uint64_t eb; memcpy(&eb, &expect, 8);
    CHECK(s_arg_dbl == eb, "finite double bits verbatim");
    CHECK(s_calls == 1, "called once");
    d141_record out[8];
    CHECK(d141_drain(out, 8) == 2 &&
          out[0].kind == D141K_PASS_ENTER &&
          out[1].kind == D141K_PASS_EXIT, "bracket emitted");
    unlink(SENT);
    d141_reset_for_test();
}

/* ---- w3: disarmed path — orig runs, nothing emitted ------------------ */
static void w3(void) {
    d141_reset_for_test();
    CHECK(d141_gate_init(SENT) == 0, "disarmed without sentinel");
    s_calls = 0;
    int r = d206_wrap_pass(stub_zero, 1, 1.0);
    CHECK(r == 0 && s_calls == 1, "orig still runs when disarmed");
    d141_record out[4];
    CHECK(d141_drain(out, 4) == 0 && d141_emitted() == 0,
          "zero records emitted when disarmed");
    d141_reset_for_test();
}

/* ---- w4: two threads — TLS stub, rendezvous, strict per-tid shape ---
 * The original is thread-safe by construction: every value it observes
 * or writes is _Thread_local, and the only shared state is the atomic
 * rendezvous counter. A deterministic arrive-barrier inside the stub
 * guarantees both threads really enter their orig before either
 * returns — so the stream provably contains interleaved brackets. */
static _Thread_local struct {
    int idx, calls, arg_int, err_entered;
    uint64_t arg_dbl;
} W4;
static struct { int calls, arg_int, err_entered, ret, err_seen;
                uint64_t arg_dbl; } W4RES[2];
static _Atomic int w4_arrived;

static int w4_stub(int x, double d) {
    memcpy(&W4.arg_dbl, &d, 8);
    W4.arg_int = x;
    W4.err_entered = errno;             /* caller's errno → orig        */
    W4.calls++;
    /* rendezvous: both origs must be entered before either returns */
    atomic_fetch_add_explicit(&w4_arrived, 1, memory_order_acq_rel);
    unsigned spins = 0;
    while (atomic_load_explicit(&w4_arrived, memory_order_acquire) < 2) {
        if (++spins > 200000000u) return -999;   /* fail, never hang   */
        sched_yield();
    }
    errno = 100 + W4.idx;               /* orig's own errno outcome     */
    return 1000 + W4.idx;
}
static void *w4_thread(void *a) {
    int i = (int)(intptr_t)a;
    W4.idx = i;
    errno = 50 + i;                     /* per-thread caller errno      */
    int r = d206_wrap_pass(w4_stub, 300 + i, (double)(i + 1) * 0.5);
    W4RES[i].calls = W4.calls;
    W4RES[i].arg_int = W4.arg_int;
    W4RES[i].arg_dbl = W4.arg_dbl;
    W4RES[i].err_entered = W4.err_entered;
    W4RES[i].ret = r;
    W4RES[i].err_seen = errno;          /* orig's errno → caller        */
    return NULL;
}
static void w4(void) {
    fresh();
    atomic_store(&w4_arrived, 0);
    pthread_t th[2];
    pthread_create(&th[0], NULL, w4_thread, (void *)0);
    pthread_create(&th[1], NULL, w4_thread, (void *)1);
    pthread_join(th[0], NULL);
    pthread_join(th[1], NULL);

    /* per-thread originals: own args, own return, own errno */
    for (int i = 0; i < 2; i++) {
        double ed = (double)(i + 1) * 0.5; uint64_t eb;
        memcpy(&eb, &ed, 8);
        CHECK(W4RES[i].calls == 1, "orig called exactly once");
        CHECK(W4RES[i].arg_int == 300 + i, "own int arg verbatim");
        CHECK(W4RES[i].arg_dbl == eb, "own double bits verbatim");
        CHECK(W4RES[i].err_entered == 50 + i, "own errno → orig");
        CHECK(W4RES[i].ret == 1000 + i, "own return verbatim");
        CHECK(W4RES[i].err_seen == 100 + i, "own errno → caller");
    }

    d141_record out[8];
    CHECK(d141_drain(out, 8) == 4, "four records from two threads");
    /* strict per-tid shape: each tid → exactly one ENTER then exactly
     * one EXIT — a tid with two ENTERs or two EXITs is rejected. */
    uint64_t tids[2] = {0, 0}; int ntid = 0;
    for (int i = 0; i < 4; i++) {
        int known = 0;
        for (int j = 0; j < ntid; j++)
            if (out[i].tid == tids[j]) known = 1;
        if (!known && ntid < 2) tids[ntid++] = out[i].tid;
    }
    CHECK(ntid == 2, "two distinct measured tids");
    for (int t = 0; t < 2; t++) {
        int enters = 0, exits = 0, first_enter = -1, last_enter = -1,
            first_exit = -1, last_exit = -1;
        for (int i = 0; i < 4; i++) {
            if (out[i].tid != tids[t]) continue;
            if (out[i].kind == D141K_PASS_ENTER) {
                enters++;
                if (first_enter < 0) first_enter = i;
                last_enter = i;
            } else if (out[i].kind == D141K_PASS_EXIT) {
                exits++;
                if (first_exit < 0) first_exit = i;
                last_exit = i;
            }
        }
        CHECK(enters == 1 && exits == 1 &&
              first_enter < last_exit && last_enter == first_enter,
              "tid gets exactly one ENTER then one EXIT");
    }
    unlink(SENT);
    d141_reset_for_test();
}

/* ---- w5: stub returning caller's errno — emit isolation both ways --- */
static void w5(void) {
    fresh();
    s_calls = 0;
    errno = 77;
    int r = d206_wrap_pass(stub_read_errno, 0, 0.5);
    CHECK(r == 77, "orig saw caller's errno, not an emit-clobbered one");
    CHECK(errno == 77, "orig's unchanged errno returned verbatim");
    unlink(SENT);
    d141_reset_for_test();
}

int main(void) {
    struct { const char *n; void (*f)(void); } t[] = {
        { "w1", w1 }, { "w2", w2 }, { "w3", w3 },
        { "w4", w4 }, { "w5", w5 },
    };
    uint64_t lo; memcpy(&lo, U16, 8);
    GEN = ((uint64_t)(uint32_t)PID << 32) ^ lo;
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        int before = checks;
        t[i].f();
        printf("%-3s %3d checks\n", t[i].n, checks - before);
    }
    printf("%s: %d checks, %d failures\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
