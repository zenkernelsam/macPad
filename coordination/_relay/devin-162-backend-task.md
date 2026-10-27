# D162 backend — close D145 D4 timeout-exclusion test gap

Test-only change under `analysis/d141-svctime-candidate/d141_svctime_test.c` (and test README count if needed). Do not change production or candidate implementation API. Based on D161 exact source audit:

1. Include `<mach/message.h>` and use `MACH_RCV_TIMED_OUT` in T18 instead of `0x10000003`; if the host SDK cannot expose the constant, define a test-local named value `0x10004003` with a source citation to vendored `osfmk/mach/message.h:1231`. Remove the “-ish” comment and make expected RET aux reference the same constant.
2. Make the decoder model `pair_for` reject non-success RETs before scanning calls (`recs[ret_idx].aux != MACH_MSG_SUCCESS` ⇒ `-1`). This function is explicitly the decoder-side pairing rule, not a candidate production API change.
3. In T18, assert the timeout RET pairs to `-1` and leaves the preceding CALL(0x8000) unmatched. Add a delayed successful RET with a distinct ID after the timeout and prove it pairs to that still-unmatched CALL; ensure the record-array capacity and expected drain count are updated. Preserve the existing distinct-id, async interleave, aux, and errno checks.
4. Run `bash analysis/d141-svctime-candidate/run_tests.sh`; report exact ASan/UBSan, TSan, and arm64 results/check counts. No device, product integration, packaging, sentinel, GUI, or commit.

Save a short D162 evidence report plus relay result ending `(handover to codex now)`. Candidate remains PENDING until independent reviewer accepts. Devin commits only after acceptance of this concrete milestone.
