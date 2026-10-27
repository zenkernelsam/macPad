# Backend R004 result — native vs chroot responsibility getter boundary

Date: 2026-10-18 (Asia/Hong_Kong). Scope: `round-004-to-backend.md` and
`reviews/R005-frontend-R003-backend-20261018.md`. Read-only source,
tests, build inputs and Git: nothing changed. One finite diagnostic
helper (source outside shared tree) was built in native iOS and chroot
macOS variants and used only for the responsibility **getter**. No
setter, kernel write, entitlement expansion, fabricated token/session,
TCC grant/db edit, checker override or package rebuild. The device was
used exclusively for this packet; a newly appearing live GUI generation
(50427/50574) was started outside this packet and was left untouched.

## Inventory and identity (FACT)

| Item | Value |
| --- | --- |
| Device / OS | iPad13,11 (T8103), iPadOS 16.3 / 20D47, Darwin 22.3.0 |
| Rootfs | macOS 13.4.1 / 22F82 |
| tccd | fat sha256 `42c388e8…`, arm64e thin `039f64b7…`, UUID `9845D17F-0409-3884-8A17-260C729636E5` |
| Native import contract | `dyld_info -imports tccd-22F82-arm64e` → `_responsibility_get_responsible_audit_token_for_audit_token (from libSystem)`; same in `nm -m` |
| Native provider | libSystem link-time interface; not exposed to a user-space `dlsym`/direct import (details below) |
| Chroot provider | `/usr/lib/system/libquarantine.dylib`, UUID `A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3`, base `0x1a6d5f000`, symbol `0x1a6d60b00` |
| Helper source | `analysis/r004-responsibility/responsibility_probe.c`, sha256 `78ffa354…` (ignored analysis/) |
| Native helper binary | local sha256 `5e1f428f…`, arm64e, platform iOS 14.0 |
| Chroot helper binary | local sha256 `9d2db85a…`, arm64e, platform macOS 13.0 |
| Native health | tccd 883, SpringBoard 872, backboardd 905 |
| Thermal | nominal 34.59 °C at the end |

ANALYSIS INPUTS: the tccd arm64e thin (`039f64b7…`) and the cache
metadata are in the device evidence dir; text disassembly/excerpts and
the helper source are under ignored `analysis/r004-responsibility/`.
No Apple binary is placed in Git.

## 1. ABI verification before any private call (FACT)

tccd arm64e (`039f64b7…`, UUID `9845D17F…`) calls the getter at
`0x10004fb48`:

```
0x10004fb48  bl _responsibility_get_responsible_audit_token_for_audit_token
0x10004fb4c  cbz w0, 0x10004fbc4        ; success path
             ; nonzero -> request context marked failed
0x100051a54  "ERROR: responsibility_get_responsible_audit_token_for_audit_token()
             from PID[%d]: (#%d) %{public}s"  (errno + strerror)
```

The call site passes x0 = target `audit_token_t *`, x1 = output
`audit_token_t *`, x2 = 0, x3 = 0 and consumes `w0` with errno on
failure. The chroot provider's prologue
`7f2303d5 ffc300d1 fd7b02a9 …` (`pacibsp; sub sp,sp,#0x30;
stp x29,x30,[sp,#0x20]; stp x20,x19,[sp,#0x10]; …`) preserves the
arguments, and the helper's call returned a normal error rather than
crashing. No guessed setter/getter signature was used.

## 2. Native getter availability boundary (FACT, blocker)

The native iOS symbol is imported by the platform tccd, but is **not
reachable from a non-platform helper**:

- `dlsym(RTLD_DEFAULT, …)` → NULL;
- `dlopen` + `dlsym` on `/usr/lib/libSystem.B.dylib`,
  `libsystem_coreservices.dylib`, `libsystem_info.dylib`,
  `libsystem_kernel.dylib`, `libsystem_darwin.dylib` and
  `CoreServices.framework/CoreServices` → symbol not found;
- `libquarantine.dylib` and `libresponsibility.dylib` are absent from
  the iOS cache (`errno=2`, not in dyld cache);
- a direct import with `-Wl,-undefined,dynamic_lookup` fails at dyld:
  `symbol not found in flat namespace
  '_responsibility_get_responsible_audit_token_for_audit_token'`
  (`resp_native_direct_r004-2026-10-18-162405.ips`, EXC_CRASH/SIGABRT,
  namespace DYLD).

This is an availability/gating boundary: the getter is reserved for
platform/private clients. We did **not** add platform or responsibility
entitlements; doing so would violate the packet. Consequently no
native-process getter control is possible with this helper.

## 3. Chroot getter controls (FACT)

The Ventura getter is available via `RTLD_DEFAULT`; `dladdr` identifies
the provider as `/usr/lib/system/libquarantine.dylib`, UUID
`A2D17FF6-CBC6-3D19-89E1-F5E57191E8A3`, symbol `0x1a6d60b00`.

Control A — live chroot self token:

```
SELF/TARGET=ffffffff 00000000 ... 0000ca14 00000000 0001827b pidversion=98939
RC=-1 ERRNO=103 ERRSTR="Policy not found"
OUT=all zero
```

Control B — **live native iOS token** passed unchanged to the chroot
getter. A native holder (PID 51948, uid 0) was kept alive during the
call; its token was
`ffffffff 00000000 ... 0000caec 00000000 00018427 pidversion=99367`.
The chroot getter returned the same:

```
RC=-1 ERRNO=103 ERRSTR="Policy not found"
OUT=all zero
```

The identical target token carries its real pid/pidversion; both
targets are live/owned. The native side of the same token cannot be
tested because the native getter is gated (section 2).

## 4. Boundary interpretation and limits

- The Ventura getter is reachable and returns ENOPOLICY (103) for both
  a live chroot token and a live native token.
- The native getter is unavailable to a non-platform helper; therefore
  this packet cannot state that a native implementation would succeed
  or that it would return a different result.
- **Do not infer a missing kernel policy from ENOPOLICY.** The observed
  result is consistent with either (a) the tested targets lacking the
  responsibility relationship the Ventura lookup expects, or (b) the
  Ventura wrapper/lookup contract not resolving these identities. The
  evidence does not distinguish them.
- A selector/request-layout comparison between native and Ventura
  implementations could not be performed because the native getter is
  unavailable; the chroot provider prologue and the tccd caller
  contract are preserved as the reproducible inputs.
- No setter, kernel write, fabricated token/session, grant/db edit,
  checker override or forced result was used.

## 5. Commands, exits and cleanup

- Helper builds: native iOS arm64e platform 14.0 and chroot macOS arm64e
  platform 13.0, exit 0; both signed with the existing project profile
  and trustcache-admitted (CDHash entries inert after removal).
- Native self run: exit 4 (`SYMBOL_MISSING`); direct import: exit 134
  (dyld symbol not found, expected blocker).
- Chroot self and chroot-on-live-native-token runs: exit 0, each
  `RC=-1 ERRNO=103`.
- Helper binaries moved from active paths to
  `/var/mobile/macws_r004_evidence_20261018/`; no helper/job/sampler
  processes remain. No new launchd job was created (unlike R003).
- Markers: both diagnostic namespaces absent. Native health: tccd 883,
  SpringBoard 872, backboardd 905. Final thermal nominal 34.59 °C.
- A live GUI generation (tccd 50427, WS 50574, ~8 min old) appeared
  outside this packet while it ran; it was not started, stopped or
  mutated here and is documented as external state.
- Crash/resource delta in the run window: the expected
  `resp_native_direct_r004` DYLD SIGABRT report; a
  `WindowServer.wakeups_resource` bug_type 142 resource report; no new
  WindowServer crash report. (The `.wakeups_resource` report is not a
  crash.)

## 6. Bounded next proposal

If a native getter control is required, the next packet should first
obtain an exact platform-client/entitlement contract review (frontend’s
offline responsibility/audit analysis) and then use **an existing
entitled native client or a minimal exact-entitlement test**, not broad
rights. Test the same live token on both sides and accept only raw
return/errno/output plus image UUIDs. Do not add a setter, kernel write
or global checker override. The chroot control in this packet is
reproducible with the archived helper source and the device cache
metadata.

Local evidence: `coordination/artifacts/R004-responsibility-controls-20261018.txt`
and `coordination/artifacts/R004-responsibility-boundary-20261018.json`.
Ignored analysis inputs: `analysis/r004-responsibility/` (helper source,
metadata, hashes). No source/test/build/Git change was made.

(handover to codex now)
