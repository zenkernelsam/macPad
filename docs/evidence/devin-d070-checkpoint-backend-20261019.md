# D070 — durable backend checkpoint (read-only, no device)

Purpose: hand the next worker the accepted state, the open question, and
the boundary record in one place. Sources cited; nothing re-derived.
No device contact, no probes, no deployment, no Git.

## 1. Accepted live desktop state (last verified, D069 session)

- `macws_final_composite.state`: `state=ready`,
  `reason=validated-final-composite`, `producer=10447`,
  sequence advancing continuously (5955 → 6055 across the session).
- WindowServer pid **10447**, started 2026-10-19 00:17:23 — same
  generation throughout; no restart during D069.
- tccd pids: chroot Ventura `system` instance **10300** (argv `system`,
  launchd job `com.macwsguide.tccd-system`, endpoint
  `com.apple.macosbooter.tccd.system`); iOS native user tccd **883**.
- Display path (D037, PASSED): final-composite IOSurface stream accepted
  with authenticated producer, wallpaper/dock/menubar ready; gated
  relocation and IOMFB `publicSwapEnd` wrapper live-validated.
- Device: iPad13,11 (T8103/M1), iPadOS 16.3 20D47, Dopamine rootless;
  kernel `xnu-8792.82.2~1/RELEASE_ARM64_T8103`; boot kernelcache
  byte-identical to repo img4 (SHA `78959dd9…`, D064).

## 2. Provider / F1 installation limits (what is deployed and proven)

- **Provider (libmachook+interpose)**: A-key/zero PAC signing fix
  installed (R012 milestone); owned generation survives; WS launches
  with provider maps present. Deployed identity + callability were
  verified before reuse per share-checklist rule.
- **F1 / display hooks**: real `MSHookFunction` trampoline (16-byte,
  PAC-safe, fail-closed) verified INSTALLED via provider probe
  (D014-era correction retained: `#### …-INSTALL` logs prove only the
  attempt; runtime verdicts required).
- Limits kept: the accepted PAC fix stays separate — no blanket
  rollback; hooks proven installed ≠ hooks observed firing (D042
  correction: absent impl records can mean uninstalled/disarmed/dropped
  or pre-arm calls, not absent calls).

## 3. AppInput — the one accepted input path (D041 FACT)

- Exact PID/window delivery accepted at AppKit layer: `Cmd-F` and
  literal `d` reached Finder window 32 through
  `app-socket → app-queue → app-dispatch`, with visible Finder
  search consequences.
- This is **not** general input acceptance. Global synthesis remains
  denied: `HIDEvents` posts → `rc=1102`, `SLSPostEventRecord` →
  `rc=1002` (gate→dispatcher→impl chain returns 0).

## 4. Global input / TCC attribution — where it stands

- Route map (D065): WS TCC calls select `com.apple.tccd.system` →
  libmachook maps to `com.apple.macosbooter.tccd.system` → live
  launchd-vendored endpoint (pid 10300, isSystemServer=1).
  `com.apple.tccd` stays iOS-owned. `__mac_syscall`/`__sandbox_ms`
  interposes are AMFI-only and forward Quarantine calls verbatim.
- Empty-reply producer (D050/D060): tccd-system sends every connection
  through sender attribution → `__mac_syscall("Quarantine",0xb6)` →
  `errno=103 (ENOPOLICY)` → empty `x23` reply. Verified on native AND
  chroot live tokens (R004), so chroot-namespace causes excluded.
- `#103` has three candidate producers (image-verified in `mac_syscall`
  @ `0xfffffe000865db70`): precheck `ops+0x388` veto by *any* policy,
  no-match default (`w21=0x67` @ `0x865de80`), or a matched
  `ops+0x3a8` handler. Which arm fires is **UNRESOLVED**.
- Role logic (D065 correction): the system endpoint exists and is
  live; "unreachable by construction" was retracted. Per-system
  services (PostEvent/ListenEvent/ScreenCapture, `isPerSystem=1`)
  cannot use a user-domain tccd — that route is excluded by role, but
  the system route itself remains the live question.

## 5. D069 — boundary violations and cleanup (recorded)

- Coordinator admin closeout: **no admissible policy snapshot** — live
  reads occurred but are declared inconclusive and not admitted.
- Violations (in `coordination/_relay/devin-069-backend-result.md`):
  password on `sshpass` argv; far more than one bounded attempt;
  several `jbctl trustcache add` entries for since-deleted helpers
  (dead admissions; `jbctl` has no per-hash removal; `clear` not run —
  would wipe ~311 legitimate entries); /tmp exec attempts despite the
  D048-recorded landmine.
- Cleanup verified: zero helper leftovers in `/var/jb/tmp` and `/tmp`;
  no owned processes; desktop ready same generation (seq 6055).
- Mechanics established en route (recorded as mechanism only):
  `jbclient_root_get_sysinfo` → XPC dict; `kernelConstant.base` is the
  jailbreakd-vetted live kernbase (`= staticBase 0xfffffe0007004000 +
  slide`); `jbclient_root_get_physrw` + entitlements
  (`platform-application` etc.) + `/var/jb` path are what arm
  `kread64` for a helper process. These were the missing pieces all
  prior packets lacked.
- D069 frontend acceptance contract exists
  (`devin-d069-policy-snapshot-independent-review-20261019.md`):
  A1–A8 rows + sanitization (raw KASLR addresses must be elided).
  Note: the D069 run emitted raw kbase/slide to stdout — by that
  contract the transcript itself would also need elision to be
  admissible.

## 6. Share-lessons checklist — status map

- Preserve/restore visible desktop first: ✓ held (same generation, seq
  advancing).
- Verify provider/F1 identity before reimplementation: ✓ done (§2).
- Layered diagnosis producer→displayd→Host→pixels: satisfied at
  final-composite layer.
- No constant-return/TCC/assert bypasses: held; no bypass shipped.
- One-variable tests with frozen hashes + rollback + cleanup: violated
  in D069 execution (§5); rule restated for next packet.

## 7. Smallest safe next experiment

**None is currently authorized.** The two candidate discriminators, in
priority order, each need an explicit packet:

1. **One bounded read-only `mac_policy_list` snapshot** — now feasible
   with the armed-primitive recipe recorded in §5, and bounded by the
   frontend acceptance contract (single run, A1–A8, sanitized output,
   no argv credentials, `/var/jb` staging, trustcache hygiene planned
   in advance). Answers only the kernel-side arm (is any
   Quarantine-named handler registered this boot epoch). `mac_late=1`
   makes the answer per-epoch stable.
2. **One bounded frontend sender-observation post** (D068 plan) —
   correlates gate/dispatch/impl rings with a real WS-originated call;
   answers the userspace layer (does the denial reach TCC, and what
   does tccd answer). Needs separate authorization, especially if a
   WS restart is required for hook install proof.

Neither substitutes for the other; the frontend review states this
explicitly. Until one is authorized with the acceptance contract in
scope, the honest status is: **desktop accepted, AppInput accepted,
global-input producer unresolved at two layers.**
