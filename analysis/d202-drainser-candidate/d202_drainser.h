/* D202 — bounded offline drain→v3 serializer harness.
 *
 * Finite, host-only: collects committed records out of the D141 ring
 * into ONE bounded CAP buffer via the leased single consumer, then
 * serializes an exact d169 v3 stream file (sizeof(hdr)+n*64, no end
 * marker). This is NOT a production consumer, transport, or WS
 * observer — it authorizes no hook/permit/device/product integration
 * and measures no waiting.
 *
 * Boundaries honored (from accepted candidates):
 *  - single-consumer lease: drain from exactly one thread, ever (D5)
 *  - never waits for a stalled claim: one zero-return ends the loop
 *  - INCOMPLETE states are explicit; a partial batch is never labeled
 *    complete — the caller takes another batch
 *  - metadata: uuid via d141_get_uuid (-1 → uuid_valid=0), pid/gen
 *    caller-supplied (the producer's peer — no pid accessor exists);
 *    every record is consistency-checked before writing
 *  - emitted/dropped/uncommitted are verbatim post-drain snapshots,
 *    never synthesized
 */
#ifndef D202_DRAINSER_H
#define D202_DRAINSER_H

#include "../d141-svctime-candidate/d141_svctime.h"
#include "../d169-svcdecode-candidate/d169_svcdecode.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* One bounded collection batch — fixed CAP buffer, append-only across
 * d202_collect calls. Caller decides when another batch starts; the
 * harness never drains-and-discards once the buffer is full. */
typedef struct {
    d141_record rec[D141_RING_CAP];
    size_t      n;             /* records collected this batch        */
    unsigned    drain_calls;   /* d141_drain invocations (bounded)    */
    /* verbatim post-drain snapshots — never derived, only copied    */
    uint64_t    emitted, dropped, uncommitted, head, tail;
    int         status;        /* enum d202_status                    */
} d202_batch;

enum d202_status {
    D202_COMPLETE = 0,      /* head == tail after drain: all committed
                             * records collected                       */
    D202_INCOMPLETE_CAP,    /* buffer full AND head != tail: more
                             * pending — caller must take next batch   */
    D202_INCOMPLETE_CLAIM,  /* drain stopped on a claimed-but-
                             * uncommitted slot (stalled producer) —
                             * head != tail, never waited             */
    D202_INCOMPLETE_BUDGET, /* drain-call budget expired, head != tail */
    D202_ERR = -1,          /* d141_drain refused (-1 lease) or bad arg */
};

/* Collect committed records into b (continues appending from b->n).
 * `budget` bounds drain calls PER INVOCATION (b->drain_calls stays a
 * cumulative observability counter); each drain call is bounded
 * (max = min(chunk, CAP - b->n); chunk==0 → remaining capacity).
 * b->n > D141_RING_CAP is rejected before any pointer arithmetic.
 * Post-state is classified on the live head/tail:
 *   head==tail            → D202_COMPLETE
 *   n==CAP && head!=tail  → D202_INCOMPLETE_CAP
 *   drain stopped short   → D202_INCOMPLETE_CLAIM  (no waiting)
 *   budget exhausted      → D202_INCOMPLETE_BUDGET
 * Returns b->status; D202_ERR on lease refusal / NULL / corrupt batch.
 *
 * COMPLETE is a quiescent-snapshot verdict: it proves head==tail at
 * observation time under a quiesced test producer — it is NOT a
 * concurrent-producer shutdown guarantee, and no v3 header flag claims
 * completeness (flags stay 0; status lives only in the batch).
 * A partial batch may be resumed by calling again — each call
 * re-evaluates head/tail, never a silent no-op. */
int d202_collect(d202_batch *b, size_t chunk, unsigned budget);

/* Serialize an exact v3 stream file: sizeof(hdr) + n*64 bytes.
 * ALL validation happens before any filesystem mutation; a
 * pre-existing destination is never opened or truncated. The payload
 * goes to a unique same-directory temp file (mkstemp), is
 * write/flush/close-checked, and lands on the destination by atomic
 * rename() — failures unlink only the owned temp, so an invalid
 * argument, inconsistent record, or write/close error can never
 * destroy or leave behind a false artifact at `path`.
 * uuid/uuid_valid: caller resolves d141_get_uuid ONCE — rc==0 →
 * uuid_valid=1 + uuid bytes; rc<0 → uuid_valid=0 + zeroed uuid.
 * Every record is consistency-checked BEFORE writing:
 *   magic == D141_SVC_MAGIC, rec.pid == pid, and when uuid is valid
 *   rec.gen == ((uint64_t)pid<<32) ^ uuid_lo64; with uuid invalid the
 *   records must at least share one uniform gen + pid.
 * Snapshots are verbatim (emitted/dropped/uncommitted from the batch).
 * errno is captured at the actual failing operation (write, flush,
 * close, rename) and preserved to the caller. */
enum d202_write_rc {
    D202_W_OK = 0,
    D202_W_ARG,          /* NULL/oversize args                        */
    D202_W_INCONSISTENT, /* a record fails the pid/gen/magic check     */
    D202_W_IO,           /* temp open/write/flush/close/rename error   */
};
int d202_write_stream(const char *path, const d141_record *rec, size_t n,
                      int32_t pid, const uint8_t *uuid16 /*NULL→invalid*/,
                      uint64_t emitted, uint64_t dropped,
                      uint64_t uncommitted, const char *label);
/* FILE* variant — write+flush only (caller owns the FILE*: no
 * close, no rename). Deterministic write-failure injection for tests
 * via fmemopen/funopen — no /dev/full assumption. */
int d202_write_stream_file(FILE *f, const d141_record *rec, size_t n,
                           int32_t pid, const uint8_t *uuid16,
                           uint64_t emitted, uint64_t dropped,
                           uint64_t uncommitted, const char *label);

const char *d202_status_str(int st);

#ifdef D202_TEST_HOOKS
/* test-only: replace the temp-file open step (a funopen cookie whose
 * write/close fails deterministically proves error provenance). */
extern FILE *(*d202_hook_file)(const char *path);
#endif

#endif /* D202_DRAINSER_H */
