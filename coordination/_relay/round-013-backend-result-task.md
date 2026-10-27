# Backend R013 — layered display/pixel and reopen freshness acceptance

Read `reviews/R012-backend-20261018.md`, R012 evidence, the share-update
priorities and R016 reopen audit. The R012 generation was stopped and native
SpringBoard/tccd/backboardd were healthy at handover; inventory again before
starting anything. Preserve the installed R012 package and baseline backups.

Own one bounded generation and focus only on the display acceptance chain:
decode/input asset if applicable → provider output → authoritative WindowServer
composite producer → displayd receive → system/native pixel readback. Determine
why `macws_final_composite.state` remains `missing producer=0 sequence=0`
(`displayd-start`) using existing logs/counters and exact process identities.
Do not claim pixels from process readiness, provider readiness, non-null image,
or finalize/status flags. If the chain cannot reach a real producer, preserve
the exact upstream failure and stop rather than patching a symptom.

If the generation reaches a usable app endpoint, run the R016 Terminal reopen
freshness witness exactly: valid G0>0 zero-entry sidecar, new Host request and
result, G1>G0, same PID/executable/start time, visible metrics entry, and no
windowless-retire. Report it separately from pixels. Re-test TCC reply and
input only as separate observations if the existing controlled path naturally
exercises them; do not add grants, fake replies or input bypasses.

Do not change source, package, publisher Identifier, pacing, compiler, TCC
policy, entitlements, assertion behavior or ownership. Do not restart native
services; start/stop only this owned generation and clean it fully. Preserve
crash deltas including EXC_GUARD reports, markers, thermal/native health,
exact hashes, commands/exits and all layer results. Write only your dated
evidence/artifact and `coordination/_relay/round-013-backend-result.md`; end
exactly `(handover to codex now)`.
