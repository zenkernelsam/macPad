// Pure acceptance/route contract for one MacWSInputRecord.
//
// The AppInput endpoint is the last authority that can reject a record that
// was addressed to the wrong process, carries a stale window identity, or
// claims a global system-surface route it does not own. Keeping that decision
// pure makes the identity invariants testable without launching AppKit, a
// WindowServer, or a device:
//
//   * target PID must equal the receiving process PID (the endpoint socket
//     path is /private/tmp/macws_app_input.<pid>.sock, so a record for a
//     different PID can only be a stale or misrouted producer);
//   * a window identity is carried as a nonzero CGWindowID in the high half
//     of sceneID (MacWSInputSceneForWindow); window-targeted kinds must name
//     one, and an exact-window record is never reinterpreted as an
//     application-scoped or global-surface record;
//   * a record carrying MacWSInputFlagGlobalSystemSurface belongs to the
//     CGS-connected system-surface owner, not to an arbitrary AppKit window.
//     The Dock-only SystemGesture stream and the relative-pointer stream
//     (which must stay exact-window) are the two explicit exceptions;
//   * frame size, finite geometry and source enumeration are envelope
//     invariants shared by every kind.
//
// AppInputBridge.m calls MacWSAppInputRouteClassify() before its
// kind-specific checks, so this header is the single source of truth for the
// invariants above. It performs no allocation and no AppKit/WindowServer
// lookup; the downstream exact-window dispatch still resolves the CGWindowID
// with -[NSApplication windowWithWindowNumber:] and drops a stale number.

#ifndef MACWS_APP_INPUT_ROUTE_H
#define MACWS_APP_INPUT_ROUTE_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "macws_host_protocol.h"

enum {
    MacWSAppInputRouteRejectMagic = -1,
    MacWSAppInputRouteRejectVersionKind = -2,
    MacWSAppInputRouteRejectTargetPID = -3,
    MacWSAppInputRouteRejectCoordinates = -4,
    MacWSAppInputRouteRejectSource = -5,
    MacWSAppInputRouteRejectWindowRequired = -6,
    MacWSAppInputRouteRejectGlobalOwner = -7,

    // Accepted routes. ProcessScoped is an untargeted/application-level
    // record (quit, reopen, initial window); ExactWindow names a live
    // CGWindowID; GlobalSystemSurface names a CGS system-surface owner.
    MacWSAppInputRouteProcessScoped = 0,
    MacWSAppInputRouteExactWindow = 1,
    MacWSAppInputRouteGlobalSystemSurface = 2,
};

static inline bool MacWSAppInputRouteAccepted(int disposition) {
    return disposition >= 0;
}

// Kinds whose consumer resolves an exact per-window AppKit target. A zero
// window ID here cannot be repaired downstream without guessing ownership.
static inline bool MacWSAppInputRouteKindRequiresExactWindow(uint16_t kind) {
    return kind == MacWSInputKindConfigureWindow ||
           kind == MacWSInputKindCloseWindow ||
           kind == MacWSInputKindRelativePointer;
}

// Classify one wire record. `selfPID` is the receiving process
// (getpid()), `dockEndpoint` is true only in the Dock process, and
// `outWindowID` (optional) receives the decoded exact window, or 0 for an
// application-scoped/global record. A negative return is a rejection reason;
// a nonnegative return is the accepted route.
static inline int MacWSAppInputRouteClassify(
        const MacWSInputRecord *record, int32_t selfPID,
        bool dockEndpoint, uint32_t *outWindowID) {
    if (outWindowID) *outWindowID = 0;
    if (!record || record->magic != MACWS_INPUT_MAGIC)
        return MacWSAppInputRouteRejectMagic;
    if (!MacWSInputVersionSupportsKind(record->version, record->kind))
        return MacWSAppInputRouteRejectVersionKind;
    if (record->targetPID != selfPID)
        return MacWSAppInputRouteRejectTargetPID;
    if (record->frameWidth == 0 || record->frameHeight == 0 ||
        !isfinite(record->x) || !isfinite(record->y) ||
        !isfinite(record->altitude) || !isfinite(record->azimuth) ||
        !isfinite(record->tiltX) || !isfinite(record->tiltY) ||
        record->tiltX < -1.0f || record->tiltX > 1.0f ||
        record->tiltY < -1.0f || record->tiltY > 1.0f)
        return MacWSAppInputRouteRejectCoordinates;
    if (record->source > MacWSInputSourceMax)
        return MacWSAppInputRouteRejectSource;

    uint32_t windowID = MacWSInputWindowIDForScene(record->sceneID);
    if (outWindowID) *outWindowID = windowID;
    bool globalSurface =
        (record->flags & MacWSInputFlagGlobalSystemSurface) != 0;

    if (record->kind == MacWSInputKindRelativePointer) {
        // Relative motion belongs to the exact focused window; a global
        // surface record would silently widen it to the whole desktop.
        if (globalSurface) return MacWSAppInputRouteRejectGlobalOwner;
        if (windowID == 0) return MacWSAppInputRouteRejectWindowRequired;
        return MacWSAppInputRouteExactWindow;
    }
    if (record->kind == MacWSInputKindSystemGesture) {
        // Only Dock consumes the navigation gesture stream, and only from
        // the global pointer surface (a real trackpad-style gesture).
        if (!globalSurface || !dockEndpoint)
            return MacWSAppInputRouteRejectGlobalOwner;
        return MacWSAppInputRouteGlobalSystemSurface;
    }
    // Exact-window kinds are checked BEFORE the generic global branch. A
    // ConfigureWindow/CloseWindow record is addressed to one real AppKit
    // window; GlobalSystemSurface is incompatible with that contract even
    // when a window ID is also present. Letting the flag win would accept a
    // zero-window control record that the broker already rejects and that
    // the consumer cannot repair. Regressing this order is caught by the
    // focused route test (window zero/nonzero x flag on/off).
    if (MacWSAppInputRouteKindRequiresExactWindow(record->kind)) {
        if (globalSurface) return MacWSAppInputRouteRejectGlobalOwner;
        if (windowID == 0) return MacWSAppInputRouteRejectWindowRequired;
        return MacWSAppInputRouteExactWindow;
    }
    if (globalSurface) return MacWSAppInputRouteGlobalSystemSurface;
    return windowID != 0 ? MacWSAppInputRouteExactWindow
                         : MacWSAppInputRouteProcessScoped;
}

#endif
