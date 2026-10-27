# devin-131 frontend result — D127/D129 layer review + SENDEROBS decoder

Read-only review; note at
`docs/evidence/devin-d131-frontend-20261019.md`.

## Verdicts

- **D127 layer table confirmed**: layer 1 (package/hash/decode) is the
  only PASS; layers 2–6 NOT REACHED. `mount_bindfs` absence = real
  infrastructure blocker (absent device-wide, not dpkg-owned, not in
  0.3.4 payload; a historical copy exists only in the share-review
  overlay → packaging-history regression). Restored absolute symlink
  `target→/var/jb/usr` provably passes `ensure_jb_usr_bind.sh:29-38`
  (D127 `exit 0`).
- **D126 branch now named**: verbatim `MacWS: mount_bindfs is
  unavailable` (:71) → exit 1. Applying it to the earlier hostd
  attempts remains THEORY.
- **Flagged D127 overclaim**: "the start attempt's own cleanup tore it
  down" has no source line — no script removes
  `/var/mnt/rootfs/var/jb/usr`; disappearance unattributed (a lost
  live bind mount is equally consistent).
- **Inventory bookkeeping**: D127 superseded deployed hashes —
  libmachook `42b75cf3`/`e84d992a`, provider `c97acda3`; D129's
  libmachook anchor must rebind to these.
- **Decoder spec delivered**: 144-B LE frame, exact field offsets,
  magic/version/site/flags validation, seq-gap ⇒ dropped (not corrupt),
  chain rules (CHAIN_ROOT⇒GATE only, STANDALONE⇒DISP/IMPL only,
  invocation≠0, GATE→DISP→IMPL order per (tid,invocation)),
  `size%144` tail ⇒ truncated-tail. No timing inferred — records carry
  no timestamps.
- **D129 cross-check**: accept rule = ≥30/arm + exit_ns clustering
  (ε≈2 ms) + median excess ≥ max(8 ms, 0.5×interval) + services-tid
  only; anything else rejects adaptation. Reopen freshness
  (generation>previous + Visible) stays a separate bar; the D129
  experiment is precondition-blocked until a generation exists.

(handover to codex now)
