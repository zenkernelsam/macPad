# Frontend R030 — bitstream retarget validation and independent review

R029 accepted; R028 final confirms malformed-profile candidate only reached Target OS is incompatible. Backend R030 owns disposable bitstream-aware retarget to exact 28-byte air64-apple-ios19.0.0-macabi and bounded private replay. Do not modify backend files or interrupt its terminal.

Develop an independent local read-only validation harness under analysis/r030-independent-audit: compare original vs candidate decoded bitstream records (module triple alone allowed to change), intrinsic and non-intrinsic/parameter/call-site attributes, body/constants/metadata/calling conventions, abbreviations/block-length consistency, wrapper declared payload and zero padding, exact triple/profile/data layout, and MTLB offsets/hashes if generated. Use authoritative bitstream grammar; do not merely rerun builder assertions. Include negative fixtures for wrong same-length triple, unexpected attribute/parameter/body changes and truncated block. No production/source/package/Git/device changes.

Audit stable saved backend artifacts if available; otherwise deliver executable validator and precise pending input without polling. No compiler acceptance claim absent exact final evidence. Document productization criteria and whether all transforms in current converter are preserved rather than lost by replacing a whole module. Write docs/evidence/r030-retarget-independent-audit-20261018.md and coordination/_relay/round-030-frontend-result.md. End file and terminal with:
(handover to codex now)
