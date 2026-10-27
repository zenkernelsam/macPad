# D087 — backend final audit of D086 revision-3 authorization text

Offline audit only. No execution, no approval. Subject:
`docs/evidence/devin-d086-backend-20261019.md` rev-3, checked against
the frozen runner v3 bytes (D080, sha256 `8bb90399…`), the D085-frontend
residual disposition, and the D076 witness contract.

## Verified clean

- All four D085-frontend residuals (R1–R4) correctly applied —
  declared-assumption wording, `sha256sum` + exact state path +
  before-python timing, `0..min(maxindex,0x3f)` formula (matches
  `range(min(midx + 1, 0x40))` exactly), CrashReporter `-maxdepth 2`.
- STOP list is 1:1 with the script's 16 terminals; `exit 9` ≡
  `sys.exit(9)`; emitted fields (kident `*-ok`, hdr counts +
  `entries_off`, `[%d] <empty>`/`name=`/presence flags) all exist in
  the frozen bytes.
- One-attempt semantics explicit ("including one that ends in a
  STOP"); no-capture/no-injection/no-sentinel exclusions name
  `macws_frame_pace_diagnostics`; `root` principal, 120 s + kill plan,
  `sshpass -e`/`read -s`, host-key risk acknowledgment — all intact.
- Whitelist covers ps/cat/sha256sum/find/python3-stdin only; nothing
  else improvisable.

## Concrete residuals found (text-level; runner bytes unaffected)

- **D087-1 — `find` lost `| sort` (witness defect).** D076's witness
  command was `find … | sort`; rev-3's byte-identical baseline diff
  requires deterministic order, but filesystem traversal order is not
  guaranteed. Two identical directory contents could diff nonzero on
  ordering alone → false residue alarm (fail-safe direction: blocks a
  clean run, never fabricates one). Fix: append `| sort` to the
  whitelisted find.
- **D087-2 — declared header read size is stale.** Text says "header
  ×2 (56 B each)". 56 B was v1's kread64-everywhere size; v3 reads
  `6×kread32` (24 B) + `1×kread64` (8 B) = **32 B per pass, 64 B
  total** (`:84–85`). Correct the declared bytes, not the script.
- **D087-3 — host-side commands unenumerated.** The "complete"
  whitelist reads as device-side, but the host command set is
  implied, not listed: local `shasum -a256` re-verify, `cat | ssh`,
  `read -s`/`export`, `kill` of the ssh process. Declare the host-side
  set or label the whitelist explicitly device-scoped.
- **D087-4 — name-read bytes touched vs interpreted.** Declared
  "names ≤0x80 printable bytes" bounds the *interpreted* string; the
  script issues up to `0x80` `kread64`s (≤1024 B touched) per
  populated entry (`:99–104`). One line stating touched-vs-interpreted
  closes it — bounds already exist on both sides.
- **D087-5 — full step order unnumbered.** `sha256sum` is declared
  "before the python step" but its order vs the **pre-witness** is
  unstated. Numbered sequence (pre-witness → sha256 → runner →
  post-witness) removes the last ambiguity.

## Verdict

Five text-level residuals; none touches the frozen script, none
requires new evidence, all correctable in a revision-4 edit. The
packet is **not** presented as approved — the named-grant requirement
and one-attempt rule are unchanged.
