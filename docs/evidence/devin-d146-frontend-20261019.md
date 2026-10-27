# D146 — provider/permit positive-witness audit (frozen D127/D130/D134)

Scope: what directly proves, in the current WindowServer generation,
(1) the CydiaSubstrate provider image is mapped, (2) each relevant hook
permit was registered, (3) the QC cancel-completion publisher hook is
installed and firing. Evidence classes separated. No device access.

## 0. Structural fact that governs everything

`mac_hooks.m:330-341`: `#define fprintf macws_filtered_fprintf` — every
`fprintf(stderr,…)` inside libmachook returns 0 unless
`macws_runtime_diagnostics_enabled()` is true, and that gate is a
`static _Atomic int cached` latch sampled once (:273-). The provider's
`macws_hook_log` (`cydiasubstrate_macos.m:436`) is raw `write(2)` — it
always lands on fd 2.

**Consequence**: a production WindowServer.err can *only ever* contain
`MACWSHook: REFUSED …` lines (provider, raw write). All `####`-prefixed
install/firing lines are unreachable in any non-sentinel generation —
their absence carries zero information, and *creating* the sentinel now
cannot unlatch the already-sampled cache.

## 1. Provider image mapped in the running WS

**Direct witness exists, but only as transcription for the current
generation.** `MACWSHook: REFUSED no permit for target` is emitted by the
provider's own MSHookFunction — its presence in WindowServer.err proves
the provider image is mapped and its code executed inside that process.

- r013-era saved artifact `analysis/r013-display/device-logs/
  WindowServer.err`: **58 REFUSED lines** — direct, but an older
  generation (Oct-18, pre-D127 binaries).
- D130 generation (WS 22527): D130 quotes "17 REFUSED lines in
  WindowServer.err (and osxvnc.log)" — **the raw file was not saved**;
  the count rests on doc transcription (same artifact gap D134 flagged).
- Verdict: mechanism directly proven; current-generation instance is
  transcription-grade only. Requires re-capture (§4) to upgrade.

## 2. Each relevant permit registered

**No direct positive witness exists by design.** `macws_register_
hook_permit` success has no log; the `#### …installed` fprintfs are
filtered (§0). Success is observable only through downstream behavior:

- **QC cancel-completion observer permit** (`enableTagListOffset`,
  :19992): inference chain — displayd's `macws_final_composite.state`
  showed `state=ready producer=22527 sequence=141→622
  reason=validated-final-composite` advancing live. The publisher
  `macws_publish_scanout_after_swap` has exactly **one** callsite in the
  tree (:21856, inside the frame-info callback); the callback runs only
  if the observer hook installed; the hook installs only if
  `permit_rc == 0`. Chain is source-grounded, not a log witness.
- **IOMFB publicSwapEnd permit** (:6425): transitively covered — the
  hooked wrapper performs the SwapEnd→SwapCancel translation that *is*
  the cancellation pipeline the observer listens to; no cancel-
  completions ⇒ no callback ⇒ no publishes. Advancing sequence therefore
  evidences this permit too.
- **Pacing begin/finish permits** (:20127/29): **NOT evidenced** —
  pacing is an optional behavior layer (`g_macws_iomfb_coexist_swap_
  cancel` gate); the publish chain does not traverse it. No saved
  artifact distinguishes pacing armed vs skipped.
- Composite-pipeline hooks (StartComposite*/EndUpdate/etc.): correctly
  unregistered by design — REFUSED lines for them are expected, not
  failures (D130/D134 concur; re-verified: no registration sites).

**Integrity caution for any future "installed" line**: on refusal the
provider sets `*result = orig_symbol` (non-NULL) — so a gated-ON
`#### …installed target=%p trampoline=%p` line does NOT alone prove
install; discriminate by `trampoline != target` plus absence of a
same-target REFUSED line.

## 3. QC publisher installed / firing

- **Firing — consumer-side direct**: displayd's state file is written by
  the validator on accepted frames — `validated-final-composite` +
  advancing seq + `producer=WS-pid` is a direct record that publishes
  arrived and passed validation (not inference; the consumer observed).
- **Firing — producer-side**: `#### IOMFB CANCEL-COMPLETION delivered`
  exists only behind the latched diagnostics gate → unreachable without
  cold start.
- **Installed**: only the §2 inference chain; no direct witness (the
  install fprintf is gated; the sole callsite argument converts
  consumer-side delivery into install evidence).

## 4. Smallest additional observation, no restart

1. Re-read `/var/mnt/rootfs/tmp/macws_final_composite.state` twice —
   seq still advancing with same `producer` pid = publisher still firing
   in the live generation (consumer-side, refreshes cheaply).
2. Save the live WindowServer `.err` as a raw artifact — REFUSED lines
   confirm provider mapping **for this generation**; also closes the
   D134 transcription gap.
3. Confirm WS pid liveness for the producer-pid binding.
4. **Strongest available positive witness**: read the 16 bytes at
   `publicSwapEndOffset` (IOMFB image) and `enableTagListOffset`
   (QuartzCore) in the live WS address space — the patch stub
   (`ldr x17, #8; br x17; .quad target`) is unambiguous: stub present =
   hook installed; original prologue = not. `task_for_pid`+vm_read or
   lldb — read-only, no restart, decides install independent of logs.
   Same technique covers the pacing begin/finish offsets (the only way
   to witness pacing permits at all, §2).
5. displayd-side replay/metrics: none exist for pacing — item 4 is the
   only path.

## 5. Inherently cold-SkyLight-load items

- Any `####`-prefixed line (install confirms, `delivered` firing logs,
  pacing `installed`/`pace #N`) — latched gate, must exist before first
  filtered call in WS.
- Permit `rc` values on success — never emitted anywhere.
- SENDEROBS gate/dispatcher/impl hooks — install only at SkyLight-load
  with sentinel armed (D130 L3, re-confirmed).
- All load-time witnesses for an already-running generation — the
  trampoline-bytes read (§4.4) is the only retroactive install check.

## Verdict

(1) mapped: direct mechanism, transcription-grade for current gen;
(2) permits: no direct witness exists by design — QC-observer +
SwapEnd permits are strongly inferred via the sole-callsite publish
chain; pacing permits are unwitnessed in every saved generation;
(3) publisher: firing is consumer-direct (state file), install is
source-grounded inference; per-fire log is cold-load-only.
