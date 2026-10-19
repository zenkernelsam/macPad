/*
 * D169 svcdecode — two-stream synthetic tests implementing the D170
 * fixture list (G12). Probe stream = client-side MACH legs on probe
 * tids; WS stream = PASS/HANDLER/IMPL/ENQUEUE/FLUSH on the services
 * tid. Each stream has an independent seq space (C3).
 */

#include "d169_svcdecode.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures, checks;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { failures++; printf("FAIL %s: %s\n", __func__, msg); } \
} while (0)

#define SVC_TID   0x1111ull
#define WS_TID2   0x2222ull   /* other WS thread */
#define PROBE_TID 0xAAAAull
/* Realistic identity: gen = (pid<<32) ^ uuid_lo64 — one shared 128-bit
 * image UUID across probe pid 555 and WS pid 777 (D175). The low 8
 * bytes are U in little-endian byte order. */
#define U         0xDEADBEEF12345678ull
#define U2        0x0123456789ABCDEFull
static const uint8_t U16[16]  = {0x78,0x56,0x34,0x12,0xEF,0xBE,0xAD,0xDE,
                                 0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
/* same low64, different upper64 — must be rejected as different image */
static const uint8_t U16B[16] = {0x78,0x56,0x34,0x12,0xEF,0xBE,0xAD,0xDE,
                                 0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00};
/* different low64 — drives the uuid/gen consistency rejection */
static const uint8_t U16C[16] = {0xEF,0xCD,0xAB,0x89,0x67,0x45,0x23,0x01,
                                 0x10,0x32,0x54,0x76,0x98,0xBA,0xDC,0xFE};
#define PPID      555
#define WPID      777
#define PGEN      (((uint64_t)PPID << 32) ^ U)
#define WSGEN     (((uint64_t)WPID << 32) ^ U)
#define PROBE     0x74a7ull
#define REPLY     (PROBE + D169_REPLY_DELTA)

static d141_record PR[2048], WR[512];
static size_t np_, nw_;
static uint64_t pseq, wseq;

static void reset2(void) { np_ = nw_ = 0; pseq = wseq = 1; }
static void prec(uint32_t kind, uint64_t msgid, uint64_t aux,
                 uint64_t tid, uint64_t ts, int gap) {
    PR[np_++] = (d141_record){ .magic = D141_SVC_MAGIC, .seq = pseq++,
        .ts_ns = ts, .tid = tid, .pid = PPID, .kind = kind,
        .msgid = msgid, .aux = aux, .gen = PGEN };
    pseq += gap;
}
static void wrec(uint32_t kind, uint64_t msgid, uint64_t aux,
                 uint64_t tid, uint64_t ts, int gap) {
    WR[nw_++] = (d141_record){ .magic = D141_SVC_MAGIC, .seq = wseq++,
        .ts_ns = ts, .tid = tid, .pid = WPID, .kind = kind,
        .msgid = msgid, .aux = aux, .gen = WSGEN };
    wseq += gap;
}

static d169_opts OPTS = { .probe_msgid = PROBE, .image_uuid = {0},
    .image_uuid_set = 0, .wait_ns = 4000000,
    .sender_label = "one-probe" };

static d169_stream mkprobe(void) {
    d169_stream s = { .recs = PR, .n = np_, .gen = PGEN, .pid = PPID,
        .emitted = np_, .dropped = 0, .uncommitted = 0,
        .uuid_valid = 1, .label = "probe" };
    memcpy(s.image_uuid, U16, 16);
    return s;
}
static d169_stream mkws(void) {
    d169_stream s = { .recs = WR, .n = nw_, .gen = WSGEN, .pid = WPID,
        .emitted = nw_, .dropped = 0, .uncommitted = 0,
        .uuid_valid = 1, .label = "ws" };
    memcpy(s.image_uuid, U16, 16);
    return s;
}
static d169_err dec(d169_report *rp) {
    d169_stream p = mkprobe(), w = mkws();
    return d169_decode(&p, &w, &OPTS, rp);
}

/* one complete clean probe span across both streams (ts order) */
static void probe_span(uint64_t t0) {
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, t0 + 50, 0);
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, t0 + 100, 0);
    wrec(D141K_DISPATCH_BEGIN, PROBE, 0, SVC_TID, t0 + 300, 0);
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, t0 + 700, 0);
    wrec(D141K_IMPL_INVOKE,   PROBE, 0, SVC_TID, t0 + 750, 0);
    wrec(D141K_REPLY_ENQUEUE, REPLY, 0, SVC_TID, t0 + 800, 0);
    wrec(D141K_FLUSH_BEGIN,   REPLY, 0, SVC_TID, t0 + 850, 0);
    wrec(D141K_PASS_EXIT,     0,     0, SVC_TID, t0 + 900, 0);
    prec(D141K_MACH_RET,  REPLY, D169_MACH_MSG_SUCCESS,
         PROBE_TID, t0 + 950, 0);
}

/* ---- t1 clean two-stream merge ------------------------------------ */
static void t1(void) {
    reset2(); probe_span(1000);
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.svc_tid == SVC_TID && rp.tid_state == D169_TID_OK,
          "services tid resolved");
    CHECK(rp.probes_scored == 1, "one probe scored");
    CHECK(rp.latency_ns[0] == 600, "HANDLER−CALL across merged streams");
    CHECK(rp.replies == 1, "reply joined by id+100");
    CHECK(!rp.ambiguous && !rp.gap_voided && !rp.open_eof &&
          !rp.call_absent, "no flags on clean merge");
}

/* ---- t2 timeout RET excluded; delayed RET still pairs ---------------- */
static void t2(void) {
    reset2();
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1050, 0);
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    prec(D141K_MACH_RET, 0, D169_MACH_RCV_TIMED_OUT, PROBE_TID, 1200, 0);
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 0);
    prec(D141K_MACH_RET, REPLY, D169_MACH_MSG_SUCCESS, PROBE_TID, 1950, 0);
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.timeout_ret == 1, "timeout RET unpaired");
    CHECK(rp.probes_scored == 1 && rp.latency_ns[0] == 600,
          "delayed success RET closes span");
}

/* ---- t3 WS-side seq gap inside span voids ---------------------------- */
static void t3(void) {
    reset2();
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1050, 0);
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 1); /* seq hole */
    wrec(D141K_REPLY_ENQUEUE, REPLY, 0, SVC_TID, 1800, 0); /* seq +2   */
    prec(D141K_MACH_RET, REPLY, D169_MACH_MSG_SUCCESS, PROBE_TID, 1950, 0);
    /* ws footprint inside span = {2,4} → seq 3 missing → void */
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.gap_voided == 1, "in-span ws gap voids");
    CHECK(rp.probes_scored == 0, "voided span not scored");
    CHECK(rp.ws_s.gaps >= 1, "gap counted on ws stream");
}

/* ---- t4 gap outside span is reported-only ---------------------------- */
static void t4(void) {
    reset2();
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 100, 3);   /* gap, before span */
    probe_span(1000);
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 5000, 2);  /* gap, after span  */
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.probes_scored == 1, "span survives outside gaps");
    CHECK(rp.gap_voided == 0, "outside gaps do not void");
    CHECK(rp.ws_s.gaps >= 3, "outside gaps still reported");
}

/* ---- t5 foreign tid/msgid interleave inside span — inert ------------- */
static void t5(void) {
    reset2();
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1050, 0);
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    prec(D141K_MACH_CALL, 0x5555, 0x1, PROBE_TID, 1150, 0); /* other id  */
    wrec(D141K_HANDLER_BEGIN, 0x9999, 0, SVC_TID, 1600, 0); /* batch     */
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 0);
    prec(D141K_MACH_RET, 0x5555, D169_MACH_MSG_SUCCESS, PROBE_TID,1800,0);
    prec(D141K_MACH_RET, REPLY, D169_MACH_MSG_SUCCESS, PROBE_TID, 1950,0);
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.probes_scored == 1 && rp.latency_ns[0] == 600,
          "foreign traffic does not disturb");
    CHECK(!rp.ambiguous && !rp.gap_voided, "no false flags");
}

/* ---- t6 second open CALL(0x74a7) → both ambiguous --------------------- */
static void t6(void) {
    reset2();
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1050, 0);
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1150, 0);
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 0);
    prec(D141K_MACH_RET, REPLY, D169_MACH_MSG_SUCCESS, PROBE_TID, 1950, 0);
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.ambiguous == 1, "violation event flagged");
    CHECK(rp.probes_scored == 0, "both sides excluded from scoring");
}

/* ---- t7 multigen stream → fail-closed reject -------------------------- */
static void t7(void) {
    reset2(); probe_span(1000);
    WR[nw_].magic = D141_SVC_MAGIC;
    WR[nw_] = (d141_record){ .magic = D141_SVC_MAGIC, .seq = wseq++,
        .ts_ns = 9999, .tid = SVC_TID, .pid = 777,
        .kind = D141K_PASS_ENTER, .gen = 0x7777 };  /* second gen */
    nw_++;
    d169_report rp;
    CHECK(dec(&rp) == D169_E_MULTIGEN, "mixed-gen stream rejected");
}

/* ---- t8 drop accounting + reconcile ---------------------------------- */
static void t8(void) {
    reset2(); probe_span(1000);
    d169_stream p = mkprobe(), w = mkws();
    w.dropped = 3; w.emitted = nw_;          /* report drops verbatim   */
    d169_report rp;
    CHECK(d169_decode(&p, &w, &OPTS, &rp) == D169_OK, "decode rc");
    CHECK(rp.drops_flagged == 1, "nonzero drops flagged");
    CHECK(rp.ws_s.dropped == 3 && rp.probe_s.dropped == 0,
          "per-stream drops verbatim");
    CHECK(rp.ws_s.gaps >= rp.ws_s.dropped || rp.reconcile_flag,
          "gaps>=dropped or reconcile flagged");
}

/* ---- t9 tid tie / zero-density flags --------------------------------- */
static void t9(void) {
    reset2();
    /* tie: two tids with equal PASS_ENTER density */
    for (int i = 0; i < 2; i++) {
        wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 100 + i, 0);
        wrec(D141K_PASS_ENTER, 0, 0, WS_TID2, 200 + i, 0);
    }
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 0);
    prec(D141K_MACH_RET, REPLY, D169_MACH_MSG_SUCCESS, PROBE_TID, 1950,0);
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.tid_state == D169_TID_AMBIGUOUS_TIE, "tie flagged not picked");

    reset2();
    /* zero PASS_ENTER anywhere → insufficient */
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 0);
    CHECK(dec(&rp) == D169_OK, "decode rc2");
    CHECK(rp.tid_state == D169_TID_INSUFFICIENT,
          "zero density flagged insufficient");
}

/* ---- t10 EOF-open span reported, never auto-closed -------------------- */
static void t10(void) {
    reset2();
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1050, 0);
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    /* no RET — client died / excluded timeout at EOF */
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.open_eof == 1 && rp.probes_scored == 0,
          "open span reported, not scored");
}

/* ---- t11 batch bracket: multiple HANDLERs, pairing by msgid ---------- */
static void t11(void) {
    reset2();
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1050, 0);
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    /* three dequeued messages in one bracket; probe is the middle one */
    wrec(D141K_HANDLER_BEGIN, 0x1111, 0, SVC_TID, 1600, 0);
    wrec(D141K_FLUSH_BEGIN, 0x9999, 0, SVC_TID, 1650, 0); /* foreign   */
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 0);
    wrec(D141K_HANDLER_BEGIN, 0x2222, 0, SVC_TID, 1750, 0);
    wrec(D141K_REPLY_ENQUEUE, REPLY, 0, SVC_TID, 1800, 0);
    wrec(D141K_PASS_EXIT, 0, 0, SVC_TID, 1900, 0);
    prec(D141K_MACH_RET, REPLY, D169_MACH_MSG_SUCCESS, PROBE_TID, 1950,0);
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.probes_scored == 1 && rp.latency_ns[0] == 600,
          "probe bound by msgid inside batch");
    CHECK(rp.handler_dup == 0 && rp.orphan_handler == 0,
          "foreign handlers not misbound");
}

/* ---- t12 non-monotonic drain-order ts is legal ----------------------- */
static void t12(void) {
    reset2();
    /* ws records written out of ts order (claim order ≠ ts order) */
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1900, 0);          /* late ts  */
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 0);
    wrec(D141K_REPLY_ENQUEUE, REPLY, 0, SVC_TID, 1800, 0);
    wrec(D141K_PASS_EXIT, 0, 0, SVC_TID, 2000, 0);
    prec(D141K_MACH_RET, REPLY, D169_MACH_MSG_SUCCESS, PROBE_TID, 1950,0);
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc — nonmonotonic ts not malformed");
    CHECK(rp.probes_scored == 1 && rp.latency_ns[0] == 600,
          "ts-merge rescues order");
}

/* ---- t13 reply-id mismatch anomaly (C7) ------------------------------ */
static void t13(void) {
    reset2();
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1050, 0);
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 0);
    wrec(D141K_REPLY_ENQUEUE, 0xDEAD, 0, SVC_TID, 1800, 0); /* wrong id  */
    prec(D141K_MACH_RET, 0x7777, D169_MACH_MSG_SUCCESS, PROBE_TID,1950,0);
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.reply_mismatch == 1, "RET id != call+100 flagged");
    CHECK(rp.reply_missing == 1, "missing +100 ENQUEUE flagged");
    CHECK(rp.probes_scored == 1, "pairing still stands (adjacency)");
}

/* ---- t14 CALL-absent window → explicit metric failure ---------------- */
static void t14(void) {
    reset2();
    /* WS stream active, probe stream has only unrelated legs */
    probe_span(1000);
    /* remove the probe CALL leg: overwrite it with a foreign id */
    for (size_t i = 0; i < np_; i++)
        if (PR[i].kind == D141K_MACH_CALL && PR[i].msgid == PROBE)
            PR[i].msgid = 0x5555;
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.no_probe_call == 1 && rp.call_absent == 0,
          "CALLs-but-wrong-id → metric fails explicitly (F3 split)");
    CHECK(rp.probes_scored == 0, "no degraded proxy scoring");
}

/* ---- t15 wrong-tid HANDLER + outside bracket -------------------------- */
static void t15(void) {
    reset2();
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1050, 0);
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    /* handler on a non-services tid — must not bind */
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, WS_TID2, 1600, 0);
    /* handler on services tid but outside any bracket */
    wrec(D141K_PASS_EXIT, 0, 0, SVC_TID, 1650, 0);
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 0);
    prec(D141K_MACH_RET, REPLY, D169_MACH_MSG_SUCCESS, PROBE_TID, 1950,0);
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.handler_wrong_tid == 1, "non-svc HANDLER flagged");
    CHECK(rp.handler_outside_bracket == 1, "outside-bracket flagged");
}

/* ---- t16 file round-trip + malformed classes -------------------------- */
static int write_stream(const char *path, const d169_stream *s,
                        uint64_t over) {
    d169_stream_hdr h = {
        .magic = D169_STREAM_MAGIC, .version = D169_STREAM_VERSION,
        .cap = D141_RING_CAP, .gen = s->gen, .pid = s->pid,
        .flags = 0, .emitted = s->emitted, .dropped = s->dropped,
        .uncommitted = s->uncommitted, .n_records = s->n + over,
        .uuid_valid = s->uuid_valid, .reserved = 0 };
    memcpy(h.image_uuid, s->image_uuid, 16);
    strncpy(h.label, s->label, sizeof(h.label) - 1);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    write(fd, &h, sizeof(h));
    if (s->n) write(fd, s->recs, s->n * sizeof(d141_record));
    close(fd);
    return 0;
}

static void t16(void) {
    reset2(); probe_span(1000);
    const char *pp = "/tmp/d169_p.bin", *wp = "/tmp/d169_w.bin";
    d169_stream p = mkprobe(), w = mkws();
    CHECK(write_stream(pp, &p, 0) == 0 && write_stream(wp, &w, 0) == 0,
          "stream files written");
    d169_report rp;
    CHECK(d169_decode_files(pp, wp, &OPTS, &rp) == D169_OK,
          "file decode rc");
    CHECK(rp.probes_scored == 1 && rp.ws_s.dropped == 0, "round trip");

    /* malformed classes — each distinct (G8) */
    d169_stream_mem m;
    int fd;
    /* truncated header */
    { char stub[8] = {'D','S','V','1',0,0,0,0};
      fd = open("/tmp/d169_b1.bin", O_WRONLY | O_CREAT | O_TRUNC, 0600);
      write(fd, stub, sizeof(stub)); close(fd); }
    CHECK(d169_stream_load("/tmp/d169_b1.bin", &m) == D169_E_HDR_TRUNC,
          "hdr trunc");
    /* bad magic — full-size file */
    { d169_stream_hdr h = { .magic = 0xDEAD, .version = D169_STREAM_VERSION,
        .cap = D141_RING_CAP, .n_records = 0 };
      fd = open("/tmp/d169_b2.bin", O_WRONLY | O_CREAT | O_TRUNC, 0600);
      write(fd, &h, sizeof(h)); close(fd); }
    CHECK(d169_stream_load("/tmp/d169_b2.bin", &m) == D169_E_MAGIC,
          "bad magic");
    /* version mismatch */
    { d169_stream_hdr h = { .magic = D169_STREAM_MAGIC, .version = 99,
        .cap = D141_RING_CAP, .n_records = 0 };
      fd = open("/tmp/d169_b3.bin", O_WRONLY | O_CREAT | O_TRUNC, 0600);
      write(fd, &h, sizeof(h)); close(fd); }
    CHECK(d169_stream_load("/tmp/d169_b3.bin", &m) == D169_E_VERSION,
          "bad version");
    /* CAP mismatch */
    { d169_stream_hdr h = { .magic = D169_STREAM_MAGIC,
        .version = D169_STREAM_VERSION, .cap = 128, .n_records = 0 };
      fd = open("/tmp/d169_b4.bin", O_WRONLY | O_CREAT | O_TRUNC, 0600);
      write(fd, &h, sizeof(h)); close(fd); }
    CHECK(d169_stream_load("/tmp/d169_b4.bin", &m) == D169_E_CAP,
          "cap mismatch");
    /* declared count > actual data → truncated */
    CHECK(write_stream("/tmp/d169_b5.bin", &w, 1) == 0 &&
          d169_stream_load("/tmp/d169_b5.bin", &m) == D169_E_TRUNCATED,
          "truncated records");
    /* trailing bytes after declared end → malformed */
    { write_stream("/tmp/d169_b6.bin", &w, 0);
      fd = open("/tmp/d169_b6.bin", O_WRONLY | O_APPEND);
      write(fd, "XX", 2); close(fd); }
    CHECK(d169_stream_load("/tmp/d169_b6.bin", &m) == D169_E_TRAILING,
          "trailing bytes");
    /* oversize count */
    { d169_stream_hdr h = { .magic = D169_STREAM_MAGIC,
        .version = D169_STREAM_VERSION, .cap = D141_RING_CAP,
        .n_records = D169_MAX_STREAM_RECS + 1 };
      fd = open("/tmp/d169_b7.bin", O_WRONLY | O_CREAT | O_TRUNC, 0600);
      write(fd, &h, sizeof(h)); close(fd); }
    CHECK(d169_stream_load("/tmp/d169_b7.bin", &m) == D169_E_COUNT_BOUND,
          "count bound");
    /* nonzero reserved field → malformed header */
    { d169_stream_hdr h = { .magic = D169_STREAM_MAGIC,
        .version = D169_STREAM_VERSION, .cap = D141_RING_CAP,
        .n_records = 0, .reserved = 1 };
      fd = open("/tmp/d169_b8.bin", O_WRONLY | O_CREAT | O_TRUNC, 0600);
      write(fd, &h, sizeof(h)); close(fd); }
    CHECK(d169_stream_load("/tmp/d169_b8.bin", &m) == D169_E_RESERVED,
          "nonzero reserved rejected");
    /* uuid_valid outside {0,1} → malformed, never normalized */
    { d169_stream_hdr h = { .magic = D169_STREAM_MAGIC,
        .version = D169_STREAM_VERSION, .cap = D141_RING_CAP,
        .n_records = 0, .uuid_valid = 2 };
      fd = open("/tmp/d169_b9.bin", O_WRONLY | O_CREAT | O_TRUNC, 0600);
      write(fd, &h, sizeof(h)); close(fd); }
    CHECK(d169_stream_load("/tmp/d169_b9.bin", &m) == D169_E_UUID_FLAGS,
          "uuid_valid=2 rejected");
    /* uuid_valid=0 decodes with unverified image — not normalized */
    { d169_stream_hdr h = { .magic = D169_STREAM_MAGIC,
        .version = D169_STREAM_VERSION, .cap = D141_RING_CAP,
        .n_records = 0, .uuid_valid = 0 };
      fd = open("/tmp/d169_b10.bin", O_WRONLY | O_CREAT | O_TRUNC, 0600);
      write(fd, &h, sizeof(h)); close(fd); }
    CHECK(d169_stream_load("/tmp/d169_b10.bin", &m) == D169_OK &&
          m.s.uuid_valid == 0, "uuid_valid=0 accepted as false");
    d169_stream_free(&m);
    /* in-memory view gets the same domain check */
    { d169_stream p = mkprobe(), w = mkws(); w.uuid_valid = 5;
      d169_report r2;
      CHECK(d169_decode(&p, &w, &OPTS, &r2) == D169_E_UUID_FLAGS,
            "in-memory uuid_valid=5 rejected"); }
    unlink(pp); unlink(wp);
    unlink("/tmp/d169_b1.bin"); unlink("/tmp/d169_b2.bin");
    unlink("/tmp/d169_b3.bin"); unlink("/tmp/d169_b4.bin");
    unlink("/tmp/d169_b5.bin"); unlink("/tmp/d169_b6.bin");
    unlink("/tmp/d169_b7.bin"); unlink("/tmp/d169_b8.bin");
    unlink("/tmp/d169_b9.bin"); unlink("/tmp/d169_b10.bin");
}

/* ---- t17 duplicate seqs + orphan RET --------------------------------- */
static void t17(void) {
    reset2(); probe_span(1000);
    /* duplicate a seq on the ws stream */
    WR[nw_] = WR[1]; nw_++;
    /* orphan RET on probe stream (no pending CALL) */
    prec(D141K_MACH_RET, 0x1234, D169_MACH_MSG_SUCCESS, PROBE_TID, 4000,0);
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.ws_s.dups == 1, "duplicate seq counted");
    CHECK(rp.orphan_ret == 1, "orphan RET counted");
}

/* ---- t18 two arms decode independently; full-UUID filter -------------- */
static void t18(void) {
    reset2(); probe_span(1000);
    d169_report a, b;
    CHECK(dec(&a) == D169_OK && a.probes_scored == 1, "arm A");
    /* arm B: cold start minted a new WS gen for the SAME image —
     * (pid999<<32)^U recovers the same low64 AND the declared 128-bit
     * UUID matches → identity joins fine across cold starts */
    { d169_stream p = mkprobe(), w = mkws();
      for (size_t i = 0; i < nw_; i++) {
          WR[i].gen = ((uint64_t)999 << 32) ^ U;
          WR[i].pid = 999;
      }
      w.gen = ((uint64_t)999 << 32) ^ U;
      w.pid = 999;
      CHECK(d169_decode(&p, &w, &OPTS, &b) == D169_OK &&
            b.probes_scored == 1,
            "new pid, same 128-bit image → accepted"); }
    /* image_uuid filter selects only streams carrying UUID U16 */
    { reset2(); probe_span(1000);
      d169_stream p = mkprobe(), w = mkws();
      d169_opts o = OPTS; o.image_uuid_set = 1;
      memcpy(o.image_uuid, U16, 16);
      CHECK(d169_decode(&p, &w, &o, &b) == D169_OK &&
            b.probes_scored == 1, "uuid filter accepts U16"); }
    { reset2(); probe_span(1000);
      d169_stream p = mkprobe(), w = mkws();
      d169_opts o = OPTS; o.image_uuid_set = 1;
      memcpy(o.image_uuid, U16B, 16);
      CHECK(d169_decode(&p, &w, &o, &b) == D169_E_IMAGE_MISMATCH,
            "uuid filter rejects non-U16"); }
}

/* ---- t19 F2: in-range seq ts-sorted OUTSIDE span → survives ----------- */
static void t19(void) {
    reset2();
    /* ws seqs 1,2 inside span; seq 3 exists but its ts sorts before
     * the CALL — the in-span footprint [1,2] is contiguous in the full
     * index ⇒ no false void */
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1050, 0);          /* seq1 */
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    /* seq2 lands in footprint; seq3 has ts 100 (before CALL) */
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 0);   /* seq2 */
    WR[nw_++] = (d141_record){ .magic = D141_SVC_MAGIC, .seq = wseq++,
        .ts_ns = 100, .tid = SVC_TID, .pid = WPID,
        .kind = D141K_DISPATCH_BEGIN, .msgid = 0x9999,
        .gen = WSGEN };                                     /* seq3,ts=50 */
    wrec(D141K_REPLY_ENQUEUE, REPLY, 0, SVC_TID, 1800, 0);   /* seq4 */
    prec(D141K_MACH_RET, REPLY, D169_MACH_MSG_SUCCESS, PROBE_TID, 1950,0);
    d169_report rp;
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.probes_scored == 1 && rp.latency_ns[0] == 600,
          "in-range seq outside ts-window does NOT void (F2)");
    CHECK(rp.gap_voided == 0, "no false gap");

    /* control: the same footprint with seq3 truly missing → voids */
    reset2();
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1050, 0);          /* seq1 */
    prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 1100, 0);
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 1);   /* seq2,+1  */
    wrec(D141K_REPLY_ENQUEUE, REPLY, 0, SVC_TID, 1800, 0);   /* seq4     */
    prec(D141K_MACH_RET, REPLY, D169_MACH_MSG_SUCCESS, PROBE_TID, 1950,0);
    CHECK(dec(&rp) == D169_OK, "decode rc2");
    CHECK(rp.gap_voided == 1, "true missing seq still voids");
}

/* ---- t20 D175: full-UUID identity failures ----------------------------- */
static void t20(void) {
    d169_report rp;
    /* same low64, different upper64 → still a different image → reject */
    reset2(); probe_span(1000);
    { d169_stream p = mkprobe(), w = mkws();
      memcpy(w.image_uuid, U16B, 16);         /* lo64 == U, hi64 differs */
      CHECK(d169_decode(&p, &w, &OPTS, &rp) == D169_E_IMAGE_MISMATCH,
            "same low64/different upper64 rejected"); }
    /* stream UUID bytes inconsistent with record gen low64 → reject */
    reset2(); probe_span(1000);
    { d169_stream p = mkprobe(), w = mkws();
      memcpy(w.image_uuid, U16C, 16);         /* lo64 = U2, gens carry U */
      CHECK(d169_decode(&p, &w, &OPTS, &rp) == D169_E_IMAGE_MISMATCH,
            "uuid/gen low64 mismatch rejected"); }
    /* per-record pid diverging from header pid → reject */
    reset2(); probe_span(1000);
    WR[1].pid = 999;
    { d169_stream p = mkprobe(), w = mkws();
      CHECK(d169_decode(&p, &w, &OPTS, &rp) == D169_E_IMAGE_MISMATCH,
            "rec.pid != header pid rejected"); }
    /* missing UUID on WS → decodes, but UNSCORABLE (not scored by low64) */
    reset2(); probe_span(1000);
    { d169_stream p = mkprobe(), w = mkws(); w.uuid_valid = 0;
      CHECK(d169_decode(&p, &w, &OPTS, &rp) == D169_OK, "decode rc");
      CHECK(rp.image_unverified == 1 && rp.scoring_suppressed == 1 &&
            rp.probes_scored == 0 && rp.suppressed_samples == 1,
            "missing UUID → unscorable, sample withheld"); }
    /* empty probe stream: header UUID alone is sufficient identity */
    reset2(); probe_span(1000);
    { d169_stream p = mkprobe(), w = mkws(); p.n = 0;
      CHECK(d169_decode(&p, &w, &OPTS, &rp) == D169_OK &&
            rp.call_absent == 1 && rp.image_unverified == 0 &&
            rp.scoring_suppressed == 0,
            "empty stream header UUID is sufficient identity"); }
    /* empty probe stream with NO UUID → unverifiable → suppressed */
    { d169_stream p = mkprobe(), w = mkws();
      p.n = 0; p.uuid_valid = 0;
      CHECK(d169_decode(&p, &w, &OPTS, &rp) == D169_OK,
            "unknown uuid still decodes");
      CHECK(rp.image_unverified == 1 && rp.scoring_suppressed == 1,
            "unknown uuid → suppressed"); }
}

/* ---- t21 bounded overflow → counters + suppressed scoring ------------- */
static void t21(void) {
    d169_report rp;
    /* a clean probe span + >64 pending CALLs: cap fires → the sample
     * is withheld (fail closed), counters stay honest */
    reset2();
    probe_span(1000);
    for (int i = 0; i < 70; i++)
        prec(D141K_MACH_CALL, 0x6000 + i, 0x1, PROBE_TID, 5000 + i, 0);
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.pend_overflow > 0, "pending overflow counted");
    CHECK(rp.scoring_suppressed == 1 && rp.probes_scored == 0 &&
          rp.suppressed_samples == 1,
          "pend cap → scoring suppressed, sample withheld");
    /* >64 distinct PASS_ENTER tids → tids_capped, no scored argmax */
    reset2(); probe_span(1000);
    for (int i = 0; i < 70; i++)
        wrec(D141K_PASS_ENTER, 0, 0, 0x9000 + i, 5000 + i, 0);
    CHECK(dec(&rp) == D169_OK, "decode rc2");
    CHECK(rp.tids_capped > 0 && rp.scoring_suppressed == 1 &&
          rp.probes_scored == 0,
          "tid cap → argmax from truncated table is not scored");
    CHECK(rp.mach_calls > 0, "counters retained under suppression");
}

/* ---- t23 span_overflow: >D169_MAX_PROBES probe CALL spans ------------- */
static void t23(void) {
    d169_report rp;
    /* one fully-valid probe span scores, then enough probe CALLs to
     * fill the span table and overflow it — span_overflow must fire
     * (not merely pend_overflow) */
    reset2(); probe_span(1000);
    /* scored span occupies span slot 0; 1023 more fill to 1024, the
     * 1024th extra CALL overflows the table */
    for (unsigned i = 0; i < D169_MAX_PROBES; i++)
        prec(D141K_MACH_CALL, PROBE, 0x3000903, PROBE_TID, 5000 + i, 0);
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.span_overflow > 0, "span table overflow counted");
    CHECK(rp.scoring_suppressed == 1 && rp.probes_scored == 0,
          "span cap → scoring suppressed");
    CHECK(rp.suppressed_samples == 1,
          "the one valid sample is withheld, not lost");
    CHECK(rp.mach_calls == 1025, "all CALLs counted honestly");
}

/* ---- t22 call_absent split: no CALLs vs wrong-id CALLs ---------------- */
static void t22(void) {
    d169_report rp;
    /* probe stream has CALLs, none msgid=probe → no_probe_call */
    reset2();
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1050, 0);
    prec(D141K_MACH_CALL, 0x5555, 0x1, PROBE_TID, 1100, 0);
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 0);
    CHECK(dec(&rp) == D169_OK, "decode rc");
    CHECK(rp.call_absent == 0 && rp.no_probe_call == 1,
          "CALLs-but-wrong-id distinguished from no-CALLs");
    /* truly no CALL records → call_absent */
    reset2();
    wrec(D141K_PASS_ENTER, 0, 0, SVC_TID, 1050, 0);
    wrec(D141K_HANDLER_BEGIN, PROBE, 0, SVC_TID, 1700, 0);
    CHECK(dec(&rp) == D169_OK, "decode rc2");
    CHECK(rp.call_absent == 1 && rp.no_probe_call == 0,
          "zero CALLs → call_absent");
}

int main(void) {
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "t1",t1 },{ "t2",t2 },{ "t3",t3 },{ "t4",t4 },{ "t5",t5 },
        { "t6",t6 },{ "t7",t7 },{ "t8",t8 },{ "t9",t9 },{ "t10",t10 },
        { "t11",t11 },{ "t12",t12 },{ "t13",t13 },{ "t14",t14 },
        { "t15",t15 },{ "t16",t16 },{ "t17",t17 },{ "t18",t18 },
        { "t19",t19 },{ "t20",t20 },{ "t21",t21 },{ "t22",t22 },
        { "t23",t23 },
    };
    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        int before = checks;
        tests[i].fn();
        printf("%-4s %3d checks\n", tests[i].name, checks - before);
    }
    printf("%s: %d checks, %d failures\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
