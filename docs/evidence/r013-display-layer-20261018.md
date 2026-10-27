# R013 — layered display acceptance and `displayd-start` attribution

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-013-backend-result-task.md`. One bounded owned
generation (`macos_gui.sh start coexist --no-vnc`), read-only acceptance
inspection, one cleanup. No source, package, publisher Identifier, pacing,
compiler, TCC policy, entitlement, assertion or ownership change. Native
SpringBoard/tccd/backboardd were not restarted. Diagnostics markers were
absent throughout, matching the launcher's production preflight.

## 1. Frozen state before the generation

| Item | Value |
|---|---|
| HEAD | `c97e219f3f367662dc9bdb1f20d7e76fba544df1` |
| `libmachook/mac_hooks.m` SHA-256 | `e6cc4597a6a61bf65a25782d4939eb1470c0e237aaf3af40c46fa296eeb9463d` (R012 A-key/zero change, uncommitted; unchanged this round) |
| installed package | `com.kdt.macosbooter 0.3.4`, `install ok installed` |
| provider | inode 250671913, SHA-256 `b2ae21da42704b1d584f2b655e837f7db62a10f2a035b3f741adc819ce1423e7` |
| `libmachook.dylib` (arm64e) | SHA-256 `47f78880f6de1fadc9faabbf79d16ccc56cfba2feb5a45e2155e09c22c2d9432`, CDHash `b579715ec2cef37f7ffb4c8c89e42eccc32b04df` |
| `libmachook_arm64.dylib` | SHA-256 `6de9b6880495c3e0f431c2fc0e328b4420aecac178391d466fa71b141efcbd18`, CDHash `0bbcbb6f2ac99bbf98901a3e9a65e821d6f5bd08` |
| native | SpringBoard 872, tccd 883, backboardd 905, all started 12:26:48 |
| markers | all six runtime/MTL/compiler markers absent (iOS and chroot namespaces) |
| thermal | `sampled-at=1792324203 thermal-state=nominal raw=0 low-power=no ... effective-temp-centic=3400` (34.00 °C) |
| R010 baseline backups | present at `/var/mobile/macws_r012_evidence_20261018/r010-baseline/` |

Pre-start process inventory contained no WindowServer, displayd, chroot
helper or macOS application. The generation began on a clean owned boundary.

## 2. Owned generation

Command, run once (relay wrapper exit 0, `timeout=0`):

```
timeout 300 bash /var/jb/usr/macOS/bin/macos_gui.sh start coexist --no-vnc
START_EXIT=0
```

Launcher statements of record:

```
[macos_gui] NATIVE-AGX: built-in command ABI + cancelled-swap completion + final-composite IOSurface enabled; requested RFB CPU bridge disabled.
[macos_gui] PRODUCTION-PREFLIGHT: native AGX required; diagnostics/env traces/dump sentinels OFF.
[macos_gui] Display mode: COEXISTENCE — iPad panel stays on iOS, macOS renders to VNC only.
[macos_gui] WindowServer process ready (pid=80310, stable for 8s; graphics not yet witnessed).
```

`--no-vnc` does **not** disable the built-in final-composite transport; it
disables the RFB CPU bridge. It also changes the launcher's readiness
predicate: `layout/usr/macOS/bin/macos_gui.sh:686-694` takes the
`stable >= 8` process-only branch and never consults
`/private/tmp/macws_graphics_ready`. The phrase "graphics not yet witnessed"
is therefore a mode-specific process-readiness statement and must **not** be
cited as proof about the composite producer.

Exact identities (from the in-generation status and later `ps`):

| Role | PID | Started |
|---|---|---|
| WindowServer (`-virtualonly -daemon`) | 80310 | ~19:50:00 |
| macwsdisplayd | 80405 | 19:50:49 |
| Dock | 80529 | 19:50:52 |
| Terminal (`-ApplePersistenceIgnoreState YES`) | 80562 | 19:50:56 |
| OSXvnc pointer proxy | 80546 → restarted 80670 | 19:50:56 / 19:51:01 |
| macOS system tccd | 80165 | 19:50 |
| macOS authd | 80153 | 19:50 |
| macOS systemstatusd | 79898 | 19:50 |
| MacWSHost | 81325 | 19:57:14 (late, no generation-start participation) |

Provider layer — PASS (load and callability only):

```
MAP pid=80310 path=/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate vaddr=0x102cc0000 size=0x4000
MAP pid=80310 path=/usr/local/lib/libmachook_arm64.dylib vaddr=0x102f04000 size=0xf8000
provider-image=/System/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate
provider-uuid=57 00 e6 49-b2 71-36 cf-88 8c-4c cc 8e 1b a6 ec
original-published=0x102e74c90
via-hook=43 replacement_called=1 original_called=1
VERDICT=INSTALLED-ORIGINAL-CALLABLE
```

## 3. Layer results

### 3.1 Decode/asset layer — FAIL (aborted inside the MPS/F11H path)

Terminal produced a valid AppInput endpoint and a V3 metrics sidecar
(`/var/mnt/rootfs/private/tmp/macws_window_metrics.80562.bin`, 80 bytes,
SHA-256 `b01ce21f02dfc34665d66d1399acb01e1bb1794ac8a4f765471444af91b74b38`):
generation 2, one entry, window 31, flags `visible,has_shadow,resizable,focused`,
`configuration_ack.received=false` (`no_identified_configure_yet`). This is
app-side state, not a decoded or presented frame.

Within the generation, two independent Metal compiler aborts occurred:

- 19:51:37.298, `MTLCompilerService` pid 80741, coalition
  `com.macwsguide.iconservicesagent`, `EXC_CRASH/SIGABRT`. Triggered thread:
  `MTLCompilerObject::readModuleFromBinaryRequest(ReadModuleParameters const&, ...)`
  → `buildSpecializedFunctionRequest` → `llvm::errorToErrorCodeAndEmitErrors`
  → `fatalDiagnosticHandler` → `abort`. Images: MTLCompilerService
  `6d2cfe56-8d88-39aa-bc25-7ffe5058ed4e`, MTLCompiler
  `482ee528-9d70-3ed7-a746-d19bb741245b`, libLLVM
  `3c9d9d6c-cc92-326a-911c-141bc96dc8be`,
  MTLCompilerBypassOSCheck `457ffbd6-21e0-35a9-9ba7-cc5d00729a0a`.
- 19:52:17.650, `iconservicesagent` pid 80735, `EXC_CRASH/SIGABRT`, in
  `MPSLibrary::MPSKey_Compile(MPSKey_data*)` waiting on
  `_os_unfair_lock_lock_slow`.

The 19:51:37 abort is byte-for-byte the same F11H signature as the
R012-generation abort at 19:33:48 (pid 78797). The latter file was surfaced
at 19:51, so its presence in the R013 crash delta is an artifact of report
mtime, not a third crash. The decode/asset path therefore does not complete
into a presented window.

### 3.2 Provider layer — PASS (installed, callable; no frame implied)

See §2. This says nothing about pixels, TCC or input.

### 3.3 WindowServer composite producer — no transported composite

WindowServer stderr for the generation (4,049 bytes, mtime 19:50) contains
three launch blocks and, in each, the same AGX device-info result:

```
#### AGX device-info ABI probe conn=<n> requested=0x78 actual=0x78 result=0xe00002c2 fallback=0x70 actual=0x70 result=0 selected=legacy-0x70
```

No `#### FINAL-COMPOSITE` line of any kind appears in `WindowServer.err`,
`macwsdisplayd.err`, or any application log (`dock.log`,
`systemuiserver.log`, `controlcenter.log`, `terminal.log`, `osxvnc.log`,
`lsd-*.log`). Since the publisher's failure and replay lines are
unconditional `dprintf(STDERR_FILENO)`, their absence means no publisher
failure was logged in those streams.

Diagnostic limits matter here:

- `#### IOMFB CANCEL-COMPLETION` and the `VNC-OWNED` counters are gated by
  `/tmp/macws_runtime_diagnostics`, which was absent. Exact completion counts
  cannot be quoted from this run.
- `/private/tmp/macws_graphics_ready` is removed by the launcher at stop and
  was not sampled after start; its post-stop absence is not evidence.
- `--no-vnc` never checks that marker (§2).

What remains proven: no qualifying final-composite message reached displayd
(§3.4). The publisher has no silent path for a valid owned surface — invalid
input is pre-filtered by the scanout function, and every transport failure
logs — so the producer boundary was never witnessed in this generation.
The AGX `legacy-0x70` selection is recorded as the same ABI state as prior
generations, not as the attributed cause of the missing composite.

### 3.4 displayd receive — ready, zero accepted and zero rejected

R013 is the final segment of the append-only `macwsdisplayd.err`
(line 19,297 onward; the preceding window=31 stream activity belongs to the
R012 segment). Counts in the R013 segment:

| Pattern | Count |
|---|---|
| `final-composite receiver-ready` | 2 |
| `final-composite-replay-request` | 10 (5 per start) |
| `final-composite-accepted` | 0 |
| `final-composite-rejected` | 0 |
| `frame-first` | 0 |
| `workspace-*` | 0 |
| `geometry-stream-restart` | 0 |
| `request-subscribe` | 0 |

Exact lines:

```
MACWS-DISPLAY final-composite receiver-ready service=com.macwsguide.display.final-composite port=21507
MACWS-DISPLAY READY service=com.macwsguide.display protocol=8
MACWS-DISPLAY final-composite-replay-request attempt=1 min-completion=1 lookup=0 service=16643 send=0
... attempts 2-5, lookup=0, send=0
```

No window-stream subscription or frame reached displayd at all: Dock,
SystemUIServer, Terminal and WindowServer produced no display-service
traffic in this segment.

Unresolved artifact-level inconsistency, recorded rather than attributed:
the replay lookups returned `lookup=0` with `send=0`, which requires a
registered `com.macwsguide.windowserver.final-composite-replay` service, but
the WindowServer stream contains no `replay-receiver-ready`,
`replay-received` or `replay-rejected` line. The receiver-side log alone
does not establish which process owned that registration. It does not
change the acceptance result: no accepted or rejected final-composite
message exists.

### 3.5 System/native pixel readback — none

No IOSurface sequence was accepted, no window stream existed, and no pixel
readback was performed. No pixel claim is made from process readiness,
provider readiness, the metrics sidecar, or finalize/status flags.

## 4. Why `macws_final_composite.state` says `missing` / `displayd-start`

`macwsdisplayd/main.m` writes the initial record at receiver startup
(`reason=displayd-start`) and advances it only from an accepted
final-composite message. The observed file is exactly that initial record:

```
state=missing
producer=0
sequence=0
updated=1792324249.663221
reason=displayd-start
```

`updated` is 2026-10-18 19:50:49 +08:00 — displayd 80405's start, the same
second as its EXC_GUARD report. Nothing ever replaced it.

The gap is therefore **upstream of displayd acceptance**, not a displayd
policy or state-writer failure: the receiver was ready on port 21507, its
protocol 8 handshake completed, its replay sends were accepted at the Mach
layer, and it logged neither an acceptance nor a rejection. The produced
side never delivered a qualifying composite. Within this bounded run, the
decode/asset layer aborted in MPS/F11H and the WindowServer composite
transport was never witnessed; the exact producer instruction/point cannot
be narrowed further without diagnostics that were intentionally off and
that this packet does not permit adding.

## 5. R016 Terminal reopen freshness witness — NOT RUN

Strict preconditions from the packet and R016 audit:

- valid `G0 > 0` **zero-entry** sidecar;
- a new Host request and result;
- `G1 > G0`;
- same PID/executable/start time;
- a visible metrics entry;
- no windowless-retire.

Observed: G0 = 2 with **one** entry, window 31, `visible` set,
`configuration_ack.received=false`. There is no zero-entry sidecar, so the
exact witness cannot be executed without inventing a precondition. The Host
application was not running at generation start and came up only at
19:57:14; no Host reopen request was issued, and `MacWSHostd.log` contains
no new Terminal session (its last entry is the old Terminal 50936
`result=signal-15`). No G1, no result, no acceptance.

## 6. TCC and input — separate observations, no acceptance

Controlled chroot probes, both exit 0, neither a policy proof:

```
TCCPREFLIGHT pid=80853 uid=0 audit-rc=0 ...
TCCPREFLIGHT service=kTCCServicePostEvent result=1
TCC_EXIT=0
```

No explicit TCC reply dictionary was captured; `result=1` from the preflight
API is recorded as an observation only.

```
postmin: SLSPostEventRecord -> rc=1002
INPUT_EXIT=0
```

The pre-existing 1002 rejection is unchanged. No grants, fake replies or
input bypasses were added.

## 7. Crash delta

29 `ExcUserFault` reports fall inside the generation window
(19:50:23–19:51:36). All 29 parse to the same class:

```
EXC_GUARD / GUARD_TYPE_USER / namespc 7 / reason_code 0x9
esr: "Address size fault"
```

Named processes include `macwsdisplayd` 80405, `Terminal` 80562,
`OSXvnc-server` 80546 and 80670, `SystemUIServer` 80470, `pkd` 80031,
`iconservicesagent` 80027/80735, `filecoordinationd`, `coreaudiod`,
`QuickLookUIService`, `ViewBridgeAuxiliary`, `pbs`, `lsregister`,
`sharedfilelistd`, `macwsinteropd`, `macwsworkspacectl`,
`com.apple.hiservices-xpcservice`, `com.apple.quicklook.ThumbnailsAgent`,
`CoreLocationAgent` and `coreservicesd`. All 29 share the same faulting
image-offset signature. The named long-lived processes (Terminal 80562,
displayd 80405, OSXvnc 80670) remained alive and continued logging; no
termination is established from these reports, and no cause is attributed.

Other delta items:

- `MTLCompilerService-2026-10-18-195137.ips` (19:51:37) — R013, F11H, §3.1.
- `MTLCompilerService-2026-10-18-193349.ips` — capture 19:33:48, R012
  generation; surfaced late (mtime 19:51), not counted as R013.
- `iconservicesagent-2026-10-18-195220.ips` (19:52:17) — MPS abort, §3.1.
- `ReportCrash-2026-10-18-195139.ips` — report-agent artifact.
- No tccd, authd, systemstatusd or `_xpc_bootstrap_services` crash; the R012
  XPC failure did not recur.
- `photoanalysisd.cpu_resource-195958` and `routined-200137` are native iOS
  reports outside the chroot display path and are not attributed.

## 8. Cleanup and final state

`timeout 240 bash /var/jb/usr/macOS/bin/macos_gui.sh stop` → `STOP_EXIT=0`
(cleanup SSH exit 0). Final state:

- no WindowServer, displayd, inputd, interopd, Terminal, OSXvnc,
  launchdchrootexec or macOS tccd/authd/statusd process remains;
- `macos_gui.sh status`: `display : COEXISTENCE`, `-- processes -- (none
  running)`;
- native SpringBoard 872 / tccd 883 / backboardd 905 unchanged, started
  12:26:48;
- all six diagnostic markers absent in both namespaces; staged probe
  directory removed;
- package still `com.kdt.macosbooter 0.3.4`, provider and both libmachook
  hashes unchanged, R010 baseline backups intact;
- thermal at cleanup `sampled-at=1792325110 thermal-state=nominal raw=0
  ... effective-temp-centic=3429` (34.29 °C).

Preserved evidence on device:
`/var/mobile/macws_r013_evidence_20261018/{logs,crashes,probes}` (33 crash
files = 29 guard reports + both MTLCompilerService reports + ReportCrash +
iconservicesagent).

## 9. Limits

- Composite-completion counters require the runtime-diagnostics marker; it
  was off by production preflight, so the precise producer step is bounded,
  not identified.
- The WindowServer replay-service registration is inconsistent with the
  absent publisher log lines; ownership of that registration is unresolved.
- The `--no-vnc` readiness branch does not validate a graphics witness.
- No pixel readback was possible without an accepted stream.
- R016 was not run because its zero-entry precondition and Host request were
  both absent.

## 10. Conclusion

The R012 package and provider remain loaded and callable, and the former
XPC bootstrap crash does not recur. The display chain stops before
displayd acceptance: the decode/asset layer aborts in MPS/F11H, no window
stream or final composite reaches displayd, and the state file remains the
initial `missing producer=0 sequence=0 displayd-start` record. The
`displayd-start` gap is upstream of displayd, not a receiver failure.
Pixels, R016 reopen freshness, TCC acceptance and input acceptance remain
unaccepted and are reported separately. No source, package or policy change
was made.

Evidence: `coordination/artifacts/R013-display-layer-20261018.json`,
`analysis/r013-display/` (generation/cleanup logs, displayd segment counts,
WindowServer stderr, preserved logs, 33 crash reports, metrics/TCC/input
probe outputs, SHA-256 manifest).
