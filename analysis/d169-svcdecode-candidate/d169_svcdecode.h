/*
 * D169 svcdecode — offline two-stream service-wait decoder candidate.
 * Diagnostic-only; repairs the single-stream model per the frozen D170
 * checklist (G1–G12 + C1–C7). No sender, hooks, permits, device, or
 * product integration.
 *
 * C1 two-stream model: probe-side MACH_CALL/MACH_RET legs live on the
 * PROBE process's tid in the probe stream; PASS/HANDLER/IMPL/ENQUEUE/
 * FLUSH edges live on the WS services tid in the WS stream. The two
 * streams merge by ts_ns (stable tie-break: probe stream first on equal
 * ts, then original index). CALL↔RET pairing happens ONLY inside the
 * probe stream (nearest unmatched same-tid CALL); WS edges bind to the
 * open span by msgid on the selected services tid.
 *
 * C2 CALL-absent: probe legs exist only if the probe process is
 * instrumented — a window without CALL(probe) fails the timing metric
 * explicitly (call_absent flag); no degraded HANDLER−PASS proxy.
 *
 * C3 per-stream seq validation runs BEFORE merge; each stream has its
 * own seq space. A gap inside a span's stream footprint voids that
 * span; gaps elsewhere are reported, never voiding.
 *
 * C4 merged order is ts order, not drain order — non-monotonic ts is
 * legal, never malformed. Negative HANDLER−CALL latency is invalid.
 *
 * C5 EOF-open spans (CALL unpaired) are reported, never auto-closed.
 *
 * C6 batch: multiple HANDLER_BEGIN per PASS bracket is legal; pairing
 * is by msgid; foreign edges inside a bracket do not disturb pairing.
 *
 * C7 reply cross-check: probe replies are expected at msgid+100 on
 * ENQUEUE and the paired RET; a mismatch flags an anomaly — pairing
 * still stands (adjacency rule).
 *
 * Generation isolation (G2, fail-closed): each stream must carry
 * exactly one gen tag; a second distinct gen rejects the stream
 * (E_MULTIGEN). opts->gen_filter nonzero requires an exact match.
 * Cross-stream tags differ legitimately (different processes) — only
 * per-stream uniformity is enforced.
 */

#ifndef D169_SVCDECODE_H
#define D169_SVCDECODE_H

#include <stdint.h>
#include <stddef.h>
#include "../d141-svctime-candidate/d141_svctime.h"

#define D169_STREAM_MAGIC   0x44535631u   /* 'DSV1' */
#define D169_STREAM_VERSION 3u
#define D169_MAX_PROBES     1024u
#define D169_PENDING_CALLS  64u
#define D169_MAX_STREAM_RECS (1u << 22)

#define D169_MACH_MSG_SUCCESS    0x00000000ull
#define D169_MACH_RCV_TIMED_OUT  0x10004003ull  /* osfmk/mach/message.h:1231 */
#define D169_MACH_RCV_TOO_LARGE  0x10004004ull
#define D169_REPLY_DELTA         100ull
#define D169_CLUSTER_FLOOR_NS    8000000ull

typedef struct {
    uint64_t magic;        /* D169_STREAM_MAGIC                        */
    uint64_t version;      /* must equal D169_STREAM_VERSION (G1)      */
    uint64_t cap;          /* must equal D141_RING_CAP (G1)            */
    uint64_t gen;          /* stream generation tag                    */
    int32_t  pid;
    uint32_t flags;
    uint64_t emitted;      /* committed-record snapshot (G9)           */
    uint64_t dropped;      /* producer drop snapshot                   */
    uint64_t uncommitted;
    uint64_t n_records;
    uint8_t  image_uuid[16]; /* full 128-bit SkyLight Mach-O UUID      */
    uint32_t uuid_valid;   /* exactly 1 = producer-declared UUID valid  */
    uint32_t reserved;
    char     label[24];
} d169_stream_hdr;

typedef enum {
    D169_OK = 0,
    D169_E_ARG,            /* bad arguments                            */
    D169_E_OPEN,           /* cannot open                              */
    D169_E_HDR_TRUNC,      /* file shorter than the header             */
    D169_E_MAGIC,          /* wrong magic                              */
    D169_E_VERSION,        /* unsupported version                      */
    D169_E_CAP,            /* ring-cap mismatch                        */
    D169_E_COUNT_BOUND,    /* n_records out of bounds                  */
    D169_E_TRUNCATED,      /* file ends before declared records        */
    D169_E_TRAILING,       /* bytes after declared records / end marker*/
    D169_E_READ,           /* short read mid-stream                    */
    D169_E_MULTIGEN,       /* second distinct gen inside one stream    */
    D169_E_IMAGE_MISMATCH, /* identity failure: rec.pid != header pid,
                          * per-record recovered low64 diverges from the
                          * stream's UUID bytes or between records,
                          * cross-stream UUIDs differ, or the
                          * image_uuid filter is unmet                 */
    D169_E_ALLOC,          /* correlation state allocation failed      */
    D169_E_RESERVED,       /* header reserved field nonzero            */
    D169_E_UUID_FLAGS,     /* uuid_valid not exactly 0 or 1            */
} d169_err;

const char *d169_err_str(d169_err e);

/* In-memory stream view (probe or WS). For files use
 * d169_stream_load/_free. */
typedef struct {
    const d141_record *recs;
    size_t   n;
    uint64_t gen;          /* declared stream gen (0 = derive from recs) */
    int32_t  pid;
    uint64_t emitted, dropped, uncommitted;
    uint8_t  image_uuid[16]; /* 128-bit image UUID; only when uuid_valid */
    int      uuid_valid;
    char     label[24];
} d169_stream;

typedef struct {           /* owns a malloc'd record buffer            */
    d169_stream   s;
    d141_record  *owned;
} d169_stream_mem;

typedef struct {
    uint64_t probe_msgid;
    uint8_t  image_uuid[16]; /* exact 128-bit image filter             */
    int      image_uuid_set; /* nonzero = filter both streams by uuid  */
    uint64_t wait_ns;      /* arm wait interval (cluster bound input)  */
    char     sender_label[24];
} d169_opts;

typedef struct {
    uint64_t records, gaps, dups, emitted, dropped, uncommitted;
    uint64_t gen; int32_t pid;
    uint8_t  image_uuid[16]; /* stream's authoritative UUID (verbatim) */
    int      uuid_valid;     /* nonzero = image_uuid usable            */
} d169_stream_stats;

enum { D169_TID_OK = 0, D169_TID_AMBIGUOUS_TIE = 1, D169_TID_INSUFFICIENT = 2 };

typedef struct {
    /* inputs echoed */
    uint64_t svc_tid;
    int      tid_state;
    int      call_absent;      /* zero MACH_CALL records at all (C2)   */
    int      no_probe_call;    /* CALLs exist, none msgid=probe (C2)   */
    int      image_unverified; /* either stream's 128-bit UUID unknown */
    int      stats_unknown;    /* seq stats unavailable (alloc fail)   */
    int      scoring_suppressed; /* measurement invalid: any bounded
                               * cap fired, image unverified, or stats
                               * unknown — probes_scored forced to 0  */
    uint64_t suppressed_samples; /* would-be scored samples withheld  */
    int      drops_flagged;    /* nonzero drops inside a probe window  */
    int      reconcile_flag;   /* gaps < dropped — impossible, flagged */
    d169_stream_stats probe_s, ws_s;

    /* bounded-capacity overflow counters (never silently dropped) */
    uint64_t mach_calls;       /* MACH_CALL records seen, probe stream */
    uint64_t pend_overflow;    /* CALLs beyond D169_PENDING_CALLS      */
    uint64_t span_overflow;    /* probe CALLs beyond D169_MAX_PROBES   */
    uint64_t tids_capped;      /* PASS_ENTER tids skipped past cap 64  */

    /* sample classes — every span reported, none silently excluded */
    uint64_t spans_total;
    uint64_t probes_scored;
    uint64_t ambiguous;        /* serialize-one-probe violation events */
    uint64_t gap_voided;
    uint64_t invalid_neg;      /* negative-latency samples             */
    uint64_t open_eof;         /* spans left open at stream end (C5)   */
    uint64_t handler_absent;   /* closed spans with no HANDLER bound   */
    uint64_t orphan_handler;   /* HANDLER(probe) with no open span     */
    uint64_t timeout_ret;      /* non-success RETs (unpaired)          */
    uint64_t replies;          /* ENQUEUE(probe+100) inside spans      */
    uint64_t reply_missing;    /* closed spans lacking ENQUEUE(+100)   */
    uint64_t reply_mismatch;   /* RET.msgid != call.msgid+100 (C7)     */
    uint64_t handler_wrong_tid;
    uint64_t handler_outside_bracket;
    uint64_t handler_dup;      /* second HANDLER(probe) in one span    */
    uint64_t orphan_ret;       /* RET with no pending CALL             */

    uint64_t latency_ns[D169_MAX_PROBES];
    uint64_t median_ns;
    uint64_t cluster_count, cluster_bound_ns;
} d169_report;

/* Decode a probe/WS stream pair. Returns D169_OK on success; streams
 * are checked per G2 (single gen each) before merging. */
d169_err d169_decode(const d169_stream *probe, const d169_stream *ws,
                     const d169_opts *opts, d169_report *out);

/* Canonical stream file: exact size == sizeof(hdr) + n_records*64.
 * Truncation, trailing bytes, count oversize, CAP/version/magic
 * mismatch → distinct d169_err (G1/G8). */
d169_err d169_stream_load(const char *path, d169_stream_mem *out);
void     d169_stream_free(d169_stream_mem *s);

/* Convenience: load both files, decode, free. */
d169_err d169_decode_files(const char *probe_path, const char *ws_path,
                           const d169_opts *opts, d169_report *out);

#endif /* D169_SVCDECODE_H */
