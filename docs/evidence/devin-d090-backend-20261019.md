# D090 — errata to D088 revision-4 authorization text

Offline errata only. Runner v3 bytes and hash unchanged
(`8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`).
**Status: NOT APPROVED** — valid only if granted by name and hash.

Resolves the two D089 residuals as explicit clarification lines in the
D088 revision-4 verbatim block. No other text changes.

## Errata 1 — delta=0 disposition (resolves D089-1)

In `**Witnesses**` → `**Desktop**`, the sentence

> Sequence must be **nondecreasing**; `delta=0` with same pid is
> *flagged for investigation*, not an unconditional pass.

is amended to:

> Sequence must be **nondecreasing**; `delta=0` with unchanged pid
> **and** lstart is **admissible but flagged for investigation** —
> it annotates the witness record and does not by itself invalidate
> the snapshot; any decrease, or a pid/lstart change, invalidates.

## Errata 2 — pre-witness abort vs the one-attempt rule (resolves D089-2)

In `**Scope.**`, after "One attempt total — including one that ends in
a STOP.", insert:

> A pre-witness abort (e.g., `state≠ready` at step 1) occurs **before**
> the runner attempt and **does not consume** it; the single attempt
> is spent only when the pinned script is fed to `python3`.

## Status after errata

No residuals remain open against the authorization text. The packet —
D088 revision 4 + these two errata lines — is ready to present for the
user's named authorization of `macws-policy-snapshot-v3` at sha256
`8bb90399…`. This document grants nothing.

## Provenance

D082 → D083-frontend (23) → D084 rev 2 → D085-frontend (4) → D086
rev 3 → D087-backend audit (5) → D088 rev 4 → D089-backend audit (2)
→ this errata. Runner v3 frozen since D080.
