/*
 * D169 svcdecode — two-stream offline decoder per the frozen D170
 * checklist + D172 review repairs. See d169_svcdecode.h for the
 * C1–C7 contract. Pure functions over record buffers + a bounded file
 * reader. Single-threaded; no locks, no device paths.
 *
 * Image identity (D172 F1): gen = (pid<<32) ^ uuid_lo64, so
 *   gen_lo32 = uuid_lo32            (pid contributes zero low bits)
 *   gen_hi32 = pid ^ uuid_hi32  =>  uuid_hi32 = gen_hi32 ^ rec.pid
 * A record's image identity is recoverable per-record as
 *   image = (u64)(gen_hi32 ^ rec.pid) << 32 | gen_lo32
 * Both streams of a legitimate pair share the image UUID; different
 * pids are expected and handled. Every record pid must equal the
 * stream's declared pid (when nonzero); per-record recovered images
 * must be uniform within a stream; both streams must recover the same
 * image. Any failure is fail-closed E_IMAGE_MISMATCH. An empty stream
 * uses its header (gen,pid); gen=0 or pid=0 there yields an
 * unrecoverable image (reported as 0 / image_unverified — never
 * silently "verified").
 *
 * Seq-gap correctness (D172 F2): each stream gets a sorted unique seq
 * index over the WHOLE stream before merge. A span's per-stream
 * footprint is the [min,max] seq interval of that stream's records
 * observed while the span was open; presence is judged against the
 * full index — a record ts-sorted outside the span still disproves a
 * hole.
 */

#include "d169_svcdecode.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { ST_PROBE = 0, ST_WS = 1 };

enum span_state {
    SP_OPEN = 0,        /* CALL(probe) seen, awaiting HANDLER/RET      */
    SP_HANDLER_SEEN,    /* HANDLER_BEGIN(probe) bound                  */
    SP_DONE             /* RET paired — span closed                    */
};

typedef struct {
    int      state;
    uint64_t call_ts, call_seq;
    uint64_t call_tid;
    uint64_t handler_ts;
    int      handler_seen;
    int      replied;
    int      poisoned;   /* serialize-one-probe violation              */
    int      impl_seen;
} span_t;

typedef struct {         /* nearest-unmatched same-tid CALL (probe)    */
    uint64_t tid, msgid, seq;
    int      is_probe;
    int      span;
} pending_call;

/* sorted unique seq index over a whole stream (C3/F2) */
typedef struct {
    uint64_t *v;         /* NULL when !known or n==0                   */
    size_t    n;         /* distinct seqs                            */
    int       known;     /* 0 when allocation failed → stats unknown  */
} seqidx;

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static seqidx seqidx_build(const d169_stream *s) {
    seqidx ix = { .v = NULL, .n = 0, .known = 1 };
    if (!s->n) return ix;                          /* known empty    */
    ix.v = malloc(s->n * sizeof(uint64_t));
    if (!ix.v) { ix.known = 0; return ix; }        /* unknown        */
    size_t m = 0;
    for (size_t i = 0; i < s->n; i++)
        if (s->recs[i].magic == D141_SVC_MAGIC) ix.v[m++] = s->recs[i].seq;
    qsort(ix.v, m, sizeof(uint64_t), cmp_u64);
    size_t d = 0;                                  /* dedupe in place */
    for (size_t i = 0; i < m; i++) {
        if (d && ix.v[i] == ix.v[d - 1]) continue;
        ix.v[d++] = ix.v[i];
    }
    ix.n = d;
    return ix;
}

/* helper pair: first index >= lo, first index > hi */
static size_t idx_lower(const seqidx *ix, uint64_t lo) {
    size_t a = 0, b = ix->n;
    while (a < b) { size_t m = a + (b - a) / 2;
                    ix->v[m] < lo ? (a = m + 1) : (b = m); }
    return a;
}
static size_t idx_upper(const seqidx *ix, uint64_t hi) {
    size_t a = 0, b = ix->n;
    while (a < b) { size_t m = a + (b - a) / 2;
                    ix->v[m] <= hi ? (a = m + 1) : (b = m); }
    return a;
}
/* missing seqs in [lo,hi] against the full stream index; -1 unknown */
static long seqidx_missing(const seqidx *ix, uint64_t lo, uint64_t hi) {
    if (hi < lo) return 0;
    if (!ix->known) return -1;
    if (hi - lo == UINT64_MAX) return 1;   /* wraps → certainly gaps */
    uint64_t range = hi - lo + 1;
    uint64_t present = idx_upper(ix, hi) - idx_lower(ix, lo);
    return (long)(range - present);
}

typedef struct { const d141_record *r; int stream; size_t idx; } mev;
static int cmp_mev(const void *a, const void *b) {
    const mev *x = a, *y = b;             /* stable: ts, stream, index */
    if (x->r->ts_ns != y->r->ts_ns)
        return x->r->ts_ns < y->r->ts_ns ? -1 : 1;
    if (x->stream != y->stream) return x->stream - y->stream;
    return x->idx < y->idx ? -1 : x->idx > y->idx;
}

/* recovered image low64 of one record — COMPATIBILITY CHECK ONLY.
 * Identity is the authoritative 128-bit image_uuid; low64 equality is
 * a consistency test between the record's gen/pid and the stream's
 * declared UUID bytes, never a substitute for it (D175). */
static uint64_t rec_image_lo64(const d141_record *r) {
    uint32_t hi = (uint32_t)(r->gen >> 32) ^ (uint32_t)r->pid;
    return ((uint64_t)hi << 32) | (uint32_t)r->gen;
}
static uint64_t uuid_lo64(const uint8_t uuid[16]) {
    uint64_t v; memcpy(&v, uuid, 8); return v;
}

/* ---- G2 + D175: uniform gen/pid + low64-vs-UUID consistency -------- */
static d169_err stream_identity(const d169_stream *s, uint8_t uuid_out[16],
                                int *valid_out) {
    uint64_t g0 = 0, img = 0; int seen = 0;
    int has_uuid = s->uuid_valid != 0;
    uint64_t ulo = has_uuid ? uuid_lo64(s->image_uuid) : 0;
    for (size_t i = 0; i < s->n; i++) {
        const d141_record *r = &s->recs[i];
        if (r->magic != D141_SVC_MAGIC) continue;
        if (s->pid && r->pid != s->pid)
            return D169_E_IMAGE_MISMATCH;  /* rec pid != header pid  */
        if (!seen) {
            g0 = r->gen; img = rec_image_lo64(r); seen = 1;
            continue;
        }
        if (r->gen != g0)
            return D169_E_MULTIGEN;        /* second gen → reject    */
        if (rec_image_lo64(r) != img)
            return D169_E_IMAGE_MISMATCH;  /* pid/gen inconsistency  */
    }
    if (s->gen && seen && g0 != s->gen)
        return D169_E_MULTIGEN;            /* header disagrees       */
    /* record-generation low64 must recover the stream's declared UUID
     * bytes — UUID is authoritative, gen must agree (uuid/gen check) */
    if (seen && has_uuid && img != ulo)
        return D169_E_IMAGE_MISMATCH;
    memcpy(uuid_out, s->image_uuid, 16);
    *valid_out = has_uuid;                 /* empty stream → header   */
    return D169_OK;
}

/* ---- G3: services tid by argmax PASS_ENTER density; tie/0 → flag --- */
static int resolve_svc_tid(const d169_stream *ws, uint64_t gen,
                           uint64_t *tid_out, uint64_t *capped) {
    uint64_t tids[64], dens[64]; size_t nt = 0;
    memset(dens, 0, sizeof(dens));
    for (size_t i = 0; i < ws->n; i++) {
        const d141_record *r = &ws->recs[i];
        if (r->magic != D141_SVC_MAGIC) continue;
        if (gen && r->gen != gen) continue;
        if (r->kind != D141K_PASS_ENTER) continue;
        size_t j;
        for (j = 0; j < nt; j++) if (tids[j] == r->tid) break;
        if (j == nt) {
            if (nt == 64) { (*capped)++; continue; }   /* bounded, counted */
            tids[nt] = r->tid; dens[nt] = 0; nt++;
        }
        dens[j]++;
    }
    int tied = 0;
    uint64_t maxd = 0; size_t maxj = 0; int have = 0;
    for (size_t j = 0; j < nt; j++) {
        if (!have || dens[j] > maxd) { maxd = dens[j]; maxj = j; have = 1; }
    }
    if (!have || maxd == 0) { *tid_out = 0; return D169_TID_INSUFFICIENT; }
    for (size_t j = 0; j < nt; j++)
        if (j != maxj && dens[j] == maxd) tied = 1;
    *tid_out = tids[maxj];
    return tied ? D169_TID_AMBIGUOUS_TIE : D169_TID_OK;
}

const char *d169_err_str(d169_err e) {
    static const char *names[] = {
        "OK","ARG","OPEN","HDR_TRUNC","MAGIC","VERSION","CAP",
        "COUNT_BOUND","TRUNCATED","TRAILING","READ","MULTIGEN",
        "IMAGE_MISMATCH","ALLOC","RESERVED","UUID_FLAGS" };
    return (e >= 0 && e <= D169_E_UUID_FLAGS) ? names[e] : "?";
}

d169_err d169_decode(const d169_stream *probe, const d169_stream *ws,
                     const d169_opts *opts, d169_report *out) {
    if (!probe || !ws || !opts || !out || opts->probe_msgid == 0)
        return D169_E_ARG;
    /* in-memory views hold the same validity domain as file headers */
    if (probe->uuid_valid & ~1 || ws->uuid_valid & ~1)
        return D169_E_UUID_FLAGS;
    memset(out, 0, sizeof(*out));
    const uint64_t PM = opts->probe_msgid;
    if (probe->n > D169_MAX_STREAM_RECS || ws->n > D169_MAX_STREAM_RECS)
        return D169_E_COUNT_BOUND;

    /* G2/D175 fail-closed: per-stream gen+pid+UUID consistency first.
     * low64 recovery is only a consistency check; identity requires
     * the authoritative 128-bit UUID on BOTH streams. */
    d169_err e;
    if ((e = stream_identity(probe, out->probe_s.image_uuid,
                             &out->probe_s.uuid_valid)) != D169_OK)
        return e;
    if ((e = stream_identity(ws, out->ws_s.image_uuid,
                             &out->ws_s.uuid_valid)) != D169_OK)
        return e;
    if (out->probe_s.uuid_valid && out->ws_s.uuid_valid &&
        memcmp(out->probe_s.image_uuid, out->ws_s.image_uuid, 16) != 0)
        return D169_E_IMAGE_MISMATCH;      /* cross-stream UUIDs differ */
    if (!out->probe_s.uuid_valid || !out->ws_s.uuid_valid)
        out->image_unverified = 1;
    if (opts->image_uuid_set &&
        (!out->probe_s.uuid_valid || !out->ws_s.uuid_valid ||
         memcmp(out->probe_s.image_uuid, opts->image_uuid, 16) != 0 ||
         memcmp(out->ws_s.image_uuid, opts->image_uuid, 16) != 0))
        return D169_E_IMAGE_MISMATCH;      /* full-UUID filter unmet    */

    /* full-stream sorted seq indexes (C3 pre-merge, F2 fix) */
    seqidx ip = seqidx_build(probe), iw = seqidx_build(ws);
    if (!ip.known || !iw.known) out->stats_unknown = 1;

    /* per-stream stats verbatim */
    out->probe_s.records = probe->n; out->probe_s.emitted = probe->emitted;
    out->probe_s.dropped = probe->dropped;
    out->probe_s.uncommitted = probe->uncommitted;
    out->probe_s.pid = probe->pid;
    out->probe_s.gen = probe->gen ? probe->gen :
        (probe->n ? probe->recs[0].gen : 0);
    out->ws_s.records = ws->n; out->ws_s.emitted = ws->emitted;
    out->ws_s.dropped = ws->dropped;
    out->ws_s.uncommitted = ws->uncommitted;
    out->ws_s.pid = ws->pid;
    out->ws_s.gen = ws->gen ? ws->gen : (ws->n ? ws->recs[0].gen : 0);
    if (ip.known)
        for (size_t i = 1; i < ip.n; i++)
            out->probe_s.gaps += ip.v[i] - ip.v[i-1] - 1;
    if (iw.known)
        for (size_t i = 1; i < iw.n; i++)
            out->ws_s.gaps += iw.v[i] - iw.v[i-1] - 1;
    /* dups = records_with_magic - distinct */
    if (ip.known) { size_t m = 0;
        for (size_t i = 0; i < probe->n; i++)
            if (probe->recs[i].magic == D141_SVC_MAGIC) m++;
        out->probe_s.dups = m - ip.n; }
    if (iw.known) { size_t m = 0;
        for (size_t i = 0; i < ws->n; i++)
            if (ws->recs[i].magic == D141_SVC_MAGIC) m++;
        out->ws_s.dups = m - iw.n; }

    /* G9 reconcile only meaningful when stats are known (F12) */
    if (!out->stats_unknown &&
        (out->probe_s.gaps < out->probe_s.dropped ||
         out->ws_s.gaps < out->ws_s.dropped))
        out->reconcile_flag = 1;
    if (probe->dropped || ws->dropped)
        out->drops_flagged = 1;

    /* G3 services-tid resolution on the WS stream */
    out->tid_state = resolve_svc_tid(ws, out->ws_s.gen, &out->svc_tid,
                                     &out->tids_capped);

    /* C4 merge by ts; stable tie-break: stream id (probe first), idx  */
    size_t nm = probe->n + ws->n;
    mev *mer = malloc(nm * sizeof(mev));
    span_t *spans = calloc(D169_MAX_PROBES, sizeof(span_t));
    if (!mer || !spans) { free(mer); free(spans);
                          free(ip.v); free(iw.v); return D169_E_ALLOC; }
    size_t k = 0;
    for (size_t i = 0; i < probe->n; i++)
        if (probe->recs[i].magic == D141_SVC_MAGIC)
            mer[k++] = (mev){ &probe->recs[i], ST_PROBE, i };
    for (size_t i = 0; i < ws->n; i++)
        if (ws->recs[i].magic == D141_SVC_MAGIC)
            mer[k++] = (mev){ &ws->recs[i], ST_WS, i };
    qsort(mer, k, sizeof(mev), cmp_mev);

    /* per-stream footprints of the currently open span: min/max seq
     * only — presence is judged against the full index at close (F2) */
    uint64_t fp_min[2] = {0,0}, fp_max[2] = {0,0};
    int      fp_have[2] = {0,0};
    pending_call pend[D169_PENDING_CALLS]; int np = 0;
    int nsp = 0, open_span = -1, bracket = 0;

    for (size_t i = 0; i < k; i++) {
        const d141_record *r = mer[i].r;
        int st = mer[i].stream;

        /* footprint update: every record while a span is open extends
         * its own stream's footprint interval */
        if (open_span >= 0 && spans[open_span].state != SP_DONE) {
            if (!fp_have[st] || r->seq < fp_min[st]) fp_min[st] = r->seq;
            if (!fp_have[st] || r->seq > fp_max[st]) fp_max[st] = r->seq;
            fp_have[st] = 1;
        }

        if (st == ST_PROBE) {
            if (r->kind == D141K_MACH_CALL) {
                out->mach_calls++;
                /* span bookkeeping is independent of the pend cap —
                 * a full pending table must not starve span accounting
                 * (D177: otherwise span_overflow can never fire) */
                int spn = -1;
                if (r->msgid == PM) {
                    int violated = 0;
                    if (open_span >= 0 &&
                        spans[open_span].state != SP_DONE) {
                        spans[open_span].poisoned = 1;
                        violated = 1;
                        out->ambiguous++;            /* G5 */
                    }
                    if (nsp < (int)D169_MAX_PROBES) {
                        spn = nsp;
                        spans[nsp] = (span_t){ .state = SP_OPEN,
                            .call_ts = r->ts_ns, .call_seq = r->seq,
                            .call_tid = r->tid,
                            .poisoned = violated };
                        open_span = nsp++;
                        out->spans_total++;
                        /* fresh footprints for the new span (C3) */
                        fp_have[0] = fp_have[1] = 0;
                        fp_min[0] = fp_max[0] = r->seq;
                        fp_have[0] = 1;
                    } else {
                        out->span_overflow++;        /* bounded, counted */
                    }
                }
                if ((unsigned)np < D169_PENDING_CALLS)
                    pend[np++] = (pending_call){ r->tid, r->msgid,
                                                 r->seq,
                                                 r->msgid == PM, spn };
                else
                    out->pend_overflow++;            /* bounded, counted */
            } else if (r->kind == D141K_MACH_RET) {
                if (r->aux != D169_MACH_MSG_SUCCESS) {
                    out->timeout_ret++;              /* G7: no consume */
                } else {
                    /* nearest unmatched same-tid CALL on probe stream */
                    ssize_t pi = -1;
                    for (int j = np - 1; j >= 0; j--)
                        if (pend[j].tid == r->tid) { pi = j; break; }
                    if (pi < 0) {
                        out->orphan_ret++;
                    } else {
                        pending_call pc = pend[pi];
                        memmove(&pend[pi], &pend[pi+1],
                                (np - pi - 1) * sizeof(pend[0]));
                        np--;
                        if (pc.is_probe && pc.span >= 0 &&
                            spans[pc.span].state != SP_DONE) {
                            span_t *sp = &spans[pc.span];
                            sp->state = SP_DONE;
                            /* C7 cross-check: reply id should be +100 */
                            if (r->msgid != pc.msgid + D169_REPLY_DELTA)
                                out->reply_mismatch++;
                            /* scoring/gap evaluation applies only to
                             * the span that was open at close; older
                             * poisoned spans close without scoring */
                            if (pc.span == open_span) {
                                /* C3/G6 + F2: footprint intervals vs
                                 * the WHOLE stream index */
                                long mp = fp_have[ST_PROBE]
                                    ? seqidx_missing(&ip, fp_min[ST_PROBE],
                                                     fp_max[ST_PROBE]) : 0;
                                long mw = fp_have[ST_WS]
                                    ? seqidx_missing(&iw, fp_min[ST_WS],
                                                     fp_max[ST_WS]) : 0;
                                int gap = (mp != 0) || (mw != 0);
                                if (!sp->handler_seen) {
                                    out->handler_absent++;
                                } else if (gap) {
                                    out->gap_voided++;
                                } else if (!sp->poisoned &&
                                           out->probes_scored <
                                               D169_MAX_PROBES) {
                                    int64_t l = (int64_t)sp->handler_ts -
                                                (int64_t)sp->call_ts;
                                    if (l < 0)
                                        out->invalid_neg++;
                                    else
                                        out->latency_ns[
                                            out->probes_scored++]
                                            = (uint64_t)l;
                                }
                                if (!sp->replied && sp->handler_seen)
                                    out->reply_missing++;
                            }
                        }
                    }
                }
            }
        } else {
            /* WS stream — service edges on the selected services tid */
            switch (r->kind) {
            case D141K_PASS_ENTER:
                if (r->tid == out->svc_tid) bracket++;
                break;
            case D141K_PASS_EXIT:
                if (r->tid == out->svc_tid && bracket > 0) bracket--;
                break;
            case D141K_HANDLER_BEGIN:
                if (r->tid != out->svc_tid) {
                    if (r->msgid == PM) out->handler_wrong_tid++;
                    break;
                }
                if (bracket <= 0) out->handler_outside_bracket++;
                if (r->msgid == PM) {
                    if (open_span >= 0 &&
                        spans[open_span].state == SP_OPEN &&
                        !spans[open_span].poisoned) {
                        span_t *sp = &spans[open_span];
                        sp->handler_seen = 1;
                        sp->handler_ts = r->ts_ns;
                        sp->state = SP_HANDLER_SEEN;
                    } else if (open_span >= 0 &&
                               spans[open_span].state == SP_HANDLER_SEEN
                               && !spans[open_span].poisoned) {
                        out->handler_dup++;      /* second HANDLER(probe) */
                    } else {
                        out->orphan_handler++;   /* no open span (C5-adj)*/
                    }
                }
                break;
            case D141K_IMPL_INVOKE:
                if (r->tid != out->svc_tid) break;
                if (r->msgid == PM && open_span >= 0 &&
                    spans[open_span].state == SP_HANDLER_SEEN)
                    spans[open_span].impl_seen = 1;
                break;
            case D141K_REPLY_ENQUEUE:
                if (r->tid != out->svc_tid) break;
                if (r->msgid == PM + D169_REPLY_DELTA &&
                    open_span >= 0 &&
                    spans[open_span].state == SP_HANDLER_SEEN &&
                    !spans[open_span].replied) {
                    spans[open_span].replied = 1;
                    out->replies++;
                }
                break;
            default:
                break;
            }
        }
    }

    /* C5: spans never closed are reported, never auto-closed */
    for (int i = 0; i < nsp; i++)
        if (spans[i].state != SP_DONE) out->open_eof++;
    /* C2 split (F3): no CALLs at all vs CALLs with wrong ids */
    if (out->spans_total == 0) {
        if (out->mach_calls == 0) out->call_absent = 1;
        else                      out->no_probe_call = 1;
    }

    /* D175: ANY bounded-capacity truncation or unverifiable identity
     * makes the measurement invalid — counters stay honest, scoring
     * is suppressed. Never score from a truncated argmax/cap. */
    if (out->image_unverified || out->stats_unknown ||
        out->tids_capped || out->pend_overflow || out->span_overflow) {
        out->scoring_suppressed = 1;
        out->suppressed_samples = out->probes_scored;
        out->probes_scored = 0;
    }

    /* G10 statistics over scored samples only — bounded stack scratch
     * (D169_MAX_PROBES entries) so the median path cannot fail-closed
     * half-initialized */
    if (out->probes_scored) {
        uint64_t sbuf[D169_MAX_PROBES];
        memcpy(sbuf, out->latency_ns,
               out->probes_scored * sizeof(uint64_t));
        qsort(sbuf, out->probes_scored, sizeof(uint64_t), cmp_u64);
        out->median_ns = sbuf[out->probes_scored / 2];
        uint64_t bound = opts->wait_ns / 2;
        if (bound < D169_CLUSTER_FLOOR_NS) bound = D169_CLUSTER_FLOOR_NS;
        out->cluster_bound_ns = bound;
        for (uint64_t i = 0; i < out->probes_scored; i++)
            if (out->latency_ns[i] >= bound) out->cluster_count++;
    }
    free(spans); free(mer); free(ip.v); free(iw.v);
    return D169_OK;
}

/* ---- canonical stream file (G1/G8): exact-size validation ---------- */
d169_err d169_stream_load(const char *path, d169_stream_mem *out) {
    if (!path || !out) return D169_E_ARG;
    memset(out, 0, sizeof(*out));
    int fd = open(path, O_RDONLY);
    if (fd < 0) return D169_E_OPEN;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return D169_E_OPEN; }
    if ((uint64_t)st.st_size < sizeof(d169_stream_hdr)) {
        close(fd); return D169_E_HDR_TRUNC;
    }
    d169_stream_hdr h;
    ssize_t hr = read(fd, &h, sizeof(h));
    if (hr != (ssize_t)sizeof(h)) { close(fd); return D169_E_HDR_TRUNC; }
    if (h.magic != D169_STREAM_MAGIC) { close(fd); return D169_E_MAGIC; }
    if (h.version != D169_STREAM_VERSION) { close(fd); return D169_E_VERSION; }
    if (h.cap != D141_RING_CAP) { close(fd); return D169_E_CAP; }
    if (h.reserved != 0) { close(fd); return D169_E_RESERVED; }
    if (h.uuid_valid != 0 && h.uuid_valid != 1) {
        close(fd); return D169_E_UUID_FLAGS;   /* never normalize       */
    }
    if (h.n_records > D169_MAX_STREAM_RECS) { close(fd); return D169_E_COUNT_BOUND; }
    /* exact boundary: file size must be hdr + n*64 — no trailer marker;
     * short = truncated, long = trailing bytes (both malformed, G1) */
    uint64_t want = sizeof(h) + h.n_records * sizeof(d141_record);
    if ((uint64_t)st.st_size < want) { close(fd); return D169_E_TRUNCATED; }
    if ((uint64_t)st.st_size > want) { close(fd); return D169_E_TRAILING; }
    if (h.n_records) {
        out->owned = malloc(h.n_records * sizeof(d141_record));
        if (!out->owned) { close(fd); return D169_E_READ; }
        size_t got = 0, w = h.n_records * sizeof(d141_record);
        while (got < w) {
            ssize_t rr = read(fd, (char *)out->owned + got, w - got);
            if (rr <= 0) { free(out->owned); out->owned = NULL;
                           close(fd); return D169_E_READ; }
            got += (size_t)rr;
        }
    }
    close(fd);
    out->s = (d169_stream){ .recs = out->owned, .n = h.n_records,
        .gen = h.gen, .pid = h.pid, .emitted = h.emitted,
        .dropped = h.dropped, .uncommitted = h.uncommitted,
        .uuid_valid = h.uuid_valid != 0 };
    memcpy(out->s.image_uuid, h.image_uuid, 16);
    memcpy(out->s.label, h.label, sizeof(out->s.label));
    return D169_OK;
}

void d169_stream_free(d169_stream_mem *s) {
    if (s) { free(s->owned); s->owned = NULL; s->s.recs = NULL; s->s.n = 0; }
}

d169_err d169_decode_files(const char *pp, const char *wp,
                           const d169_opts *o, d169_report *out) {
    d169_stream_mem ps, ws;
    d169_err e = d169_stream_load(pp, &ps);
    if (e != D169_OK) return e;
    e = d169_stream_load(wp, &ws);
    if (e != D169_OK) { d169_stream_free(&ps); return e; }
    e = d169_decode(&ps.s, &ws.s, o, out);
    d169_stream_free(&ps); d169_stream_free(&ws);
    return e;
}
