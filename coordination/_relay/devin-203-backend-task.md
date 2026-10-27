# D203 backend — D202 writer and collector boundary repair

D202 remains unaccepted after independent 134/0 sanitizer runs. Fix only analysis/d202-drainser-candidate.

1. d202_write_stream currently opens/truncates path before validating arguments/record consistency, then removes it on failure. Validate all arguments/magic/PID/gen before filesystem mutation; preserve existing files on invalid input and on write/flush/close failure. Use a unique same-directory temp file, checked write/flush/close, then atomic rename to the explicit destination on success; remove only the owned temporary on failure. Never unlink another/pre-existing target. Add tests preserving a sentinel target on invalid args/record and injected write/close failure, plus success replacement and no temp leftovers.
2. Capture errno at the actual failing operation, not the pre-fclose stale errno; deterministic close-failure test with stale nonzero errno proves reported error is the close error. Use host file abstraction/funopen or test-only injection, no /dev/full assumption.
3. d202_collect must reject b->n > D141_RING_CAP before pointer arithmetic/drain and flag ERR. Specify whether budget is per call or cumulative; current drain_calls cumulative loop differs from 'at most budget calls per invocation'. Make semantics explicit and test resuming the same partial batch after budget exhaustion without spinning/no-op forever.
4. Explain that COMPLETE is a quiescent fixture snapshot, not a concurrent producer shutdown guarantee; keep partial/CAP status observable and do not silently imply a v3 header flag proves completeness (currently flags=0).

Retain accepted code unchanged, s1-s9 no-loss/lease/stall tests and full UUID accessor provenance. Run focused ASan/UBSan, TSan, arm64 compile. No commit, device/product integration, sender/WS invocation or wake conclusions. Save exact diff/test handover.
