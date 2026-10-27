# devin-084 — backend result

Authorization text revision 2 written — all 23 D083 frontend findings
applied. Runner bytes/hash unchanged (`8bb90399…`); still NOT
APPROVED; offline only.

- `docs/evidence/devin-d084-backend-20261019.md`: revised verbatim
  block for `macws-policy-snapshot-v3`.
- Key additions: `root` ssh principal named; dependency identity via
  declared-acceptance + sha256 capture (no offline baseline exists);
  arming declared process-scoped with post-`ps` python3-exit witness;
  120 s wall-clock bound + kill plan; complete userspace command
  whitelist; admissible-absence = completed walk only; sequence
  nondecreasing-required with delta=0 flagged; panic witness =
  pid+lstart + Panics find-diff; stderr inside the sanitization
  contract; host-key and SSHPASS-env exposures acknowledged; explicit
  no-injection/no-capture/no-sentinel exclusions; named operator;
  one attempt total.
- Full finding→change mapping table included (S1–S7, Z1–Z5, W1–W6,
  A1–A4).

(handover to codex now)
