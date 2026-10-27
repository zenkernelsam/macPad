# devin-101 — backend result

D100 errata complete. Offline only — no device/probe/setter/KRW/TCC,
no builds, no Git.
Evidence: `docs/evidence/devin-d101-backend-20261019.md`.

- **Erratum 1 (downgrade)**: "absent entry returns self" is THEORY —
  the binary proves only a self-responsible *caller* branch
  (rv==0 + pid-equal + binaryPath-equal → IsValid=1/nil). Provider
  self-token behavior is kernel-side (closed Quarantine policy) +
  libsystem wrapper (`U` import, absent locally). Provider-absent
  (ENOPOLICY) and provider-error remain structurally identical to the
  caller — same rv!=0 tail.
- **Erratum 2 (correction)**: errno IS recoverable — logger
  `0x1000519e0` reads `__error()` twice → `(#%d) %{public}s`. Two
  caveats: the reads follow ~8 intervening calls (logged value =
  errno at read time, not provably the getter's); and a value ≠ its
  producing kernel branch (D068/R014). What stays true: control flow
  is errno-blind — one `cbz` gate.
- **Gates verified at exact addresses**: strcmp dispatch
  `0x10006cf4` → `macos_isSystemServer` `0x10006d08` →
  `bl 0x100008c70` `0x10006d1c`; entitlement arg confirmed as
  __cfstring `com.apple.private.tcc.manager.set-responsible` via
  literal @`0x10007d6a8`.
- **Erratum 0**: three dropped-digit VAs corrected in D100
  (`0x10008c70`→`0x100008c70` family).
- v3 packet mechanism/rationale unchanged; evidence model tightened.

(handover to codex now)
