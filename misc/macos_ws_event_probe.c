// macos_ws_event_probe.c — WindowServer event-subsystem probe for the
// Ventura chroot.  This is the probe whose 2026-10-13 output proved that
// WindowServer under `-virtualonly` never registers its HID event server:
//
//     bootstrap_look_up com.apple.WindowServer.HIDEvents -> rc=1102
//     SLSGetEventPort = 1000   ← MISREAD: 1000 is the *return code* (0x3e8,
//     mach_port_type(1000) -> rc=15 KERN_INVALID_NAME   no-conn error),
//                                                        not the port.
//     CGWarpMouseCursorPosition -> cursor moved and read back
//     SLSPostEventRecord mouse record -> rc=1002 (WS reply rejection)
//
// 2026-10-14 RE correction: SLSGetEventPort's signature is
//   kern_return_t SLSGetEventPort(CGSConnectionID cid, mach_port_t *out);
// it resolves the connection object for `cid` and stores the event port
// at connection+0x18.  rc=0 means success (observed rc=0 port=4367).
//
// R001 error-code audit (22F82 disasm, no blanket convention):
//   SLSGetEventPort:            0=success, 1000=invalid/no conn
//   _XSetLoginwindowConnection: 1000 = second registry lookup miss
//                               (0x1851f7cf4 NULL → w0=#0x3e8),
//                               1001 = audit-token desc mismatch,
//                               -0x12c/-0x130 = request validation
//   _XPostEventRecord:          1002 = CGXSenderCanSynthesizeEvents==0
//   "1000 == CGX-OK" is FALSE for these routines — 1000 is a distinct
//   per-routine failure. Always decode against the specific impl.
//
// Build (on the macOS dev host, macOS SDK):
//   xcrun clang -arch arm64e -o macos_ws_event_probe \
//       misc/macos_ws_event_probe.c -framework CoreFoundation \
//       -framework CoreGraphics -framework IOKit
//
// Deploy (the atomic recipe — see
// docs/evidence/ventura-22F82-input-transport-and-composite-20261013.md):
//   python3 /var/jb/usr/macOS/bin/set_macos_version.py macos_ws_event_probe
//   ldid -Smisc/entitlements.plist macos_ws_event_probe   # twice
//   ldid -Smisc/entitlements.plist macos_ws_event_probe
//   install -m755 macos_ws_event_probe \
//       /var/mnt/rootfs/usr/local/bin/macos_ws_event_probe.new \
//   && mv /var/mnt/rootfs/usr/local/bin/macos_ws_event_probe{.new,}
//   python3 /var/jb/usr/macOS/bin/macws_boot_trust.py --readd \
//       /var/mnt/rootfs/usr/local/bin/macos_ws_event_probe
//
// Run inside the chroot:
//   /var/jb/usr/bin/chroot /var/mnt/rootfs \
//       /usr/local/bin/macos_ws_event_probe
//
// All SkyLight privates are resolved with dlsym so the probe keeps working
// if a symbol disappears; missing symbols print as absent, not crashes.

#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/bootstrap.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <sys/wait.h>
#include <math.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>

extern kern_return_t bootstrap_look_up(mach_port_t bp,
                                       const char *service_name,
                                       mach_port_t *sp);

typedef kern_return_t (*GetEventPortFn)(uint32_t cid, mach_port_t *out);
typedef uint32_t    (*MainConnectionIDFn)(void);
typedef int32_t     (*PostEventRecordFn)(uint32_t conn, const void *rec,
                                         uint32_t size, int64_t flags);
typedef void        (*GetEventRecordFn)(void *event, void *recOut,
                                        uint32_t size);

static void probe_bootstrap_service(const char *name) {
    mach_port_t port = MACH_PORT_NULL;
    kern_return_t kr = bootstrap_look_up(bootstrap_port, name, &port);
    printf("bootstrap_look_up %-46s -> rc=%d port=%u\n",
           name, kr, (unsigned)port);
    if (port != MACH_PORT_NULL)
        mach_port_deallocate(mach_task_self(), port);
}

// R003-F03 mode discipline (argv[1], exact token match only):
//   inventory  (default) — strictly read-only: bootstrap lookups,
//     loaded-UUID pin, tccd reachability, connection id, event-port
//     query, display list, session dict/list, TCC preflight queries,
//     SecTask entitlement vend. No warp/post/request/grant/create.
//   post — inventory + cursor warp + CGEventPost witness + ONE
//     canonical SLSPostEventRecord.  Bounded surface per R003 review:
//     no guessed-arg variants, no unverified-ABI calls.
//   postmin — R006 minimal attribution mode: SLSMainConnectionID +
//     ONE canonical SLSPostEventRecord and nothing else.  No warp, no
//     CGEventPost witness, no inventory phases, no session mutation —
//     exactly one _XPostEventRecord reaches WindowServer per run.
//   mutate — reserved for later explicitly-authorized runs.  postTo
//     direct delivery, SLEventTapPostEvent and SLEventTapCreate are
//     DISABLED in this build (their argument contracts are not
//     RE-established; gating is not enough — they are not callable at
//     all until the exact signatures are disassembled).  Session
//     mutation remains behind 'mutate'.
// Unknown private ABIs stay fail-closed and opt-in only.
enum { M_INVENTORY, M_POST, M_POSTMIN, M_MUTATE };
static int probe_mode(int argc, char **argv) {
    if (argc < 2) return M_INVENTORY;
    if (!strcmp(argv[1], "inventory")) return M_INVENTORY;
    if (!strcmp(argv[1], "post")) return M_POST;
    if (!strcmp(argv[1], "postmin")) return M_POSTMIN;
    if (!strcmp(argv[1], "mutate")) return M_MUTATE;
    fprintf(stderr, "unknown mode '%s' — running inventory\n", argv[1]);
    return M_INVENTORY;
}

int main(int argc, char **argv) {
    setbuf(stdout, NULL);   // unbuffered — survive a crash mid-probe
    int mode = probe_mode(argc, argv);
    printf("mode=%s\n", mode == M_MUTATE ? "mutate" :
           mode == M_POSTMIN ? "postmin" :
           mode == M_POST ? "post" : "inventory");
    if (mode == M_POSTMIN) {
        // R006: one attributable _XPostEventRecord, nothing else.
        MainConnectionIDFn mc =
            (MainConnectionIDFn)dlsym(RTLD_DEFAULT, "SLSMainConnectionID");
        PostEventRecordFn pr =
            (PostEventRecordFn)dlsym(RTLD_DEFAULT, "SLSPostEventRecord");
        GetEventRecordFn gr =
            (GetEventRecordFn)dlsym(RTLD_DEFAULT, "SLEventGetEventRecord");
        uint32_t cid = mc ? mc() : 0;
        printf("postmin: cid=%u postRecord=%p getRecord=%p\n",
               cid, (void *)pr, (void *)gr);
        if (cid && pr && gr) {
            uint8_t canon[0xf8];
            memset(canon, 0, sizeof(canon));
            CGEventRef mv = CGEventCreateMouseEvent(
                NULL, kCGEventMouseMoved, CGPointMake(500, 500), 0);
            if (mv) { gr(mv, canon, 0xf8); CFRelease(mv); }
            printf("postmin: SLSPostEventRecord -> rc=%d\n",
                   pr(cid, canon, 0xf8, 0));
        }
        return 0;
    }
    mach_port_t evport = MACH_PORT_NULL;
    GetEventPortFn getEventPort =
        (GetEventPortFn)dlsym(RTLD_DEFAULT, "SLSGetEventPort");
    // ── Phase 1: which WindowServer event services exist in bootstrap? ──
    probe_bootstrap_service("com.apple.windowserver");
    probe_bootstrap_service("com.apple.windowserver.active");
    probe_bootstrap_service("com.apple.WindowServer.HIDEvents");
    probe_bootstrap_service("com.apple.windowserver.session");
    probe_bootstrap_service("com.apple.iohid");
    probe_bootstrap_service("com.apple.CARenderServer");
    probe_bootstrap_service("macwsguide.windowserver.final-composite-replay");

    // ── Phase 1b (R001): identity pins + tccd reachability.
    // SkyLight's loaded UUID is COMPARED against the expected 22F82
    // build before any private slot/VA read below — the probe fails
    // closed on an unknown binary.  tccd lookup proves only that the
    // service is reachable from this bootstrap domain — Mach port
    // names are PER-TASK, so this port number cannot be compared with
    // remote= fields in WindowServer's own MACH-ALL-SEND log lines.
    static const uint8_t kSkyUUID[16] = {
        0x96,0x67,0x6a,0x53,0xb1,0xe0,0x3d,0x7e,
        0xb9,0x8b,0xb7,0x38,0x73,0xcd,0x18,0x80 };
    int sky_uuid_ok = 0;
    {
        for (uint32_t i = 0; i < _dyld_image_count(); i++) {
            const char *n = _dyld_get_image_name(i);
            if (n && strstr(n, "SkyLight.framework")) {
                const struct mach_header_64 *h =
                    (const struct mach_header_64 *)_dyld_get_image_header(i);
                const struct load_command *lc = (const void *)(h + 1);
                for (uint32_t c = 0; c < h->ncmds; c++) {
                    if (lc->cmd == LC_UUID) {
                        const struct uuid_command *u = (const void *)lc;
                        sky_uuid_ok =
                            (memcmp(u->uuid, kSkyUUID, 16) == 0);
                        printf("SkyLight loaded uuid ");
                        for (int b = 0; b < 16; b++) printf("%02X", u->uuid[b]);
                        printf(" (expect 96676A53B1E03D7EB98BB73873CD1880)"
                               " match=%d\n", sky_uuid_ok);
                        break;
                    }
                    lc = (const void *)((const char *)lc + lc->cmdsize);
                }
                break;
            }
        }
        mach_port_t tccd = MACH_PORT_NULL;
        kern_return_t tk = bootstrap_look_up(
            bootstrap_port, "com.apple.tccd", &tccd);
        printf("bootstrap_look_up com.apple.tccd -> rc=%d port=%u\n",
               tk, (unsigned)tccd);
        if (tk != KERN_SUCCESS || tccd == MACH_PORT_NULL) {
            tk = bootstrap_look_up(
                bootstrap_port, "com.apple.tccd.system", &tccd);
            printf("bootstrap_look_up com.apple.tccd.system -> rc=%d port=%u\n",
                   tk, (unsigned)tccd);
        }
        if (tccd != MACH_PORT_NULL)
            mach_port_deallocate(mach_task_self(), tccd);
    }

    // ── Phase 1c (R003 addendum): dispatcher stub resolution ──
    // CGXSenderCanSynthesizeEvents' dispatcher @VA 0x1854257ec calls
    // three helpers through AUTH_CONST stub slots to build its
    // permission-cache key: 0x18547c51c and 0x18547c52c decode the audit
    // token, 0x18547cb2c supplies the comparison value, and
    // 0x18547982c does the map lookup.  mac_hooks.m interposes
    // audit_token_to_auid/asid — if these slots resolved to the
    // interposed replacements, our compat layer may be producing the
    // denial key.  Read-only: resolve the slots' bound pointers in this
    // process and dladdr them for symbol+image names.
    //
    // FAIL-CLOSED: every address below is only valid for the pinned
    // 22F82 build.  If Phase 1b's UUID comparison failed, skip the
    // whole phase rather than read offsets from an unknown image.
    if (!sky_uuid_ok) {
        printf("Phase1c SKIPPED: loaded SkyLight uuid != expected "
               "22F82 build — refusing private-slot reads on an "
               "unknown image\n");
    } else {
        const struct mach_header *sky = NULL;
        for (uint32_t i = 0; i < _dyld_image_count(); i++) {
            const char *n = _dyld_get_image_name(i);
            if (n && strstr(n, "SkyLight.framework")) {
                sky = _dyld_get_image_header(i);
                break;
            }
        }
        if (sky) {
            intptr_t slide = (intptr_t)sky - 0x1850e5000;
            static const uint64_t slot_vas[] = {
                0x1dff3f358, // stub 0x18547c51c — audit decode A
                0x1dff3f360, // stub 0x18547c52c — audit decode B
                0x1dfef4b98, // stub 0x18547cb2c — comparison source
                0x1dff00d20, // stub 0x18547982c — map lookup
                0x1dfef4428, // stub 0x18547c99c — checker-name resolver
                0x1dfef4440, // stub 0x18547c9ac — resolver call
                0x1dfef5ba8, // stub 0x18547d27c — os_log-type check
                0x1dfef2678, // stub 0x18547c2ec — stackcheck fail
            };
            for (unsigned i = 0; i < 8; i++) {
                const void *slot = (const void *)(slot_vas[i] + slide);
                void *fn = *(void * const *)slot;   // bound pointer
                Dl_info dfi;
                memset(&dfi, 0, sizeof(dfi));
                int ok = dladdr(fn, &dfi);
                const char *img = ok && dfi.dli_fname ? dfi.dli_fname : "?";
                const char *base = strrchr(img, '/');
                printf("stub[%u] va=%#llx fn=%p -> %s:%s\n", i,
                       (unsigned long long)slot_vas[i], fn,
                       base ? base + 1 : img,
                       ok && dfi.dli_sname ? dfi.dli_sname : "?");
            }
            // The authoritative checker's lazily-resolved NAME string
            // (impl @0x185425da4 loads it from VA 0x1854dc3e) and the
            // gate's rightDesc object (VA 0x1d3dcb6c8): dump both.
            // Guard every VA with vm_region first — a wrong VA must
            // degrade to a printout, not crash the probe.
            static const struct { const char *tag; uint64_t va; }
                reads[] = {
                {"checker-name", 0x1854dc3e},
                {"checker-aux",  0x1854dc5f},
                {"rightDesc",    0x1d3dcb6c8},
            };
            for (unsigned r = 0; r < 3; r++) {
                vm_address_t a = (vm_address_t)(reads[r].va + slide);
                vm_address_t rg = a; vm_size_t rsz = 0;
                vm_region_basic_info_data_64_t bi;
                mach_msg_type_number_t bc = VM_REGION_BASIC_INFO_COUNT_64;
                mach_port_t obj = MACH_PORT_NULL;
                kern_return_t rr = vm_region_64(mach_task_self(), &rg,
                    &rsz, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&bi,
                    &bc, &obj);
                if (rr != KERN_SUCCESS || !(bi.protection & VM_PROT_READ)
                    || a < rg || a >= rg + rsz) {
                    printf("%s @%#llx: UNMAPPED/protected (rr=%d)\n",
                           reads[r].tag, (unsigned long long)a, rr);
                    continue;
                }
                if (r < 2) {
                    char cbuf[129];
                    memcpy(cbuf, (const void *)a, 128);
                    cbuf[128] = 0;
                    printf("%s @%p: \"%.100s\"\n", reads[r].tag,
                           (void *)a, cbuf);
                } else {
                    const uint64_t *rd = (const uint64_t *)a;
                    printf("rightDesc @%p: [0]=%#llx [+8]=%#llx "
                           "[+0x10]=%#llx [+0x18]=%#llx [+0x20]=%#llx\n",
                           rd,
                           (unsigned long long)rd[0],
                           (unsigned long long)rd[1],
                           (unsigned long long)rd[2],
                           (unsigned long long)rd[3],
                           (unsigned long long)rd[4]);
                    if (rd[0]) {
                        Dl_info di2; memset(&di2, 0, sizeof(di2));
                        dladdr((const void *)rd[0], &di2);
                        printf("rightDesc[0] -> %s (image %s)\n",
                               di2.dli_sname ? di2.dli_sname : "?",
                               di2.dli_fname ? di2.dli_fname : "?");
                        CFTypeRef cfv = (CFTypeRef)rd[0];
                        if (CFGetTypeID(cfv) == CFStringGetTypeID())
                            CFShow(cfv);
                    }
                }
            }
            // Rights-cache dict + lazy-checker cache globals (all in
            // SkyLight __DATA — same readability gate).
            {
                static const struct { const char *tag; uint64_t va; }
                    globals[] = {
                    {"rightsCache",  0x1d8ba6b40},
                    {"cacheSentinel",0x1d8ba6b48},
                    {"lazyChecker",  0x1d8ba6b50},
                    {"lazyAux",      0x1d8ba6b58},
                };
                for (unsigned g = 0; g < 4; g++) {
                    vm_address_t a =
                        (vm_address_t)(globals[g].va + slide);
                    vm_address_t rg = a; vm_size_t rsz = 0;
                    vm_region_basic_info_data_64_t bi;
                    mach_msg_type_number_t bc =
                        VM_REGION_BASIC_INFO_COUNT_64;
                    mach_port_t obj = MACH_PORT_NULL;
                    kern_return_t rr = vm_region_64(mach_task_self(),
                        &rg, &rsz, VM_REGION_BASIC_INFO_64,
                        (vm_region_info_t)&bi, &bc, &obj);
                    if (rr != KERN_SUCCESS ||
                        !(bi.protection & VM_PROT_READ) ||
                        a < rg || a >= rg + rsz) {
                        printf("%s @%#llx: UNMAPPED\n", globals[g].tag,
                               (unsigned long long)a);
                        continue;
                    }
                    void *val = *(void * const *)a;
                    printf("%s @%p = %p", globals[g].tag,
                           (void *)a, val);
                    if (val) {
                        Dl_info dg; memset(&dg, 0, sizeof(dg));
                        if (dladdr(val, &dg))
                            printf("  -> %s:%s",
                                   dg.dli_fname ? dg.dli_fname : "?",
                                   dg.dli_sname ? dg.dli_sname : "?");
                    }
                    printf("\n");
                }
            }
        }
    }

    // ── Phase 2: CGS connection (needed before the event-port query) ──
    MainConnectionIDFn mainConn =
        (MainConnectionIDFn)dlsym(RTLD_DEFAULT, "SLSMainConnectionID");
    uint32_t cid = mainConn ? mainConn() : 0;
    printf("SLSMainConnectionID = %u\n", cid);

    // ── Phase 2b: the event port SLSGetEventPort hands out — valid? ──
    if (getEventPort && cid) {
        kern_return_t rc = getEventPort(cid, &evport);
        mach_port_type_t type = 0;
        kern_return_t kr = mach_port_type(mach_task_self(), evport, &type);
        printf("SLSGetEventPort(cid=%u) -> rc=%d port=%u\n",
               cid, rc, (unsigned)evport);
        printf("mach_port_type(%u) -> rc=%d (%s) type=%#x\n",
               (unsigned)evport, kr, mach_error_string(kr), (unsigned)type);
    } else {
        printf("SLSGetEventPort symbol absent or no connection\n");
    }
    CGDirectDisplayID displays[16];
    uint32_t displayCount = 0;
    CGGetActiveDisplayList(16, displays, &displayCount);
    printf("CGGetActiveDisplayList -> %u displays:", displayCount);
    for (uint32_t i = 0; i < displayCount; i++) {
        CGSize s = CGDisplayScreenSize(displays[i]);
        printf(" %u(%gx%g)", displays[i], s.width, s.height);
    }
    printf("\n");

    // ── Phase 6: TCC post-event access — preflight is a read-only query
    // (inventory); request can create/grant state → mutate only. ──
    typedef int (*AccessFn)(void);
    AccessFn preflight =
        (AccessFn)dlsym(RTLD_DEFAULT, "SLPreflightPostEventAccess");
    AccessFn request =
        (AccessFn)dlsym(RTLD_DEFAULT, "SLRequestPostEventAccess");
    if (preflight)
        printf("SLPreflightPostEventAccess() -> %d\n", preflight());
    else
        printf("SLPreflightPostEventAccess absent\n");
    if (mode >= M_MUTATE && request)
        printf("SLRequestPostEventAccess()  -> %d\n", request());
    else if (request)
        printf("SLRequestPostEventAccess skipped (mutate-only)\n");
    else
        printf("SLRequestPostEventAccess absent\n");

    // ── Phase 6b: TCCAccessPreflightWithAuditToken on OUR OWN token ──
    // 2026-10-14 RE: _XPostEventRecord's 1002 comes from
    // CGXSenderCanSynthesizeEvents.  CORRECTED interpretation after the
    // R002 runtime witness (cred!=NULL, size=52, euid/pid = sender):
    // gCurMsgAudit IS populated on the event-post path, so the earlier
    // "only PKGTransactionDecodeCAPayload writes it → NULL early-exit"
    // theory is disproven.  The denial happens inside the audit→
    // permission-cache lookup (dispatcher 0x1854257ec), whose exact
    // producer/consumer key relationship is still open.  Preflight OUR
    // OWN audit token here only establishes that TCC evaluates this
    // process's token — it does NOT prove the WS-side lookup key.
    {
        audit_token_t tok;
        memset(&tok, 0, sizeof(tok));
        mach_msg_type_number_t n = TASK_AUDIT_TOKEN_COUNT;
        kern_return_t kr = task_info(mach_task_self(), TASK_AUDIT_TOKEN,
                                     (task_info_t)&tok, &n);
        void *tcc = dlopen("/System/Library/PrivateFrameworks/"
                           "TCC.framework/TCC", RTLD_NOW);
        // Signature per tccutil.h: (CFStringRef, audit_token_t BY VALUE,
        // CFDictionaryRef).  Passing a pointer for the token crashes.
        typedef int64_t (*PreflightAuditFn)(CFStringRef,
                                            audit_token_t, CFDictionaryRef);
        PreflightAuditFn preA = tcc
            ? (PreflightAuditFn)dlsym(
                  tcc, "TCCAccessPreflightWithAuditToken")
            : NULL;
        printf("task_info(audit) rc=%d tcc=%p preflightAudit=%p\n",
               kr, tcc, (void *)preA);
        if (preA && kr == KERN_SUCCESS) {
            CFStringRef svcs[] = {CFSTR("kTCCServicePostEvent"),
                                  CFSTR("kTCCServiceListenEvent"),
                                  CFSTR("kTCCServiceSystemEvents")};
            for (int i = 0; i < 3; i++) {
                int64_t r = preA(svcs[i], tok, NULL);
                printf("TCCAccessPreflightWithAuditToken(svc[%d], self)"
                       " -> %lld\n", i, (long long)r);
            }
        }
        // Does the kernel actually VEND our ldid entitlements to
        // TCC's evaluator?  SecTaskCopyValueForEntitlement uses the
        // same csops entitlement blob TCC consults — NULL means the
        // signature blob isn't trusted enough to expose ents.
        // R001 ABI audit: real signature is
        //   CFTypeRef SecTaskCopyValueForEntitlement(SecTaskRef,
        //                                          CFStringRef,
        //                                          CFErrorRef *);
        // the earlier 2-arg typedef left x2 (error out) as register
        // garbage — a potential write-through-wild-pointer.
        typedef void *(*SecTaskFn)(void *);
        typedef void *(*SecTaskCopyFn)(void *, CFStringRef, void *);
        SecTaskFn create = (SecTaskFn)dlsym(RTLD_DEFAULT,
                                            "SecTaskCreateFromSelf");
        SecTaskCopyFn copyEnt = (SecTaskCopyFn)dlsym(
            RTLD_DEFAULT, "SecTaskCopyValueForEntitlement");
        if (create && copyEnt) {
            void *task = create(NULL);
            CFTypeRef v = task ? (CFTypeRef)copyEnt(
                task, CFSTR("com.apple.private.tcc.allow"), NULL) : NULL;
            printf("SecTask.tcc.allow -> %p\n", (void *)v);
            if (v) { CFShow(v); CFRelease(v); }
            CFTypeRef v2 = task ? (CFTypeRef)copyEnt(
                task, CFSTR("com.apple.private.SkyLight.loginwindow"),
                NULL) : NULL;
            printf("SecTask.SkyLight.loginwindow -> %p\n", (void *)v2);
            if (v2) { CFShow(v2); CFRelease(v2); }
        } else printf("SecTask symbols absent\n");
    }


    if (mode < M_POST) {
        printf("post/warp/mutation phases skipped (mode=inventory)\n");
        goto phase8;
    }

    // ── Phase 4: is the client->WS control channel alive? ──
    // CGWarpMouseCursorPosition is a synchronous connection call — if the
    // cursor moves and reads back, the CGS connection itself works and only
    // the event pipeline is dead.
    CGEventRef ev = CGEventCreate(NULL);
    CGPoint before = CGEventGetLocation(ev);
    CFRelease(ev);
    CGWarpMouseCursorPosition(CGPointMake(600, 500));
    usleep(100000);
    ev = CGEventCreate(NULL);
    CGPoint after = CGEventGetLocation(ev);
    CFRelease(ev);
    printf("cursor before=(%.0f,%.0f) after-warp=(%.0f,%.0f) %s\n",
           before.x, before.y, after.x, after.y,
           (fabs(after.x - 600) < 2 && fabs(after.y - 500) < 2)
               ? "WARP-OK" : "WARP-DEAD");

    // ── Phase 4b: end-to-end event-port test — post via CGEventPost and
    // check whether the cursor actually moves.  evport was proven valid
    // above; if the cursor doesn't move the WS-side dispatch loop never
    // drains it. ──
    CGEventRef mv = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved,
                                          CGPointMake(700, 600), 0);
    if (mv) {
        CGEventPost(kCGHIDEventTap, mv);
        usleep(100000);
        CGEventRef cur = CGEventCreate(NULL);
        CGPoint p = CGEventGetLocation(cur);
        CFRelease(cur);
        printf("CGEventPost(HID,moved 700,600) -> cursor=(%.0f,%.0f) %s\n",
               p.x, p.y,
               (fabs(p.x - 700) < 2 && fabs(p.y - 600) < 2)
                   ? "DISPATCH-OK" : "NO-DISPATCH");
        CFRelease(mv);
    }

    // ── Phase 5: does a raw mouse record survive SLSPostEventRecord? ──
    // Record layout RE-confirmed from SLSPostMouseEventsVButtons' stack
    // construction (SkyLight-22F82 __TEXT+0x156f34..): version=3 @0x00,
    // length=0xf8 @0x04, CGEventType @0x08, double point @0x10/0x18,
    // session=-1 @0x38, button index @0x91.  rc is WindowServer's reply:
    // 0x3ea=1002 means WS received and rejected the post.
    PostEventRecordFn postRecord =
        (PostEventRecordFn)dlsym(RTLD_DEFAULT, "SLSPostEventRecord");
    if (postRecord && cid) {
        // ONE canonical post (R003 bounded surface): byte-perfect record
        // via the real CGEvent->record converter.  The hand-built record
        // family and scroll post were removed — one known-good record is
        // enough to exercise _XPostEventRecord's gate.
        GetEventRecordFn getRecord =
            (GetEventRecordFn)dlsym(RTLD_DEFAULT, "SLEventGetEventRecord");
        CGEventRef mv2 = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved,
                                                 CGPointMake(500, 500), 0);
        if (getRecord && mv2) {
            uint8_t canon[0xf8];
            memset(canon, 0, sizeof(canon));
            getRecord(mv2, canon, 0xf8);
            printf("canonical record: v=%u len=%u type=%u\n",
                   *(uint32_t *)(canon + 0x00), *(uint32_t *)(canon + 0x04),
                   *(uint32_t *)(canon + 0x08));
            printf("SLSPostEventRecord(canonical) -> rc=%d\n",
                   postRecord(cid, canon, 0xf8, 0));
        } else {
            printf("SLEventGetEventRecord unavailable\n");
        }
        if (mv2) CFRelease(mv2);
    } else {
        printf("SLSPostEventRecord unavailable or no connection\n");
    }

    // ── Phase 7: direct-to-connection delivery ──
    // SLPSPostEventRecordTo resolves a target connID to that connection's
    // event port and posts the record straight into its queue — bypassing
    // WS hit-testing/session policy entirely.  Target: Terminal's connID.
    // R003-F03: mutation + guessed-arg0 experiment → mutate mode only.
    typedef int (*PostToFn)(uint32_t targetCid, const void *rec,
                            uint32_t size, int64_t flags);
    typedef kern_return_t (*ConnForPSNFn)(uint32_t ownCid, void *psn,
                                         uint32_t *targetCid);
    typedef kern_return_t (*ProcForPIDFn)(pid_t pid, void *psn);
    PostToFn postTo = (PostToFn)dlsym(RTLD_DEFAULT, "SLPSPostEventRecordTo");
    ConnForPSNFn connForPSN =
        (ConnForPSNFn)dlsym(RTLD_DEFAULT, "SLSGetConnectionIDForPSN");
    ProcForPIDFn procForPID =
        (ProcForPIDFn)dlsym(RTLD_DEFAULT, "GetProcessForPID");
    if (!procForPID) {
        void *hs = dlopen("/System/Library/Frameworks/ApplicationServices."
                          "framework/Frameworks/HIServices.framework/"
                          "HIServices", RTLD_NOW);
        if (hs) procForPID = (ProcForPIDFn)dlsym(hs, "GetProcessForPID");
    }
    GetEventRecordFn getRecord =
        (GetEventRecordFn)dlsym(RTLD_DEFAULT, "SLEventGetEventRecord");
    pid_t term = 0;
    FILE *pf = fopen("/tmp/ws_evprobe_target.pid", "r");
    if (pf) { fscanf(pf, "%d", (int *)&term); fclose(pf); }
    if (!term) term = 0;
    // R003 (2026-10-14 review addendum): SLPSPostEventRecordTo is
    // DISABLED — its four-arg contract is not RE-established and the
    // guessed-arg0 variants were an unbounded trial surface.  The
    // resolution queries below stay (they are read-only RPCs that
    // produced useful evidence); the postTo call itself is compiled
    // out pending exact signature recovery.
    (void)postTo;
    if (mode < M_MUTATE) {
        printf("postTo direct-delivery skipped (mutate-only)\n");
    } else if (connForPSN && procForPID && getRecord && term) {
        uint32_t psn[2] = {0};          // ProcessSerialNumber {high,low}
        uint32_t target = 0;
        kern_return_t kr = procForPID(term, psn);
        printf("GetProcessForPID(%d) -> rc=%d psn={%u,%u}\n",
               term, kr, psn[0], psn[1]);
        kr = connForPSN(cid, psn, &target);
        printf("SLSGetConnectionIDForPSN -> rc=%d cid=%u\n", kr, target);
        mach_port_t tport = MACH_PORT_NULL;
        kern_return_t trc = getEventPort(target, &tport);
        printf("SLSGetEventPort(target=%u) -> rc=%d port=%u\n",
               target, trc, (unsigned)tport);
        printf("  SLPSPostEventRecordTo DISABLED pending exact RE "
               "contract\n");
    } else {
        printf("direct-delivery symbols missing or no target pid\n");
    }

phase8: ;
    // ── Phase 8: session inventory ─────────────────────────────
    // Hypothesis: WS rejects event injection because no loginwindow
    // ever created a "console session" — connections then carry an
    // unowned/invalid session tag and the post handler refuses.
    // Inventory what the session layer reports before/without
    // creating anything.
    typedef void *(*CopyFn)(void);
    CopyFn curSess =
        (CopyFn)dlsym(RTLD_DEFAULT, "SLSCopyCurrentSessionDictionary");
    CopyFn sessList =
        (CopyFn)dlsym(RTLD_DEFAULT, "SLSCopySessionList");
    typedef int (*GetUIDFn)(void);
    GetUIDFn sessUID =
        (GetUIDFn)dlsym(RTLD_DEFAULT, "SLSGetSessionUID");
    if (curSess) {
        CFTypeRef d = (CFTypeRef)curSess();
        printf("SLSCopyCurrentSessionDictionary -> %p\n", (void *)d);
        if (d) { CFShow(d); CFRelease(d); }
    } else printf("SLSCopyCurrentSessionDictionary absent\n");
    if (sessList) {
        CFTypeRef l = (CFTypeRef)sessList();
        printf("SLSCopySessionList -> %p\n", (void *)l);
        if (l) { CFShow(l); CFRelease(l); }
    } else printf("SLSCopySessionList absent\n");
    if (sessUID) {
        printf("SLSGetSessionUID -> %d\n", sessUID());
    } else printf("SLSGetSessionUID absent\n");

    // ── Phase 9: create a real login session, then retest post ──
    // SessionList is empty and our conn reports a synthetic
    // pre-login session (257 / uid=88 / LoginDone=0).  Normally
    // loginwindow calls SLSCreateLoginSession; nobody did in the
    // chroot.  Try it ourselves and see whether the post gate moves.
    // R003-F03: session mutation only in explicit mutate mode.
    int allowMutate = (mode >= M_MUTATE);
    if (!allowMutate) {
        printf("Phase9 session-mutation skipped (mode != mutate)\n");
        goto phase10;
    }
    typedef int (*CreateLoginFn)(uint32_t *sid);
    CreateLoginFn createLogin =
        (CreateLoginFn)dlsym(RTLD_DEFAULT, "SLSCreateLoginSession");
    typedef int (*SetLWConnFn)(uint32_t cid);
    SetLWConnFn setLWConn =
        (SetLWConnFn)dlsym(RTLD_DEFAULT, "SLSSetLoginwindowConnection");
    typedef int (*SwitchFn)(uint32_t sid);
    SwitchFn switchTo =
        (SwitchFn)dlsym(RTLD_DEFAULT, "SLSSessionSwitchToSessionID");
    SwitchFn switchAudit =
        (SwitchFn)dlsym(RTLD_DEFAULT,
                        "SLSSessionSwitchToAuditSessionID");
    typedef kern_return_t (*PostRecFn)(uint32_t, const void *,
                                       uint32_t, uint32_t);
    if (setLWConn) {
        int rc0 = setLWConn(cid);
        printf("SLSSetLoginwindowConnection(cid=%u) -> rc=%d\n",
               cid, rc0);
    } else printf("SLSSetLoginwindowConnection absent\n");
    if (createLogin) {
        uint32_t sid = 0;
        int rc = createLogin(&sid);
        printf("SLSCreateLoginSession -> rc=%d sid=%u\n", rc, sid);
        if (sessList) {
            CFTypeRef l = (CFTypeRef)sessList();
            printf("SessionList(after create) -> %p\n", (void *)l);
            if (l) { CFShow(l); CFRelease(l); }
        }
        if (curSess) {
            CFTypeRef d = (CFTypeRef)curSess();
            printf("CurrentSession(after create) -> %p\n", (void *)d);
            if (d) { CFShow(d); CFRelease(d); }
        }
        if (sid && switchAudit) {
            int rc2 = switchAudit(sid);
            printf("SLSSessionSwitchToAuditSessionID(%u) -> rc=%d\n",
                   sid, rc2);
        }
        if (sid && switchTo) {
            int rc2 = switchTo(sid);
            printf("SLSSessionSwitchToSessionID(%u) -> rc=%d\n",
                   sid, rc2);
        }
        if (sessList) {
            CFTypeRef l = (CFTypeRef)sessList();
            printf("SessionList(after switch) -> %p\n", (void *)l);
            if (l) { CFShow(l); CFRelease(l); }
        }
        if (curSess) {
            CFTypeRef d = (CFTypeRef)curSess();
            printf("CurrentSession(after switch) -> %p\n", (void *)d);
            if (d) { CFShow(d); CFRelease(d); }
        }
        // retest: does WS still reject the record post?
        CGEventRef mv4 = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved,
                                                 CGPointMake(650, 450), 0);
        if (mv4) {
            static uint8_t rec4[0xf8];
            memset(rec4, 0, sizeof(rec4));
            if (getRecord) getRecord(mv4, rec4, 0xf8);
            PostRecFn postRec =
                (PostRecFn)dlsym(RTLD_DEFAULT, "SLSPostEventRecord");
            if (postRec) {
                kern_return_t rc3 = postRec(cid, rec4, 0xf8, 0);
                printf("SLSPostEventRecord(after session) -> rc=%d\n",
                       rc3);
            }
            CFRelease(mv4);
        }
    } else printf("SLSCreateLoginSession absent\n");

phase10: ;
    // ── Phase 10/11: event-tap surface — DISABLED (R003 addendum) ──
    // SLEventTapPostEvent, CGPostScrollWheelEvent and SLEventTapCreate
    // are not callable in this build: their argument contracts are not
    // RE-established (tap=NULL was never a validated input; the tap
    // create wrapper's six-arg forwarding is confirmed but its
    // parameter semantics are not).  Symbol-presence inventory only.
    typedef kern_return_t (*TapPostFn)(void * /*tap*/, void * /*record*/,
                                       uint32_t /*len*/, uint32_t /*fl*/);
    TapPostFn tapPost =
        (TapPostFn)dlsym(RTLD_DEFAULT, "SLEventTapPostEvent");
    typedef void *(*TapCreateFn)(int, int, int, int, int, void *);
    TapCreateFn tapCreate =
        (TapCreateFn)dlsym(RTLD_DEFAULT, "SLEventTapCreate");
    printf("SLEventTapPostEvent symbol=%s (call DISABLED)\n",
           tapPost ? "present" : "absent");
    printf("SLEventTapCreate    symbol=%s (call DISABLED)\n",
           tapCreate ? "present" : "absent");
    (void)tapPost; (void)tapCreate;

    typedef int (*PreflightFn)(void);
    PreflightFn preL =
        (PreflightFn)dlsym(RTLD_DEFAULT, "SLPreflightListenEventAccess");
    if (preL) printf("SLPreflightListenEventAccess -> %d\n", preL());
    else printf("SLPreflightListenEventAccess absent\n");
    return 0;
}
