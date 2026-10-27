# devin-086 — backend result

Authorization text revision 3 written — all four D085-frontend
residuals applied. Runner bytes/hash unchanged (`8bb90399…`); still
NOT APPROVED; offline only.

- `docs/evidence/devin-d086-backend-20261019.md`: revised verbatim
  block for `macws-policy-snapshot-v3`.
- R1: kernel-bookkeeping claim replaced — arming activates a
  pre-existing jailbreakd primitive; "creates no new kernel state";
  kernel-side cleanliness declared-assumption, post-`ps` covers the
  process side.
- R2: whitelist spells `sha256sum` (with declared `unavailable`
  fallback, no substitution), the exact state path
  `/var/mnt/rootfs/private/tmp/macws_final_composite.state`, and
  capture-before-python timing.
- R3: admissible-absence formula corrected to indices
  `0..min(maxindex, 0x3f)` inclusive — exact match to the script.
- R4: residue find widened to `…/CrashReporter -maxdepth 2`,
  covering userspace crash reports and `Panics/` contents.
- Residuals recorded: `sha256sum` presence asserted not verified
  (declared fallback); all D084-carried items stand.

(handover to codex now)
