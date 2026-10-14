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

int main(int argc, char **argv) {
    setbuf(stdout, NULL);   // unbuffered — survive a crash mid-probe
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

    // ── Phase 1b (R001): identity pins + tccd port for WS-log
    // correlation.  SkyLight's loaded UUID must match the RE binary
    // before any image-relative offset is trusted.  The tccd port name
    // is global in this bootstrap domain — MACH-ALL-SEND lines from WS
    // carrying remote=<tccdport> prove the gate reached the TCC check;
    // their absence across a post proves an earlier bail.
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
                        printf("SkyLight loaded uuid ");
                        for (int b = 0; b < 16; b++) printf("%02X", u->uuid[b]);
                        printf(" (expect 96676A53B1E03D7EB98BB73873CD1880)\n");
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
        uint8_t rec[0xf8];
        memset(rec, 0, sizeof(rec));
        *(uint32_t *)(rec + 0x00) = 3;
        *(uint32_t *)(rec + 0x04) = 0xf8;
        *(uint32_t *)(rec + 0x08) = 5;          // kCGEventMouseMoved
        *(double *)(rec + 0x10) = 900.0;
        *(double *)(rec + 0x18) = 700.0;
        *(int32_t *)(rec + 0x38) = -1;          // session from conn state
        int32_t rc = postRecord(cid, rec, 0xf8, 0);
        printf("SLSPostEventRecord(mouseMoved) -> rc=%d\n", rc);
        *(uint32_t *)(rec + 0x08) = 1;          // down
        *(uint8_t *)(rec + 0x91) = 0;
        printf("SLSPostEventRecord(down) -> rc=%d\n",
               postRecord(cid, rec, 0xf8, 0));
        *(uint32_t *)(rec + 0x08) = 2;          // up
        printf("SLSPostEventRecord(up)   -> rc=%d\n",
               postRecord(cid, rec, 0xf8, 0));

        // 5b: byte-perfect record via the real CGEvent->record converter.
        // If WS still answers 1002 to a canonical record, the rejection is
        // server-side policy (session/permission), not record shape.
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

    // ── Phase 6: TCC post-event access — preflight + request ──
    // SLPreflightPostEventAccess / SLRequestPostEventAccess resolve the
    // lazy TCC-client singleton; on real macOS a denied preflight is what
    // makes the WS-side post handler return an error to the record.
    typedef int (*AccessFn)(void);
    AccessFn preflight =
        (AccessFn)dlsym(RTLD_DEFAULT, "SLPreflightPostEventAccess");
    AccessFn request =
        (AccessFn)dlsym(RTLD_DEFAULT, "SLRequestPostEventAccess");
    if (preflight)
        printf("SLPreflightPostEventAccess() -> %d\n", preflight());
    else
        printf("SLPreflightPostEventAccess absent\n");
    if (request)
        printf("SLRequestPostEventAccess()  -> %d\n", request());
    else
        printf("SLRequestPostEventAccess absent\n");

    // ── Phase 6b: TCCAccessPreflightWithAuditToken on OUR OWN token ──
    // 2026-10-14 RE: _XPostEventRecord's 1002 comes from
    // CGXSenderCanSynthesizeEvents, which reads gCurMsgAudit (only ever
    // populated by PKGTransactionDecodeCAPayload) and calls
    // TCCAccessPreflightWithAuditToken("kTCCServicePostEvent", audit, 0).
    // Preflight OUR OWN audit token here: if it returns 0 the TCC path
    // is healthy for this process and the 1002 is the WS-side audit
    // context (NULL gCurMsgAudit / a different captured token); if it
    // returns nonzero the TCC policy check itself fails even for an
    // entitled, trustcache-admitted binary.
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

    // ── Phase 7: direct-to-connection delivery ──
    // SLPSPostEventRecordTo resolves a target connID to that connection's
    // event port and posts the record straight into its queue — bypassing
    // WS hit-testing/session policy entirely.  Target: Terminal's connID.
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
    if (postTo && connForPSN && procForPID && getRecord && term) {
        uint32_t psn[2] = {0};          // ProcessSerialNumber {high,low}
        uint32_t target = 0;
        kern_return_t kr = procForPID(term, psn);
        printf("GetProcessForPID(%d) -> rc=%d psn={%u,%u}\n",
               term, kr, psn[0], psn[1]);
        kr = connForPSN(cid, psn, &target);
        printf("SLSGetConnectionIDForPSN -> rc=%d cid=%u\n", kr, target);
        CGEventRef mv3 = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved,
                                                 CGPointMake(500, 500), 0);
        static uint8_t rec[0xf8];
        memset(rec, 0, sizeof(rec));
        if (mv3) getRecord(mv3, rec, 0xf8);
        // try the target's event port as arg0 as well
        mach_port_t tport = MACH_PORT_NULL;
        kern_return_t trc = getEventPort(target, &tport);
        printf("SLSGetEventPort(target=%u) -> rc=%d port=%u\n",
               target, trc, (unsigned)tport);
        // arg0 variant chosen by argv[1] — a wrong guess may kill the
        // process, so each variant runs as a separate invocation.
        int sel = -1;
        if (argc > 1) sel = atoi(argv[1]);
        struct { const char *name; uint32_t a0; } tries[] = {
            {"connID",          target},
            {"target-evport",   tport},
            {"self-connID",     cid},
            {"self-evport",     evport},
        };
        // Dangerous: only run the selected variant explicitly
        // (probe N).  Default invocation skips this so the safe
        // inventory phases below always execute.
        for (int i = 0; i < 4; i++) {
            if (sel != i) continue;
            printf("  trying postTo(%s=%u)...\n", tries[i].name,
                   tries[i].a0);
            int rc2 = postTo(tries[i].a0, rec, 0xf8, 0);
            printf("  postTo(%s=%u) -> rc=%d\n",
                   tries[i].name, tries[i].a0, rc2);
        }
        if (sel < 0) printf("  postTo variants skipped (pass 0-3)\n");
    } else {
        printf("direct-delivery symbols missing or no target pid\n");
    }

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
    // R001: session mutation is OPT-IN — only runs when argv[2]=="mutate".
    // Read-only inventory (Phase 8) always runs; default runs make no
    // session/connection state changes.
    int allowMutate = (argc > 2 && strcmp(argv[2], "mutate") == 0);
    if (!allowMutate) {
        printf("Phase9 session-mutation skipped (pass 'mutate' as argv[2])\n");
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
    // ── Phase 10: alternate post paths ─────────────────────────
    // Distinguish "posting is dead" from "mouse records rejected":
    //  (a) scroll-wheel record through the same funnel
    //  (b) SLEventTapPostEvent — the event-tap injection entry
    PostRecFn postRec10 =
        (PostRecFn)dlsym(RTLD_DEFAULT, "SLSPostEventRecord");
    typedef kern_return_t (*TapPostFn)(void * /*tap*/, void * /*record*/,
                                       uint32_t /*len*/, uint32_t /*fl*/);
    TapPostFn tapPost =
        (TapPostFn)dlsym(RTLD_DEFAULT, "SLEventTapPostEvent");
    typedef void (*ScrollPostFn)(int32_t /*ignored*/, int32_t, int32_t,
                                 int32_t, int32_t);
    ScrollPostFn scrollPost =
        (ScrollPostFn)dlsym(RTLD_DEFAULT, "CGPostScrollWheelEvent");
    CGEventRef sw = CGEventCreateScrollWheelEvent(
        NULL, kCGScrollEventUnitLine, 1, -3, 0);
    if (sw && postRec10) {
        static uint8_t rec5[0xf8];
        memset(rec5, 0, sizeof(rec5));
        if (getRecord) getRecord(sw, rec5, 0xf8);
        kern_return_t rc4 = postRec10(cid, rec5, 0xf8, 0);
        printf("SLSPostEventRecord(scroll) -> rc=%d\n", rc4);
        if (tapPost) {
            kern_return_t rc5 = tapPost(0, rec5, 0xf8, 0);
            printf("SLEventTapPostEvent(tap=0,scroll) -> rc=%d\n", rc5);
        } else printf("SLEventTapPostEvent absent\n");
        CFRelease(sw);
    }
    if (scrollPost) {
        scrollPost(0, -3, 0, 0, 0);   // (ignored, delta1, delta2, d3, d4)
        printf("CGPostScrollWheelEvent -> issued (watch witness)\n");
    } else printf("CGPostScrollWheelEvent absent\n");

    // ── Phase 11: event-tap access ─────────────────────────────
    // The canonical injection path on macOS is the event tap:
    // if ListenEventAccess is granted, a tap can insert events into
    // the HID stream.  Probe entitlements carry kTCCServiceListenEvent.
    typedef int (*PreflightFn)(void);
    PreflightFn preL =
        (PreflightFn)dlsym(RTLD_DEFAULT, "SLPreflightListenEventAccess");
    PreflightFn preP =
        (PreflightFn)dlsym(RTLD_DEFAULT, "SLPreflightPostEventAccess");
    if (preL) printf("SLPreflightListenEventAccess -> %d\n", preL());
    else printf("SLPreflightListenEventAccess absent\n");
    if (preP) printf("SLPreflightPostEventAccess   -> %d\n", preP());
    else printf("SLPreflightPostEventAccess absent\n");
    typedef void *(*TapCreateFn)(int, int, int, int, int, void *);
    TapCreateFn tapCreate =
        (TapCreateFn)dlsym(RTLD_DEFAULT, "SLEventTapCreate");
    if (tapCreate && allowMutate) {
        // R001 ABI audit (RE-confirmed @0x185239a2c): SLEventTapCreate is
        // a 6-arg wrapper that shifts x1..x5→x2..x6, inserts w1=0, and
        // forwards to the impl at 0x185239a48.  Impl bounds-checks:
        //   impl.w0(outer x0) <= 2, impl.w2(outer x1) <= 1,
        //   impl.w1(=0),       impl.w3(outer x2) <= 1.
        // All-zero args pass the checks; opt-in only because the exact
        // semantics are still unverified.
        pid_t tp = fork();
        if (tp == 0) {
            void *t = tapCreate(0, 0, 0, 0, 0, NULL);
            printf("  SLEventTapCreate -> %p\n", t);
            _exit(t ? 0 : 1);
        }
        int st = 0; waitpid(tp, &st, 0);
        printf("SLEventTapCreate child status=0x%x\n", st);
    } else if (tapCreate) {
        printf("SLEventTapCreate skipped (pass 'mutate' as argv[2])\n");
    } else printf("SLEventTapCreate absent\n");
    return 0;
}
