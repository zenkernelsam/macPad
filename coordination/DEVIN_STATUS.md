# DEVIN_STATUS — R001 bounded evidence package

State: **READY** (bounded package complete; provider = proven NO-OP;
recommended next action = repair hook-provider invariant, see §6)

Reviewer task: `coordination/DEVIN_TASK.md` R001.

## 1. Changed files

- `misc/macos_ws_event_probe.c` — ABI audit fixes only, no new behavior:
  - `SecTaskCopyValueForEntitlement` typedef: 2-arg → 3-arg
    `(SecTaskRef, CFStringRef, CFErrorRef *)`, call sites pass NULL error.
    Previous form left x2 as register garbage → potential wild write.
  - `SLSGetEventPort` confirmed `(cid, mach_port_t *)` — runtime:
    `rc=0 port=7183 type=0x30000` (valid receive right).
  - `SLEventTapCreate` kept but **gated**: RE @0x185239a2c shows a 6-arg
    wrapper shifting x1..x5→x2..x6, inserting w1=0, forwarding to impl
    @0x185239a48 which bounds-checks (arg0≤2, arg1≤1, arg2≤1). Marked
    opt-in only; still unverified semantics.
  - Phase 9 (SLSSetLoginwindowConnection / SLSCreateLoginSession /
    SwitchUserSession mutation) now **opt-in** via argv[2]=="mutate";
    default run = read-only inventory + canonical post + scroll post.
  - Added Phase 1b (read-only): loaded SkyLight LC_UUID dump vs RE
    binary, bootstrap_look_up of tccd.
  - Header comment: per-routine error-code audit replaces the false
    blanket "1000 == CGX-OK" convention.
- `coordination/DEVIN_STATUS.md` — this file.

## 2. Environment

- Device: iPad13,11 (T8103), iPadOS 16.3 (20D47), XNU 8792.82.2.
- Rootfs: macOS 13.4.1 (22F82) at `/var/mnt/rootfs`, disk 44% used.
- SSH: helper `devssh` → `Cs-Pad-2.local` :2222 (no creds in repo).
- WindowServer for experiments: PID 42072,
  `UIKitApplication:com.macwsguide.windowserver`, `-virtualonly -daemon`.
- libmachook deployed: thin slices Oct 14 03:28 (arm64e →
  `libmachook.dylib`, arm64 → `libmachook_arm64.dylib`).

## 3. Priority finding — live MSHookFunction provider = NO-OP

Witness (provider_probe, PAC-safe read-back):

```
image=/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate
provider-bytes @0x104edce30:
  ff 83 00 d1 e0 0f 00 f9 e1 0b 00 f9 e2 07 00 f9   ; stp/sub, str x0/x1/x2
  e8 07 40 f9 c8 00 00 b4 01 00 00 14 e8 0f 40 f9   ; ldr x8,[x2]; cbz; b; ldr x8..
target-before @0x16b0338b0: 40 05 80 52 c0 03 5f d6 ...
target-after  @0x16b0338a0: 40 05 80 52 c0 03 5f d6 ...   ; IDENTICAL
orig=0x..04dcfeac same=1 target()=42 VERDICT=NO-OP
```

- libmachook imports `_MSHookFunction` bound to
  `@rpath/CydiaSubstrate.framework/CydiaSubstrate`; deployed framework
  UUID `DFF425E9-1EC1-3E58-AF90-321F521CB803`, implementation = store
  `*result=symbol`, never writes the target.
- Identical semantics to `misc/cydiasubstrate_macos.m:227` scaffold.
- Consequence: `#### CGXSYNTH-INSTALL gate=0x1977e9464 slot=0x1eaf921b8`
  in WindowServer.err (line 500468) is an **attempted** install; the
  gate's bytes were never modified. Commit 0c06f72's "gate hook verified
  installed" is hereby corrected to "install attempted; provider no-op;
  hook never ran". No `#### CGXSYNTH` line will ever appear until the
  provider is repaired.
- Cross-check that *something* hooks: `#### MACH-ALL-*` lines do stream —
  those are dyld-interpose hooks on imported `mach_msg*`, unaffected by
  the MSHookFunction failure. Only SkyLight-internal targets
  (CGXSenderCanSynthesizeEvents etc.) silently fail.

## 4. Probe ABI audit results

| Symbol | Status |
|---|---|
| SLSGetEventPort | fixed earlier; runtime rc=0+port ⇒ correct |
| SecTaskCopyValueForEntitlement | **fixed** this package (3-arg) |
| SLEventTapCreate | arity confirmed by RE (6 args, bounds checks); gated opt-in |
| SLEventTapPostEvent | unverified; opt-in only |
| SLSSetLoginwindowConnection | client stub verified msgid 0x7212 1-arg(cid); observed rc=1000 = registry second-lookup miss, NOT success |
| SLSCreateLoginSession | vtable dispatch → login.framework `LFSMCreateSessionTemporaryBridge` (in-process bridge); sid=0 observed |

Per-routine error decode (22F82 disasm, no blanket convention):
`_XSetLoginwindowConnection`: 1000=lookup miss, 1001=audit desc
mismatch, −0x12c/−0x130=request validation. `_XPostEventRecord`:
1002 = CGXSenderCanSynthesizeEvents==0. `SLSGetEventPort`: 0=success,
1000=no connection. **"1000 == CGX-OK" is rejected.**

## 5. Canonical 0x7288 post — bounded trace

- SkyLight loaded LC_UUID = `96676A53B1E03D7EB98BB73873CD1880` ==
  extracted RE binary ⇒ all image-relative offsets valid.
- Probe: cid=38479, `SLSGetEventPort → rc=0 port=7183`.
- 4 canonical `SLSPostEventRecord` calls → all `rc=1002`;
  scroll record → `rc=1000` (different branch, earlier rejection).
- WS delta (pid 42072): **exactly 5 replies `id=29421` (0x72ed)** =
  PostEventRecord reply msgid, sent to our 4+1 reply ports. Dispatch
  reached `_XPostEventRecord` and produced a reply — server alive,
  request well-formed.
- `_XPostEventRecord` disasm: reply code written from gate result;
  `CGXSenderCanSynthesizeEvents==0 → w8=#0x3ea (1002)`. Producer of
  1002 pinned to the gate's return value.
- tccd: `bootstrap_look_up com.apple.tccd → rc=0` from chroot (port is
  per-task; could not key WS-log sends to it). Probe's own
  `TCCAccessPreflightWithAuditToken({ListenEvent,PostEvent,kTCCService-…},
  self-audit) → 1` — probe entitlements DO evaluate allow; SecTask vend
  shows `kTCCServiceListenEvent` + `SystemPolicyAllFiles` in tcc.allow
  (PostEvent not present in that blob, but direct preflight returned 1).
- `SecTask.SkyLight.loginwindow → NULL` — entitlement not vended from
  signature (matches earlier finding).
- Audit context: `gCurMsgAudit` (SkyLight+0x1d8ba61b8 rel hdr) is written
  only by `PKGTransactionDecodeCAPayload` (CA transaction decode), which
  is not on the event-post path → gate's first bail (`audit NULL/size`)
  is the most likely early exit, **before** any tccd send — consistent
  with the absence of attributable TCC traffic in the delta.
  Unconfirmed inside-gate; needs the repaired provider or a debugger
  trace to discriminate `early-bail` vs `TCC-deny`.
- Gate hook telemetry (`#### CGXSYNTH`) remains impossible until the
  provider is fixed — this is now proven, not conjecture.

## 6. Rejected hypotheses / next bounded step

Rejected this package:
- INSTALL log ⇒ installed hook (provider is a no-op).
- 1000 == success/CGX-OK (per-routine error space).
- Request malformed / server not dispatching (5/5 replies observed).
- Probe ABI errors as the cause of 1002 (post identical before/after
  fixes; replies are genuine WS decisions).

Recommended next bounded step (R002 candidate):
- Repair the hook-provider invariant: implement a real trampoline
  `MSHookFunction` in `misc/cydiasubstrate_macos.m` (~50 LOC:
  vm_protect→write `ldr x17,#8; br x17; .quad` trampoline→icache flush→
  original-bytes trampoline allocation), verified by read-back of
  installed bytes + original thunk returning correct value; then let the
  existing CGXSYNTH diag fire inside gate to split early-bail vs
  TCC-deny. Alternative: bounded debugserver/lldb trace at
  gate+0 and the 1002 write site during one post.
- No constant-return hooks, no NOP patches, no TCC bypass installed.

## 7. Commands & exit codes

```
devssh uname/sw_vers/df/ps/ls …                  rc=0
xcrun clang -arch arm64e probe.c (build)         rc=0
xcrun clang -arch arm64  probe.c (build)         rc=0
set_macos_version.py probe                       rc=0
devscp probe → /tmp/macos_ws_event_probe         rc=0
ldid -S<ents> probe                              rc=0
macws_boot_trust.py --readd probe                rc=0
launchdchrootexec probe (WS down)                rc=0, full output
launchctl bootstrap system com.apple.WindowServer.plist  rc=0
launchctl kickstart -k … windowserver            rc=0, PID 42072
launchdchrootexec probe (WS up)                  rc=0, full output
WS.err delta: grep id=29421 → 5 replies          —
```

## 8. Cleanup

- No new hooks installed, no kernel writes, no exclusive display mode.
- Probe binary remains at `/var/mnt/rootfs/tmp/macos_ws_event_probe`
  (re-signed + trustcache'd; tmp, not persistent config).
- WindowServer left **stopped** via `launchctl bootout` at end of
  session to match user's standing instruction; re-run
  `launchctl bootstrap system <plist>` + kickstart to resume.
- `/tmp/macws_runtime_diagnostics` sentinel still present on device
  (needed for MACH-ALL logging); remove to silence.

## 9. Remaining uncertainties

- Inside-gate exit reason (early-bail on gCurMsgAudit vs TCC deny) —
  cannot discriminate without working hook or debugger trace.
- Whether tccd in WS namespace would even service
  `_audit_token_check_tcc_access` if reached (iOS tccd vs macOS
  semantics — service exists, semantics unknown).
- postTo direct-delivery path (0x7287) untested this round; same gate
  likely applies.

---

## R002 progress (2026-10-15) — provider repaired, gate telemetry live

### Hook provider invariant repaired

`misc/cydiasubstrate_macos.m` `MSHookFunction` replaced with a real
minimal trampoline implementation:

- 16-byte absolute-branch patch (`ldr x17,#8 / br x17 / .quad`) over the
  first four instructions; thunk page holds stolen bytes + branch back.
- **Fails closed** (`*result=NULL`, target untouched) when any stolen
  instruction is PC-relative — verified live: a 16-byte toy window that
  overlapped a neighboring `adrp` was correctly refused.
- PAC handled three places: `ptrauth_strip` on `symbol`/`replace`
  inputs, and `ptrauth_sign_unauthenticated(thunk, asia, 0)` for
  `*result` — arm64e callers invoke it via `blraaz` (zero discriminator),
  verified by probe disasm.

Runtime witness (new repo tool `misc/macos_mshook_provider_probe.c`,
run in chroot against the LIVE provider):

```
provider-image=/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate
target-before @0x100c2bd7c: ff 43 00 d1 08 05 80 52 e8 0f 00 b9 28 00 80 52
target-after  @0x100c2bd7c: 51 00 00 58 20 02 1f d6 b0 bd c2 00 01 00 00 00
orig=0xc951800100d4c000          (PAC-signed thunk)
target()=43 replace_called=1     (replace ran)
orig()=42                       (thunk re-executed stolen bytes)
VERDICT=INSTALLED
```

### Gate telemetry — first real `#### CGXSYNTH` lines in WindowServer

WindowServer 42072→42613 restarted with repaired provider; canonical
probe posts produced (delta of WindowServer.err):

```
#### CGXSYNTH cred=0x16da6b220 size=52 euid=0 pid=42636 rc=0
```

Decoded (with full gate disasm @VA 0x1853fd464):

- `gCurMsgAudit` **is populated** during event posts — the
  "PKGTransactionDecodeCAPayload-only writer → early NULL bail"
  hypothesis is **REFUTED** (cred!=NULL, size=52 ≥ 0x34 check passes).
- Audit belongs to the sender: pid=42636 = probe pid, euid=0.
- Gate body: copies audit token + rightDesc (0x1d3dcb6c8), paciza's
  check-impl pointer 0x185425da0→x2, calls dispatcher `0x1854257ec`.
- `rc=0` is the **genuine** return of the SkyLight check chain →
  `_XPostEventRecord` writes 1002.
- The constant-return `_audit_token_check_tcc_access → 1` diagnostic at
  mac_hooks.m:6024 could NOT have influenced this: `MSFindSymbol` cannot
  resolve the local `t` symbol, so the call is `MSHookFunction(NULL,…)`
  → inert. It is now additionally gated behind the runtime-diagnostics
  sentinel (it was an unconditional constant-return footgun under the
  repaired provider).
- 5× `id=29421` (0x72ed) replies again observed for 5 posts — dispatch
  confirmed end-to-end.

### What remains unresolved inside the check chain

- `0x1854257ec` dispatcher / `0x185425da4` check-impl decide `rc=0`.
  Early impl reads: descriptor flags bit31 + `+4==0x18` validate, then
  `bl 0x185358538`. Next bounded step: LOGGING wrapper (not
  constant-return) on `0x1854257ec` or the rightDesc dereference to
  identify which sub-check denies — cache-table miss vs TCC evaluation.
- Whether populating WS's process-right cache (the real fix) is via
  `kTCCServicePostEvent` grant path or the session-owner pathway is still
  open; the session registry remains empty (count=0) at this WS state.

### Housekeeping

- `WindowServer.err` diag canaries now stream (provider works) — log
  volume up; sentinel `/tmp/macws_runtime_diagnostics` still present.
- WS left STOPPED (bootout) pending review; CydiaSubstrate.old/.old2/
  .old3 kept beside framework for rollback.
