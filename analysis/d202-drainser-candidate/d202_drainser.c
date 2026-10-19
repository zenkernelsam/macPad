/* D202 — bounded drain→v3 serializer. See d202_drainser.h for the
 * contract; this file contains no production code. */

#include "d202_drainser.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int d202_collect(d202_batch *b, size_t chunk, unsigned budget) {
    /* reject a corrupt batch before any pointer arithmetic */
    if (!b) return D202_ERR;
    if (b->n > D141_RING_CAP) {
        b->status = D202_ERR;
        return D202_ERR;
    }
    unsigned calls = 0;      /* per-invocation budget — drain_calls is
                                * cumulative observability only       */
    int saw_zero = 0;
    while (b->n < D141_RING_CAP && calls < budget) {
        size_t room = D141_RING_CAP - b->n;
        if (chunk && chunk < room) room = chunk;
        calls++; b->drain_calls++;
        ssize_t got = d141_drain(b->rec + b->n, room);
        if (got < 0) {
            b->status = D202_ERR;
            goto out;
        }
        b->n += (size_t)got;
        if (got == 0) { saw_zero = 1; break; }
    }
    {
        uint64_t head = d141_head(), tail = d141_tail();
        if (head == tail)               b->status = D202_COMPLETE;
        else if (b->n == D141_RING_CAP) b->status = D202_INCOMPLETE_CAP;
        else if (saw_zero)              b->status = D202_INCOMPLETE_CLAIM;
        else                            b->status = D202_INCOMPLETE_BUDGET;
    }
out:
    b->emitted     = d141_emitted();
    b->dropped     = d141_dropped();
    b->uncommitted = d141_uncommitted();
    b->head        = d141_head();
    b->tail        = d141_tail();
    return b->status;
}

/* Every record must be verifiably from this producer before the stream
 * is written — never emit a file the decoder must reject. */
static int records_consistent(const d141_record *rec, size_t n,
                              int32_t pid, const uint8_t *uuid16) {
    uint64_t lo = 0;
    if (uuid16) {
        memcpy(&lo, uuid16, 8);   /* uuid_low64 = first 8 bytes (LE) —
                                     * same definition d141 uses        */
    }
    uint64_t expect = ((uint64_t)(uint32_t)pid << 32) ^ lo;
    uint64_t first = n ? rec[0].gen : 0;
    for (size_t i = 0; i < n; i++) {
        if (rec[i].magic != D141_SVC_MAGIC)   return 0;
        if (rec[i].pid != pid)                return 0;
        if (uuid16) { if (rec[i].gen != expect) return 0; }
        else        { if (rec[i].gen != first)  return 0; }
    }
    return 1;
}

int d202_write_stream_file(FILE *f, const d141_record *rec, size_t n,
                           int32_t pid, const uint8_t *uuid16,
                           uint64_t emitted, uint64_t dropped,
                           uint64_t uncommitted, const char *label) {
    if (!f || (n && !rec) || n > D169_MAX_STREAM_RECS) return D202_W_ARG;
    if (!records_consistent(rec, n, pid, uuid16))
        return D202_W_INCONSISTENT;

    d169_stream_hdr h;
    memset(&h, 0, sizeof(h));
    h.magic       = D169_STREAM_MAGIC;
    h.version     = D169_STREAM_VERSION;
    h.cap         = D141_RING_CAP;
    h.pid         = pid;
    h.flags       = 0;
    h.emitted     = emitted;
    h.dropped     = dropped;
    h.uncommitted = uncommitted;
    h.n_records   = n;
    if (uuid16) {
        memcpy(h.image_uuid, uuid16, 16);
        h.uuid_valid = 1;
        uint64_t lo; memcpy(&lo, uuid16, 8);
        h.gen = ((uint64_t)(uint32_t)pid << 32) ^ lo;
    } else {
        h.uuid_valid = 0;
        h.gen = n ? rec[0].gen : 0;
    }
    h.reserved = 0;
    if (label) {
        strncpy(h.label, label, sizeof(h.label) - 1);
        h.label[sizeof(h.label) - 1] = '\0';
    }

    int saved = 0;
    if (fwrite(&h, sizeof(h), 1, f) != 1 ||
        (n && fwrite(rec, sizeof(*rec), n, f) != n) ||
        fflush(f) != 0) {
        saved = errno ? errno : EIO;
        errno = saved;
        return D202_W_IO;
    }
    return D202_W_OK;
}

#ifdef D202_TEST_HOOKS
FILE *(*d202_hook_file)(const char *path) = NULL;
#endif

/* unique same-directory temp via mkstemp; never touches `path` */
static FILE *open_tmp(char *tmppath) {
    int fd = mkstemp(tmppath);
    if (fd < 0) return NULL;
    FILE *f = fdopen(fd, "wb");
    if (!f) {
        int s = errno;
        close(fd);
        remove(tmppath); /* mkstemp succeeded: this temp is ours */
        errno = s;
        return NULL;
    }
    return f;
}

int d202_write_stream(const char *path, const d141_record *rec, size_t n,
                      int32_t pid, const uint8_t *uuid16,
                      uint64_t emitted, uint64_t dropped,
                      uint64_t uncommitted, const char *label) {
    /* every argument/record check BEFORE any filesystem mutation —
     * a pre-existing destination is never opened or truncated */
    if (!path || (n && !rec) || n > D169_MAX_STREAM_RECS)
        return D202_W_ARG;
    if (!records_consistent(rec, n, pid, uuid16))
        return D202_W_INCONSISTENT;

    char tmp[PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s.d202tmpXXXXXX", path)
            >= (int)sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return D202_W_IO;
    }
    FILE *f;
#ifdef D202_TEST_HOOKS
    f = d202_hook_file ? d202_hook_file(tmp) : open_tmp(tmp);
#else
    f = open_tmp(tmp);
#endif
    if (!f) return D202_W_IO; /* no owned file on open failure */

    int rc = d202_write_stream_file(f, rec, n, pid, uuid16,
                                    emitted, dropped, uncommitted, label);
    /* errno at the ACTUAL failing op — captured before the cleanup
     * fclose can clobber it (D203: no pre-fclose stale errno) */
    int saved = (rc != D202_W_OK) ? (errno ? errno : EIO) : 0;
    if (fclose(f) != 0 && rc == D202_W_OK) {
        rc = D202_W_IO;
        saved = errno ? errno : EIO;      /* the close's own errno     */
    }
    if (rc != D202_W_OK) {
        remove(tmp);                      /* only ever the owned temp  */
        errno = saved;
        return rc;
    }
    if (rename(tmp, path) != 0) {
        saved = errno ? errno : EIO;      /* the rename's own errno    */
        remove(tmp);
        errno = saved;
        return D202_W_IO;
    }
    return D202_W_OK;
}

const char *d202_status_str(int st) {
    switch (st) {
    case D202_COMPLETE:           return "COMPLETE";
    case D202_INCOMPLETE_CAP:     return "INCOMPLETE_CAP";
    case D202_INCOMPLETE_CLAIM:   return "INCOMPLETE_CLAIM";
    case D202_INCOMPLETE_BUDGET:  return "INCOMPLETE_BUDGET";
    case D202_ERR:                return "ERR";
    default:                      return "?";
    }
}
