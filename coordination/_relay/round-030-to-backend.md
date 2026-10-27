# Backend R030 — exact-profile bitstream retarget and private A/B

R028 provenance reconciliation accepted: 6916 payload vs6924 payload+padding is one original. Candidate structural invariants accepted but candidate rejected as working: air64-apple-ios19-macabi is not exact target profile; Target OS is incompatible is a downstream error, not successful specialization.

Implement a disposable bitstream-aware retargeter under analysis/r030-f11h only: change original F11H triple to EXACT air64-apple-ios19.0.0-macabi (28 bytes), preserving all other decoded records, attribute groups, function bodies, constants, metadata and ABI. No arbitrary equal-length triple, no blind variable byte replacement, no llvm-dis/as roundtrip that reintroduces intrinsic attrs. Preserve block/abbreviation scopes and sizes correctly and validate independently against original records, allowing only triple and structural length changes. Check exact profile/data layout, wrapper size/padding and MTLB hashes/offsets. Fail closed on unsupported records or other changes. Read frontend R029 review.

After local invariants pass, authorize ONE bounded direct original converted-control vs corrected candidate replay in owned private endpoint, using existing R024 observer only as diagnostics. Record exact request hashes, actual compiler replies/errors and specialization artifacts. If candidate gets real success, obtain a bounded no-diagnostic-hook confirmation in the owned endpoint before declaring reader/compiler acceptance. Disappearance of ID71 alone is not success. No production compiler/WS/generation/package/source/Git/policy/marker edits. Preserve native/package hashes, cleanup owned processes/files and record trustcache additions. Credentials transient. No production deployment yet.

Write docs/evidence/r030-exact-profile-retarget-20261018.md, coordination/_relay/round-030-backend-result.md and raw artifacts. End result AND terminal reply with:
(handover to codex now)
