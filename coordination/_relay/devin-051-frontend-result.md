# devin-051-frontend — decoder/helper cross-validation RESULT

**Doc:** `docs/evidence/devin-d051-decoder-helper-crosscheck-20261019.md`
**Decoder state:** `obs_decode.py` `e80cff19` (label fix only),
`test_decode.py` `43456375` (5 adversarial cases added).
**Device/backend/Git:** untouched, not contacted, not polled.

## Corrections applied

- D049 doc+result: "helper still running" → **landed**: probe
  `f435057a` (re-hashed this round) ran **15/15 PASS on device** —
  helper-level only, still NOT real-WS install acceptance.
- Decoder matrix label `GATE_DENY_NO_DISP` →
  `GATE_DENY_AFTER_VALID_AUDIT(disp record absent — dropped inner
  record not excluded)`: `rc=0`+valid audit excludes both structural
  arms, so the only 0-source is the dispatcher path — a missing disp
  record is missing evidence, not proof of gate-internal deny.
  Arm A/B still key on captured pointer/size + record flags, never on
  descendant absence.

## Missing input — precise

**No saved binary device log exists.** Probe log was packet-owned
`/tmp/macws_d048_probe_log` and self-cleaned on device. Usable evidence
= transcript's proven assertions only (15/15, inner-first ordering,
errno/disarm witnesses). No synthetic substitution made. Host fixture
`7e8bbfbf` (256 records) remains the only binary log — CLEAN +
BUDGET_EDGE.

## Decoder re-run — 26 checks ALL PASS

21 original + 5 new adversarial:

- `inner_first` — nested emits arrive impl→disp→gate per D048 ordering
  evidence; invocation-grouped matrix still `CHAIN_FULL`/`EVALUATED_ALLOW`
- `impl_absent` — `NO_IMPL_RECORDED(missing evidence)`; never
  resolver/cache claim on absent descendant alone
- `untrusted_chain` — strict `CORRUPT`; recover → `SKIPPED_BYTES` +
  chain `UNTRUSTED` (recovered chains cannot prove cause)
- `budget_drop` — `BUDGET_EDGE`+`INVISIBLE`; later calls invisible, not
  "zero calls"

## D050 review — resolved, consequence recorded

EMPTY `count=0` producer = **tccd attribution layer** (responsibility103
in tccd's own os_log; unconditional empty send `0x69cc`), before policy.
WS-side 1002 emitted zero tccd lines. **[CORRECTED D052:]** zero lines
do NOT prove cached/local deny (REQUEST log is INFO-gated, capture
coverage unverified) — consistent with cached/arm-level deny or
unlogged delivery. Observer
`lazy_post≠0 && impl_rc=0`+EMPTY now reads as attribution bail, not a
policy verdict.

## Smallest next causal experiment

D050 §5 unchanged: byte-identical 7-key `TCCAccessRequest` from a
responsibility-attributed iOS-side sender (existing macws daemon or
`/var/jb` helper; system-domain endpoint). Non-empty+verdict →
attribution was sole blocker → fix is spawn-side `auid≥0`+registration.
Prerequisites preserve the desktop: sender-side only, no WS restart.

## Honest boundaries

- `raw1`/`impl_rc=0` collapses unresolved-checker vs evaluated-nonallow;
  three hooks cannot prove explicit TCC policy.
- Mocks ≠ real sender tokens; host fixture ≠ device records; helper
  invoke bodies ≠ WS-installed hooks.
- No live WS observation claimed; 4th TCC site remains unauthorized
  design.

(handover to codex now)
