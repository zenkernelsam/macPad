# R004B planned runtime experiment — no new private offsets

Status: PLAN ONLY until R004A delivery review; original user authorization covers bounded device diagnosis. IDA request remains pending for static RE. This experiment observes actual public loader API calls in the live WS process; it is not a substitute for static decompilation or a claim about the current denial cause.

## Evidence motivating the experiment

Known runtime: exact 22F82 gate hook executes; sender audit is present; original gate returns0; canonical post returns1002. Identity/permission checker and state producer remain unknown. Source inspection found an EXISTING dlsym interposer: `libmachook/Diagnostics/MacWSRandomDiagnostics.m:MacWSDiagnosticDlsym`, registered with DYLD_INTERPOSE. It calls real dlsym directly, saves/restores errno and optionally wraps only getentropy for the separate random diagnostic.

The probe's dladdr labels are only symbol-resolution observations. Without comparing dli_saddr to the stripped target, a nearest exported symbol (e.g. MacWSRecordInputLatency in libmachook) must not be called the exact callee. Probe globals do not describe WS globals.

## Bounded implementation contract

- Reuse the existing dlsym interposer; do not register a competing dlsym tuple. Add the smallest observer callback while preserving its existing getentropy behavior and native result. Add dlopen observation only through its public ABI and existing interposition convention. Keep tracing code in Diagnostics/ rather than expanding the monolith.
- Scope observation to a thread-local context entered immediately around the ORIGINAL `macws_orig_sender_can_synth` call, only after the existing exact-UUID/WS-process/diagnostic-opt-in checks. Associate the sequence with the sender PID and WS PID. No global all-process loader tracing; cap recorded gate invocations/loader records and string lengths.
- Preserve original args, returned pointer/handle, errno and application-visible loader error state. Never call dlerror merely to improve a log: it consumes pending errors. If error-text observation is essential, log only when the real caller invokes dlerror and return its value unchanged; this is optional, not needed for first experiment.
- Reentrancy guard: logging or image callbacks must not recursively trigger logging. Do not use dlsym to obtain dlsym's original. Direct calls in the interposing image follow the established DYLD_INTERPOSE pattern. No changes to checks, no constant returns, no fabricated/cache objects and no symbol replacement beyond existing unrelated random diagnostic behavior.
- Correct the old gate comment asserting gCurMsgAudit is always NULL. Keep hypothesis vs runtime facts distinct.

## Validation

1. Source/runtime contract tests prove: outside context and other threads produce no gate trace; inside context captures bounded calls; nesting unwinds; return values and errno are identical; loader failure error remains observable by its legitimate caller. Use fake/injected loader callbacks for unit tests where appropriate, and a disposable probe for the actual loader contract. Do not test by changing real WS permissions.
2. Build both thin libmachook architectures, identity/platform/signing checks, fresh-inode installation via project pipeline; record full commands/exits/hashes. Revalidate CLI echoHI.
3. One cold bounded coexist WS generation with watchdog and known baseline. Arm the exact diagnostic marker, send one canonical post with target PID/seq and capture the full log slice. No new static-byte parsing; public API inputs/outputs and existing validated gate only.
4. Outcome A: captured loader path/symbol+result correlated to gate; investigate the actual producer/closure with existing source/docs and runtime tools. A NULL result alone is not a policy diagnosis.
5. Outcome B: no loader calls inside gate. Record the negative result; lazy initialization may have happened earlier or elsewhere. Do NOT infer the checker is absent, widen tracing silently, or re-run posting matrices. Next step needs IDA or bounded WS debugger evidence.
6. Archive markers in correct chroot namespace, stop owned finite tools/generation as appropriate, verify backboardd/thermal/crash reports and preserve echoHI. Record raw artifacts and stable source hashes. Input success still requires correlated WS-DISPATCH and visible effect; a loader log is not that acceptance.
