# R004B evidence archive — 2026-10-14

Bounded TLS loader-observation package around the real
`CGXSenderCanSynthesizeEvents` gate.  Diagnostic-only; not an acceptance
claim.  Raw JSON records live alongside this file.

## Binary identity (pinned)

- RE input: `/Users/ciscohe/Desktop/macPad/analysis/SkyLight_22F82`
- LC_UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`
- sha256 `13d7a0ca010a9808234c71c34fd650b26329de99d1aceaeb5ddbb58f8b906c61`
- gate VA `0x1853fd464` (image base `0x1850e5000`), dispatcher
  `0x1854257ec`, impl `0x185425da4` — all RE'd on this build.

## Regression evidence: dlopen interpose breaks @loader_path

- First R004B deploy carried `DYLD_INTERPOSE(MacWSDiagnosticDlopen, dlopen)`.
- `macos_gui.sh start coexist --no-terminal` FAILED at legacy
  launchservicesd (`artifacts/r004b-gui-start.json`, MACPAD_SSH_EXIT=1):
  `launchservicesd loader: dlopen failed: dlopen(@loader_path/launchservicesd.dylib,
  0x0008): tried: '/usr/local/lib/launchservicesd.dylib' (no such file)`.
  The file exists at `/System/Library/CoreServices/launchservicesd.dylib`
  (`r004b-lsd-blocker.json`); `/usr/local/lib` is libmachook's own
  directory → dyld resolved `@loader_path` against the interposer's image.
- Fix: dlopen interposer removed from `MacWSLoaderObservation.m`
  (tail comment explains why; dlsym kept).  Rebuilt, redeployed,
  `start coexist` then completed (`r004b-gui-start2.json`, exit 0,
  WS pid 51644, watchdog pid 51106).
- Note: `dlsym` is NOT generally caller-independent either
  (RTLD_NEXT/RTLD_SELF use the caller's image list) — only the
  pre-existing interposer's unchanged behavior is test-covered here.

## Gate delta — diagnostic WS generation

Production `macos_gui.sh start` strips `/tmp/macws_runtime_diagnostics`
(diagnostics opt-in OFF) → `diag=0` → gate hook never installs, and the
INSTALL fprintf is suppressed by the stderr gate.  To observe, the
sentinel was armed in the chroot namespace and ONE diagnostic generation
was booted through the project job:

- `r004b-sentinel.json` — sentinel armed (later stripped by pipeline;
  re-armed before kickstart)
- `r004b-diag-gen.json` — `launchctl kickstart -k
  user/501/UIKitApplication:com.macwsguide.windowserver` → WS pid
  **53005**, 23:54 local; `CGXSYNTH-INSTALL gate=0x1977e9464
  slot=0x1eaf921b8 hdr=0x1974d1000 isWS=1 diag=1 permitFn=0x104cfec24
  permit=0` (hook installed).
- `r004b-canonical-post2.json` — one `macos_ws_event_probe post`
  (PROBE_RC=0, probe post-sign `da04a959…`):
  ```
  #### CGXSYNTH cred=0x16b177220 size=52 euid=0 pid=53213 rc=0 seq=1
  #### CGXSYNTH cred=0x16b177228 size=52 euid=0 pid=53213 rc=0 seq=2
  ```
  LOADER-OBS count: **0** — no public `dlsym` observed inside either gate
  execution.  `SLSPostEventRecord(canonical) -> rc=1002` unchanged.
  Verdict: INCONCLUSIVE (absence in window only; dlopen unobserved).

## Build / deploy hashes

- fat `.theos/obj/libmachook.dylib` (arm64+arm64e) `f1927e218857…`
- staged pre-sign slices: arm64e `fe93559c…`, arm64 `d81e2d0c…`
  (LC_BUILD_VERSION macOS 13.0 via set_macos_version.py)
- device post-sign (all six install paths identical pairs):
  arm64e `c408dc94cee6…`, arm64 `56224d257e50…` — see
  `r004b-deploy3.json`, `r004b-postsign-hashes.json`
- probe: pre-sign `b33d4e4a…` → post-sign `da04a959…`
- trust: `BOOT-TRUST {"added":1}` per file ×6 (`r004b-deploy3.json`)
- baseline `chroot /var/mnt/rootfs /bin/echo HI → HI` rc=0

## Cleanup state

- `macos_gui.sh stop` → iOS restored (`r004b-stop.json`, exit 0)
- `/var/mnt/rootfs/tmp/macws_runtime_diagnostics` AND
  `/tmp/macws_runtime_diagnostics` both absent (`r004b-cleanup.json`)
- no WindowServer/probe processes remain

## Fixture contract result

`misc/test_loader_observation.py` — 26/26 ALL-PASS on this source
(separate libobs.dylib + fixture exe; real interpose path exercised for
dlsym; dlopen notes are direct-call only by design).
