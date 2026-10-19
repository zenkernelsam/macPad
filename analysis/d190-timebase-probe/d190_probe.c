/*
 * D190 — analysis-only one-shot raw 0x74a7 sender core.
 * See d190_probe.h for the contract and the G15 unverified-side-effect
 * assumption. Diagnostic-only; never invoked against real WindowServer
 * on this host or any device.
 */

#include "d190_probe.h"

#include <dlfcn.h>
#include <errno.h>
#include <mach-o/dyld.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* libSystem exports used by MIG stubs; not declared in the public SDK
 * headers — declared verbatim here (D188 names both). */
extern mach_port_t mig_get_special_reply_port(void);
extern void mig_dealloc_special_reply_port(mach_port_t port);

static const uint8_t D190_SKYLIGHT_UUID[16] = D190_SKYLIGHT_UUID_BYTES;

const char *d190_class_str(enum d190_reply_class c) {
    switch (c) {
    case D190_OK:         return "OK";
    case D190_E_KR:       return "KR";
    case D190_E_TRUNC:    return "TRUNC";
    case D190_E_COMPLEX:  return "COMPLEX";
    case D190_E_BAD_ID:   return "BAD_ID";
    case D190_E_BAD_SIZE: return "BAD_SIZE";
    case D190_E_REMOTE:   return "REMOTE";
    case D190_E_RETCODE:  return "RETCODE";
    }
    return "?";
}

int d190_image_lc_uuid_bounded(const struct mach_header_64 *mh,
                               size_t hdr_bytes, uint8_t out[16]) {
    if (!mh || hdr_bytes < sizeof(*mh) || mh->magic != MH_MAGIC_64)
        return -1;
    /* command region bound = min(declared sizeofcmds, actual buffer) —
     * every declared command must fit inside it (F3). */
    size_t limit = sizeof(*mh) + (size_t)mh->sizeofcmds;
    if (limit > hdr_bytes) limit = hdr_bytes;
    size_t off = sizeof(*mh);
    unsigned found = 0;
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (off >= limit || limit - off < sizeof(struct load_command))
            return -1;                      /* command region short    */
        const struct load_command *lc =
            (const struct load_command *)((const uint8_t *)mh + off);
        if (lc->cmdsize < sizeof(struct load_command) ||
            lc->cmdsize > limit - off)
            return -1;                      /* zero/huge/truncated     */
        if (lc->cmd == LC_UUID) {
            if (found) return -1;           /* duplicate → malformed   */
            if (lc->cmdsize < sizeof(struct uuid_command))
                return -1;                  /* uuid payload truncated  */
            memcpy(out, ((const struct uuid_command *)lc)->uuid, 16);
            found = 1;
        }
        off += lc->cmdsize;
    }
    return found ? 0 : -1;
}

int d190_image_lc_uuid(const struct mach_header_64 *mh, uint8_t out[16]) {
    /* live dyld header: the mapped header region carries sizeofcmds;
     * the declared bounds still cap the walk. */
    if (!mh || mh->magic != MH_MAGIC_64) return -1;
    return d190_image_lc_uuid_bounded(mh,
        sizeof(*mh) + (size_t)mh->sizeofcmds, out);
}

int d190_addr_image_uuid(const void *addr, uint8_t out[16]) {
    memset(out, 0, 16);
    Dl_info info;
    if (!addr || !dladdr(addr, &info) || !info.dli_fbase)
        return -1;
    return d190_image_lc_uuid(
        (const struct mach_header_64 *)info.dli_fbase, out);
}

int d190_addr_image_match(const void *addr, const uint8_t want[16]) {
    uint8_t u[16];
    return (d190_addr_image_uuid(addr, u) == 0 &&
            memcmp(u, want, 16) == 0) ? 0 : -1;
}

int d190_check_skylight_uuid(uint8_t out[16]) {
    memset(out, 0, 16);
    uint8_t u[16]; unsigned matches = 0;
    for (uint32_t i = 0; i < _dyld_image_count(); i++) {
        const struct mach_header *h = _dyld_get_image_header(i);
        if (!h || h->magic != MH_MAGIC_64) continue;
        if (d190_image_lc_uuid((const struct mach_header_64 *)h, u) == 0 &&
            memcmp(u, D190_SKYLIGHT_UUID, 16) == 0)
            matches++;          /* count, not first-match: exactly-one */
    }
    if (matches != 1) return -1;
    memcpy(out, D190_SKYLIGHT_UUID, 16);
    return 0;
}

void d190_build_request(d190_req *r, mach_port_t remote,
                        mach_port_t local) {
    memset(r, 0, sizeof(*r));
    r->head.msgh_bits        = D190_MSG_BITS;
    r->head.msgh_size        = D190_SEND_SIZE;
    r->head.msgh_remote_port = remote;
    r->head.msgh_local_port  = local;
    r->head.msgh_voucher_port = MACH_PORT_NULL;
    r->head.msgh_id          = D190_REQ_ID;
}

enum d190_reply_class d190_classify_reply(const d190_msgbuf *b,
                                          kern_return_t kr) {
    if (kr != KERN_SUCCESS) return D190_E_KR;
    if (b->h.msgh_size < sizeof(mach_msg_header_t)) return D190_E_TRUNC;
    if (b->h.msgh_bits & MACH_MSGH_BITS_COMPLEX) return D190_E_COMPLEX;
    if (b->h.msgh_id != D190_REPLY_ID) return D190_E_BAD_ID;
    if (b->h.msgh_size != D190_REPLY_SIZE) return D190_E_BAD_SIZE;
    if (b->h.msgh_remote_port != MACH_PORT_NULL) return D190_E_REMOTE;
    if (b->r.retcode != 0) return D190_E_RETCODE;
    return D190_OK;
}

static uint64_t d190_now_ns(void) {
    /* CLOCK_MONOTONIC — same domain as d141_now_ns. */
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t d190_gen_tag(int32_t pid, const uint8_t uuid[16]) {
    uint64_t lo = 0;
    memcpy(&lo, uuid, 8);
    return ((uint64_t)(uint32_t)pid << 32) ^ lo;
}

void d190_fill_records(d141_record rec[2], uint64_t call_ts,
                       uint64_t ret_ts, uint64_t tid, int32_t pid,
                       uint64_t gen, kern_return_t kr,
                       mach_msg_id_t reply_id) {
    rec[0] = (d141_record){
        .magic = D141_SVC_MAGIC, .seq = 0, .ts_ns = call_ts,
        .tid = tid, .pid = pid, .kind = D141K_MACH_CALL,
        .msgid = D190_REQ_ID, .aux = D190_MSG_OPTION, .gen = gen };
    rec[1] = (d141_record){
        .magic = D141_SVC_MAGIC, .seq = 1, .ts_ns = ret_ts,
        .tid = tid, .pid = pid, .kind = D141K_MACH_RET,
        .msgid = (kr == KERN_SUCCESS) ? reply_id : 0,
        .aux = (uint64_t)kr, .gen = gen };
}

int d190_write_stream(const char *path, const d141_record rec[2],
                      const uint8_t uuid[16], int32_t pid,
                      const char *label) {
    d169_stream_hdr h;
    memset(&h, 0, sizeof(h));
    h.magic       = D169_STREAM_MAGIC;
    h.version     = D169_STREAM_VERSION;
    h.cap         = D141_RING_CAP;
    h.gen         = d190_gen_tag(pid, uuid);
    h.pid         = pid;
    h.flags       = 0;
    h.emitted     = 2;
    h.dropped     = 0;
    h.uncommitted = 0;
    h.n_records   = 2;
    memcpy(h.image_uuid, uuid, 16);   /* producer-declared (hi64)     */
    h.uuid_valid  = 1;
    h.reserved    = 0;
    if (label) {
        strncpy(h.label, label, sizeof(h.label) - 1);
        h.label[sizeof(h.label) - 1] = '\0';
    }
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = fwrite(&h, sizeof(h), 1, f) == 1 &&
             fwrite(rec, sizeof(*rec), 2, f) == 2;
    int saved = errno;
    if (fclose(f) != 0) ok = 0;
    errno = saved;
    return ok ? 0 : -1;
}

enum d190_cleanup d190_cleanup_class(kern_return_t kr,
                                     enum d190_reply_class c) {
    if (kr == KERN_SUCCESS)
        return (c == D190_OK) ? D190_CLEAN_NONE : D190_CLEAN_DESTROY;
    if (kr == MACH_SEND_INVALID_DATA || kr == MACH_SEND_INVALID_DEST ||
        kr == MACH_SEND_INVALID_HEADER)
        return D190_CLEAN_NONE;   /* send-side reject: nothing left   */
    return D190_CLEAN_DEALLOC;
}

#ifdef D190_TEST_HOOKS
int d190_hook_cleanup_none;
int d190_hook_cleanup_destroy;
int d190_hook_cleanup_dealloc;
#endif

enum d190_reply_class d190_send_once(mach_port_t remote, int32_t pid,
                                     uint64_t gen, d141_record rec[2],
                                     uint64_t *payload) {
    /* G2 (D192-corrected): local is the THREAD special reply port —
     * the task holds receive+send rights on it, bound to this thread
     * and TSD-cached across RPCs; it is NOT itself a send-once right.
     * MAKE_SEND_ONCE mints the send-once right into the outgoing
     * message's remote slot for the receiver. */
    mach_port_t rp = mig_get_special_reply_port();
    d190_msgbuf buf;
    d190_build_request((d190_req *)&buf.h, remote, rp);
    uint64_t t0 = d190_now_ns();
    /* G4: option/size/timeout/notify verbatim; exactly ONE mach_msg —
     * no loop, no retry branch (G5). G6: never the SkyLight wrapper
     * (its once-cache would suppress the send). */
    kern_return_t kr = mach_msg(&buf.h, D190_MSG_OPTION, D190_SEND_SIZE,
                                D190_RCV_SIZE, rp, D190_TIMEOUT,
                                D190_NOTIFY);
    uint64_t t1 = d190_now_ns();

    uint64_t tid = 0;
    pthread_threadid_np(NULL, &tid);
    d190_fill_records(rec, t0, t1, tid, pid, gen, kr,
                      (kr == KERN_SUCCESS) ? buf.h.msgh_id : 0);

    enum d190_reply_class c = d190_classify_reply(&buf, kr);
    /* Four exact stub cleanup classes (D192 :495c-49e0) — the SRP is
     * deallocated ONLY on class DEALLOC; send-side rejects and
     * malformed received replies both retain it. */
    switch (d190_cleanup_class(kr, c)) {
    case D190_CLEAN_DESTROY:
#ifdef D190_TEST_HOOKS
        d190_hook_cleanup_destroy++;
#endif
        mach_msg_destroy(&buf.h);             /* malformed reply      */
        break;
    case D190_CLEAN_DEALLOC:
#ifdef D190_TEST_HOOKS
        d190_hook_cleanup_dealloc++;
#endif
        mig_dealloc_special_reply_port(rp);   /* other non-success kr */
        break;
    case D190_CLEAN_NONE:
    default:
#ifdef D190_TEST_HOOKS
        d190_hook_cleanup_none++;
#endif
        break;                                /* valid / send-reject  */
    }
    if (payload)
        *payload = (c == D190_OK) ? buf.r.payload : 0;
    return c;
}
