# D178 backend — close D176's minor stats/header consistency gaps

Continue the uncommitted D169/D175/D177 offline candidate only. Address two narrow review findings:

1. Median scratch allocation failure currently leaves `probes_scored>0` but `median_ns==0` and cluster stats partial. Make the report internally consistent and fail-closed. Prefer bounded non-failing scratch (e.g. fixed `D169_MAX_PROBES` stack array) so output order in `latency_ns` remains unchanged; alternatively mark scoring suppressed, move would-be samples to `suppressed_samples`, clear `probes_scored` and skip all stats. Keep memory bound explicit.
2. Stream header v3 `reserved` must be validated as zero on load (distinct malformed-header error or existing arg/header error documented) and `uuid_valid` must be exactly 0 or 1; do not silently normalize invalid values to true.

Add focused tests for malformed reserved/uuid_valid and preserve existing tests. Do not broaden scope, commit, access device, or integrate product code. Rerun `bash analysis/d169-svcdecode-candidate/run_tests.sh`; hand over exact outputs and paths. Commit only after Codex reviews and accepts the complete offline candidate.
