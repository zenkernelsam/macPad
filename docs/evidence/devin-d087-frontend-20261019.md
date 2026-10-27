# D087 — frontend self-audit of the D086 corrected pacing plan

Offline audit only. Nothing applied; no execution, device, build,
deploy, restart, source, or Git.

Subject: `docs/evidence/devin-d086-frontend-20261019.md` re-checked
against D085-backend findings A–E and the current checkout.

## Verified clean

- **Line pins** — all re-verified: filter :331–:341, convention
  :343–:349, umbrella :273–:292 (2 env + 12 `access()`), kbd pair
  :9074/:9082–:9097, arm site :6477–:6485, site-1 flag :20050, gate
  :20056, bound `<=4||%600` at :20060, site-2 gate :27395,
  transition bound :27400; launcher :5725/:5689/:3768/:5959/:6043/:3531.
- **Uncached post-launch arming is real** — `clear_diagnostic_state`
  runs only at launch (:3771 inside preflight, :3976 second callsite);
  nothing sweeps mid-session, so a post-launch `touch` survives until
  next launch (registered ⇒ swept then = free cleanup).
- **Output scope not widened** — trace helper is file-scope `static`,
  reachable only from the two sites; dedicated path drops `server=%p`;
  site-2 fields carry no pointers; umbrella `fprintf` untouched.
- **Registration/rollback complete** — `flag`/`off` tsv row ⇒
  `diagnostic_flag_paths` sweep + preflight rejection + audit check
  (`audit_runtime_switches.py:92` builds the path list from
  `flag` rows in `{off,transient}` — correct kind for the sentinel);
  `.trace` correctly unregistered (a `state`-kind row would *not* be
  swept anyway — flag-list is flag-kind-only — so protocol `rm` is
  the right mechanism, precedent-consistent).
- **Rollback** — pinned-dylib revert + WS restart + dual-namespace rm
  + registered-sweep backstop: complete.

## Concrete residuals

- **R1 — uncached `access(2)` inside a per-frame gate (new; both D086
  and backend's Finding-A correction share the conflation).**
  Site-1's gate sits inside `begin_skylight_update` (:20048) — the
  per-frame update hook. `umbrella || dedicated` evaluates the helper
  **every call**, not only at bounded emits: uncached means
  `access(2)` at frame rate (~60–120 Hz) — exactly the hot-path
  regression class the :343–:349 convention cites (39 access(2)/6 s in
  a production sample). The throttle bounds bound the *emit*, never
  the *gate evaluation*; the keyboard-latency precedent doesn't
  transfer (per-keystroke ≠ per-frame).
  **Fix (plan-level):** rate-limit the sentinel check —
  `armed_acked`-style: cached value + last-check timestamp, re-stat at
  ≤1 Hz, cost per frame = one atomic load after the first check.
  Preserves post-launch arming (≤1 s latency) and live disarm (≤1 s).
  Alternative latch-once-true is simpler but loses live disarm.
- **R4 — ack placement merges into R1's fix.** In D086 the
  `sentinel-armed` line fires inside the trace helper, i.e., at the
  first *bounded emit* — so "armed but bounds never hit" still
  produces an empty trace (the table row relying on the ack line
  fails). Emit the ack on the rate-limited check's **0→1 armed-edge**
  instead: within ~1 s of arming the line exists regardless of emit
  bounds; disarm→re-arm produces a fresh ack per window (also fixes
  R3's re-arm gap).
- **R2 — normal-path cleanup step unstated.** D086 covers pre-arm
  `rm` and rollback `rm`; the ordinary flow must also say: retrieve
  trace → `rm` trace + `rm` sentinel (dual namespace) — the `.trace`
  is never auto-swept.
- **R3 — process-latched `armed_acked` would miss re-arms.** Superseded
  by R4's edge-triggered design (one ack per arm window); recorded so
  the rationale isn't lost.

## Patch-shape delta from D086

Same 3 files. `mac_hooks.m` grows by the rate-limited check (~10
lines: two atomics, monotonic clock, re-stat, edge-emit call) — total
≈45 lines across 3 regions; honest count updated. No launcher change;
registration/audit/rollback as in D086.

## Verdict

D086 resolves all five D085-backend findings *as designed*, but R1
makes the as-written plan un-applyable under the file's own hot-path
rule — fix is small (rate-limited check + edge-emit ack) and stays
inside the D041 pattern. Not applied; no execution implied.
