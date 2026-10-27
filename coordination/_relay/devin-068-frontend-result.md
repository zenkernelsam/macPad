# D068 frontend result — single causal WS observation, readiness design

Design only; nothing executed. No device ops, KRW, MAC syscalls, TCC
requests, setters, deployment, restart. Desktop untouched. Evidence:
`docs/evidence/devin-d068-frontend-20261019.md`; in-place corrections
applied to `devin-d042-baseline-global-boundary-20261019.md` §4.

## Corrections (preserved-history, required by task)

1. **D042 `impl_rc` direction was inverted**: impl returns
   `(preflight_rc==0)` via `cset eq` — `impl_rc≠0` = TCC *allowed*,
   `impl_rc=0` = nonzero-or-unresolved. Both matrix rows corrected.
2. **Absent impl records ≠ absent calls** — missing records also mean
   hook-not-installed / dropped emits / disarmed / pre-arm calls. The
   decision row is valid only with install proof + drop accounting.
3. **`impl_rc=0` is not an explicit verdict** — conflates
   unresolved-symbol (`lazy_post==0`), evaluation failure (#103/empty
   reply), and true denial. `lazy_post` splits the first; the
   correlated tccd ERROR line / reply-fill splits the rest.

## The prepared single observation

One bounded `macos_ws_event_probe post` (D041 canonical shape) under
the dedicated sender-observation sentinel — *not* the
runtime-diagnostics umbrella (it would arm an alternate display
producer; confound avoided by contract). Records: gate/disp/impl
ring+log (144B records, seq integrity, SEQ_GAPS drop accounting,
invocation chain correlation) + WS stderr delta + tccd-system stderr
delta + before/after `macws_final_composite.state` + pixel hash.

**Decision spine**: proven-installed + zero-drop + gate-only → deny is
pre-TCC (provider irrelevant to input); `lazy_post=0,rc=0` → missing
TCC symbol in-WS (local fixable); `lazy_post≠0,rc=0`+tccd ERROR/empty →
evaluation failure (provider hypothesis on causal path);
`lazy_post≠0,rc=0`+populated deny → real verdict; `rc≠0`+gate deny →
cache-fold producer.

## Prerequisites / blockers (enumerated, unexecuted)

1. Read current-generation `SENDEROBS-INSTALL … permitImpl` status —
   hooks arm only at WS launch under the sentinel; absent ⇒ a WS
   restart is required, which is a separate heavyweight authorization
   (rollback = sentinel removal + restart under session protocol).
2. Pin deployed libmachook + probe sha256 against repo artifacts.
3. Explicit signoff for WS→tccd transitive traffic induced by the post.
4. Live counter-reader for `site_calls/dropped/snaps` optional; the
   log's SEQ_GAPS accounting bounds it otherwise.

Fully reversible: rm sentinel = live disarm (verified D046), rm logs;
record-only wrappers never alter returns; watchdog restart mid-window
is detectable via producer-pid/state-file witness.

(handover to codex now)
