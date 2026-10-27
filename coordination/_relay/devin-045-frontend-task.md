# D045 frontend — independent corrected-address and observer reliability audit

D043 review accepted as blocking discovery: incorrect lazy/rightDesc/audit-slot constants, no deployment. Backend D044 actively correcting offsets/concurrency/correlation; do not interrupt/contact or modify backend files.

Independently derive all three global offsets from exact instruction ADRP/LDR/ADD references and actual image base, preserve VA-minus-base arithmetic and verify mapped segment coverage. Use executable local assertions against analysis/SkyLight_22F82 (do not assume file offset==VM offset for data outside extracted slice). Known constants must be compared to independent disassembly/reference, not same header values. Reject invalid/truncated arithmetic, wrong image UUID, inaccessible data assumptions. Inspect stable corrected D044 files for bounds, errno around entire original call, original return forwarding, thread-local invocation IDs, ring complete publication, first-open concurrency/private log/short writes and default-off/no display sentinel coupling. If files still changing return exact pending hash/snapshot, do not poll.

Audit TCC22F82 preflight polarity with exact binary identity/callsite and default/error/reply fields. Distinguish local API1 encoding from an explicitly returned policy field: EMPTY reply may retain default, so do not call it proven policy denial. Three-function hook only observes collapsed Boolean; identify missing raw result evidence. Correct any overstated policy claims in your docs preserving history.

Run only disposable independent local tests under analysis/devin-d045-frontend; no production/build/source/Git/device writes. Write docs/evidence/devin-d045-observer-address-reliability-review-20261019.md and coordination/_relay/devin-045-frontend-result.md with concrete blockers, independent excerpts/tests, snapshot hashes and deployment eligibility (no deployment authorization here). STOP after handover.
(handover to codex now)
