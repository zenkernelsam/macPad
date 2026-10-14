# Active R005 — distinguish cached denial from live TCC denial in WindowServer

Goal ACTIVE. Codex guides/reviews; Devin implements/runs. R004B accepted as bounded diagnostic evidence, INCONCLUSIVE about cause. Read reviews/R004B-final-20261015.md and docs/evidence/ventura-22F82-tcc-checker-20261015.md, plus the full LLDB functions in artifacts/R005-static/exact-functions.txt. Finish the previously requested static-note correction before claiming any branch confirmed.

## Evidence boundary and tooling

Latest USER replacement AGENTS is controlling: exact-file otool/Capstone/LLDB or verbatim runtime evidence is allowed. The old IDA-only wait in earlier coordination notes is superseded. Preserve UUID/hash/instruction words and complete relevant functions; no byte guessing, forced branches or constant-return checks. Input is still 1002; missing loginwindow entitlement/sid0 is not a demonstrated cause.

Exact input SkyLight_22F82: UUID 96676A53-B1E0-3D7E-B98B-B73873CD1880, SHA256 13d7a0ca010a9808234c71c34fd650b26329de99d1aceaeb5ddbb58f8b906c61, file image base 0x1850e5000. Derive runtime addresses from the loaded WS image identity/slide and verify original instruction words before breakpoints. Prefer the existing iOS-local debugger capability; inventory it first.

## One bounded experiment

1. Verify CLI echo HI, clean owned-tool state, thermal and project coexist/watchdog startup. Start ONE WS generation via the pipeline. This debugger experiment does not need the loader observer enabled; avoid enabling unrelated high-overhead traces. Use a fresh real sender PID/pidversion, not cache-clearing or fake audit values.
2. Arrange exactly one canonical SLSPostEventRecord, without preceding warp/CGEventPost/preflight mutation that would muddy attribution. Reuse verified public/private probe ABIs; add a minimal explicit mode if required. Record senderPID before posting and real reply/result. No unknown ABI calls.
3. In the ACTUAL WindowServer, capture:
   - dispatcher senderPID/pidversion/key/right descriptor and cached flags at file VA 0x1854258b4 (x20 pid, x19 key, x22 descriptor, x24 flags; validate against disassembly/runtime first);
   - whether control reaches checker dispatch 0x185425914 and audit_token_check_tcc_access entry 0x185425da0;
   - when reached, checker target presence and raw w0 at 0x185425e0c (immediately after blraaz), then final boolean/result. Raw TCC 0=>true at this caller; nonzero=>false.
   Record the branch actually observed, not a conjectured cache population path. Breakpoints/logs must be bounded, auto-continue where safe, and correlate one sender/generation. Prefer debugger reads over more injectable hooks; if unavailable, report concrete tool failure before choosing an exact-profile observational alternative.
4. If cached deny is observed, trace a fresh real sender's first evaluation / the real cache-write producer, without modifying cache state. If unresolved checker, trace real module/lookup failure. If raw TCC nonzero, capture the actual service/identity/policy request/reply or exact TCC return producer before changing anything. Match the owning TCC binary UUID if its private internals are inspected. Do not jump from a numeric error to an entitlement/session conclusion.
5. Stop after the bounded decision trace and document the actionable next invariant. A narrow upstream fix can follow only once its producing operation is evidenced; no broad TCC bypass, fabricated sessions or same-pid proxy workaround presented as a fix.

## Tool/process discipline

Use local macPad SSH runner and real exit records. Do not run legacy ios_lldb_tmux.sh attach blindly: it kills an existing session and uses an old sudo/password pattern. As root, use a UNIQUE task-owned tmux/debugger session, inspect existing handles first, no sudo/password argv. Keep credentials only in temporary env. A wait timeout does not prove the task exited. Detach debugger explicitly before closing its session, then verify target/process/marker cleanup. No kernel writes.

## Delivery

Save raw disassembly/commands/register/log slices with PID/UUID/hash/time and real exits; update dated evidence, WORKING and DEVIN_STATUS at milestones. Build affected probe architectures if changed; preserve both thin libmachook images if untouched. Check echo HI/thermal/crashes/visible desktop or completion sequence where relevant. No input acceptance without correlated WS-DISPATCH and visible effect.

Codex briefly reserves Git writes for reviewer checkpoint; implement/inventory now but do not commit until release. Keep R004B source stable; no generic tracing expansion. After release commit explicit source/evidence paths only. Never add misc/macos_ws_event_probe binary. Overall input/pacing/wallpaper/session goal remains active.
