/*
 * D190 — one-shot raw _XGetWindowServerTimebase probe. STANDALONE
 * DIAGNOSTIC: sends exactly one 0x74a7 per process, writes a D169 v3
 * probe stream to an explicit output path. Never installed/invoked on
 * this host or any device; no product hooks, sentinel, transport, or
 * GUI involvement.
 *
 * G15 assumption (verbatim): server-side side effects of
 * _XGetWindowServerTimebase are UNVERIFIED — the WindowServer handler
 * binary is not in-tree; "no server state change" is inferred from the
 * request/reply shape only, not confirmed. Client-side data only: no
 * WS handler instrumentation and no wait-vs-no-wait comparison exist
 * in this milestone.
 *
 * Exit classes (nonzero on every failure, never fabricated):
 *   2  SkyLight UUID gate failed (absent/ambiguous/mismatch)
 *   3  SkyLight image not loadable / _SLSServerPort unresolved
 *   4  server port dead (MACH_PORT_NULL/DEAD_NAME)
 *   5  usage error (missing output path)
 *   6  stream write failed
 *   10+class  send/reply failure class (10 + d190_reply_class)
 *   0  valid 0x750b/0x2c reply consumed
 */

#include "d190_probe.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <stream-out-path>\n", argv[0]);
        return 5;
    }
    const char *out = argv[1];

    /* G2: remote via the runtime _SLSServerPort route; dlopen loads the
     * image the gate then verifies (no private-framework link needed). */
    void *sl = dlopen("/System/Library/PrivateFrameworks/SkyLight."
                      "framework/SkyLight", RTLD_LAZY);
    /* F1/D195: Darwin dlsym takes the C name — "_SLSServerPort" would
     * search for __SLSServerPort and miss the real export. */
    mach_port_t (*slp)(void) =
        sl ? (mach_port_t (*)(void))dlsym(sl, "SLSServerPort") : NULL;
    if (!slp) {
        fprintf(stderr, "d190: refuse — SkyLight/_SLSServerPort "
                "unresolved\n");
        return 3;
    }

    /* G1: UUID gate BEFORE any request construction. Fail-closed — the
     * gate scans dyld LC_UUID, never a user-provided string. */
    uint8_t uuid[16];
    if (d190_check_skylight_uuid(uuid) != 0) {
        fprintf(stderr, "d190: refuse — loaded SkyLight image is not "
                "exactly one LC_UUID match for 96676A53-B1E0-3D7E-"
                "B98B-B73873CD1880\n");
        return 2;
    }

    /* F2/D195: bind the resolved ADDRESS to its owning image — the
     * called function must live inside the UUID-pinned SkyLight, not
     * merely coexist with it (interposition/version spoof). */
    if (d190_addr_image_match((const void *)(uintptr_t)slp, uuid) != 0) {
        fprintf(stderr, "d190: refuse — resolved SLSServerPort is not "
                "in the UUID-pinned image\n");
        return 3;
    }
    mach_port_t server = slp();
    if (server == MACH_PORT_NULL || server == MACH_PORT_DEAD) {
        fprintf(stderr, "d190: refuse — dead server port\n");
        return 4;
    }

    int32_t pid = (int32_t)getpid();
    uint64_t gen = d190_gen_tag(pid, uuid);

    d141_record rec[2];
    uint64_t payload = 0;
    enum d190_reply_class c =
        d190_send_once(server, pid, gen, rec, &payload);

    if (d190_write_stream(out, rec, uuid, pid, "d190-probe") != 0) {
        fprintf(stderr, "d190: stream write failed\n");
        return 6;
    }

    if (c != D190_OK) {
        fprintf(stderr, "d190: %s (class %d)\n",
                d190_class_str(c), (int)c);
        return 10 + (int)c;
    }
    printf("d190: OK timebase=%llu (stream: %s)\n",
           (unsigned long long)payload, out);
    return 0;
}
