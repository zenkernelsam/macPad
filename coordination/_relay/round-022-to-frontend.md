# Frontend R022 — offline classification of EXC_GUARD reports around R012

Read R010/R012 package evidence and R021 Identifier acceptance plan. Inspect
only existing local crash excerpts, postinstall logs and source/error handling
for `ExcUserFault_codesign` / `EXC_GUARD` namespc=7 reason=9. Determine whether
the report is structurally attributable to publisher Identifier, trustcache
admission, a codesign tool guard, or remains unrelated/unknown. Keep this
separate from the XPC PAC crash and do not infer a cause from timing alone.

Do not SSH/device, implement the Identifier fix, rebuild/install, modify
source/tests/build inputs or Git. If the local evidence is insufficient, list
the exact report fields or device correlation needed. Write only a dated
evidence note and `coordination/_relay/round-022-frontend-result.md`; end
exactly `(handover to codex now)`.
