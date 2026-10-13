// macos_ws_event_probe.c — WindowServer event-subsystem probe for the
// Ventura chroot.  This is the probe whose 2026-10-13 output proved that
// WindowServer under `-virtualonly` never registers its HID event server:
//
//     bootstrap_look_up com.apple.WindowServer.HIDEvents -> rc=1102
//     SLSGetEventPort = 1000
//     mach_port_type(1000) -> rc=15 KERN_INVALID_NAME
//     CGWarpMouseCursorPosition -> cursor moved and read back
//     SLSPostEventRecord mouse record -> rc=1002 (WS reply rejection)
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
#include <math.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>

extern kern_return_t bootstrap_look_up(mach_port_t bp,
                                       const char *service_name,
                                       mach_port_t *sp);

typedef mach_port_t (*GetEventPortFn)(void);
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

int main(void) {
    // ── Phase 1: which WindowServer event services exist in bootstrap? ──
    probe_bootstrap_service("com.apple.windowserver");
    probe_bootstrap_service("com.apple.windowserver.active");
    probe_bootstrap_service("com.apple.WindowServer.HIDEvents");
    probe_bootstrap_service("com.apple.windowserver.session");
    probe_bootstrap_service("com.apple.iohid");
    probe_bootstrap_service("com.apple.CARenderServer");
    probe_bootstrap_service("macwsguide.windowserver.final-composite-replay");

    // ── Phase 2: the event port SLSGetEventPort hands out — valid? ──
    GetEventPortFn getEventPort =
        (GetEventPortFn)dlsym(RTLD_DEFAULT, "SLSGetEventPort");
    if (getEventPort) {
        mach_port_t evport = getEventPort();
        mach_port_type_t type = 0;
        kern_return_t kr = mach_port_type(mach_task_self(), evport, &type);
        printf("SLSGetEventPort = %u\n", (unsigned)evport);
        printf("mach_port_type(%u) -> rc=%d (%s) type=%#x\n",
               (unsigned)evport, kr, mach_error_string(kr), (unsigned)type);
    } else {
        printf("SLSGetEventPort symbol absent\n");
    }

    // ── Phase 3: CGS connection + display inventory ──
    MainConnectionIDFn mainConn =
        (MainConnectionIDFn)dlsym(RTLD_DEFAULT, "SLSMainConnectionID");
    uint32_t cid = mainConn ? mainConn() : 0;
    printf("SLSMainConnectionID = %u\n", cid);

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
    } else {
        printf("SLSPostEventRecord unavailable or no connection\n");
    }
    return 0;
}
