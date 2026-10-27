# Frontend R015 — harden the R014 cache-header validation gate

Read `reviews/R014-frontend-20261018.md` and R014's result/evidence. Historical
`coordination/artifacts/r006-cache-find2.json` confirms that
`/var/mnt/rootfs/System/Cryptexes/OS/System/Library/dyld` is a working rootfs
alias; retain it and distinguish it from canonical storage under
`System/Volumes/Preboot/Cryptexes`.

Resolve the review condition using only existing offline sources/artifacts:
find an exact 22F82/dyld 1066.8 witness for the header field offsets and
mapping/subcache/patch-info formats, or state that no such witness exists.
Do not treat plausible addresses, bounds, or a 1286.10 parser as format
validation. If the witness is absent, revise the capture procedure so it
collects only cache identity plus a bounded raw header sample and stops before
deriving any mapping/page/patch offset; list the precise artifact needed to
resume. No SSH/device, no source/test/build edits, dependencies, or Git writes.
Write only a dated amendment and `coordination/_relay/round-015-frontend-result.md`.
End exactly `(handover to codex now)`.
