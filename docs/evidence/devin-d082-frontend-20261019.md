# D082 frontend — line-precise patch plan: `/tmp/macws_frame_pace_diagnostics`

**Plan only — no patch applied.** Offline source review of
`libmachook/mac_hooks.m` (current checkout). No build, deploy,
restart, device contact, injection, credentials, source edit, or Git
write.

## 1. What exists today (verified line-precise)

| Piece | Location | Behavior |
|---|---|---|
| Umbrella gate | `mac_hooks.m:273` `macws_runtime_diagnostics_enabled()` | **cached-once** (`static _Atomic int cached = -1`, first call latches); ORs 2 env vars + 11 sentinel files incl. `/tmp/macws_runtime_diagnostics` — the umbrella that also activates an alternate display producer (confound) |
| Dedicated helper precedent | `mac_hooks.m:9074–9076` `macws_keyboard_latency_diagnostics_enabled()` | `return access("/tmp/macws_keyboard_latency_diagnostics", F_OK) == 0;` — **uncached**, per-call; D041-proven lifecycle (arm ~10 min → trace → remove, zero residue) |
| COEXIST site 1 | `mac_hooks.m:20056` | `if (macws_runtime_diagnostics_enabled()) {` inside `macws_iomfbserver_begin_skylight_update`; emits `#### COEXIST pre-lock completion pace #N: interval=.. slept=.. server=..` — **already bounded**: `sequence <= 4 || sequence % 600 == 0` (`:20057–20059`) |
| COEXIST site 2 | `mac_hooks.m:27395` | `if (macws_runtime_diagnostics_enabled()) {` inside `macws_coexist_activity_pace_us`; emits `#### COEXIST DIAGNOSTIC activity pace: mode=.. pace=.. idle=.. renderTarget=.. record=.. window=1000ms interactionAgeMS=.. renderAgeMS=..` — **already bounded**: mode-transition only (`prior != mode`, `:27396–27399`) |

## 2. The patch plan (≈7 lines, two call sites + one helper)

**Insert** after line 9076 (next to the sibling helper, same style):

```objc
static BOOL macws_frame_pace_diagnostics_enabled(void) {
    return access("/tmp/macws_frame_pace_diagnostics", F_OK) == 0;
}
```

**Edit 1** — `mac_hooks.m:20056`:
```diff
-        if (macws_runtime_diagnostics_enabled()) {
+        if (macws_runtime_diagnostics_enabled() ||
+            macws_frame_pace_diagnostics_enabled()) {
```

**Edit 2** — `mac_hooks.m:27395`:
```diff
-    if (macws_runtime_diagnostics_enabled()) {
+    if (macws_runtime_diagnostics_enabled() ||
+        macws_frame_pace_diagnostics_enabled()) {
```

**OR-ing, not replacing** — preserves every existing umbrella behavior
for generations that legitimately run with the umbrella armed; the new
sentinel adds an independent, narrow gate. Both emit paths already
carry their own output bounds, so no additional throttle is needed.

## 3. Semantics contract (why this shape)

- **Uncached `access(F_OK)`** — deliberate, matching the
  keyboard-latency precedent: create/remove takes effect between
  frames, no process restart needed to arm/disarm *after* the code is
  already resident. The umbrella's cached-once latch is precisely
  what makes it unusable for this (see §4).
- **Fail-closed**: file absent → `access` returns -1 → both sites
  behave exactly as today. No state, no side effects beyond two
  bounded `fprintf`s.
- **Emit volume**: site 1 → first 4 paced updates then every 600th;
  site 2 → mode transitions only. Same stderr channel already used by
  `#### CGXSYNTH`.
- **Namespace contract**: the check is `/tmp/…` *inside the chroot* —
  host path `/var/mnt/rootfs/tmp/macws_frame_pace_diagnostics`.
  Arm/disarm must use the rootfs spelling; the prior sentinel-cleanup
  landmine applies (sweep **both** `/tmp` and `/var/mnt/rootfs/tmp`
  on removal so no stale file silently arms a later WS generation).
- **Measurement produced**: `interval=` (pace floor chosen) and
  `slept=` (actual wait) per paced update, plus `mode`, `idle`,
  `renderTarget`, `interactionAgeMS`, `renderAgeMS` on transitions —
  exactly the "artificial frame wait vs service-message availability"
  comparison the share lesson requires, with **zero** display-producer
  confound (this sentinel touches nothing else).

## 4. Why the current generation cannot be measured (FACT)

1. The only existing emit paths are gated on the umbrella helper,
   which is **cached-once** — even if the file were created now, the
   already-running WS latched the negative on its first call; arming
   cannot take effect.
2. Arming the umbrella is additionally the *wrong* toggle by contract:
   it activates the alternate display producer → any wait observed
   would be on a non-production display path (non-causal).
3. The helper + two gates live inside the deployed `libmachook`
   binary — no file on disk can inject new code into the running
   process. There is no existing non-confounding toggle in the
   deployed image.

## 5. Authorization required to activate (not granted)

1. Rebuild `libmachook` (both slices) with the ≈7-line change; pin the
   resulting sha256.
2. Redeploy per the established recipe (signing + trustcache
   re-admission where the binary identity changes).
3. **WS restart** under the full session/rollback protocol (composite
   state + producer witness capture, sentinel sweep in both
   namespaces, rollback = revert dylib + restart) — hooks/pacing code
   only install at process launch, so this is restart-bound.
4. Sentinel lifecycle signoff: creating
   `/var/mnt/rootfs/tmp/macws_frame_pace_diagnostics` is a chroot
   filesystem mutation — minimal and D041-precedented, but still an
   explicit grant.

Until all four exist, this remains a *prepared* diagnostic, not a
measurement.

## Provenance

`libmachook/mac_hooks.m` lines 273–292, 9074–9076, 20046–20066,
27354–27428 reviewed verbatim this round. Prior context:
`devin-d071-frontend-20261019.md`, `devin-d072-frontend-20261019.md`.
