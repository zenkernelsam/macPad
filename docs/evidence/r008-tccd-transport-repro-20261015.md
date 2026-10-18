# R008 — reproducible Ventura system-tccd transport; reply semantics qualified

Date: 2026-10-15. Scope from `coordination/DEVIN_TASK.md` +
`coordination/reviews/R007-20261015.md` (CHANGES_REQUESTED, F1–F5).
Goal: converge a reproducible transport chain — lifecycle, admission,
readiness, reply qualification — **without changing TCC policy**. The
R007 progress (real `OS_xpc_dictionary` replies reaching WindowServer)
is preserved; nothing in this packet alters grants, databases,
entitlement scope beyond the established merge contract, or the native
iOS daemon.

## F1 — lifecycle: tccd restored on every dependent-republish path

`stop_ws_dependents` was already removing the job during generation
changes; the recovery side never recreated it. `start_tccd_system` is
now called from all three service-republish sites, each time **after**
`publish_desktop_operation_services` and **before** dependent client
work (`layout/usr/macOS/bin/macos_gui.sh`):

- cold start (`start_macos`): after settings/desktop-operation contract
  publication, before `launchctl load $LAUNCHSERVICESD_PLIST`.
- `start_ws_dependents_after_replacement` (shared watchdog/recovery
  entry, used by `recover_ws_dependents`): after
  `publish_desktop_operation_services`, before locationd/LSD/desktop
  jobs.
- explicit desktop-session WindowServer rebuild: after
  `publish_desktop_operation_services`, before the replacement WS is
  launched.

`stop_ws_dependents` unloads/removes only `$TCCD_SYSTEM_PLIST` /
`com.macwsguide.tccd-system` and kills only argv-matched
`TCC.framework/Support/tccd system`; the native iOS `com.apple.tccd`
(uid 501, no `system` argument) is never matched. No KeepAlive, no
crash-loop, no second daemon manager; `EnablePressuredExit` stays stock.

Ordering is pinned by fixtures:
`misc/test_tccd_lifecycle_contract.py::TccdOrderingContracts`.

## F3 — readiness: one checked path for warm/cold/recovery

`start_tccd_system` reuse/reload decision and the post-load gate both
run `tccd_transport_ready`, which requires, in order:

1. Exact job resolution — `launchctl print <domain>/<label>` over
   `system`, `user/501`, `user/0`, `gui/501` (macosbooter jobs land in
   the loading context's domain, not `system/`; R007 runtime: user/501).
   Lookup errors fail closed (unknown, never success/absence).
2. When the job print carries `pid = N`: N numeric and `kill -0` live —
   a stale/dead generation pid fails. A job print *without* `pid =`
   (pressured exit, on-demand) is not auto-failed; steps 3+4 arbitrate.
3. The job's own service table contains the exact token
   `com.apple.macosbooter.tccd.system` — boundary regex
   `([^A-Za-z0-9_.-]|$)` rejects `….system.other` and substring matches.
4. A bounded real client-context protocol reply —
   `probe_tccd_transport` runs
   `launchdchrootexec 0 0 $ROOTFS /usr/local/bin/macwstccdprobe ping`
   (the same injected-libmachook path WindowServer uses; the probe
   resolves the logical `com.apple.tccd.system` through the same
   `macws_private_chroot_service_name` mapping) with a ~8s watchdog
   that kills only the probe child (`TERM`→`KILL` on that pid). Ready
   only on stdout marker `TCCPING reply-type=dictionary `.
5. Generation stability — `start_tccd_system` re-runs `kill -0` on the
   pid captured at load after the probe window.

Every R007 false-ready case is now a failing fixture plus the positive
witnesses (`misc/test_tccd_lifecycle_contract.py::TccdReadinessFixtures`,
22/22): warm-running-without-endpoint → not ready + reload attempted;
unrelated `….system.other` substring → not ready; reported dead pid →
not ready; `launchctl print` error → not ready; probe transport-error
reply → not ready; warm exact-domain+endpoint+live-pid+dict → ready
WITHOUT reload; cold load reaching the same state → ready;
pid-less pressured-exit job answering a dictionary → ready.

## F2 — admission: tccd in the reproducible cold/package path

**Cold-trust closure.** The Ventura tccd path
`$ROOTFS/System/Library/PrivateFrameworks/TCC.framework/Support/tccd`
is appended to `restore_cold_boot_trust`'s mandatory list before the
fast-path probe. `trust_closure_already_live` now takes the manifest
AND the full argument list and requires, beyond all-recorded-hashes-live:

- every explicit regular file → a manifest entry whose recorded slice
  hashes are all live;
- every explicit directory → ≥1 recorded manifest member under it;
- every `--hash V` / `--hash=V` flag → `V` itself live (the walk
  registers these verbatim, so a flag added after the manifest was
  written must not be skipped);
- malformed/absent manifest, empty hash set, or any miss → walk runs.

The probe stays thermally safe (reads only `jbctl trustcache info`,
no CodeDirectories) and runs before `application_trust_thermally_safe`
gates the expensive walk.

**Package + deep-repair signing.** Both postinst entry points sign the
stock binary through the established native-merge contract, on a
**staged fresh inode**:

- `layout/DEBIAN/postinst` — new `ensure_tccd_system_signature`:
  fast-path marker check on the installed binary
  (project `graphics-restart-no-kill` + native
  `notificationcenterui.tcc` + `Identifier=com.apple.tccd`), else
  preserve `.macws-original`, copy to `.macws-new.$$`, run
  `ensure_chroot_native_service_signature` on the stage, verify
  entitlements+identifier on the stage, `chmod 0755`, `mv -f` into
  place, `trust_installed_macho`. Pristine slice preserved like
  filecoordinationd's.
- `layout/usr/macOS/bin/postinst.sh` — same contract around
  `sign_and_trustcache_merging_native_entitlements` on
  `$tccd_temporary`, then `mv -f`; when already merged, only the
  per-arch `trust_cdhash` loop runs (idempotent re-admission).
- `layout/usr/macOS/bin/tccd-native-entitlements.plist` — the real
  12-key native closure, with slice UUIDs documented
  (x86_64 `63AD17DE-5198-3333-8EF7-6B04ED50DCF7`,
  arm64e `9845D17F-0409-3884-8A17-260C729636E5`, identifier
  `com.apple.tccd`, platform 14 — reviewer-verified against the
  deployed binary in `R007-independent-comparison.json`).
- Only `mkdir -p '…/Library/Application Support/com.apple.TCC'` is
  done for daemon state — no database, no grant seeding, ever.

**Per-slice key/value accounting (corrected).** Reviewer comparison:
original **12** top-level keys per slice → installed **124** keys per
slice; no native key absent; `com.apple.private.tcc.allow` =
`[kTCCServiceSystemPolicyAllFiles]` →
`[kTCCServiceListenEvent, kTCCServiceSystemPolicyAllFiles]` (union
merge, not verbatim). The earlier 24/252 numbers were XML-occurrence
counts across fat slices and nested keys.

**Probe toolchain.** `macwstccdprobe` (new Theos `tool`, arm64,
`platform MACOS` minos 13.0, project entitlement profile) is in the
top-level `SUBPROJECTS` aggregate and rides the existing bridge
install loop to `$ROOTFS/usr/local/bin/macwstccdprobe` via
`install_chroot_runtime` (fresh-inode + sign + trust). Build witness:
`make` in `macwstccdprobe/` → `.theos/obj/macosx/macwstccdprobe`,
sha256 `11c42e78f9a19f07ce1f7b0537d4cbff23a7bb23a8135544c36e1f8547a81741`,
LC_UUID `28C2B10E-EC5B-3293-8318-2EE94E6E4DA7`.

## F4 — reply semantics: qualified, capture mechanism wired

What R007's lldb evidence actually established (after correction):
reply *objects* reach the client (never an error singleton in any
capture), later captures read one as `OS_xpc_dictionary`, and
`xpc_dictionary_get_bool(reply,"result")` returned `w0=0`. Per the
SDK-semantics control (`R007-xpc-bool-and-build-checks.json`), accessor
0 does not distinguish explicit `result=false` from missing/non-bool
key — so this packet does NOT claim a policy denial, only a real
daemon-produced reply reaching the accessor.

Capture mechanism now in source (no new interposition — diagnostic
interpose already exists and preserves the real transaction):

```
MACWS_LSXPC_TRACE=1 \
/var/jb/usr/macOS/bin/launchdchrootexec 0 0 /var/mnt/rootfs \
    /usr/local/bin/macwstccdprobe preflight kTCCServicePostEvent
```

`launchdchrootexec` passes `environ` through `posix_spawn`, so the
flag reaches libmachook's `DYLD_INTERPOSE` on
`xpc_connection_send_message_with_reply_sync`, which writes
`#### LSXPC-CONN`/`request`/`reply` descriptions (≤4096 chars each) to
stderr — the exact request schema TCC.framework emits and the daemon's
decoded reply, correlated by connection name. `preflight` mode calls
the real `TCCAccessPreflightWithAuditToken` via dlsym with the
caller's own `task_info(TASK_AUDIT_TOKEN)` and prints pid/uid/audit +
raw result for caller attribution. On-device capture is pending (see
below); until it lands, the reply stays classified as "genuine
dictionary, accessor-zero" — neither transport failure nor proven
policy-denial field.

## Validation run (this host)

- `misc/test_tccd_lifecycle_contract.py` — **22/22 pass** (behavioral
  fixtures run the extracted real shell functions under mocked OS
  edges; manifest heredoc executed verbatim against fixture manifests).
- `test_boot_trust.py` + `test_restore_boot_contract.py` +
  `test_workspace_wallpaper_launch_contract.py` — **55/55 pass**
  combined with the lifecycle module.
- `bash -n` clean: `macos_gui.sh`, `layout/DEBIAN/postinst`,
  `layout/usr/macOS/bin/postinst.sh`.
- `test_artifact_contract`, `test_hardware_keyboard_contract`,
  `test_production_defaults`, `test_runtime_gate_inventory` — pass when
  invoked directly (`python3 misc/test_*.py`). The `-m unittest misc.X`
  loader fails for these modules with `No module named
  'audit_runtime_switches'` — a pre-existing sys.path quirk, unchanged
  by this packet.
- `test_nocopy_contract_probe` + `test_texture_sampling_probe` Metal
  allocation cases fail on this Mac (`allocation-failed buffer=0x0`)
  — hardware/Metal-dependent, unrelated to this change set.
- Source hashes (this packet):
  `macos_gui.sh` `75e1c584…`, `layout/DEBIAN/postinst` `a9343416…`,
  `postinst.sh` `477b6075…`, `tccd-native-entitlements.plist`
  `dd9d8b69…`, `com.macwsguide.tccd-system.plist` `d2e8669d…`,
  `macwstccdprobe/main.c` `54fe253c…`,
  `misc/test_tccd_lifecycle_contract.py` `6626906c…`.

## Pending device-side steps (SSH env absent this session)

`MACWS_DEVICE`/`MACWS_SSH_PASSWORD` are not set in this environment, so
every device mutation/observation below is queued, not skipped:

1. Full pipeline: `misc/build_on_ios.sh` (make package →
   `macws_artifact_contract.py verify-package` → `dpkg -i`) — record
   the final command, real exit, and source/build/deployed sha256 for
   each installed file (including both thin libmachook slices and the
   Apple-ld64 MacWSWindowing artifact). R007 deploy failures to
   preserve: `r007-deploy3.json` exit 134, `r007-deploy4.json` exit 1
   (`strings` missing) — kept in `coordination/artifacts/`.
2. Cold-start witness: `macos_gui.sh stop && macos_gui.sh start
   coexist`; expect `macOS system tccd endpoint ready (pid=…)` and the
   exact job+endpoint in `launchctl print user/501/com.macwsguide.tccd-system`.
3. Recovery witness: trigger `recover_ws_dependents`/desktop rebuild;
   expect the same ready line before dependent jobs; verify the job is
   re-armed after `stop_ws_dependents`.
4. Reply capture: the `MACWS_LSXPC_TRACE=1` + `preflight` recipe above;
   archive the `#### LSXPC` request/reply blocks verbatim.
5. Cleanup inventory (reviewer-found, owned-only):
   `…/rootfs/usr/local/bin/tccd_nosig`, `tccd_adhoc`,
   `tccd_fullent`, `tccd_eA/eB/e1/e2`, `daxadhoc`, `mysvcx`,
   `spawnreason*`, `execreason`, `tccd_p/q/m`, `/tmp/tccd_inv/` staging
   and any `/var/mobile` copies; verify both
   `/tmp/macws_runtime_diagnostics` namespaces absent; no debugger,
   tmux, or stray probe processes. Remove only enumerated owned files —
   no globs.
6. Thermal: run `macwsthermal` before the pipeline (reviewer snapshot:
   serious, raw 2, 37.29 °C, exit 3); record the fresh value; respect
   `application_trust_thermally_safe` — do not weaken the gate.
7. Native health: iOS `com.apple.tccd` pid/uid unchanged, backboardd
   alive, `chroot /var/mnt/rootfs /bin/echo HI` baseline.

## Honest limits

- No device runtime evidence is claimed for R008 changes; readiness
  and admission are proven by source review + fixtures + local build.
- `result=false` is NOT asserted; the reply is "genuine dictionary,
  accessor-zero" pending the decoded capture.
- The probe's `ping` witness proves registration + reachability + a
  live responder; it does not assert policy answers.
- `macos_gui.sh`'s `launchctl list <label>` warm check resolves the
  label in launchd's default domain search — if a future environment
  loads the job where `list` cannot see it, the cold path reloads;
  `launchctl load` of an already-loaded job exits nonzero and fails
  closed (no duplicate manager risk, at the cost of a reported start
  failure rather than silent reuse — matching the existing fail-closed
  convention).

## 2026-10-18 — reviewer edge closure (local, no device mutation)

Re-ran the unmodified reviewer script
`coordination/artifacts/R008-review-edge-repro.py` (candidate functions,
fake OS tools only). All six saved defects now fail closed:

| Case | Before | After | What changed |
| --- | ---: | ---: | --- |
| `endpoint_token_only_in_environment` | 0 | **1** | Endpoint token is searched only inside the job's own `services`/`endpoints` block; the `environment` block is excluded. |
| `observation_error_still_unloads_job` | 1 | **1** | `tccd_transport_state` returns 0 ready / 1 observed-broken / 2 unknown. Warm path unloads only on 1; a failed lookup (2) leaves the job untouched. |
| `missing_mandatory_path` | 0 | **1** | Every supplied prerequisite path must be an existing file or directory; an unknown path-shaped argument fails the fast path. |
| `mandatory_target_empty_codes` | 0 | **1** | A manifest entry with no usable, non-empty CodeDirectory set cannot satisfy coverage. |
| `trust_query_nonzero_with_partial_stdout` | 0 | **1** | `jbctl trustcache info` must exit 0 before its stdout is treated as a complete membership answer. |
| `mandatory_signature_trust_failure_swallowed` | 0 | **1** | `trust_installed_macho <path> 1` verifies live membership after admission; `ensure_tccd_system_signature` and every native service propagate a failed query/missing CDHash. The deep postinst's `trust_cdhash` no longer records a failed `jbctl add` as trusted. |

Readiness details: `launchctl print` must start with the exact
`<domain>/<label> = {` header; four definite "Could not find service"
answers are required to call a job absent. Cold-path reload is still
allowed for an observed-but-broken job (matching
`test_warm_no_endpoint_is_not_ready`), while an observation error is
neither success nor permission to destroy the job.

Manifest fast path: the optional `$ROOTFS$VNC_BIN`, `$ROOTFS/tmp/GlassDemo`
and `$ROOTFS/opt/local/libexec/macws-cursor` are only supplied when
present; the mandatory tccd/WindowServer/dyld/packaged targets remain
unconditional.

Local verification (host):

- `python3 misc/test_tccd_lifecycle_contract.py` — **27/27 pass**,
  including new fixtures for the environment false-ready, the
  observation-error no-restart invariant, the three trust-manifest
  edges, the strict-trust positive control and the array-scoped
  `tcc.allow` parser.
- Full `misc/test_*.py` suite: 710 tests, **14 failures / 5 errors /
  14 skipped**. The failures are the previously known hardware/Metal
  probes and in-progress AppInput/provider tests in the shared working
  tree; the tccd lifecycle module passes inside the suite. This is a
  recorded baseline, not a green-suite claim.
- Final source hashes: `macos_gui.sh`
  `471fc752ed72d0a376ef747d834530607c484839b1d2af2585a4eb3fab6ebc9d`,
  `layout/DEBIAN/postinst`
  `c45c1d366070966b3b1ac57cca3379d07c4589b3857dda3f9d759dbe07c3b202`,
  `layout/usr/macOS/bin/postinst.sh`
  `ed42f4782563e1110f0d86fc0d0a279b48eeee9b4c7cb4013e15d58948fe2df1`,
  `misc/test_tccd_lifecycle_contract.py`
  `11dd30f3c225b1c9fbc12bde53184ad387a1a57c85bdf1f56d39473b0420f8dc`,
  `MTLCompilerBypassOSCheck/Tweak.x`
  `d1b7bc89a058e9925cf894134c2ea4dbbe7e16fa62e1e45ed48944aab0446fcc`.
- Host build portability (not a runtime change): the host Theos resolves
  `latest` to Xcode's iPhoneOS 26.2 SDK. `MTLCompilerBypassOSCheck/Tweak.x`
  now includes `<stdatomic.h>` and uses the real `<xpc/xpc.h>` when
  present, falling back to the existing `void *` ABI declarations on the
  old SDK. Behavior and call sites are unchanged.

Device validation, cold/recovery, reply capture and input acceptance for
this candidate are recorded in
`coordination/artifacts/R008-device-acceptance-20261018.json`. Summary:

- Package built from a clean `HEAD` worktree + this patch (the shared tree
  also held another worker's uncommitted AppInput/provider changes, which
  were excluded), `dpkg -i` and full postinst exit 0, both thin
  libmachook slices and the Apple-ld64 MacWSWindowing artifact verified.
- Cold start `start coexist --no-vnc` exit 0: tccd protocol round-trip
  ready, endpoint in `user/501`, WS stable. The VNC-enabled
  (`MACWS_VNC_SHARE=1`) start stalls after the AGX probes and is recorded
  as a separate open issue, not acceptance.
- Attributable preflight: caller pid/uid/audit token, connection
  `com.apple.macosbooter.tccd.system`, a 7-key request and a genuine
  **empty dictionary reply** (`count=0`, no `result`/`preflight_unknown`/
  `restricted`/`do_not_cache` key), SPI result 1. This is a missing-key
  default, **not** an explicit policy refusal.
- Recovery: killing WS 42862 produced replacement 43210; the dependent
  stop removed tccd and the repair path republished it as PID 43545 with
  a new protocol round-trip — F1 ordering witnessed on the real path.
- Input still returns `SLSPostEventRecord -> rc=1002`; no visible input
  is claimed. WS-sender reply capture remains unavailable: the production
  preflight rejects `MACWS_LSXPC_TRACE` in the WS plist and no on-device
  debugger is installed, so only the probe-context schema is asserted.

Union correction from the device run: the package's original merge order
(project → native → native) silently dropped `kTCCServiceListenEvent`
from `com.apple.private.tcc.allow`; the final native pass replaced the
array. The fix applies the project profile once more, last, through
`ensure_chroot_native_service_signature`'s optional final profile and the
deep-repair helper's seventh argument. The deployed tccd now reports 124
keys with `allow = [kTCCServiceListenEvent,
kTCCServiceSystemPolicyAllFiles]` and the native
`notificationcenterui.tcc` key, identifier `com.apple.tccd`.
