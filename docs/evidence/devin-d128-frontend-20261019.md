# D128 — independent audit: IconServices crash identity, trust, panic boundary

Task: `coordination/_relay/devin-128-frontend-task.md`. Read-only; no
device, build, source, trustcache/TCC/KRW, GUI or Git actions. Inputs:
R005 excerpt, R013 `.ips` bundle, D033 inventory, D071 §2, D094 layered,
D095/R024/R031/R032, Oct-19 read-only bundle.

## 1. Crash identity — FACT (verbatim, two corroborating captures)

- Process: `/System/Library/CoreServices/iconservicesagent` — the
  **chroot agent**, NOT `iconservicesd`. `.ips` `slice_uuid
  c0891064-1633-3883-b091-b90e6490e270` = the chroot file's arm64e slice
  (fat `[6a93e10f, c0891064]`); the iOS on-disk agent is `eb76fa5e`
  (D033 verified the chroot file matches). cpuType ARM-64.
- R005-era reports: pids incl. 50980, `launchd` parent, `userID 0`,
  coalition `com.macwsguide.iconservicesagent`; R013 report pid 80735
  19:52:17 same coalition. Job: `launchdchrootexec 0 0 /var/mnt/rootfs
  …/iconservicesagent runAsRoot`, `KeepAlive SuccessfulExit=false`,
  `ThrottleInterval 10`, `last terminating signal Abort trap: 6`.
- Assertion (verbatim agent stderr):
  `MPSLibrary.mm:485: failed assertion 'MPSLibrary::MPSKey_Create
  internal error: Unable to get MPS kernel F11H. Error: Compiler
  encountered an internal error'`.
- Stack: faulting queue `com.apple.MPS.MPSLibrary.preheat` —
  `__pthread_kill→pthread_kill→abort→__assert_rtn→MTLReportFailure.cold.1
  →MTLReportFailure→MPSLibrary::MPSKey_Compile+944`. Workloop thread
  parked in `_os_unfair_lock_lock_slow→MPSKey_Compile+56` beneath
  `CIBlurProcessor`→`ISGenerationRequest generateImageReturningRecord-
  Identifiers` — icon compositing triggered the MPS preheat.
- Loaded libmachook: `/usr/local/lib/libmachook.dylib` arm64e uuid
  `c439dbec` present in `usedImages`; a `macws_core_services_map_
  bridge_thread` runs inside the process. libmachook is loaded, but
  nothing in the stack implicates it — the abort is Apple's own
  `__assert_rtn` on the compiler error.
- Dependency trust closed (R005 live record): agent CDHash `1c0d3271…`
  trusted; injected libmachook arm64e CDHash `ee0f018f…` trusted.
- **Agent ≠ daemon**: sibling `iconservicesd` is a separate job,
  `launchdchrootexec 240 240`, uid/gid 240, service
  `com.apple.macosbooter.iconservices.store`, **stable** — zero crashes.
- Count separation (D033): 17 real SIGABRT crashes (+looping ~40–90 s
  cycle) vs 25 `ExcUserFault_*` reports = `EXC_GUARD GUARD_TYPE_USER
  namespc 7 reason 9`/`XPC_EXIT_REASON_FAULT` — a different lightweight
  fault class shared by 29 chroot processes; not the MPS abort.

## 2. Directory facts — FACT, non-causal

`HOME=/Users/root`, `TMPDIR=/tmp` (launchdchrootexec-supplied);
`/var/mnt/rootfs/Users/root/Library/Caches` **absent** on the fs
(observed; no confstr call was made); store
`/var/mnt/rootfs/Library/Caches/com.apple.iconservices.store` =
`240:240` mode `0700`, owned by the stable daemon. These are
observations only — R005's own verdict, ratified: not a
uid/gid/HOME/cache cause; the supplier UID501/GID20 recipe is an
untested hypothesis that would not touch an `MTLReportFailure` compile
abort. No runtime evidence turns directory state into a crash cause.

## 3. Causal boundary — FACT / THEORY / MISSING

- FACT: the agent-side abort is an MPS **kernel-F11H compile failure**
  at the client. FACT: the paired service-side abort exists —
  `MTLCompilerService-195137.ips` (iOS-native slice `6d2cfe56`, NOT the
  chroot XPCService copy), **same coalition** `com.macwsguide.
  iconservicesagent`, 40 s earlier, in `readModuleFromBinaryRequest →
  fatalDiagnosticHandler → abort`; its image list carries the project's
  `MTLCompilerBypassOSCheck 457ffbd6`. A third file
  (`MTLCompilerService-193349.ips`) is the R012-generation report
  surfaced late by mtime — not a third crash.
- FACT: R024 attr-71 is same-module evidence **for the owned module
  only** — `Unknown attribute kind (71) (Producer 'LLVM16.0.0' Reader
  'LLVM ')` reproduced 4/4 on the R014-owned private endpoint with a
  byte-verified hook. The LLVM16→reader attr-71 rejection is the
  decoder-level reason that module family fails; it does not by itself
  prove the production crash compiled that exact payload.
- MISSING — same-module proof: no production-side request/module hash
  ties the crashing compile to payloads `ce018722…`/`042303be…` /
  `5d805ec5…`/`75663758…`. D095's limit stands: symbol-name inference is
  not lineage.
- FACT: R031 retarget **has never been replayed in the exact service**.
  Canonical candidate (only `MODULE_CODE_TRIPLE`→`air64-apple-ios19.0
  .0-macabi`, 0 diffs across 21 blocks/476 records) passed host
  LLVM-17 and device LLVM-16 `llvm-dis` gates; the bounded private-
  endpoint replay was frozen by the worker-switch request before
  execution. Park preserved (R031 header, D095 §6). Not a production
  fix; no claim made.

## 4. Crash vs iPad reboot/panic — separated

- The saved IconServices abort is a **userspace SIGABRT** in a
  keep-alive agent — a per-process failure that cannot panic a kernel.
- The **only** kernel-panic report anywhere in saved evidence is
  `analysis/dyldwork/panic_2130.ips` — **2026-09-26 21:30**, `panic
  (cpu 5): Ptrauth failure with DA key`, panicked task `python3.9`
  pid 27161 — a dyld-work-era event, unrelated to IconServices and to
  the Oct-18/19 window.
- The Oct-19 read-only bundle contains **no panic report**: no
  `panic*.ips`, no resetCounter, empty `log show` window. D033 already
  established the Oct-18 12:26:48 SpringBoard/backboardd/tccd restart
  was a userspace respring while kernel uptime >1.5 d — not a kernel
  reboot.
- **Explicit statement**: for the user-reported iPad reboot/panic there
  is no panic report in saved evidence; it must not be attributed to
  IconServices. Even a temporal correlation is unestablished — a
  crash-looping agent does not reboot a kernel.

## 5. Minimum next artifacts

- Same-module proof: production-side F11H request/module hash (crash-
  side metadata or a bounded capture of the real compiler request)
  matched to the owned payload hashes.
- Panic question: the panic `.ips` itself (`/var/mobile/Library/Logs/
  CrashReporter/panic*` or boot-session records) — absent; nothing else
  substitutes.
- Replay: authorized un-park + bounded private-endpoint replay of the
  R031 candidate (per D095 §5-6 gates incl. no-hook elimination).

(handover to codex now)
