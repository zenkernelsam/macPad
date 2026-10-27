# D039 — independent runtime acceptance audit of backend D037

Date: 2026-10-18 · Devin frontend 9589 · read-only review of
`docs/evidence/devin-d037-gated-live-desktop-20261018.md` +
`coordination/_relay/devin-037-backend-result.md`. No device access,
no backend contact. Evaluated against the six-rung ladder from
`devin-d037-relocation-final-independent-review-20261018.md` §5 and the
task's frames-vs-visible distinction. FACT = witnessed in saved
artifacts; THEORY = inferred, stated what would confirm.

## Gate 1 — live probe: PASS (independently assessed)

- **Identity**: IOMFB uuid `7AC531B7-05F5-38FE-92E5-DA6D86D22A14` and
  prologue `b4000080 f9439401 b4000041 d61f083f` read from the live WS's
  `dyld_shared_cache_arm64e` — matches the D035 pins exactly (FACT,
  resolves the arm64-vs-arm64e slice-uuid risk: the live WS image IS
  arm64e). Method note: `task_for_pid` denied (`kr=0x5`), so identity
  came from cache extraction — sound: the mapped cache is the image
  source; residual = extraction fidelity, bounded by exact byte match.
- **Execution**: arm64e on-device probe `RESULT: ALL PASS` — the exact
  real wrapper words patched (`58000051 d61f0220` stub), relocated
  `braaz` authenticated an IA+0-signed target and tail-called it with
  x0 preserved; callable original replayed all three paths; named
  refusals clean for b.cond / blrabz / intra-window / unpermitted.
  This closes every device-only residual from my D037 review (FACT).
- **Probe logistics** recorded honestly: `cdhash.py` vs `ldid -h`
  mismatch (kernel wants ldid's CDHash), `/private/var/tmp` exec
  SIGKILL (launch constraint), `ptrauth_strip` needed before data-use
  of function pointers, `launchdchrootexec` needs launchd job context.
  These are test-harness fixes, not provider changes (FACT).

## Gate 2 — controlled A/B: per-rung assessment

| Rung | Verdict | Basis |
|---|---|---|
| 1. Probe pass | PASS | above |
| 2. uuid+prologue match | PASS | live extraction matches all 4 pins (IOMFB + 3 QC) (FACT) |
| 3. Changed bytes in WS | PASS-VIA-FUNCTION | `task_for_pid` unavailable, so no direct in-WS byte dump; but `final-composite-received producer=7763 surface=155 2732x2048` is the behavioral witness — the published surface can only exist if SwapEnd installed AND the QC registration chain fired. Functional proof accepted; a raw in-WS byte read would still be nice-to-have (P3 residual). |
| 4. Counters | PASS | `replay-receiver-ready`, `replay-received 1..7`, displayd `final-composite-received` (FACT per logged lines) |
| 5. State | PASS | `state=ready producer=7763 sequence 9→11→32→46→58→60` over ~190 s, `reason=validated-final-composite` — transport AND displayd validation both confirmed (FACT) |
| 6. Pixels | **PASS (FACT — independently inspected)** | Saved PNG artifacts landed during this audit (`docs/evidence/devin-d037-gated-live-desktop-20261018/`) and were visually verified by this reviewer, not taken on description: `frame-macos-desktop.png` (2732x2048, 9.9 MB) shows a genuine macOS Aqua desktop — Finder menu bar (文件/编辑/显示/前往/窗口/帮助 + status items), Ventura forest/lake wallpaper, macOS Dock (Calendar JUL 17, Contacts, Notes, TV, Music, App Store, System Settings, Terminal, Finder, Trash; several `?` icons consistent with unresolved chroot app icons) — with the iPadOS-native `macPad 控制中心` host panel overlaid (expected host UI). `frame-pre-activation-ipados.png` (2.6 MB) shows real iPadOS WeChat split-view + iPad Dock + status bar (下午11:56 10月18日) — the negative control proves the capture reads the presented scene. Numeric: `nonblack=0.984 mean=107.5` post-`uiopen macwshost://`; displayd `workspace-graph-state wallpaper/dock/menubar/backing=ready`. |

**Frames vs visible, distinctly**: surface TRANSPORT is proven by
counters+state (FACT); DESKTOP VISIBLE is now proven by reviewer-
inspected PNG artifacts (FACT) — menu bar, wallpaper, and Dock are
all literally present in the saved capture.

### Confound check — enumerated channels verified clean against source

- Launch args `start coexist --no-vnc --no-terminal --experimental
  --runtime-cap=300` — `--diagnostics` absent → `WANT_DIAGNOSTICS=0` →
  preflight `rm -f`s `EXPERIMENTAL_RUNTIME_DIAGNOSTICS`
  (macos_gui.sh:5747-5757) → the marker-gated didComplete alternate
  producer was OFF. The ONLY path that could produce
  `final-composite-received` is the new SwapEnd→SwapCancel→
  complete_cancelled_swap→after-swap-publish chain = the relocator-
  dependent hook path. **This A/B is a direct causal witness for the
  D033 attribution** (permit-gate refusal was the regression boundary),
  not merely a correlation.
- `--no-vnc` → VNC/RFB CPU bridge off; production presentation path
  only (:5760-5764 log branch). `--runtime-cap=300` → watchdog-enforced
  bound (:1682-1684), fired as designed.
- iOS-side `/var/jb` provider twin left unchanged — declared scope;
  no second variable (FACT).

### Lifecycle/teardown — CLEAN

Test lease held during the round and released; `macos_gui.sh stop` via
launcher (watchdog job unloaded, no respawn — per D038 audit);
`state=fallback reason=layer-topology` during shutdown is the expected
displayd topology transition; probe leftovers removed; no WS/displayd/
Host crashes; 3 ExcUserFault in the known lightweight baseline class.
Deployment KEPT — consistent with the plan's keep-on-pass rule; the
next ordinary start inherits the fix.

### Honest residuals (non-blocking, recorded for follow-up)

1. No direct in-WS changed-bytes read (task_for_pid denied) — acceptance
   rested on functional chain proof. A pymacho/dyld-map read of
   img+0x11cc in a live generation would add the raw byte rung.
2. ~~No saved pixel PNG~~ — CLOSED during this audit: backend
   published `frame-macos-desktop.png` + `frame-pre-activation-ipados.png`
   under `docs/evidence/devin-d037-gated-live-desktop-20261018/`;
   reviewer visually verified both (real Aqua desktop vs real iPadOS
   negative control).
3. Sequence cadence ~60/190 s — event-driven composites, not a 60 Hz
   stream. Consistent with desktop-at-rest; flag if a continuously
   animating surface shows stalls.
4. `replay-request` replies report `source-completion=0
   source=unavailable published=NO` — **RESOLVED + independently
   verified**: backend's source analysis is accurate per this reviewer's
   own read — `RememberReplaySource` exists only inside
   `MacWSFinalCompositePublisherEnqueueCompletion`
   (MacWSFinalCompositePublisher.m:820/:876), called solely from the
   command-buffer completion path (Metal_hooks.x:10355), dead on this
   build because EndUpdate never runs (established 10-13). The live
   path is SwapEnd → `macws_iomfb_complete_cancelled_swap` →
   `macws_publish_scanout_after_swap` (Metal_hooks.x:8348) →
   `PublishSurface` push gated on `changed || keepaliveDue` where
   `keepaliveDue = elapsedNS >= 4.0e9` (:8427-8431) — a literal 4 s
   keepalive. **Independent cross-check this audit adds**: observed
   sequence 9→60 = 51 publishes in ~190 s ≈ 3.7 s/publish, matching the
   4 s keepalive cadence almost exactly on an idle desktop — the state
   numbers independently corroborate the source-level cadence claim.
   Replay-pull is vestigial on this pipeline, not a delivery defect.
5. Sequence/state `fallback reason=layer-topology` at teardown — expected,
   but worth confirming it wasn't masking an earlier silent fallback
   mid-run (state reads during run all showed ready — documented).

### Artifact integrity + deploy-consistency addendum (verified 00:0x)

- PNG sha256 independently recomputed — both match the evidence doc
  exactly: desktop `1d8ff479…c558e2`, negative control `6bfd1827…7bed0640`.
- Installed-vs-source hash deltas (`f2143308`→`aa041efd`,
  `bf3ae467`→`21f81b5c`, `82e74519`→`134b7eb9`) are expected, not an
  anomaly: `ldid -S<ents> -M` rewrites LC_CODE_SIGNATURE, mutating file
  bytes. Source prefixes match the reviewed D035/D036 artifacts.
- Deploy used `rm`+`cp` (fresh inode) — consistent with the D038
  inode-landmine requirement; preserved-original sha256 recorded for
  all three files (rollback capability documented).
- IOMFB wrapper decode in the doc (`cbz x0,+0x10` / `cbz x1,+8`)
  matches the established capstone decode — both branches land at
  sym+0x10; notation is PC-relative, consistent.
- `signed-impl=0xb84d00…` carries the IA+0 signature in high bits —
  consistent with the braaz authentication claim.

## Verdict

**ACCEPTED — fully evidenced** — the visible desktop is restored;
this is the first *independently verified* visible desktop since the
10-13 baseline (commit `0614a5b` / runtime `0db21ae`, sequence 278+
with framebuffer PNG per the D033 audit — not "since Oct-12"; the
10-13 generation was also visible, so precise wording is "restoration
of the 10-13-era baseline"), produced through the corrected relocator
+ 4-permit chain. Every rung now rests on FACT-level evidence:
probe bytes executed on-device, cache-extracted identity match (with
the stated extraction-vs-mapped-memory limit), functional install
proof, advancing counters/state, and reviewer-inspected PNG artifacts
showing the literal Aqua desktop (with a true iPadOS negative
control). Causation scope: the A/B proves the deployed *bundle*
(relocator + all 4 permits together) is sufficient to restore the
displayed desktop — it does not isolate which subset of the four
targets is individually necessary. Confound scope: the enumerated
confound channels (diagnostics sentinel, VNC path, iOS twin,
teardown-fallback confusion, stale-buffer capture) were each checked
against source and found clean — this is scoped channel verification,
not an exhaustive zero-confound guarantee. Input axis untouched by
design — correct.
