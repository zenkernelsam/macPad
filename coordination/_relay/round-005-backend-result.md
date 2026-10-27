# Backend R005 result — exact libquarantine input; IconServices and pacing evidence

Date: 2026-10-18 (Asia/Hong_Kong). Scope: `round-005-to-backend.md`,
`reviews/R007-frontend-R004-backend-20261018.md` and
`reviews/share-update-priorities-20261018.md`. Read-only: no shared
source, tests, build inputs or Git changed; no package/build/dependency
install; no setter, kernel write, entitlement expansion, TCC grant/db
edit or checker override. The external WS generation (tccd 50427,
WindowServer 50574, started 16:18:02/16:18:08) was identified and left
running and unmodified. No owned helper was needed in this packet; the
finite spindump process ended.

## 1. Exact responsibility provider input (FACT)

Extracted from the Ventura rootfs cache with the project’s extractor and
preserved under ignored analysis inputs:

| Item | Value |
| --- | --- |
| Provider | `/usr/lib/system/libquarantine.dylib` |
| UUID | `A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3` |
| Full image (host) | `analysis/r005-responsibility-inputs/libquarantine.dylib` (1.3 GB) |
| Full SHA-256 | `f5989c8f4afc9985e95795bf683679eed629af8b5ae288a0fb87317ba0e844f2` |
| __TEXT | vmaddr `0x18b31f000`, fileoff `0x0`, size `0x3000` |
| Getter | file offset `0x1b00` (runtime base `0x1a6d5f000` + `0x1b00`) |
| Getter bytes | `7f2303d5ffc300d1fd7b02a9fd830091000440ade00700ade003009104000094` (`pacibsp; sub sp,sp,#0x30; stp x29,x30,...`) |

Direct dependencies (`LC_LOAD_DYLIB`): libsystem_kernel, libsystem_malloc,
libsystem_platform, libsystem_pthread, libsystem_c, libdyld, libdispatch,
libsystem_blocks. Full images (1.3–1.5 GB each) are preserved on the
device at
`/var/mobile/macws_r005_evidence_20261018/responsibility-inputs/deps/`
with UUIDs and full hashes; 32 KB header/code slices and the complete
metadata are on the host at
`analysis/r005-responsibility-inputs/slices/` and `metadata.json`.
No Apple binary is staged in Git (`analysis/` is ignored).

**Tccd provenance correction (FACT):** the Ventura tccd
(`/var/mnt/rootfs/.../tccd`, fat `42c388e8…`, arm64e thin `039f64b7…`,
UUID `9845D17F…`) imports
`_responsibility_get_responsible_audit_token_for_audit_token` from
libSystem. The native iOS tccd
(`/System/Library/PrivateFrameworks/TCC.framework/Support/tccd`,
sha256 `67669ed7…`, arm64e, UUID `0120CE08-6868-352E-B7EC-819850D57587`,
CDHash `1ba18ff5…`) has **no responsibility imports** (`nm -m` and
`dyld_info -imports`). R004’s import metadata therefore described the
Ventura tccd only. The failed user-space `dlsym`/flat import in R004 is a
FACT about our helper; “native platform/private gating” remains THEORY,
not established.

## 2. IconServices crash audit (FACT, root cause)

Real recurring reports (15:49:51, 16:10:16, 16:10:58, 16:19:50) are
`EXC_CRASH/SIGABRT` with faulting thread
`com.apple.MPS.MPSLibrary.preheat`. The exact agent stderr assertion:

```
MPSLibrary.mm:485: failed assertion `MPSLibrary::MPSKey_Create internal
error: Unable to get MPS kernel F11H. Error: Compiler encountered an
internal error'
```

Faulting stack: `abort → __assert_rtn → MTLReportFailure.cold.1 →
MTLReportFailure → MPSLibrary::MPSKey_Compile +944`; the
iconservicesagent workloop is simultaneously blocked in
`MPSLibrary::MPSKey_Compile +56` on the unfair-lock wait. This is a
**MetalPerformanceShaders kernel compile internal error**, not a
uid/gid/HOME/cache/trust failure.

Protocol/identity audit (read-only):

- `com.macwsguide.iconservicesagent` (root role): `launchdchrootexec 0 0
  /var/mnt/rootfs /System/Library/CoreServices/iconservicesagent
  runAsRoot`, uid/gid 0, Mach service
  `com.apple.macosbooter.iconservices`, `KeepAlive SuccessfulExit=false`,
  throttle 10 s; `runs=3`, last terminating signal `Abort trap: 6`.
- `com.macwsguide.iconservicesd` (session role): `launchdchrootexec 240
  240 … iconservicesd`, uid/gid 240, Mach service
  `com.apple.macosbooter.iconservices.store`; stable.
- Store `/var/mnt/rootfs/Library/Caches/com.apple.iconservices.store` is
  240:240 mode 0700; the marker is root:wheel. The agent environment has
  only `XPC_SERVICE_NAME`; `launchdchrootexec` supplies `HOME=/Users/root`
  and `TMPDIR=/tmp`. `/var/mnt/rootfs/Users/root/Library/Caches` is
  absent (observed; no `confstr` call without attaching).
- Trust: stock agent CDHash `1c0d3271…` trusted; injected
  `/usr/local/lib/libmachook.dylib` arm64e CDHash `ee0f018f…` trusted.

Consequence: the supplier UID501/GID20 recipe is an untested hypothesis
and would not address an MPS `MTLReportFailure` compile error. No
uid/runAsRoot/ownership/entitlement change was made. The current agent
PID 51072 is alive after the 16:19:50 crash; a later generation may still
hit the same MPS compile abort.

## 3. Pacing / message-hold evidence (bounded, with exact gap)

On the external generation only (not started/stopped/restarted), one
finite passive `spindump 50574 3 20` sample was taken:
`/var/mobile/macws_r005_evidence_20261018/ws-spindump-20261018.txt`
(3.8 MB, exit 0). Observations:

- WS 50574 at 0.8–1.2 % CPU; `macws_final_composite.state` is
  `state=missing producer=0 sequence=0` (no final composite/pixels).
- WS main thread is in a deep **unsymbolicated** call chain; the
  `com.apple.coreanimation.display.primary` thread is blocked on WS main
  thread `0x978af` for 142/150 samples.
- No `macws_coexist_wait_for_completion_slot` symbol was visible and no
  service request was sent to this externally owned generation. Therefore
  this packet makes **no claim** that the artificial pacing wait holds
  queued messages. The source-level wait is at `mac_hooks.m ~27319`
  (poll/usleep pacing mutex), but runtime enqueue→wait→dispatch→response
  correlation requires an owned generation and instrumentation.

## 4. Commands, exits and final state

- Cache extraction (libquarantine + 8 direct deps): all extractor exits
  0; full hashes recorded; host copy of libquarantine SHA-256 matches
  `f5989c8f…`; slice hashes match the device.
- `spindump 50574 3 20`: exit 0; 3.8 MB report archived.
- No new crash reports during R005 (latest pre-existing: the R004
  direct-import DYLD report and the 16:19 IconServices report); no
  WindowServer crash.
- Native health: tccd 883, SpringBoard 872, backboardd 905. Thermal
  nominal 34.50 °C. Both diagnostic marker namespaces absent.
- Owned cleanup: no helper/job/sampler remains; the spindump ended. The
  external generation 50427/50574 and its iconservicesagent 51072 were
  left untouched.
- Evidence: `coordination/artifacts/R005-*`; ignored analysis inputs
  `analysis/r005-responsibility-inputs/` (metadata + full libquarantine +
  slices; full dependency images on device). No Git operation.

## 5. Next minimal scope

1. **IconServices:** identify why MPS kernel `F11H` fails to compile for
   the root agent (MPS default metallib/target route vs the 22F82 AGX
   target). Prefer an exact-target MPS library/route correction; do not
   change uid/runAsRoot/ownership or widen entitlements. Validate with a
   new agent generation and the exact MPS assertion absent, then icon
   pixels/requests.
2. **Pacing:** on an owned fresh generation, add bounded read-only
   timestamps/counters around the wait at `mac_hooks.m ~27319`, send one
   finite attributable request, and correlate enqueue → WS wait/dispatch
   → response with frame sequence and CPU; no new receiver, fence or
   forced completion.
3. **Responsibility:** use the preserved libquarantine image/deps for the
   frontend’s offline ABI analysis; keep native gating as THEORY until a
   native platform client with an exact verified contract is tested.

(handover to codex now)
