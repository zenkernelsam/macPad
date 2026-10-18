"""Exercise the AppInput exact-PID/exact-window route contract.

The route decision used by AppInputBridge.m is pure C in
include/macws_app_input_route.h. These cases pin the invariants a producer
must satisfy before a record can reach an AppKit target:

  * the record must be addressed to the receiving PID;
  * window-targeted kinds must carry a nonzero CGWindowID in sceneID;
  * a global system-surface record is never treated as an exact window;
  * the Dock-only SystemGesture stream cannot be claimed by another process;
  * relative-pointer motion cannot widen to the global surface.

No AppKit, WindowServer or device is involved.
"""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
APP_INPUT = ROOT / "libmachook/AppInputBridge.m"
ROUTE_HEADER = ROOT / "include/macws_app_input_route.h"


class AppInputRouteContractTests(unittest.TestCase):
    def test_route_classification_matrix(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler unavailable")
        source = r'''
#include "macws_app_input_route.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SELF_PID 4242

static MacWSInputRecord base(void) {
    MacWSInputRecord r;
    memset(&r, 0, sizeof(r));
    r.magic = MACWS_INPUT_MAGIC;
    r.version = MacWSInputWireVersionForKind(MacWSInputKindTap);
    r.kind = MacWSInputKindTap;
    r.targetPID = SELF_PID;
    r.frameWidth = 1000;
    r.frameHeight = 800;
    r.x = 10.0f;
    r.y = 20.0f;
    r.source = MacWSInputSourceFinger;
    return r;
}

static int classify(const MacWSInputRecord *r, bool dock, uint32_t *w) {
    return MacWSAppInputRouteClassify(r, SELF_PID, dock, w);
}

int main(void) {
    uint32_t w = 99;

    // Application-scoped record: no window identity is required for quit.
    MacWSInputRecord r = base();
    r.kind = MacWSInputKindPerformQuit;
    r.version = MacWSInputWireVersionForKind(r.kind);
    r.targetPID = SELF_PID;
    assert(classify(&r, false, &w) == MacWSAppInputRouteProcessScoped);
    assert(w == 0);

    // Exact window: the CGWindowID is the high half of the encoded scene.
    r = base();
    r.kind = MacWSInputKindTap;
    r.version = MacWSInputWireVersionForKind(r.kind);
    r.sceneID = MacWSInputSceneForWindow(73, 0x20u);
    assert(classify(&r, false, &w) == MacWSAppInputRouteExactWindow);
    assert(w == 73);
    assert(MacWSInputModifiersForScene(r.sceneID) == 0x20u);

    // A record addressed to another process is stale/misrouted, even with a
    // perfectly valid window identity.
    r.targetPID = SELF_PID + 1;
    assert(classify(&r, false, &w) == MacWSAppInputRouteRejectTargetPID);
    assert(w == 0);
    r.targetPID = SELF_PID;

    // Magic/version/kind envelope.
    r = base();
    r.magic = 0;
    assert(classify(&r, false, &w) == MacWSAppInputRouteRejectMagic);
    r = base();
    r.version = 1;
    assert(classify(&r, false, &w) == MacWSAppInputRouteRejectVersionKind);
    r = base();
    r.source = (uint16_t)(MacWSInputSourceMax + 1);
    assert(classify(&r, false, &w) == MacWSAppInputRouteRejectSource);

    // Geometry envelope: frame and non-finite coordinates are rejected.
    r = base();
    r.frameWidth = 0;
    assert(classify(&r, false, &w) == MacWSAppInputRouteRejectCoordinates);
    r = base();
    r.x = (float)__builtin_nan("");
    assert(classify(&r, false, &w) == MacWSAppInputRouteRejectCoordinates);
    r = base();
    r.tiltX = 1.5f;
    assert(classify(&r, false, &w) == MacWSAppInputRouteRejectCoordinates);

    // Window-targeted kinds cannot be repaired downstream without an ID.
    r = base();
    r.kind = MacWSInputKindConfigureWindow;
    r.version = MacWSInputWireVersionForKind(r.kind);
    assert(classify(&r, false, &w) ==
           MacWSAppInputRouteRejectWindowRequired);
    r = base();
    r.kind = MacWSInputKindCloseWindow;
    r.version = MacWSInputWireVersionForKind(r.kind);
    assert(classify(&r, false, &w) ==
           MacWSAppInputRouteRejectWindowRequired);

    // R003-F1 regression: GlobalSystemSurface must never widen an
    // exact-window kind. Both kinds are exercised with window zero/nonzero
    // and flag off/on. A flagged record is rejected even when it names a
    // window, because the flag is incompatible with the exact-window route.
    for (int flagged = 0; flagged < 2; flagged++) {
        for (int windowed = 0; windowed < 2; windowed++) {
            uint32_t expectedWindow = windowed ? 73u : 0u;

            r = base();
            r.kind = MacWSInputKindConfigureWindow;
            r.version = MacWSInputWireVersionForKind(r.kind);
            r.sceneID = MacWSInputSceneForWindow(expectedWindow, 0);
            if (flagged) r.flags |= MacWSInputFlagGlobalSystemSurface;
            if (flagged) {
                assert(classify(&r, false, &w) ==
                       MacWSAppInputRouteRejectGlobalOwner);
            } else if (windowed) {
                assert(classify(&r, false, &w) ==
                       MacWSAppInputRouteExactWindow);
                assert(w == 73u);
            } else {
                assert(classify(&r, false, &w) ==
                       MacWSAppInputRouteRejectWindowRequired);
            }

            r = base();
            r.kind = MacWSInputKindCloseWindow;
            r.version = MacWSInputWireVersionForKind(r.kind);
            r.sceneID = MacWSInputSceneForWindow(expectedWindow, 0);
            if (flagged) r.flags |= MacWSInputFlagGlobalSystemSurface;
            if (flagged) {
                assert(classify(&r, false, &w) ==
                       MacWSAppInputRouteRejectGlobalOwner);
            } else if (windowed) {
                assert(classify(&r, false, &w) ==
                       MacWSAppInputRouteExactWindow);
                assert(w == 73u);
            } else {
                assert(classify(&r, false, &w) ==
                       MacWSAppInputRouteRejectWindowRequired);
            }
        }
    }

    // The fix must not over-restrict genuine global pointer records.
    r = base();
    r.kind = MacWSInputKindTap;
    r.version = MacWSInputWireVersionForKind(r.kind);
    r.flags |= MacWSInputFlagGlobalSystemSurface;
    r.sceneID = MacWSInputSceneForWindow(0, 0);
    assert(classify(&r, false, &w) ==
           MacWSAppInputRouteGlobalSystemSurface);
    r.sceneID = MacWSInputSceneForWindow(44, 0);
    assert(classify(&r, false, &w) ==
           MacWSAppInputRouteGlobalSystemSurface);

    r = base();
    r.kind = MacWSInputKindRelativePointer;
    r.version = MacWSInputWireVersionForKind(r.kind);
    r.source = MacWSInputSourceIndirectPointer;
    assert(classify(&r, false, &w) ==
           MacWSAppInputRouteRejectWindowRequired);

    // Relative pointer with an exact window is accepted, but it may not
    // widen to the global system surface.
    r.sceneID = MacWSInputSceneForWindow(9, 0);
    assert(classify(&r, false, &w) == MacWSAppInputRouteExactWindow);
    assert(w == 9);
    r.flags |= MacWSInputFlagGlobalSystemSurface;
    assert(classify(&r, false, &w) == MacWSAppInputRouteRejectGlobalOwner);

    // System gesture is the Dock-only global stream.
    r = base();
    r.kind = MacWSInputKindSystemGesture;
    r.version = MacWSInputWireVersionForKind(r.kind);
    assert(classify(&r, false, &w) == MacWSAppInputRouteRejectGlobalOwner);
    assert(classify(&r, true, &w) == MacWSAppInputRouteRejectGlobalOwner);
    r.flags |= MacWSInputFlagGlobalSystemSurface;
    assert(classify(&r, true, &w) ==
           MacWSAppInputRouteGlobalSystemSurface);
    assert(classify(&r, false, &w) == MacWSAppInputRouteRejectGlobalOwner);

    // A global-surface pointer record may name a captured system layer
    // window, but it remains a global record rather than an exact AppKit
    // window claim.
    r = base();
    r.flags |= MacWSInputFlagGlobalSystemSurface;
    r.sceneID = MacWSInputSceneForWindow(44, 0);
    assert(classify(&r, false, &w) ==
           MacWSAppInputRouteGlobalSystemSurface);
    assert(w == 44);

    // Fullscreen global pointer: encoded window zero with the flag set.
    r = base();
    r.flags |= MacWSInputFlagGlobalSystemSurface;
    r.sceneID = MacWSInputSceneForWindow(0, 0);
    assert(WINDOW_ID_FOR_SCENE(r.sceneID) == 0);
    assert(classify(&r, false, &w) ==
           MacWSAppInputRouteGlobalSystemSurface);
    assert(w == 0);

    puts("app-input-route PASS: pid/window/global classification matrix");
    return 0;
}
'''
        source = source.replace(
            "WINDOW_ID_FOR_SCENE(", "MacWSInputWindowIDForScene(")
        with tempfile.TemporaryDirectory(prefix="macws-app-route-") as tmp:
            c = pathlib.Path(tmp) / "test.c"
            binary = pathlib.Path(tmp) / "test"
            c.write_text(source)
            subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT / "include"), str(c), "-o", str(binary)],
                check=True, capture_output=True)
            result = subprocess.run([str(binary)], check=True,
                                    capture_output=True, text=True,
                                    timeout=10)
            self.assertIn("app-input-route PASS", result.stdout)

    def test_production_uses_the_contract_and_keeps_exact_window_lookup(self):
        source = APP_INPUT.read_text()
        self.assertIn('#import "macws_app_input_route.h"', source)
        start = source.index("static BOOL MacWSInputRecordIsValid(")
        end = source.index("\n}\n", start)
        validity = source[start:end]
        self.assertIn("MacWSAppInputRouteClassify(", validity)
        self.assertIn("MacWSAppInputRouteAccepted(", validity)
        # The identity rule must not be duplicated (and therefore able to
        # drift) inside the ObjC validation wrapper.
        self.assertNotIn("record->targetPID != getpid()", validity)
        self.assertNotIn(
            "MacWSInputWindowIDForScene(record->sceneID) != 0", validity)
        # The pure contract classifies; it must not replace the live
        # per-process CGWindowID -> NSWindow resolution that proves the
        # window still exists in this application.
        self.assertIn(
            'sel_registerName("windowWithWindowNumber:")', source)

    def test_contract_header_is_self_contained(self):
        text = ROUTE_HEADER.read_text()
        self.assertIn("static inline int MacWSAppInputRouteClassify(", text)
        self.assertIn("record->targetPID != selfPID", text)
        self.assertIn("MacWSInputWindowIDForScene(record->sceneID)", text)
        self.assertIn("MacWSInputFlagGlobalSystemSurface", text)
        # R003-F1: the exact-window branch must precede the generic global
        # branch. Behavior is exercised above; this pins the source order so
        # a future edit cannot silently reintroduce the widening.
        body = text[text.index(
            "static inline int MacWSAppInputRouteClassify("):]
        exact_branch = body.index(
            "MacWSAppInputRouteKindRequiresExactWindow(record->kind)")
        generic_global = body.index(
            "if (globalSurface) return "
            "MacWSAppInputRouteGlobalSystemSurface;")
        self.assertLess(exact_branch, generic_global)
        # No framework dependency: this must stay usable from a plain C test.
        for forbidden in ("#import", "Foundation", "objc/"):
            self.assertNotIn(forbidden, text)


if __name__ == "__main__":
    unittest.main()
