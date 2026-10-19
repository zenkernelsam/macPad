/*
 * D190 — analysis-only one-shot raw _XGetWindowServerTimebase (0x74a7)
 * diagnostic sender. NEVER linked, installed, or invoked in production;
 * host test coverage only (loopback ports, no WindowServer).
 *
 * Wire shape is verbatim from the D188 exact 22F82 disassembly of
 * _SLSGetWindowServerTimebase @ 0x185364890 (SkyLight UUID
 * 96676A53-B1E0-3D7E-B98B-B73873CD1880), gated by the frozen D189
 * contract G1–G17.
 *
 * UNVERIFIED ASSUMPTION (G15, verbatim): server-side side effects of
 * _XGetWindowServerTimebase are UNVERIFIED — the WindowServer handler
 * binary is not in-tree; "read-only" is inferred from the request/reply
 * shape, not from handler disassembly. This tool is a labelled
 * diagnostic, not a fix.
 */

#ifndef D190_PROBE_H
#define D190_PROBE_H

#include <mach/mach.h>
#include <mach-o/loader.h>
#include <stdint.h>

#include "../d141-svctime-candidate/d141_svctime.h"   /* d141_record */
#include "../d169-svcdecode-candidate/d169_svcdecode.h" /* v3 stream hdr */

/* ---- wire constants (D188 RE-confirmed) --------------------------- */
#define D190_REQ_ID       0x74a7u    /* _XGetWindowServerTimebase        */
#define D190_REPLY_ID     0x750bu    /* 0x74a7 + 100 (MIG convention)    */
#define D190_MSG_BITS     0x1513u    /* remote COPY_SEND, local
                                      MAKE_SEND_ONCE (msgh_bits verbatim)*/
/* option = SEND|RCV|SYNC_OVERRIDE|PROPAGATE_QOS|SYNC_WAIT — verbatim.
 * = MACH_SEND_MSG|MACH_RCV_MSG|MACH_SEND_SYNC_OVERRIDE|
 *   MACH_SEND_PROPAGATE_QOS|MACH_RCV_SYNC_WAIT (xnu message.h:904-932) */
#define D190_MSG_OPTION   0x00304003u
#define D190_SEND_SIZE    0x18u      /* header-only request              */
#define D190_RCV_SIZE     0x34u      /* receive buffer bound             */
#define D190_REPLY_SIZE   0x2cu      /* hdr24 + NDR8 + retcode4 + u64    */
#define D190_TIMEOUT      0u         /* no timeout bits — verbatim       */
#define D190_NOTIFY       MACH_PORT_NULL

#define D190_SKYLIGHT_UUID_BYTES {0x96,0x67,0x6a,0x53,0xb1,0xe0,0x3d,0x7e,\
                                  0xb9,0x8b,0xb7,0x38,0x73,0xcd,0x18,0x80}

/* request: 24-byte header only, no body (D188 :4904-4938) */
typedef struct { mach_msg_header_t head; } d190_req;

/* reply: 0x2c — header + NDR + retcode + u64 payload @+0x24.
 * packed: payload sits at byte 36 (4-aligned, not 8) — no padding. */
typedef struct __attribute__((packed)) {
    mach_msg_header_t head;      /* 24 */
    NDR_record_t      ndr;       /* 8  */
    int               retcode;   /* 4  */
    uint64_t          payload;   /* 8  — timebase value @+0x24        */
} d190_reply;

typedef union {
    d190_reply        r;
    mach_msg_header_t h;
    uint8_t           b[D190_RCV_SIZE];
} d190_msgbuf;

/* reply classification — mirrors the MIG epilogue order (:4988-49e0) */
enum d190_reply_class {
    D190_OK = 0,
    D190_E_KR,        /* mach_msg kr != KERN_SUCCESS                  */
    D190_E_TRUNC,     /* msgh_size < mach_msg_header_t                */
    D190_E_COMPLEX,   /* bit31 set — descriptor rights present        */
    D190_E_BAD_ID,    /* msgh_id != 0x750b                            */
    D190_E_BAD_SIZE,  /* msgh_size != 0x2c                            */
    D190_E_REMOTE,    /* msgh_remote_port != 0                        */
    D190_E_RETCODE,   /* MIG retcode != 0                             */
};

/* SRP cleanup classes — the EXACT four stub classes (D192, 22F82 stub
 * epilogue :495c-49e0), replacing D189's wrong two-class split:
 *   NONE    valid reply (send-once consumed; SRP stays TSD-cached) OR
 *           send-side reject kr ∈ {MACH_SEND_INVALID_DATA(0x10000002),
 *           MACH_SEND_INVALID_DEST(0x10000003),
 *           MACH_SEND_INVALID_HEADER(0x10000010)} — the message never
 *           left; SRP retained, no destroy.
 *   DESTROY SUCCESS but any validation failed → mach_msg_destroy on
 *           the received reply (covers header-carried remote right +
 *           descriptors); SRP retained.
 *   DEALLOC any other non-success kr (RCV_TIMED_OUT, TOO_LARGE,
 *           INTERRUPTED, PORT_DIED, out-of-range) →
 *           mig_dealloc_special_reply_port destructs ALL rights on the
 *           thread SRP and clears the TSD cache.
 */
enum d190_cleanup {
    D190_CLEAN_NONE = 0,
    D190_CLEAN_DESTROY,
    D190_CLEAN_DEALLOC,
};

/* cleanup decision — pure; send_once executes exactly this mapping. */
enum d190_cleanup d190_cleanup_class(kern_return_t kr,
                                     enum d190_reply_class c);

const char *d190_class_str(enum d190_reply_class c);

/* ---- pure / testable pieces ----------------------------------------*/

/* extract LC_UUID bytes from a 64-bit Mach-O's load commands.
 * BOUNDED (D195/F3): hdr_bytes is the actual buffer size for synthetic
 * inputs; the walk is additionally capped at sizeof(mh)+mh->sizeofcmds.
 * Rejects: cmdsize < sizeof(load_command), cmdsize advancing past the
 * bound, LC_UUID with cmdsize < sizeof(uuid_command), duplicate LC_UUID,
 * ncmds exceeding the declared region, hdr_bytes < sizeof(*mh).
 * returns 0 on exactly one uuid_command found, -1 otherwise. */
int  d190_image_lc_uuid_bounded(const struct mach_header_64 *mh,
                                size_t hdr_bytes, uint8_t out[16]);
/* live-image variant: bound = sizeof(mh)+mh->sizeofcmds (the dyld-mapped
 * header region) — the declared command bounds still cap the walk. */
int  d190_image_lc_uuid(const struct mach_header_64 *mh, uint8_t out[16]);

/* LC_UUID of the image OWNING an address (dladdr → dli_fbase walk).
 * -1 + zeroed out on failure. F2/D195: binds a resolved symbol to its
 * actual owning image, not just to some loaded image. */
int  d190_addr_image_uuid(const void *addr, uint8_t out[16]);
/* 0 iff addr's owning-image LC_UUID == want (exact 16 bytes). */
int  d190_addr_image_match(const void *addr, const uint8_t want[16]);

/* dyld image walk: 0 + out=uuid iff EXACTLY ONE loaded image carries
 * LC_UUID == D190_SKYLIGHT_UUID; -1 otherwise (out zeroed). Fail-closed
 * per G1 — no flag/env bypass exists. */
int  d190_check_skylight_uuid(uint8_t out[16]);

/* fill the verbatim 24-byte request header (G3). */
void d190_build_request(d190_req *r, mach_port_t remote,
                        mach_port_t local);

/* classify a received buffer. kr is the mach_msg return (G7 verbatim).
 * Order: kr → truncation → complex → id → size → remote → retcode. */
enum d190_reply_class d190_classify_reply(const d190_msgbuf *b,
                                        kern_return_t kr);

/* fill the two observed legs per the accepted D150b contract (G12/G13):
 * CALL(seq0, msgid=0x74a7, aux=option) then RET(seq1, msgid=reply id iff
 * kr==SUCCESS else 0, aux=kr verbatim). Same measured tid/pid/gen. */
void d190_fill_records(d141_record rec[2], uint64_t call_ts,
                       uint64_t ret_ts, uint64_t tid, int32_t pid,
                       uint64_t gen, kern_return_t kr,
                       mach_msg_id_t reply_id);

/* write a D169 v3 probe stream: header (uuid producer-declared,
 * uuid_valid=1, emitted=2, dropped=0, uncommitted=0, n_records=2)
 * followed by the two records. Exact hdr+2*64 bytes; no trailer.
 * returns 0 or -1 (no partial-file success claim). */
int  d190_write_stream(const char *path, const d141_record rec[2],
                       const uint8_t uuid[16], int32_t pid,
                       const char *label);

/* generation tag per d141_set_generation: (pid<<32) ^ uuid_low64 */
uint64_t d190_gen_tag(int32_t pid, const uint8_t uuid[16]);

/* ---- the send primitive ---------------------------------------------*/
/*
 * Exactly one mach_msg — no loop, no retry branch (G5). Takes the
 * remote port as an argument so host tests can bind a loopback port
 * (G17); main passes _SLSServerPort(). NEVER invoked against a real
 * WindowServer on this machine or device.
 *
 * Records carry the supplied pid/gen so the emitted legs bear the real
 * producer identity (G11/G12) — main computes gen from the gated UUID,
 * a loopback test supplies its own. On return: rec[2] holds the
 * observed CALL/RET legs; *payload is the u64 @+0x24 iff
 * class==D190_OK. Cleanup executes d190_cleanup_class — the four
 * exact stub classes (D192): the SRP is the THREAD special reply port
 * (receive+send rights, TSD-cached — NOT itself a send-once right;
 * MAKE_SEND_ONCE mints the send-once into the outgoing message), and
 * it is deallocated ONLY on class DEALLOC.
 */
enum d190_reply_class d190_send_once(mach_port_t remote, int32_t pid,
                                     uint64_t gen, d141_record rec[2],
                                     uint64_t *payload);
/* Deterministic test seam: counts which cleanup branch send_once
 * actually executed. Compiled only in test builds. */
#ifdef D190_TEST_HOOKS
extern int d190_hook_cleanup_none;
extern int d190_hook_cleanup_destroy;
extern int d190_hook_cleanup_dealloc;
#endif

#endif /* D190_PROBE_H */
