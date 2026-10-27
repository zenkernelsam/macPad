# D051 — decoder ↔ helper cross-validation (independent, read-only)

Date 2026-10-19, packet `coordination/_relay/devin-051-frontend-task.md`.
**Scope: local read-only audit of saved D048 helper artifacts against the
frozen observer ABI + the D049 strict decoder. No device contact, no
backend interruption, no production/source/Git changes.** Desktop
untouched.

## 1. Stale-wording corrections applied

D049 stated "the backend's arm64e/PAC helper is still running, not
accepted" — stale. Corrected in place (D049 doc + result):

- `misc/macws_sender_obs_probe.c` (`f435057a`, sha256-prefix verified
  against the on-disk file this round) ran **15/15 PASS on device** —
  helper-level only: real `*_invoke` bodies + mock originals + a real
  PAC-signed function-pointer chain, run from `/var/jb/tmp`.
- This is NOT real-WindowServer hook-install acceptance. WS-level status
  stays "structurally ready, pending coordinator-elected deployment
  review."

## 2. Frozen linkage — all verified this round

| artifact | declared hash | status |
|---|---|---|
| `MacWSSenderObservation.h` | `a7c8438b…` | matches D048 snapshot |
| `MacWSSenderObservation.c` | `6641d9df…` | the `**env->audit_slot` fix reviewed D048 |
| `mac_hooks.m` | `7321d92c…` | unchanged |
| `misc/test_sender_observation.c` | `39d6791e…` | frozen suite ALL PASS |
| fat dylib `fddc64c1…` (arm64 `5B9F5C19` / arm64e `353600B3`) | verified D048 | unchanged |
| probe src `f435057a` | verified | on-disk file re-hashed this round |

Production-vs-test macro boundary: production `.theos` object provably
macro-free (no `-DMACWS_SENDER_OBS_TEST` in build log; `.o`/dylib export
only the real API — D048 nm check). The probe carries the macro on its
own compile only.

## 3. Precise missing input — no saved binary device log

The probe's `/tmp/macws_d048_probe_log` was **packet-owned and
self-cleaned** on device — no binary device record file survives to feed
the decoder. Therefore:

- **Usable evidence = the probe transcript's proven assertions only**
  (15/15 list, ordering note, errno/disarm witnesses). No synthetic
  substitution was made.
- The only binary log that exists remains the **host fixture** (256
  records, `7e8bbfbf`, produced by the frozen host test) — decode
  `verdict=CLEAN`, `BUDGET_EDGE` at seq 255.
- Mock-vs-real distinction preserved: probe token/desc bytes are test
  fixtures, not sender audit tokens; invoke bodies are production, the
  "originals" are mocks; PAC fn-ptr identity was verified but no
  SkyLight callsite ran.

## 4. Decoder re-run — 26 checks ALL PASS (21 + 5 adversarial)

Re-ran the full suite against the frozen decoder; added the cases the
packet named. New results (all PASS):

| case | what it proves |
|---|---|
| `inner_first` | D048 ordering evidence handled: nested emits arrive **impl→disp→gate** sharing one invocation id — grouped by invocation, order-agnostic matrix still yields `CHAIN_FULL`/`EVALUATED_ALLOW` |
| `impl_absent` | gate+disp present (rc=1), impl record missing → `NO_IMPL_RECORDED(missing evidence)` — **never** mislabeled resolver-fail or cache-deny on absent descendant alone |
| `untrusted_chain` | interior tear → strict `CORRUPT`; `--recover` → `SKIPPED_BYTES` + chain `UNTRUSTED` — recovered chains cannot prove cause |
| `budget_drop` | seq hits 255 → `BUDGET_EDGE` + `INVISIBLE` — later calls are invisible to the log, never "zero calls" |

## 5. Matrix audit — one label tightened for missing-evidence honesty

`GATE_DENY_NO_DISP` → **`GATE_DENY_AFTER_VALID_AUDIT(disp record absent —
dropped inner record not excluded)`**. Rationale: `gate rc=0` with a
valid audit envelope (`FLAG_AUDIT` set, `audit_size≥0x34`) means the
dispatcher path ran — the structural arms (A: container NULL, B:
undersized) are already excluded by captured flag/scalar. A missing
dispatcher record is missing evidence, not proof of a gate-internal deny.
Arm A/B classifications are unchanged — they key on the captured
pointer/size + record flags, not on descendant absence, which is exactly
the packet's required discipline.

## 6. D050 producer findings — reviewed (stable, saved)

`devin-d050-empty-tcc-reply-producer-20261019.md` resolves the EMPTY
producer: the real Ventura tccd authors `count=0` at the **attribution
layer**, before policy evaluation — observed in tccd's own os_log
(`responsibility_get_responsible_audit_token_for_audit_token() (#103)
Policy not found` → "Failed to create attribution chain from message."
→ unconditional empty send at `0x69e0`). Cross-confirmed statically on
the R002-fingerprinted tccd (`42c388e8`, arm64e `9845D17F`).

Consequence for the observer matrix: WS-side 1002 produced **zero tccd
lines** in-window. **[CORRECTED D052:]** zero observed lines do NOT
prove a cached/local WS deny — tccd's REQUEST log is INFO-level
`os_log_type_enabled`-gated and capture coverage of that subsystem in
the window is unverified; consistent with cached deny, gate-arm deny,
or unlogged delivery. It is not discriminating evidence. A future
observer decode showing `lazy_post≠0 && impl_rc=0` + EMPTY reply would
read as the attribution bail (D050), still not an explicit policy
verdict — `raw1`/`impl_rc=0` remains a collapsed class.

## 7. Smallest next causal experiment + prerequisites

Backend's D050 §5 discriminator, unchanged and still minimal: **post the
byte-identical 7-key `TCCAccessRequest` from a responsibility-attributed
sender** (iPadOS-launchd-spawned — an existing iOS-side macws daemon or
a tiny `/var/jb` helper; the system-domain endpoint is reachable from a
natively-launched process). One bounded post + LSXPC reply dump.

Discrimination: non-empty reply with verdict field → attribution was the
sole blocker (fix direction = spawn-side `auid≥0` + responsibility
registration for chroot processes); same empty+same error → the private
endpoint's identity requirements are the layer.

Prerequisites preserving the current desktop: no WS restart, no sentinel
changes, no observer install — the experiment is sender-side only and
reuses an already-running process.

## 8. Limits (unchanged)

- No live device log exists to decode — helper transcript assertions
  only; no synthetic device records were fabricated.
- `raw1`/`impl_rc=0` still collapses unresolved-checker vs
  evaluated-nonallow; three hooks cannot prove explicit policy.
- Decoder remains disposable/local diagnostic code, not production.
