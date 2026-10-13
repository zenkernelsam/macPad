#!/usr/bin/env python3
"""Send one MacWSInputRecord to macwsinputd — minimal one-shot injector.

This is the standalone version of the record format used by
misc/host_input_matrix.py, reduced to "fire one record from the command
line" for interactive bring-up. Run it on the iPadOS side:

    python3 misc/macws_inject_record.py tap 15 8            # menubar tap
    python3 misc/macws_inject_record.py tap 683 990         # Dock icon tap
    python3 misc/macws_inject_record.py activate 15 8       # ActivateTarget
    python3 misc/macws_inject_record.py scroll 683 400 --dy -120
    python3 misc/macws_inject_record.py tap 400 300 --window 12 --target 78777

Wire format — MacWSInputRecord, 84 bytes packed
(include/macws_host_protocol.h:446):

    <IHHQdfffIIIiHHIffffII
    magic   version kind    sceneID timestamp x      y    pressure
    contactID frameW frameH targetPID source  flags  buttons
    altitude azimuth tiltX   tiltY   sampleSeq reserved

sceneID encoding (MacWSInputSceneForWindow): bit31 marker | windowID<<32 |
modifier flags in bits 0..30.  Untargeted desktop records keep the marker
clear and sceneID zero — the broker hit-tests every live endpoint.

macwsinputd validates: magic, version-per-kind, finite x/y, non-zero frame,
targetPID >= 0, source <= max, tilt in [-1,1].  An 84-byte record with a
wrong field layout arrives as kind=0 / point=(0,0) and is REJECTED — check
macwsdisplayd/inputd stderr for the telltale line first:

    MACWS-INPUT REJECT bytes=84 magic=0x4d574556 version=9 kind=0 ...
"""

import argparse
import os
import socket
import struct
import time

MAGIC = 0x4D574556
VERSION = 9
RECORD = struct.Struct("<IHHQdfffIIIiHHIffffII")
WINDOW_SCENE_FLAG = 0x80000000

KINDS = {
    "down": 1, "move": 2, "up": 3, "cancel": 4, "hover": 5,
    "tap": 6, "probe": 7, "activate": 8, "menuhover": 10,
    "keydown": 11, "keyup": 12, "secondary": 13, "scroll": 14,
    "relative": 27,
}
SOURCES = {
    "unknown": 0, "finger": 1, "pencil": 2, "indirect": 3,
    "hwkey": 4, "swkey": 5, "vnc": 6, "interop": 7,
}
SOCK = "/var/mnt/rootfs/private/tmp/macws_host_input.sock"


def main():
    p = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="\n".join(__doc__.splitlines()[1:14]))
    p.add_argument("kind", choices=sorted(KINDS))
    p.add_argument("x", type=float, help="point.x in frame coords")
    p.add_argument("y", type=float, help="point.y")
    p.add_argument("--sock", default=SOCK)
    p.add_argument("--source", default="vnc", choices=sorted(SOURCES))
    p.add_argument("--target", type=int, default=0, help="targetPID (0=auto)")
    p.add_argument("--window", type=int, default=0,
                   help="exact CGWindowID (encodes sceneID marker)")
    p.add_argument("--modifiers", type=lambda s: int(s, 0), default=0)
    p.add_argument("--flags", type=lambda s: int(s, 0), default=0x40,
                   help="default 0x40 = GlobalSystemSurface")
    p.add_argument("--frame", default="1366x1024",
                   help="frame WxH the coords live in")
    p.add_argument("--pressure", type=float, default=0.0,
                   help="pressure / scroll vertical delta / gesture value")
    p.add_argument("--buttons", type=lambda s: int(s, 0), default=0)
    p.add_argument("--seq", type=int, default=0,
                   help="sampleSequence (0 = no sequence)")
    args = p.parse_args()

    fw, fh = (int(v) for v in args.frame.split("x"))
    scene_id = 0
    if args.window:
        scene_id = (WINDOW_SCENE_FLAG | (args.window << 32) |
                    (args.modifiers & 0x7fffffff))
    rec = RECORD.pack(
        MAGIC, VERSION, KINDS[args.kind], scene_id,
        time.time(),
        args.x, args.y, args.pressure,
        0,                    # contactID
        fw, fh,               # frameWidth/frameHeight (uint32)
        args.target,          # targetPID (int32)
        SOURCES[args.source], # source (uint16)
        args.flags & 0xffff,  # flags (uint16)
        args.buttons,         # buttons (uint32)
        0.0, 0.0,             # altitude, azimuth
        0.0, 0.0,             # tiltX, tiltY
        args.seq,             # sampleSequence
        0,                    # reserved
    )
    assert len(rec) == 84, len(rec)

    s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
    bind_path = f"/tmp/macws_inject_{os.getpid()}"
    s.bind(bind_path)  # recv path requires a bound peer name on this platform
    try:
        sent = s.sendto(rec, args.sock)
    finally:
        s.close()
        try:
            os.unlink(bind_path)
        except OSError:
            pass
    print(f"sent kind={KINDS[args.kind]} ({args.kind}) at "
          f"({args.x},{args.y}) flags={args.flags:#x} "
          f"target={args.target} window={args.window} "
          f"source={SOURCES[args.source]} bytes={sent}")


if __name__ == "__main__":
    main()
