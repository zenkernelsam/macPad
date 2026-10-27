# Backend R012 — implement and validate the narrow XPC function-pointer fix

Read `reviews/R011-backend-R018-frontend-20261018.md`, R010/R011 evidence,
and frontend R019 packet. Use one clean worktree from frozen HEAD `c97e219`.

Apply exactly one narrow source change at `libmachook/mac_hooks.m`'s direct
`__xpc_bootstrap_services` call: authenticate the raw `MSFindSymbol` result
with the established A-key/zero function-pointer contract before the existing
`blraaz` call. Keep the existing dictionary, setup timing, result logging and
failure behavior. Do not globally change `MSFindSymbol`; do not alter Steam,
Metal, TCC, assertions, HID or role gates. Add or update only a focused static
contract check if needed; do not add a fake runtime success test.

Build once from a clean frozen tree, record source/binary/package hashes and
true exits, verify the installed callsite bytes contain the intended signing
contract, and deploy through the existing package/trust pipeline. Preserve the
R010 baseline and use a fresh inode. Before starting any generation, inventory
native health, markers and existing processes; never kill the pre-existing
MacWSHost PID unless it is owned by this packet. Start one owned generation
only after the install checks pass. Verify:

1. tccd/authd/systemstatusd survive the former XPC bootstrap point;
2. WindowServer reaches readiness and its loaded provider/libmachook map is
   read back with exact UUID/path/hash;
3. no new `_xpc_bootstrap_services` crash occurs;
4. final-composite sequence/pixels, TCC reply and accepted input remain
   separately reported. Reuse R016 reopen freshness only if a live generation
   exists; require G0/G1 and same PID before calling it a witness.

If the signed call enters libxpc but a later failure occurs, preserve that
distinction and stop at the next exact blocker. Do not bypass assertions,
fabricate TCC/pixels/input, restart shared compiler, or broaden the packet into
pacing changes. Clean up only processes started by this packet, leave native
SpringBoard/tccd/backboardd untouched, and preserve all crash deltas and
baseline files.

Write only the dated evidence/artifact and
`coordination/_relay/round-012-backend-result.md`; no unrelated WIP. End the
result exactly `(handover to codex now)`.
