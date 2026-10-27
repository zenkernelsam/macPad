# D117 frontend — offline responsibility/TCC upstream fault closure map

This is a read-only offline audit of the unresolved responsibility/TCC
upstream attribution. Start with the durable conclusions in:

- `docs/evidence/r005-responsibility-audit-20261018.md`
- `docs/evidence/r006-responsibility-wrapper-abi-20261018.md`
- `docs/evidence/r008-libquarantine-103-branch-20261018.md`
- `docs/evidence/devin-d105-backend-20261019.md`
- `docs/evidence/devin-d106-backend-20261019.md`
- `docs/evidence/independent-pacing-quarantine-review-20261019.md`

Do not repeat settled points: historical R003 message #103/empty reply is
not the current WS 10447 transaction; the inspected wrapper passes through
the downstream return; GOT/provider resolution remains open; current errno
source is not attributable to a particular kernel branch. Preserve the
three-way distinction: precheck, no-match, and matched handler.

Goal: using only exact local source/decompilation already in the tree,
identify the narrowest still-unresolved upstream edge from responsibility
request construction → wrapper/GOT target → provider → kernel/client reply.
For every positive claim cite exact evidence (binary UUID+offset/disassembly
or copied runtime line already in evidence); label theories and say exactly
what falsifies them. Search existing analysis and evidence before creating
anything so prior capability is not rebuilt. If the current checkout lacks
the exact veneer/GOT target, stop at that boundary and specify the minimal
additional artifact needed; do not infer it from adjacent branches.

No device/SSH/panic-log access, KRW, TCC setters, kernel snapshot, source
edits, builds, deploys, restarts, or Git. Do not read or edit the backend's
moving D116 transport files. Save one concise dated evidence report and a
relay result with facts, rejected/remaining theories, exact commands and
exits, and the smallest next step. End with a separate line:
`(handover to codex now)`.
