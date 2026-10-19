/*
 * D190 host-side tests — loopback ports only; NEVER invokes SkyLight,
 * _SLSServerPort, or WindowServer. Covers wire constants, request
 * shape, LC_UUID walking, reply classification, record serialization,
 * stream round-trip through the accepted d169 loader+decoder, and a
 * two-thread loopback send proving the full send/receive/cleanup path
 * (G17 acceptance class 1).
 */

#include "d190_probe.h"

#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach/ndr.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static int checks, failures;
#define CHECK(cond, msg) do { checks++; \
    if (!(cond)) { failures++; printf("FAIL %s: %s\n", __func__, msg); } \
} while (0)

static const uint8_t TUUID[16] = D190_SKYLIGHT_UUID_BYTES;

/* ---- p1: wire constants ------------------------------------------ */
static void p1(void) {
    CHECK(sizeof(d190_req) == 24, "request is exactly 24 bytes");
    CHECK(sizeof(d190_reply) == 0x2c, "reply is exactly 0x2c");
    CHECK(sizeof(d190_msgbuf) >= D190_RCV_SIZE, "rcv buffer >= 0x34");
    CHECK(D190_MSG_OPTION ==
          (MACH_SEND_MSG | MACH_RCV_MSG | MACH_SEND_SYNC_OVERRIDE |
           MACH_SEND_PROPAGATE_QOS | MACH_RCV_SYNC_WAIT),
          "option decodes to SEND|RCV|SYNC_OVERRIDE|PROPAGATE_QOS|SYNC_WAIT");
    CHECK((D190_MSG_BITS & 0xff) == MACH_MSG_TYPE_COPY_SEND,
          "remote disposition COPY_SEND");
    CHECK(((D190_MSG_BITS >> 8) & 0xff) == MACH_MSG_TYPE_MAKE_SEND_ONCE,
          "local disposition MAKE_SEND_ONCE");
    CHECK(D190_REPLY_ID == D190_REQ_ID + 100, "reply = id+100 (MIG)");

    d190_req r;
    d190_build_request(&r, 0x1234, 0xabcd);
    CHECK(r.head.msgh_bits == 0x1513, "build: bits 0x1513");
    CHECK(r.head.msgh_size == 0x18, "build: size 0x18");
    CHECK(r.head.msgh_remote_port == 0x1234, "build: remote");
    CHECK(r.head.msgh_local_port == 0xabcd, "build: local send-once");
    CHECK(r.head.msgh_voucher_port == 0, "build: voucher 0");
    CHECK(r.head.msgh_id == 0x74a7, "build: id 0x74a7");
    uint8_t *b = (uint8_t *)&r;
    int body_zero = 1;
    for (unsigned i = 24; i < sizeof(r); i++) body_zero &= b[i] == 0;
    CHECK(sizeof(r) == 24 && body_zero, "header-only, no body");
}

/* ---- p2: LC_UUID extraction + gate ------------------------------- */
static void p2(void) {
    /* fabricated 64-bit Mach-O: header + uuid_command */
    struct { struct mach_header_64 mh; struct uuid_command uc; } f;
    memset(&f, 0, sizeof(f));
    f.mh.magic = MH_MAGIC_64; f.mh.ncmds = 1;
    f.mh.sizeofcmds = sizeof(f.uc);          /* declared region (F3)  */
    f.uc.cmd = LC_UUID; f.uc.cmdsize = sizeof(f.uc);
    memcpy(f.uc.uuid, TUUID, 16);
    uint8_t out[16]; memset(out, 0, 16);
    CHECK(d190_image_lc_uuid(&f.mh, out) == 0 &&
          memcmp(out, TUUID, 16) == 0, "LC_UUID extracted");
    f.mh.magic = MH_MAGIC; /* 32-bit → rejected */
    CHECK(d190_image_lc_uuid(&f.mh, out) == -1, "non-64 header refused");
    f.mh.magic = MH_MAGIC_64; f.uc.cmd = LC_SEGMENT_64;
    CHECK(d190_image_lc_uuid(&f.mh, out) == -1, "no uuid cmd → -1");

    memset(out, 0xAA, 16);
    int rc = d190_check_skylight_uuid(out);
    if (rc == 0) {
        CHECK(memcmp(out, TUUID, 16) == 0, "gate success → target uuid");
    } else {
        int z = 1; for (int i = 0; i < 16; i++) z &= out[i] == 0;
        CHECK(rc == -1 && z, "gate failure → -1 + zeroed out");
    }
}

/* ---- p3: reply classification (all malformed classes) --------------- */
static void p3(void) {
    d190_msgbuf b;
    memset(&b, 0, sizeof(b));
    b.h.msgh_bits = 0x12;
    b.h.msgh_size = D190_REPLY_SIZE;
    b.h.msgh_id = D190_REPLY_ID;
    b.r.retcode = 0;
    CHECK(d190_classify_reply(&b, KERN_SUCCESS) == D190_OK, "valid → OK");

    CHECK(d190_classify_reply(&b, MACH_SEND_INVALID_DEST) == D190_E_KR,
          "kr != SUCCESS → E_KR");
    b.h.msgh_size = 8;
    CHECK(d190_classify_reply(&b, KERN_SUCCESS) == D190_E_TRUNC, "trunc");
    b.h.msgh_size = D190_REPLY_SIZE;
    b.h.msgh_bits |= MACH_MSGH_BITS_COMPLEX;
    CHECK(d190_classify_reply(&b, KERN_SUCCESS) == D190_E_COMPLEX,
          "bit31 → complex");
    b.h.msgh_bits = 0x12;
    b.h.msgh_id = 0x750a;
    CHECK(d190_classify_reply(&b, KERN_SUCCESS) == D190_E_BAD_ID, "bad id");
    b.h.msgh_id = D190_REPLY_ID;
    b.h.msgh_size = 0x30;
    CHECK(d190_classify_reply(&b, KERN_SUCCESS) == D190_E_BAD_SIZE,
          "size != 0x2c");
    b.h.msgh_size = D190_REPLY_SIZE;
    b.h.msgh_remote_port = 55;
    CHECK(d190_classify_reply(&b, KERN_SUCCESS) == D190_E_REMOTE,
          "remote != 0");
    b.h.msgh_remote_port = 0;
    b.r.retcode = 1002;
    CHECK(d190_classify_reply(&b, KERN_SUCCESS) == D190_E_RETCODE,
          "retcode != 0");
}

/* ---- p4: record legs + gen tag ------------------------------------- */
static void p4(void) {
    uint64_t gen = d190_gen_tag(555, TUUID);
    uint64_t lo; memcpy(&lo, TUUID, 8);
    CHECK(gen == (((uint64_t)555u << 32) ^ lo),
          "gen = (pid<<32)^uuid_lo64 — d141 encoding");

    d141_record rec[2];
    d190_fill_records(rec, 1000, 1250, 0xAAAA, 555, gen,
                      KERN_SUCCESS, D190_REPLY_ID);
    CHECK(rec[0].magic == D141_SVC_MAGIC && rec[0].kind == D141K_MACH_CALL,
          "CALL leg kind/magic");
    CHECK(rec[0].seq == 0 && rec[1].seq == 1, "seq 0,1");
    CHECK(rec[0].msgid == 0x74a7 && rec[0].aux == D190_MSG_OPTION,
          "CALL msgid + option verbatim");
    CHECK(rec[1].kind == D141K_MACH_RET &&
          rec[1].msgid == 0x750b && rec[1].aux == 0,
          "RET msgid=reply id, aux=kr verbatim");
    CHECK(rec[0].tid == rec[1].tid && rec[0].pid == 555 &&
          rec[0].gen == gen && rec[1].gen == gen, "same tid/pid/gen");
    CHECK(rec[1].ts_ns >= rec[0].ts_ns, "RET ts >= CALL ts");

    d190_fill_records(rec, 1, 2, 0xAAAA, 555, gen,
                      MACH_SEND_INVALID_DEST, 0);
    CHECK(rec[1].msgid == 0 && rec[1].aux == MACH_SEND_INVALID_DEST,
          "kr fail → RET msgid 0, aux=kr");
}

/* ---- p5: stream write + d169 parser round-trip + decode ------------ */
static void p5(void) {
    const char *path = "/tmp/d190_stream_test.bin";
    uint64_t gen = d190_gen_tag(555, TUUID);
    d141_record rec[2];
    d190_fill_records(rec, 1000, 1600, 0xAAAA, 555, gen,
                      KERN_SUCCESS, D190_REPLY_ID);
    CHECK(d190_write_stream(path, rec, TUUID, 555, "d190-probe") == 0,
          "stream written");

    /* parser round-trip through the ACCEPTED d169 loader */
    d169_stream_mem m;
    CHECK(d169_stream_load(path, &m) == D169_OK, "d169 loader accepts");
    const d169_stream *s = &m.s;
    CHECK(s->n == 2 && s->pid == 555 && s->gen == gen, "hdr pid/gen/n");
    CHECK(s->emitted == 2 && s->dropped == 0 && s->uncommitted == 0,
          "emitted=2 drops=0 uncommitted=0");
    CHECK(s->uuid_valid == 1 &&
          memcmp(s->image_uuid, TUUID, 16) == 0,
          "uuid_valid + full uuid producer-declared");
    CHECK(s->recs[0].seq == 0 && s->recs[1].seq == 1 &&
          s->recs[0].ts_ns == 1000 && s->recs[1].ts_ns == 1600,
          "records verbatim");

    /* full decode against a synthetic WS stream sharing the image —
     * proves the writer output is exactly the accepted contract */
    uint8_t wuuid[16]; memcpy(wuuid, TUUID, 16);
    uint64_t wgen = d190_gen_tag(777, wuuid);
    d141_record w[8] = {
        { D141_SVC_MAGIC, 0, 1100, 0x1111, 777, D141K_PASS_ENTER,    0,      0, wgen },
        { D141_SVC_MAGIC, 1, 1200, 0x1111, 777, D141K_DISPATCH_BEGIN,0x74a7, 0, wgen },
        { D141_SVC_MAGIC, 2, 1300, 0x1111, 777, D141K_HANDLER_BEGIN, 0x74a7, 0, wgen },
        { D141_SVC_MAGIC, 3, 1400, 0x1111, 777, D141K_IMPL_INVOKE,   0x74a7, 0, wgen },
        { D141_SVC_MAGIC, 4, 1500, 0x1111, 777, D141K_REPLY_ENQUEUE, 0x750b, 0, wgen },
        { D141_SVC_MAGIC, 5, 1700, 0x1111, 777, D141K_PASS_EXIT,     0,      0, wgen },
    };
    d169_stream ws = {0};
    ws.gen = wgen; ws.pid = 777; ws.n = 6; ws.recs = w;
    memcpy(ws.image_uuid, wuuid, 16); ws.uuid_valid = 1;
    d169_opts o = {0}; o.probe_msgid = D190_REQ_ID;
    d169_report rep;
    CHECK(d169_decode(s, &ws, &o, &rep) == D169_OK, "decode OK");
    CHECK(rep.probes_scored == 1, "one scored span");
    CHECK(rep.latency_ns[0] == 1300 - 1000,
          "latency = handler_begin − call = 300ns");
    CHECK(rep.svc_tid == 0x1111, "services tid resolved");
    CHECK(rep.mach_calls == 1 && rep.replies == 1, "call + reply join");
    d169_stream_free(&m);
    unlink(path);
}

/* ---- p6: loopback send (G17) — fake WS thread ----------------------- */
static mach_port_t loop_port;
static d190_msgbuf loop_req;
static uint32_t loop_reply_id = D190_REPLY_ID;
static uint32_t loop_reply_size = D190_REPLY_SIZE;
static int      loop_retcode;

static void *fake_ws(void *a) {
    (void)a;
    /* receive the 24-byte request verbatim off the wire */
    mach_msg(&loop_req.h, MACH_RCV_MSG, 0, (mach_msg_size_t)sizeof(loop_req),
             loop_port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    /* reply on the send-once right the kernel vended in the received
     * message's REMOTE port slot (received local = port received on);
     * buffer is oversized so TOO_LARGE injection is possible */
    static uint8_t repb[128];
    memset(repb, 0, sizeof(repb));
    d190_msgbuf *rep = (d190_msgbuf *)repb;
    rep->h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_MOVE_SEND_ONCE, 0);
    rep->h.msgh_size = loop_reply_size;
    rep->h.msgh_remote_port = loop_req.h.msgh_remote_port;
    rep->h.msgh_id = loop_reply_id;
    rep->r.ndr = NDR_record;
    rep->r.retcode = loop_retcode;
    rep->r.payload = 0xDEADBEEFCAFEF00Dull;
    mach_msg(&rep->h, MACH_SEND_MSG, rep->h.msgh_size, 0, MACH_PORT_NULL,
             MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
    return NULL;
}

static void p6(void) {
    /* bind a loopback remote: receive right + inserted send right */
    CHECK(mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
                             &loop_port) == KERN_SUCCESS, "loop port");
    CHECK(mach_port_insert_right(mach_task_self(), loop_port, loop_port,
                                 MACH_MSG_TYPE_MAKE_SEND) == KERN_SUCCESS,
          "send right");

    d190_hook_cleanup_none = d190_hook_cleanup_destroy =
        d190_hook_cleanup_dealloc = 0;
    loop_reply_id = D190_REPLY_ID; loop_reply_size = D190_REPLY_SIZE;
    loop_retcode = 0;
    pthread_t t;
    pthread_create(&t, NULL, fake_ws, NULL);

    uint64_t gen = d190_gen_tag(555, TUUID);
    d141_record rec[2]; uint64_t payload = 0;
    enum d190_reply_class c =
        d190_send_once(loop_port, 555, gen, rec, &payload);
    pthread_join(t, NULL);

    CHECK(c == D190_OK, "loopback reply accepted");
    CHECK(payload == 0xDEADBEEFCAFEF00Dull, "payload @+0x24");
    /* request as captured off the wire (G3 proof): size/id/voucher
     * verbatim; the kernel TRANSLATES msgh_bits on receipt — remote
     * becomes MOVE_SEND_ONCE — so assert the received semantics:
     * complex bit clear + a vendored send-once reply right present. */
    CHECK(loop_req.h.msgh_size == 0x18 &&
          loop_req.h.msgh_id == 0x74a7 &&
          loop_req.h.msgh_voucher_port == 0,
          "wire size/id/voucher verbatim");
    CHECK(!(loop_req.h.msgh_bits & MACH_MSGH_BITS_COMPLEX),
          "request not complex");
    CHECK(loop_req.h.msgh_local_port == loop_port,
          "received on loopback port");
    CHECK(loop_req.h.msgh_remote_port != MACH_PORT_NULL &&
          loop_req.h.msgh_remote_port != loop_port,
          "vendored send-once reply right");
    /* records (G12) */
    CHECK(rec[0].kind == D141K_MACH_CALL && rec[0].msgid == 0x74a7 &&
          rec[0].aux == D190_MSG_OPTION, "CALL leg");
    CHECK(rec[1].kind == D141K_MACH_RET && rec[1].msgid == 0x750b &&
          rec[1].aux == 0, "RET leg verbatim");
    CHECK(rec[1].ts_ns >= rec[0].ts_ns && rec[0].ts_ns != 0,
          "client-side monotonic legs");
    uint64_t mytid = 0; pthread_threadid_np(NULL, &mytid);
    CHECK(rec[0].tid == mytid && rec[1].tid == mytid, "measured tid");
    CHECK(rec[0].pid == 555 && rec[0].gen == gen, "identity carried");
    /* D192 class 1: valid reply consumed send-once; SRP retained —
     * no cleanup ran */
    CHECK(d190_hook_cleanup_none == 1 &&
          d190_hook_cleanup_destroy == 0 &&
          d190_hook_cleanup_dealloc == 0, "valid reply → cleanup NONE");

    /* malformed-reply injection: bad reply id → BAD_ID (class 2:
     * mach_msg_destroy ran, SRP retained — NOT dealloc) */
    loop_reply_id = 0x1234;
    pthread_create(&t, NULL, fake_ws, NULL);
    c = d190_send_once(loop_port, 555, gen, rec, &payload);
    pthread_join(t, NULL);
    CHECK(c == D190_E_BAD_ID, "malformed reply → BAD_ID");
    CHECK(d190_hook_cleanup_destroy == 1 &&
          d190_hook_cleanup_dealloc == 0,
          "malformed → DESTROY executed, SRP retained");
    CHECK(rec[1].aux == 0 && rec[1].msgid == 0x1234,
          "RET aux=kr=0, id verbatim on malformed");

    /* dead remote → MACH_SEND_INVALID_DEST = class 3: nothing runs —
     * NOT dealloc (D192: dealloc would churn a pristine port) */
    c = d190_send_once(MACH_PORT_NULL, 555, gen, rec, &payload);
    CHECK(c == D190_E_KR, "dead remote → E_KR");
    CHECK(rec[1].aux == MACH_SEND_INVALID_DEST,
          "kr is SEND_INVALID_DEST = send-side reject");
    CHECK(d190_hook_cleanup_none == 2 &&
          d190_hook_cleanup_dealloc == 0,
          "send-reject → cleanup NONE (SRP retained)");
    CHECK(rec[1].kind == D141K_MACH_RET && rec[1].msgid == 0,
          "RET msgid=0 — pair never fabricated");
    CHECK(rec[0].msgid == 0x74a7, "CALL still recorded");

    /* oversized reply → MACH_RCV_TOO_LARGE = class 4: real
     * mig_dealloc_special_reply_port executes on the thread SRP */
    loop_reply_id = D190_REPLY_ID; loop_reply_size = 0x40;
    pthread_create(&t, NULL, fake_ws, NULL);
    c = d190_send_once(loop_port, 555, gen, rec, &payload);
    pthread_join(t, NULL);
    CHECK(c == D190_E_KR && rec[1].aux == MACH_RCV_TOO_LARGE,
          "oversized reply → RCV_TOO_LARGE");
    CHECK(d190_hook_cleanup_dealloc == 1,
          "other-kr → DEALLOC executed on thread SRP");

    mach_port_mod_refs(mach_task_self(), loop_port,
                       MACH_PORT_RIGHT_RECEIVE, -1);
}

/* ---- p7: cleanup decision — all four classes, deterministic ---------- */
static void p7(void) {
    CHECK(d190_cleanup_class(KERN_SUCCESS, D190_OK) == D190_CLEAN_NONE,
          "valid → NONE");
    CHECK(d190_cleanup_class(KERN_SUCCESS, D190_E_BAD_ID) ==
          D190_CLEAN_DESTROY, "malformed → DESTROY");
    CHECK(d190_cleanup_class(KERN_SUCCESS, D190_E_COMPLEX) ==
          D190_CLEAN_DESTROY, "complex → DESTROY");
    CHECK(d190_cleanup_class(MACH_SEND_INVALID_DATA, D190_E_KR) ==
          D190_CLEAN_NONE, "SEND_INVALID_DATA → NONE");
    CHECK(d190_cleanup_class(MACH_SEND_INVALID_DEST, D190_E_KR) ==
          D190_CLEAN_NONE, "SEND_INVALID_DEST → NONE");
    CHECK(d190_cleanup_class(MACH_SEND_INVALID_HEADER, D190_E_KR) ==
          D190_CLEAN_NONE, "SEND_INVALID_HEADER → NONE");
    CHECK(d190_cleanup_class(MACH_RCV_TOO_LARGE, D190_E_KR) ==
          D190_CLEAN_DEALLOC, "RCV_TOO_LARGE → DEALLOC");
    CHECK(d190_cleanup_class(MACH_RCV_TIMED_OUT, D190_E_KR) ==
          D190_CLEAN_DEALLOC, "RCV_TIMED_OUT → DEALLOC");
    CHECK(d190_cleanup_class(MACH_RCV_INTERRUPTED, D190_E_KR) ==
          D190_CLEAN_DEALLOC, "RCV_INTERRUPTED → DEALLOC");
    CHECK(d190_cleanup_class(MACH_RCV_PORT_DIED, D190_E_KR) ==
          D190_CLEAN_DEALLOC, "RCV_PORT_DIED → DEALLOC");
    CHECK(d190_cleanup_class(0x10000020, D190_E_KR) ==
          D190_CLEAN_DEALLOC, "out-of-range kr → DEALLOC");
    CHECK(MACH_SEND_INVALID_DATA == 0x10000002 &&
          MACH_SEND_INVALID_DEST == 0x10000003 &&
          MACH_SEND_INVALID_HEADER == 0x10000010,
          "send-reject constants verbatim");
}

/* ---- p8: F1 dlsym C-name + F2 address-owning-image bind ---------------
 * F1 mechanism proven on a harmless fixture symbol (never SkyLight):
 * Darwin dlsym resolves the C name and does NOT resolve the nlist
 * "_"-prefixed spelling. F2: dladdr binds an address to its owning
 * image's LC_UUID — match vs the test binary, mismatch vs fixture. */
static int self_uuid(uint8_t out[16]) {
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const char *n = _dyld_get_image_name(i);
        if (n && strstr(n, "d190_test"))
            return d190_image_lc_uuid(
                (const struct mach_header_64 *)_dyld_get_image_header(i),
                out);
    }
    return -1;
}
static void p8(void) {
    void *h = dlopen("/tmp/d190_fixture.dylib", RTLD_NOW);
    CHECK(h != NULL, "fixture loaded");
    void *fp = h ? dlsym(h, "d190_fixture_serverport") : NULL;
    CHECK(fp != NULL, "C-name resolves (F1 mechanism)");
    CHECK(h && dlsym(h, "_d190_fixture_serverport") == NULL,
          "underscored name does NOT resolve — the F1 bug shape");
    uint8_t su[16];
    CHECK(self_uuid(su) == 0, "self image LC_UUID via dyld walk");
    CHECK(d190_addr_image_match((const void *)(uintptr_t)&p8, su) == 0,
          "addr bound to owning image uuid (match)");
    CHECK(d190_addr_image_match((const void *)(uintptr_t)&p8, TUUID)
          == -1, "addr vs pinned SkyLight uuid → mismatch");
    uint8_t fu[16];
    CHECK(fp && d190_addr_image_uuid(fp, fu) == 0 &&
          memcmp(fu, su, 16) != 0,
          "fixture addr owns a DIFFERENT image uuid");
    CHECK(fp && d190_addr_image_match(fp, TUUID) == -1,
          "fixture addr vs target → mismatch");
    if (h) dlclose(h);
}

/* ---- p9: F3 bounded LC scanner -------------------------------------- */
static void p9(void) {
    struct {
        struct mach_header_64    mh;
        struct segment_command_64 s;
        struct uuid_command       u;
    } f;
    memset(&f, 0, sizeof f);
    f.mh.magic = MH_MAGIC_64; f.mh.ncmds = 2;
    f.mh.sizeofcmds = sizeof(f.s) + sizeof(f.u);
    f.s.cmd = LC_SEGMENT_64; f.s.cmdsize = sizeof(f.s);
    f.u.cmd = LC_UUID; f.u.cmdsize = sizeof(f.u);
    memcpy(f.u.uuid, TUUID, 16);
    uint8_t out[16];

    CHECK(d190_image_lc_uuid_bounded(&f.mh, sizeof f, out) == 0 &&
          memcmp(out, TUUID, 16) == 0, "bounded scan: valid");
    f.s.cmdsize = 0;
    CHECK(d190_image_lc_uuid_bounded(&f.mh, sizeof f, out) == -1,
          "cmdsize=0 rejected, not a hang");
    f.s.cmdsize = sizeof(f.s);
    CHECK(d190_image_lc_uuid_bounded(&f.mh,
          sizeof(f.mh) + sizeof(f.s) + 8, out) == -1,
          "hdr_bytes < declared sizeofcmds → bounded reject, no OOB");
    f.u.cmdsize = sizeof(struct load_command);
    CHECK(d190_image_lc_uuid_bounded(&f.mh, sizeof f, out) == -1,
          "LC_UUID cmdsize < uuid_command rejected");
    f.u.cmdsize = sizeof(f.u);
    f.mh.ncmds = 99;                 /* more cmds than region holds */
    CHECK(d190_image_lc_uuid_bounded(&f.mh, sizeof f, out) == -1,
          "ncmds past declared region rejected");
    f.mh.ncmds = 2;
    f.s.cmdsize = 0x1000;            /* cmdsize leaps past limit */
    CHECK(d190_image_lc_uuid_bounded(&f.mh, sizeof f, out) == -1,
          "oversized cmdsize rejected");
    f.s.cmdsize = sizeof(f.s);

    { /* duplicate LC_UUID → malformed */
      struct { struct mach_header_64 mh;
               struct uuid_command a, b; } g;
      memset(&g, 0, sizeof g);
      g.mh.magic = MH_MAGIC_64; g.mh.ncmds = 2;
      g.mh.sizeofcmds = sizeof(g.a) + sizeof(g.b);
      g.a.cmd = LC_UUID; g.a.cmdsize = sizeof(g.a);
      memcpy(g.a.uuid, TUUID, 16);
      g.b.cmd = LC_UUID; g.b.cmdsize = sizeof(g.b);
      memcpy(g.b.uuid, TUUID, 16);
      CHECK(d190_image_lc_uuid_bounded(&g.mh, sizeof g, out) == -1,
            "duplicate LC_UUID rejected"); }

    CHECK(d190_image_lc_uuid_bounded(&f.mh, sizeof(f.mh) - 1, out)
          == -1, "hdr_bytes < header rejected");
}

int main(void) {
    struct { const char *n; void (*f)(void); } t[] = {
        { "p1", p1 }, { "p2", p2 }, { "p3", p3 },
        { "p4", p4 }, { "p5", p5 }, { "p6", p6 }, { "p7", p7 },
        { "p8", p8 }, { "p9", p9 },
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        int before = checks;
        t[i].f();
        printf("%-3s %3d checks\n", t[i].n, checks - before);
    }
    printf("%s: %d checks, %d failures\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
