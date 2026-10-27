# Frontend R019 — static scope review for the narrow XPC pointer-signing fix

Read the R011/R018 review and inspect the proposed narrow change in the
working tree when visible. This is a read-only scope audit: enumerate direct
function-pointer calls fed by `MSFindSymbol`, classify each as hook/patch/direct
call, and verify that the proposed change signs only the
`__xpc_bootstrap_services` direct call with A-key/zero. Confirm existing
provider/AppInput signing contracts and identify any accidental global API
change, extra callsite activation, or unrelated diff.

Do not edit source/tests/build inputs, build, deploy, SSH, or write Git. If the
backend has not produced a candidate, record that and the exact static checks
it must satisfy. Write only a dated evidence note and
`coordination/_relay/round-019-frontend-result.md`; end exactly
`(handover to codex now)`.
