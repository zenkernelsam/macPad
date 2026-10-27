# Frontend R010 — symbolize the existing WS main-thread stall

Read reviews/R009-frontend-R006-backend-20261018.md and backend R005 preserved spindump metadata. Current display remains composite missing; passive sample has142/150 CA thread samples waiting on main. Use existing copied spindump, loaded-image list/UUID/slides, exact SkyLight22F82 and installed libmachook artifact identity to resolve main-thread addresses and owning functions. No new sampling/device access required. If full report is only on device, list exact file needed in your report; backend retains device ownership and coordinator will transfer it later.

Resolve the finite sampled main-thread chain to producing operation/lock/message wait where possible, distinguish wrong-symbol artifact from unsymbolicated code, and determine whether it actually reaches coexist pacing wait. Link observed PCs to exact function offsets/raw disassembly, not source proximity. Prepare one request enqueue->wait->dispatch->response timing experiment only if the sample supports it. No claim of message starvation or frontend-provider cause without same-artifact evidence.

Also retain current responsibility result: exact Quarantine/0xb6 helper found but GOT/stub/actual103 branch unknown, not a proven no-policy result. Do not repeat adjacent-version analysis or implement setter.

Read-only offline only; own evidence plus coordination/_relay/round-010-frontend-result.md. No shared source/test/build inputs, builds, Git or SSH/device operations. Backend is doing bounded F11H control. Last line:
(handover to codex now)
