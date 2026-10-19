/* D202 host tests — finite drain→v3 harness (D201 s1–s9, D202-corrected).
 * Everything is deterministic; the only concurrency is the deliberately
 * parked stalled-claim producer (s4) and the foreign drainer (s7) — both
 * joined before any d141_reset_for_test (quiescence rule). */

#include "d202_drainser.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { failures++; printf("FAIL %s: %s\n", __func__, msg); } } \
    while (0)

#define SENT  "/tmp/d202_drainser_test.sentinel"
#define OUTF  "/tmp/d202_out.bin"
#define OUTF2 "/tmp/d202_out2.bin"

static const int32_t PID  = 0xD202;
static const uint8_t U16[16] = { 0x11,0x22,0x33,0x44, 0x55,0x66,0x77,0x88,
                                 0xAA,0xBB,0xCC,0xDD, 0xEE,0xFF,0x00,0x11 };
static uint64_t GEN;

/* set_generation BEFORE gate_init — documented C3 order */
static void fresh(void) {
    d141_reset_for_test();
    int fd = open(SENT, O_CREAT | O_WRONLY, 0600); close(fd);
    CHECK(d141_set_generation(PID, U16) == 0, "generation published");
    CHECK(d141_gate_init(SENT) == 1, "armed");
}

/* parked producer for s4 — claims a slot, blocks before commit */
static _Atomic int hook_gate, hook_entered;
static void pause_hook(void) {
    atomic_store_explicit(&hook_entered, 1, memory_order_release);
    while (!atomic_load_explicit(&hook_gate, memory_order_acquire))
        sched_yield();
}
static void *paused_producer(void *a) {
    (void)a;
    d141_emit(D141K_DISPATCH_BEGIN, 0xDEADBEEF, 0);
    return NULL;
}
static ssize_t foreign_rc;
static void *foreign_drainer(void *a) {
    d141_record dummy[1];
    (void)a;
    foreign_rc = d141_drain(dummy, 1);
    return NULL;
}

/* D203 fixtures: sentinel-target preservation + funopen injection */
static void write_sentinel(const char *path) {
    FILE *f = fopen(path, "w");
    fputs("SENTINEL", f);
    fclose(f);
}
static int sentinel_intact(const char *path) {
    char buf[16] = {0};
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    (void)fread(buf, 1, 15, f);
    fclose(f);
    return strcmp(buf, "SENTINEL") == 0;
}
static int no_tmp_leftover(const char *path) {
    char pat[512];
    snprintf(pat, sizeof(pat), "%s.d202tmp*", path);
    glob_t g;
    int r = glob(pat, 0, NULL, &g);
    size_t n = (r == 0) ? g.gl_pathc : 0;
    globfree(&g);
    return r == GLOB_NOMATCH || n == 0;
}

/* deterministic injection FILE*: write fails after N calls and/or
 * close fails with EIO — proves error provenance, no /dev/full */
static struct { int writes; int fail_after; int fail_close; } INJ;
static int inj_write(void *c, const char *b, int n) {
    struct { int writes; int fail_after; int fail_close; } *i = c;
    (void)b;
    if (i->writes >= i->fail_after) { errno = EIO; return 0; }
    i->writes++;
    return n;
}
static int inj_close(void *c) {
    struct { int writes; int fail_after; int fail_close; } *i = c;
    if (i->fail_close) { errno = EIO; return -1; }
    return 0;
}
static FILE *inj_file(const char *path) {
    (void)path;
    return funopen(&INJ, NULL, inj_write, NULL, inj_close);
}

/* ws-side synthetic stream for decode round-trips (same image uuid) */
#define PROBE   0x74a7ull
#define REPLY   (PROBE + D169_REPLY_DELTA)
static d141_record WR[64]; static size_t nw; static uint64_t wseq;
static void wrec(uint32_t kind, uint64_t msgid, uint64_t ts, uint64_t tid,
                 uint64_t wgen) {
    WR[nw++] = (d141_record){ .magic = D141_SVC_MAGIC, .seq = wseq++,
        .ts_ns = ts, .tid = tid, .pid = 0x777, .kind = kind,
        .msgid = msgid, .aux = 0, .gen = wgen };
}
static d169_stream mkws(uint64_t wgen) {
    d169_stream s = { .recs = WR, .n = nw,
        .gen = wgen, .pid = 0x777,
        .emitted = nw, .dropped = 0, .uncommitted = 0,
        .uuid_valid = 1, .label = "ws" };
    memcpy(s.image_uuid, U16, 16);
    return s;
}

/* ---- s1: emitted records → file → loader → decode scores ---------- */
static void s1(void) {
    fresh();
    uint8_t u[16];
    CHECK(d141_get_uuid(u) == 0 && memcmp(u, U16, 16) == 0,
          "accessor returns published uuid");
    d141_mach_call(PROBE, 0x3000903);
    /* filler emits spread real ts between CALL and RET so ws edges can
     * merge strictly between them — scoring evaluates at RET close */
    for (int i = 0; i < 32; i++)
        d141_emit(D141K_DISPATCH_BEGIN, 0xF000 + i, 0);
    d141_mach_ret(REPLY, 0);
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 0, 4) == D202_COMPLETE, "collect complete");
    CHECK(b.n == 34 && b.drain_calls == 2,
          "34 records; one productive drain + one zero confirm");
    CHECK(b.rec[0].kind == D141K_MACH_CALL &&
          b.rec[0].msgid == PROBE &&
          b.rec[33].kind == D141K_MACH_RET &&
          b.rec[33].msgid == REPLY, "record legs verbatim");
    CHECK(d202_write_stream(OUTF, b.rec, b.n, PID, u,
          b.emitted, b.dropped, b.uncommitted, "probe") == D202_W_OK,
          "stream written");
    d169_stream_mem m;
    CHECK(d169_stream_load(OUTF, &m) == D169_OK, "loader accepts file");
    CHECK(m.s.n == 34 && m.s.uuid_valid == 1 && m.s.pid == PID &&
          m.s.gen == GEN && memcmp(m.s.image_uuid, U16, 16) == 0,
          "header fields verbatim incl uuid+gen");
    CHECK(m.s.emitted == 34 && m.s.dropped == 0 &&
          m.s.uncommitted == 0, "counter snapshots verbatim");
    /* decode: pair with synthetic ws span on an independent tid —
     * HANDLER must merge strictly between CALL and RET (real ts). */
    nw = 0; wseq = 1;
    uint64_t t_call = b.rec[0].ts_ns, t_ret = b.rec[33].ts_ns;
    uint64_t mid = t_call + (t_ret - t_call) / 2;
    CHECK(t_ret > t_call, "real ts separation between legs");
    uint64_t wgen = GEN ^ ((uint64_t)PID << 32) ^ ((uint64_t)0x777 << 32);
    wrec(D141K_PASS_ENTER,   0,     t_call - 400, 4242, wgen);
    wrec(D141K_HANDLER_BEGIN, PROBE, mid,          4242, wgen);
    wrec(D141K_PASS_EXIT,    0,     t_ret + 400,  4242, wgen);
    d169_stream w = mkws(wgen);
    d169_opts o = { .probe_msgid = PROBE, .wait_ns = 4000000,
        .sender_label = "t" };
    d169_report rp;
    CHECK(d169_decode(&m.s, &w, &o, &rp) == D169_OK, "decode rc");
    CHECK(rp.probes_scored == 1, "drained probe scores in decoder");
    d169_stream_free(&m);
    unlink(OUTF); unlink(SENT);
    d141_reset_for_test();
}

/* ---- s2: identity consistency + refusal on corruption ------------- */
static void s2(void) {
    fresh();
    uint8_t u[16]; (void)d141_get_uuid(u);
    d141_emit(D141K_DISPATCH_BEGIN, 1, 0);
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 0, 2) == D202_COMPLETE, "collect");
    b.rec[0].gen ^= 1;                          /* corrupt identity */
    CHECK(d202_write_stream(OUTF, b.rec, b.n, PID, u,
          b.emitted, b.dropped, b.uncommitted, "x")
          == D202_W_INCONSISTENT, "corrupt gen refused");
    CHECK(access(OUTF, F_OK) == -1, "no artifact left by refusal");
    b.rec[0].gen = GEN;
    CHECK(d202_write_stream(OUTF, b.rec, b.n, PID + 1, u,
          b.emitted, b.dropped, b.uncommitted, "x")
          == D202_W_INCONSISTENT, "wrong caller pid refused");
    CHECK(access(OUTF, F_OK) == -1, "no artifact left (pid)");
    unlink(SENT);
    d141_reset_for_test();
}

/* ---- s3: full ring → drops verbatim ------------------------------- */
static void s3(void) {
    fresh();
    for (unsigned i = 0; i < D141_RING_CAP + 40; i++)
        d141_emit(D141K_DISPATCH_BEGIN, i, 0);
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 0, 4) == D202_COMPLETE, "collect complete");
    CHECK(b.n == D141_RING_CAP, "collected exactly CAP");
    CHECK(b.dropped == 40 && b.emitted == D141_RING_CAP,
          "dropped/emitted verbatim");
    uint8_t u[16]; (void)d141_get_uuid(u);
    CHECK(d202_write_stream(OUTF, b.rec, b.n, PID, u,
          b.emitted, b.dropped, b.uncommitted, "s3") == D202_W_OK,
          "stream written");
    d169_stream_mem m;
    CHECK(d169_stream_load(OUTF, &m) == D169_OK &&
          m.s.dropped == 40, "file carries drop snapshot");
    d169_stream_free(&m);
    unlink(OUTF); unlink(SENT);
    d141_reset_for_test();
}

/* ---- s4: stalled claim → INCOMPLETE_CLAIM, no wait, remainder ---- */
static void s4(void) {
    fresh();
    atomic_store(&hook_gate, 0); atomic_store(&hook_entered, 0);
    d141_emit(D141K_DISPATCH_BEGIN, 0xAA, 0);      /* committed prefix */
    d141_hook_post_claim = pause_hook;
    pthread_t th;
    pthread_create(&th, NULL, paused_producer, NULL);
    while (!atomic_load_explicit(&hook_entered, memory_order_acquire))
        sched_yield();
    /* hook must be cleared BEFORE the main-thread emit — post_claim
     * fires on every claim, including this one. */
    d141_hook_post_claim = NULL;
    d141_emit(D141K_DISPATCH_BEGIN, 0xBB, 0);      /* behind open slot */
    d202_batch b; memset(&b, 0, sizeof b);
    int st = d202_collect(&b, 0, 8);
    CHECK(st == D202_INCOMPLETE_CLAIM, "stalled claim → INCOMPLETE_CLAIM");
    CHECK(b.n == 1 && b.rec[0].msgid == 0xAA, "only committed prefix");
    CHECK(b.uncommitted >= 1, "uncommitted snapshot counted");
    CHECK(b.drain_calls <= 2, "no waiting: one zero-return ended it");
    /* a partial batch is NOT written as complete — caller retries */
    atomic_store_explicit(&hook_gate, 1, memory_order_release);
    pthread_join(th, NULL);                       /* joined → quiescent */
    CHECK(d202_collect(&b, 0, 4) == D202_COMPLETE,
          "remainder collected after release");
    CHECK(b.n == 3 && b.rec[1].msgid == 0xDEADBEEF &&
          b.rec[2].msgid == 0xBB, "claim order incl parked record");
    unlink(SENT);
    d141_reset_for_test();
}

/* ---- s5: write errors — errno preserved, no false artifact -------- */
static void s5(void) {
    fresh();
    uint8_t u[16]; (void)d141_get_uuid(u);
    d141_emit(D141K_DISPATCH_BEGIN, 7, 0);
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 0, 2) == D202_COMPLETE, "collect");
    /* unopenable path */
    CHECK(d202_write_stream("/tmp/d202_no_such_dir/x.bin", b.rec, b.n,
          PID, u, b.emitted, b.dropped, b.uncommitted, "x")
          == D202_W_IO && errno == ENOENT, "open failure → IO + errno");
    /* deterministic mid-write failure: fmemopen undersized buffer */
    char membuf[sizeof(d169_stream_hdr) + 8];  /* too small for recs  */
    FILE *mf = fmemopen(membuf, sizeof(membuf), "w");
    CHECK(mf != NULL, "fmemopen fixture");
    CHECK(d202_write_stream_file(mf, b.rec, b.n, PID, u,
          b.emitted, b.dropped, b.uncommitted, "x") == D202_W_IO,
          "short-write → IO, never false success");
    fclose(mf);
    /* path wrapper cleans partial artifacts on close/write failure */
    CHECK(access(OUTF, F_OK) == -1, "no stale artifact");
    unlink(SENT);
    d141_reset_for_test();
}

/* ---- s6: capacity-limited batching + tiny-chunk collection -------- */
static void s6(void) {
    fresh();
    /* tiny chunks: 64 records, chunk 8 → 8 drain calls, complete */
    for (unsigned i = 0; i < 64; i++)
        d141_emit(D141K_DISPATCH_BEGIN, i, 0);
    d202_batch b1; memset(&b1, 0, sizeof b1);
    CHECK(d202_collect(&b1, 8, 8) == D202_COMPLETE,
          "tiny chunks reach complete");
    CHECK(b1.n == 64 && b1.drain_calls == 8,
          "64 records across 8 bounded calls, none lost/dup");
    for (unsigned i = 1; i < 64; i++)
        CHECK(b1.rec[i].seq == b1.rec[i-1].seq + 1,
              "contiguous seq, no duplicate");
    /* capacity-incomplete: refill to CAP-8, then emit 16 more */
    d141_reset_for_test();
    int fd = open(SENT, O_CREAT | O_WRONLY, 0600); close(fd);
    CHECK(d141_set_generation(PID, U16) == 0, "gen");
    CHECK(d141_gate_init(SENT) == 1, "armed");
    for (unsigned i = 0; i < D141_RING_CAP - 8; i++)
        d141_emit(D141K_DISPATCH_BEGIN, i, 0);
    d202_batch c1; memset(&c1, 0, sizeof c1);
    CHECK(d202_collect(&c1, 0, 4) == D202_COMPLETE, "phase 1 complete");
    CHECK(c1.n == D141_RING_CAP - 8, "248 in batch");
    for (unsigned i = 0; i < 16; i++)
        d141_emit(D141K_DISPATCH_BEGIN, 0x1000 + i, 0);
    CHECK(d202_collect(&c1, 0, 4) == D202_INCOMPLETE_CAP,
          "buffer full + head!=tail → INCOMPLETE_CAP");
    CHECK(c1.n == D141_RING_CAP, "batch holds exactly CAP");
    CHECK(c1.dropped == 0, "nothing dropped — only capacity-bound");
    d202_batch c2; memset(&c2, 0, sizeof c2);     /* explicit next batch */
    CHECK(d202_collect(&c2, 0, 4) == D202_COMPLETE, "batch 2 completes");
    CHECK(c2.n == 8 && c2.rec[0].msgid == 0x1008,
          "remainder captured, no skip/dup");
    unlink(SENT);
    d141_reset_for_test();
}

/* ---- s7: foreign consumer refused --------------------------------- */
static void s7(void) {
    fresh();
    d141_emit(D141K_DISPATCH_BEGIN, 1, 0);
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 0, 2) == D202_COMPLETE, "main holds lease");
    foreign_rc = 0;
    pthread_t th;
    pthread_create(&th, NULL, foreign_drainer, NULL);
    pthread_join(th, NULL);
    CHECK(foreign_rc == -1 && d141_drain_refused() >= 1,
          "foreign drainer refused by lease");
    unlink(SENT);
    d141_reset_for_test();
}

/* ---- s8: empty stream — exact hdr, scores nothing ----------------- */
static void s8(void) {
    fresh();
    uint8_t u[16]; (void)d141_get_uuid(u);
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 0, 2) == D202_COMPLETE, "empty complete");
    CHECK(b.n == 0 && b.drain_calls == 1, "one bounded empty drain");
    CHECK(d202_write_stream(OUTF, b.rec, 0, PID, u,
          b.emitted, b.dropped, b.uncommitted, "s8") == D202_W_OK,
          "empty stream written");
    int efd = open(OUTF, O_RDONLY);
    off_t sz = lseek(efd, 0, SEEK_END);
    close(efd);
    CHECK(sz == (off_t)sizeof(d169_stream_hdr),
          "file is exactly the header");
    d169_stream_mem m;
    CHECK(d169_stream_load(OUTF, &m) == D169_OK && m.s.n == 0,
          "empty stream loads");
    d169_stream_free(&m);
    unlink(OUTF); unlink(SENT);
    d141_reset_for_test();
}

/* ---- s9: no set_generation → uuid_valid=0, suppressed ------------- */
static void s9(void) {
    d141_reset_for_test();                        /* no generation set */
    int fd = open(SENT, O_CREAT | O_WRONLY, 0600); close(fd);
    CHECK(d141_gate_init(SENT) == 1, "armed (C3-forbidden order in test)");
    d141_emit(D141K_DISPATCH_BEGIN, 1, 0);        /* pid=0/gen=0 rec   */
    uint8_t u[16];
    CHECK(d141_get_uuid(u) == -1, "accessor invalid without publish");
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 0, 2) == D202_COMPLETE, "collect");
    /* uuid invalid → pass NULL; caller-supplied pid must match records */
    CHECK(d202_write_stream(OUTF2, b.rec, b.n, 0, NULL,
          b.emitted, b.dropped, b.uncommitted, "s9") == D202_W_OK,
          "uuid_valid=0 stream written");
    d169_stream_mem m;
    CHECK(d169_stream_load(OUTF2, &m) == D169_OK &&
          m.s.uuid_valid == 0, "uuid_valid=0 loads as unverified");
    unlink(OUTF2); unlink(SENT);
    d141_reset_for_test();
}

/* ---- t1: sentinel target preserved on W_ARG ------------------------ */
static void t1(void) {
    fresh();
    uint8_t u[16]; (void)d141_get_uuid(u);
    d141_emit(D141K_DISPATCH_BEGIN, 1, 0);
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 0, 2) == D202_COMPLETE, "collect");
    write_sentinel(OUTF);
    CHECK(d202_write_stream(OUTF, NULL, 5, PID, u, 0, 0, 0, "x")
          == D202_W_ARG, "NULL rec + n>0 → ARG");
    CHECK(sentinel_intact(OUTF), "target untouched on ARG");
    CHECK(d202_write_stream(OUTF, b.rec, D169_MAX_STREAM_RECS + 1,
          PID, u, 0, 0, 0, "x") == D202_W_ARG, "oversize n → ARG");
    CHECK(sentinel_intact(OUTF), "target untouched on ARG(2)");
    unlink(OUTF); unlink(SENT);
    d141_reset_for_test();
}

/* ---- t2: sentinel target preserved on W_INCONSISTENT ---------------- */
static void t2(void) {
    fresh();
    uint8_t u[16]; (void)d141_get_uuid(u);
    d141_emit(D141K_DISPATCH_BEGIN, 1, 0);
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 0, 2) == D202_COMPLETE, "collect");
    b.rec[0].gen ^= 1;
    write_sentinel(OUTF);
    CHECK(d202_write_stream(OUTF, b.rec, b.n, PID, u,
          b.emitted, b.dropped, b.uncommitted, "x")
          == D202_W_INCONSISTENT, "corrupt gen refused");
    CHECK(sentinel_intact(OUTF), "target untouched on INCONSISTENT");
    unlink(OUTF); unlink(SENT);
    d141_reset_for_test();
}

/* ---- t3: sentinel preserved on injected write failure --------------- */
static void t3(void) {
    fresh();
    uint8_t u[16]; (void)d141_get_uuid(u);
    d141_emit(D141K_DISPATCH_BEGIN, 1, 0);
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 0, 2) == D202_COMPLETE, "collect");
    write_sentinel(OUTF);
    INJ.writes = 0; INJ.fail_after = 0; INJ.fail_close = 0;
    d202_hook_file = inj_file;
    CHECK(d202_write_stream(OUTF, b.rec, b.n, PID, u,
          b.emitted, b.dropped, b.uncommitted, "x")
          == D202_W_IO && errno == EIO,
          "injected write failure → IO+EIO");
    d202_hook_file = NULL;
    CHECK(sentinel_intact(OUTF), "target untouched on write failure");
    CHECK(no_tmp_leftover(OUTF), "owned temp removed, none left");
    unlink(OUTF); unlink(SENT);
    d141_reset_for_test();
}

/* ---- t4: close-failure errno provenance (stale errno not reported) -- */
static void t4(void) {
    fresh();
    uint8_t u[16]; (void)d141_get_uuid(u);
    d141_emit(D141K_DISPATCH_BEGIN, 1, 0);
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 0, 2) == D202_COMPLETE, "collect");
    write_sentinel(OUTF);
    INJ.writes = 0; INJ.fail_after = 99; INJ.fail_close = 1;
    d202_hook_file = inj_file;
    errno = ESPIPE;                      /* stale nonzero errno        */
    int rc = d202_write_stream(OUTF, b.rec, b.n, PID, u,
          b.emitted, b.dropped, b.uncommitted, "x");
    CHECK(rc == D202_W_IO && errno == EIO,
          "reported errno is the CLOSE's, not stale ESPIPE");
    d202_hook_file = NULL;
    CHECK(sentinel_intact(OUTF), "target untouched on close failure");
    CHECK(no_tmp_leftover(OUTF), "temp cleaned after close failure");
    unlink(OUTF); unlink(SENT);
    d141_reset_for_test();
}

/* ---- t5: success replaces target atomically, no leftovers ---------- */
static void t5(void) {
    fresh();
    uint8_t u[16]; (void)d141_get_uuid(u);
    d141_emit(D141K_DISPATCH_BEGIN, 1, 0);
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 0, 2) == D202_COMPLETE, "collect");
    write_sentinel(OUTF);
    CHECK(d202_write_stream(OUTF, b.rec, b.n, PID, u,
          b.emitted, b.dropped, b.uncommitted, "t5") == D202_W_OK,
          "valid write succeeds over existing target");
    CHECK(!sentinel_intact(OUTF), "success REPLACES the target");
    d169_stream_mem m;
    CHECK(d169_stream_load(OUTF, &m) == D169_OK && m.s.n == 1,
          "replaced file is the real v3 stream");
    d169_stream_free(&m);
    CHECK(no_tmp_leftover(OUTF), "no temp leftovers on success");
    unlink(OUTF); unlink(SENT);
    d141_reset_for_test();
}

/* ---- t6: corrupt batch capacity boundary ---------------------------- */
static void t6(void) {
    fresh();
    d202_batch b; memset(&b, 0, sizeof b);
    b.n = D141_RING_CAP + 1;             /* corrupt: past buffer end  */
    CHECK(d202_collect(&b, 0, 4) == D202_ERR,
          "n > CAP rejected before pointer arithmetic");
    CHECK(b.drain_calls == 0, "no drain issued on corrupt batch");
    CHECK(b.status == D202_ERR, "corrupt batch status agrees with return");
    unlink(SENT);
    d141_reset_for_test();
}

/* ---- t7: per-invocation budget resumes a partial batch -------------- */
static void t7(void) {
    fresh();
    for (unsigned i = 0; i < 64; i++)
        d141_emit(D141K_DISPATCH_BEGIN, i, 0);
    d202_batch b; memset(&b, 0, sizeof b);
    CHECK(d202_collect(&b, 8, 2) == D202_INCOMPLETE_BUDGET,
          "budget expiry → INCOMPLETE_BUDGET");
    CHECK(b.n == 16 && b.drain_calls == 2, "16 records in 2 calls");
    /* resuming the same batch makes progress — no silent no-op */
    int st = d202_collect(&b, 8, 2);
    CHECK(st == D202_INCOMPLETE_BUDGET && b.n == 32 &&
          b.drain_calls == 4, "resume: 32 records, 4 cumulative calls");
    while (st != D202_COMPLETE) {
        st = d202_collect(&b, 8, 2);
        if (b.drain_calls > 12) break;   /* bounded test loop */
    }
    CHECK(st == D202_COMPLETE && b.n == 64 && b.drain_calls == 8,
          "resumed batch completes: 64 records, 8 total calls");
    unlink(SENT);
    d141_reset_for_test();
}

int main(void) {
    struct { const char *n; void (*f)(void); } t[] = {
        { "s1", s1 }, { "s2", s2 }, { "s3", s3 },
        { "s4", s4 }, { "s5", s5 }, { "s6", s6 },
        { "s7", s7 }, { "s8", s8 }, { "s9", s9 },
        { "t1", t1 }, { "t2", t2 }, { "t3", t3 },
        { "t4", t4 }, { "t5", t5 }, { "t6", t6 }, { "t7", t7 },
    };
    uint64_t lo; memcpy(&lo, U16, 8);  /* d141 low64 = first 8 bytes */
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
