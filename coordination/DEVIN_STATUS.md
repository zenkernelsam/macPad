# DEVIN_STATUS — current delivery

State: **R004A audit/docs package** (active).  R003 diagnostic
checkpoint delivered at `0f5b8f4` (+ artifact commits) — reusable
bounded evidence, NOT an acceptance claim; `reviews/R003-final-20261014.md`
lists its remaining scope.  R004A work below is docs/audit debt only.

## R004A in progress

- `misc/audit_runtime_switches.py`: `analysis/` (git-ignored read-only
  reference tree) excluded from scanning by directory name; real
  project sources outside it are still scanned.  `source_files(root)`
  now takes an injectable root for fixture tests.
- `misc/test_runtime_gate_inventory.py`: new fixture test proves
  reference-only switches are dropped while a real untracked source is
  still detected; 19/19 pass.
- `docs/runtime-switches.tsv`: 23 real-tree env names + the
  `/tmp/macws_ws_dispatch_witness` flag classified from their actual
  consumers (all `off` diagnostics/standalone-probe controls, except
  `MACWS_SKYLIGHT_MANUAL_POP` which is production-default-on like the
  other `getenv("X") && strcmp(X,"0")` opt-outs).  Cleanup helper
  regenerated via `--write-cleanup` and shell-checked.
- Audit: `runtime-switch audit OK: 309 source/plist env names, 86
  source flag files, 467 total recorded entries` (exit 0).
- Doc corrections in `AGENTS.md`, `PROJECT-ARCHAEOLOGY.md`,
  `ventura-22F82-session-reproduction-20261013.md`,
  `ventura-22F82-bringup-state.md` and the 10-13 evidence doc retract:
  event-port-invalid, `1000=success`, INSTALL-proves-hook,
  always-NULL-audit, probe-globals-imply-WS-state.  Chroot-vs-iOS
  sentinel namespace recorded; private host refs replaced with
  placeholders.
- `deploy-probe-r003c.json`: embedded base64 payload replaced with an
  omitted-payload descriptor preserving `command_sha256` and the real
  `exit 127`.
- Not done in this package: no device mutation, no new private-offset
  reads, no behavior changes; IDA-dependent static RE stays pending.

---

## Current R003 delivery summary

- Provider: `MSHookFunction` real trampoline + `MACWSHookPermitV2`
  (4-arg, dlsym capability lookup — old providers fail closed).
  Identity pin: caller-supplied expected UUID + image offset + exact
  16-byte prologue.  All refusal/failure paths return the callable
  original (proven: `res()=42` stripped AND raw-signed inputs).
- Event probe: default = read-only inventory; `post` = one canonical
  record only.  tapPost/tapCreate/postTo calls DISABLED.  Phase 1c
  private-slot reads are gated behind a real `memcmp` UUID check
  (`match=1` observed) — fail closed on unknown build.
- Final no-pipe records (real remote exits, full streams):
  `provider-probe-r003-final{,-orig}.json` (both exit 0,
  `VERDICT=INSTALLED`), `probe-inventory-r003-final.json` (exit 0),
  `probe-upload.json` + `probe-deploy-r003final.json` (upload hash ==
  local pre-sign `b33d4e4a…`, `install -m755`, post-sign
  `740ea29a…`).
- Baseline gate: `chroot /var/mnt/rootfs /bin/echo HI` → `HI`,
  exit 0 (raw output in `probe-deploy-r003final.json`).
- Cleanup truth (corrected): the chroot sentinel
  `/var/mnt/rootfs/tmp/macws_runtime_diagnostics` was still present —
  my earlier removal hit the iOS-namespace path only.  Now archived
  to `…/tmp/diag-archive/macws_runtime_diagnostics.r003-20261014`;
  BOTH namespaces verified absent
  (`cleanup-sentinel-r003.json`).  WS/probe processes: none.
- Git: push unauthenticated — https `could not read Username`,
  ssh `Permission denied (publickey)`, `gh` absent (recorded, not
  fabricated; no PUSH_EXIT masking).

Open/unaccepted: checker identity + rights-cache producer (needs IDA
MCP or WS-side debug — none available); input still `rc=1002`; probe
lacks `SkyLight.loginwindow` entitlement (R004 lead); probe-process
global reads do NOT describe WindowServer state.

---

# History — R001 bounded evidence package (superseded sections below)

## 1. Changed files (R001)

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

---

## R003 progress (2026-10-14/15) — permit containment + hardening

### Update — V2 contract finalized (post-addendum 22:08)

- Permit API renamed **`MACWSHookPermitV2`** (4-arg contract). The V1
  name is retired so a stale/mixed provider build resolves nothing at
  the dlsym capability lookup and fails closed instead of being called
  with a wrong signature. Probe exit 8 covers an old-provider mix.
- **Every** non-installed path now returns a callable original
  (`*result = symbol` as passed): refusal, thunk mmap/mprotect failure,
  target vm_protect failure, and post-patch RX-restore failure.
- Negative witness strengthened: the probe CALLS the refused result for
  both input forms —
  `nopermit-strip: res==input all16unchanged=1 res()=42`
  (called via ptrauth_sign_unauthenticated, the caller's own PAC
  contract) and `nopermit-raw: res==signed-input res()=42`
  (called directly through the signed pointer). Both arches covered by
  the same build (`__arm64e__` conditional).
- Live results (post-V2 provider, `provider-probe-r003-v2*.json`):
  wrong-uuid permit `-2`; permit `0`; patch bytes land; `target()=43`,
  `orig()=42`; VERDICT=INSTALLED in normal AND `orig` mode (RC=0).
- Third WS generation confirms the gate telemetry:
  `CGXSYNTH-INSTALL isWS=1 diag=1 permitFn=… permit=0` and two
  `#### CGXSYNTH cred=… size=52 euid=0 pid=<probe> rc=0` per post run
  plus the 0x72ed reply `id=29421` as a real MACH-ALL-SEND.

### Dispatcher RE (deny-point narrowed)

`CGXSenderCanSynthesizeEvents` → dispatcher `0x1854257ec` decodes, with
stub slots resolved LIVE and dladdr'd in-process:

```
stub A = libbsm.0.dylib : audit_token_to_pid          → sender pid
stub B = libbsm.0.dylib : audit_token_to_pidversion   → sender pidversion
stub C = libsystem_kernel: __getpid                   → WS's own pid
stub D = CoreFoundation : CFDictionaryGetValue        → rights cache
```

Decision structure (RE-confirmed disasm):
- `sender_pid == getpid()` → allow (posts from WindowServer itself).
- `sender_pid == 0` → `rightDesc+0x20` byte decides.
- else key `{pid | pidversion<<32}` → CFDictionaryGetValue on the
  process-rights cache at `__DATA+0x1d8ba6b40` (sentinel -1 at +0xb48);
  on cache miss → authoritative impl `0x185425da4` (blraaz) which
  lazily resolves a checker symbol (name @0x1854dc3e, resolve via
  stub pair 0x18547c99c/0x18547c9ac, cached at __DATA+0x1d8ba6b50) and
  returns `(checker(rightName, audit, 0) == 0)`.

**Addendum interpose theory refuted for this path**: the two audit
decoders bound here are the REAL libbsm functions — mac_hooks'
`audit_token_to_auid`/`audit_token_to_asid` interposes are not in this
dispatcher. The denial is therefore a genuine cache-miss + checker
verdict keyed by {pid,pidversion} — our process simply has no entry
and the checker says no. Open: which checker symbol resolves
(lazy-bound; not yet dladdr'd inside WS) and what populates the cache
(loginwindow registration path is the leading candidate).

### R003 candidate state — REVISING (not self-declared READY)

**RE channel disclosure (required by review):** no IDA process and no
IDA MCP listener is running on this host (verified: `mcp_list_servers`
returns none; reviewer confirmed 127.0.0.1:13337-13339 quiet).  All
static disasm in this round was **capstone on the extracted file**
`/tmp/SkyLight_22F82` — now copied by reviewer to
`analysis/SkyLight_22F82` (git-ignored), UUID
`96676A53-B1E0-3D7E-B98B-B73873CD1880`, SHA256
`13d7a0ca010a9808234c71c34fd650b26329de99d1aceaeb5ddbb58f8b906c61`.
Offset/VA facts that name real runtime objects (stub slots, gate
address) were additionally **runtime-confirmed** by probe `dladdr`/
read-backs; symbolic names of internal helpers came from live bound
pointers, not byte guessing.  The lazily-resolved checker NAME (impl
string operand) is **not yet identified** — pending IDA or a WS-side
read.

**Closed this round (with clean full-stream records):**
- Refusal orig-callable: `nopermit-strip res()=42`,
  `nopermit-raw res()=42` — artifacts
  `artifacts/provider-probe-r003-v3.json` (exit 0) and
  `-orig.json` (exit 0), raw stdout+stderr, no remote pipe.
- ABI versioning: `MACWSHookPermitV2` live (`permit=0` armed in WS;
  `permit-wronguuid -> -2`; `dlsym` capability lookup fails closed on
  old providers; probe exit 8 reserved for mixed-version detection).
- All install-failure paths return the callable original (code review;
  fault-injection paths not exercisable without induced failures —
  listed as contract-reviewed, not runtime-proven).
- Inventory probe run: `artifacts/probe-inventory-r003c.json`
  (real exit 0, full streams).

**Device cleanup snapshot (post-experiment):**
- WindowServer (46364) booted out — no WS/launchd orphans.
- CORRECTION: iOS-ns `/tmp/macws_runtime_diagnostics` was removed, but
  the chroot marker `/var/mnt/rootfs/tmp/macws_runtime_diagnostics`
  survived (separate namespace — reviewer-caught).  Archived to
  `…/tmp/diag-archive/macws_runtime_diagnostics.r003-20261014`;
  both namespaces verified absent (`cleanup-sentinel-r003.json`).
- `thermalmonitord`/`watchdogd` in ps are stock iPadOS daemons, not
  ours. MacWSHost (15797, mobile) is the user's host app — left alone.
- WS log tail shows only normal teardown noise (SENDFAIL on dying
  ports). Rootfs 44% used, unchanged.

**Final acceptance runs (this build, zero remote pipes):**
- `artifacts/provider-probe-r003-final.json` (normal, real exit 0,
  full streams) and `…-final-orig.json` (orig mode, exit 0,
  `via_orig_returned=1`).  Numbers: `nopermit-strip res()=42`,
  `nopermit-raw res()=42`, 16-byte unchanged, `permit-wronguuid -> -2`,
  `VERDICT=INSTALLED`.
- `artifacts/probe-inventory-r003-final.json` (exit 0): UUID compare
  `match=1` gates Phase 1c — first private read happens only after
  identity pin passes.  All 8 dispatcher stubs resolved live.
- `artifacts/final-state-r003.json`: device post-sign SHA256 for all
  six deployed artifacts + clean snapshot (sentinel absent, no WS/
  probe/mshook processes).
- `artifacts/probe-upload.json` + `probe-deploy-r003final.json`:
  final probe re-deployed through the reviewer's `--upload` path
  (PreferredAuthentications=password, real exit 0), remote hash
  verified against local pre-sign `b33d4e4a…` BEFORE signing,
  `install -m755` new inode (fixes the earlier `-rw-r--r--`
  non-executable which caused `posix_spawn: No such file or
  directory` — a mode fault, not a missing libmachook; the dylibs
  were present), ldid + boot-trust added=1, post-sign
  `740ea29a…`.
- Baseline gate held: `chroot /var/mnt/rootfs /bin/echo HI` → `HI`,
  remote exit 0 (raw output in deploy record).
- IMPORTANT caveat (reviewer-noted): stub/rightDesc/cache reads occur
  in the **probe's own** SkyLight instance.  `rightsCache=0`,
  `lazyChecker=0` in the probe prove NOTHING about WindowServer's
  live globals; `checker-name`/`checker-aux` VA misses are mapping
  facts of the probe address space, not protocol facts.

**Still open (explicitly NOT accepted):**
- Identity of the lazily-resolved checker and what populates WS's
  per-{pid,pidversion} rights cache — needs IDA MCP or WS-side
  debugger, not present.
- Input success: still none.  `SLSPostEventRecord(canonical) -> 1002`.
- `SecTask.SkyLight.loginwindow -> 0x0` in final inventory: the probe
  lacks the loginwindow entitlement (relevant to R004 handshake work,
  not an acceptance criterion here).
- Push: `git push origin main` → `could not read Username` (https),
  `git@github.com` → publickey denied, `gh` absent.
  **Push remains unauthenticated — not fabricated.**

**Git state:** `685624f` impl, `b7aea48` addenda, `71ea1ae` evidence,
plus one diagnostic checkpoint commit on top (this status + final
artifacts).  Labeled diagnostic checkpoint, not acceptance.

Review inputs: `reviews/R003-20261014.md` (F01–F05) and
`reviews/R003-addendum-20261014.md` (identity pin, refusal contract,
rollback, capability lookup, tap-surface disablement).

### Provider contract (misc/cydiasubstrate_macos.m)

- `MACWSHookPermit(image_base, expected_uuid16, offset, expected[4])`:
  caller supplies the KNOWN-BUILD LC_UUID the offset was derived from;
  registration returns **-2** when the live image's UUID differs —
  identity is pinned, not self-copied. Registration also verifies the
  16-byte prologue against live bytes (-1 mismatch, -3 no LC_UUID,
  -4 table full).
- Refusal path preserves the original no-op-scaffold contract:
  `*result = symbol` (the caller's PAC-signed pointer, unchanged) +
  `MACWSHook: REFUSED <why>` written to stderr via raw `write()`.
  Legacy call sites that unconditionally call `*result` keep working.
- RX-restore failure after patch write now restores the stolen bytes,
  re-flushes, and retries RX; if restoration itself fails it emits a
  loud `FATAL rx-restore` marker instead of silently leaving a
  writable/non-exec code page.
- All refusal paths audited: no permit, prologue drift, PC-relative or
  terminal insn in the stolen window → refuse, bytes untouched.

### Callsite (libmachook/mac_hooks.m CGXSYNTH block)

- `MACWSHookPermit` resolved by **dlsym capability lookup**, not a
  strong extern — an older/foreign provider without the symbol fails
  closed instead of breaking libmachook's dyld load.
- Supplies the expected SkyLight 22F82 UUID pin
  `96676A53B1E03D7EB98BB73873CD1880` at registration.
- Whole diagnostic additionally gated to the real WindowServer process
  (`_NSGetExecutablePath` contains `SkyLight.framework/Resources/
  WindowServer`) AND `macws_runtime_diagnostics_enabled()` — it cannot
  arm inside probes or other injected clients.
- F01 footgun removed: the constant-return `_audit_token_check_tcc_access
  →1` install is deleted (was inert anyway — MSFindSymbol NULL on a
  local `t` symbol).

### Probe discipline (misc/macos_ws_event_probe.c)

- `inventory` (default) is strictly read-only; `post` adds warp +
  CGEventPost witness + ONE canonical SLSPostEventRecord; `mutate`
  keeps session mutation opt-in.
- **DISABLED (not merely gated)** pending exact RE contracts:
  `SLPSPostEventRecordTo` (four guessed arg0 variants removed),
  `SLEventTapPostEvent` (tap=NULL never validated), `SLEventTapCreate`,
  `CGPostScrollWheelEvent`. Presence is still inventoried.
- Phase 6b comment corrected: gCurMsgAudit is proven POPULATED on post
  (R002 witness); denial is inside the audit→permission-cache lookup.
- `argv[2]` exact "0"–"3" parsing removed along with postTo.

### Runtime evidence (rebuilt artifacts, deployed hashes below)

Provider probe, both modes, live provider:

```
nopermit: result=0x..ab0 (expect=same) all16unchanged=1 target()=42
permit-wronguuid -> -2            (identity pin enforced)
permit(image=…,off=0x3a7c) -> 0
target-after: 51 00 00 58 20 02 1f d6 <replace64>   (real patch)
target()=43  orig()=42
VERDICT=INSTALLED   RC=0     (normal AND 'orig' modes; via_orig_returned=1)
```

WindowServer restart under new build:

```
CGXSYNTH-INSTALL gate=0x1977e9464 slot=0x1eaf921b8 hdr=0x1974d1000
    isWS=1 diag=1 permitFn=0x1009aed88 permit=0      ← armed
(probe process run showed isWS=0 → permit skipped, diag hooks did NOT
arm in a non-WS process — process gate verified both directions)
```

Canonical post under armed gate (WS err delta):

```
#### CGXSYNTH cred=0x16f4c7220 size=52 euid=0 pid=45811 rc=0
#### CGXSYNTH cred=0x16f4c7228 size=52 euid=0 pid=45811 rc=0
#### MACH-ALL-SEND seq=3590 id=29421 bits=0x12 remote=67195
```

- Two gate firings (warp-path + canonical post) each return rc=0 →
  `_XPostEventRecord` still writes 1002.
- This run the 0x72ed reply logged as a real `MACH-ALL-SEND`
  (earlier boots showed SENDFAIL invalid-dest — that was send-right
  lifecycle, not dispatch failure).
- Probe: `SLSPostEventRecord(canonical) -> rc=1002` — still denied.

### Deployed artifact hashes (post-ldid, on device)

```
CydiaSubstrate          9824114ac8b6c7703a7f49fc44bc9d96f91d7ea948f9bf6dad583a719a178ad2
libmachook.dylib (e)    001d21229891f297a62d9fa77ae32289421e06944ac6e13d192ecdc4856eedac
libmachook_arm64.dylib  026491e9c478a6dee94501a84af7471c0511fe99c15469659cfb59060c7a02b2
mshook_provider_probe   faa87d220408c6f803237d9886156c3df9f4d48d0890b13bdd1128e2412da49e
macos_ws_event_probe    d7c790616fb52a4849ff60508e16b5f002d8be129b584d53b76b184736d13600
```

Local pre-sign hashes (build evidence): CydiaSubstrate
`3e2d373f…`, libmachook e `748bd251…`, arm64 `58fa526c…`,
probes `20a004c0…` / `1ab796ba…`. Every build command exited 0
(clang/make/setver/scp/ldid/trustcache) — see R003 commands below.
trustcache readd: `{"added":7,"files":6,"hashes":7}`.

### Remaining unaccepted scope (explicit)

- Provider accepted ONLY for the toy target under explicit permit.
  The CGXSYNTH gate hook in WS is armed+ firing, but its own permit is
  the only production permit; no arbitrary SkyLight target is hookable.
- `rc=0` inside the gate is the dispatcher's (0x1854257ec) verdict, not
  yet attributed: cache-table miss vs TCC-eval deny remains open.
  Addendum THEORY (unverified): mac_hooks interposes
  `audit_token_to_auid→501` / `audit_token_to_asid` fallback — if the
  dispatcher's decoded key mismatches the cache's populated key, our own
  interpose layer could be producing the denial. Next bounded step:
  disasm the dispatcher's key computation, then a LOGGING wrapper or
  debugger read of the effective lookup key vs cache contents. Do NOT
  globally remove the compat interposes as an experiment.
- No input success claimed: cursor still unmoved by posts (NO-DISPATCH,
  rc=1002). Session registry still empty; `SetLoginwindowConnection`
  remains 1000 (registry lookup miss).
- MSHookMessageEx still no-op; no generic hook engine planned.
- WS currently running (PID ~45786) for continued bounded experiments.
