# D200 — exact-binary ABI audit: CGXRunOneServicesPass observer

Image: `analysis/SkyLight_22F82` — LC_UUID `96676A53-B1E0-3D7E-B98B-
B73873CD1880` (matches D198 pin). All vaddrs image-relative:
offset `0x33C63C` = vaddr `0x18542163c` (__TEXT base 0x1850e5000).

## Identity & span (binary fact)

- `_CGXRunOneServicesPass` — local `t` symbol @ `0x18542163c` — the
  D198 pin is **exact**.
- Span `0x18542163c`–`0x185421d7f` (~1.8KB). Next symbols: a helper
  `pacibsp` @`:1d80` (the `:1e40` retab belongs to IT, not this fn),
  `_post_port_data` @`:1e48` (the D198 DISPATCH edge — adjacent but
  distinct).

## Prologue / relocatability (binary fact)

First 16 bytes = `7f 23 03 d5 ef 3b b6 6d ed 33 01 6d eb 2b 02 6d`:
`pacibsp` + `stp d15,d14,[sp,#-0xa0]!` + `stp d13,d12` + `stp d11,d10`
— all position-independent, no PC-relative refs, no control flow.
Fits the provider's 16-byte trampoline window; `pacibsp` executes
safely in the detour (re-signs whatever LR called it; the body's own
`retab` authenticates normally → PAC-clean). Exact bytes above are
the permit's expected-prologue.

## Signature & exits (binary fact + inference)

- Args: `x0` → `w21` (int), `d0` → `d8` (double). Caller idiom
  (`0x185236ad4-6adc`): `f(1, 0.0)` inside a 2s `SLSCurrentRealTime`
  poll loop, `cbz w0` exits — return is an int "did work" status.
- **One** `retab` exit @`:1cb0` (returns `x0=w21`); every non-fatal
  path merges via `:1bd4`→epilogue (incl. the alternate
  `_push_out_message`+vm-accounting branch at `:1cb4`, re-join `:1d2c`).
- `__exit` @`:1d78` (SLSLogBreak+os_log_error fatal path) and
  `__stack_chk_fail` @`:1d7c` — **process-death paths**: uncoverable
  by any wrapper and correct to exclude.
- An entry/exit wrapper preserves behavior on every *reachable*
  exit — all args (x0,d0) pass through, single retab funnels return.

## Internal waits (binary fact)

The pass CONTAINS the idle wait: `os_nospin_lock` around
`mach_msg_overwrite` (:1974) + `mach_msg` (:19a4,:1a24) +
`mach_msg_destroy`; `voucher_mach_msg_adopt/revert` around dispatch;
plus `_run_timer_pass`, `_CGXRunOneEventPass` polling loop,
`_waitpid`, `SLSCurrentRealTime` deadline math against `d8`,
session-select + event-tap buffer flush on exit. The function IS
one complete services pass — wait + receive + dispatch + flush —
which is precisely the D140 bracket unit. The internal mach_msg
legs will nest inside the PASS bracket on the same tid (D141
pairing-compatible).

## Inferred (labelled — needs runtime observation)

- `x0` flag semantics (1 = poll/nonblocking mode?); `d0` deadline vs
  relative-wait semantics — the d8 deadline math suggests absolute
  deadline but is not proven.
- Return contract ("nonzero = handled") inferred from the poll-loop
  caller; unverified against WS-side state.
- Which port the internal `mach_msg*` receives on (services portset
  assumed; not traced).
- Whether the `__exit` path fires on a healthy system (guarded by
  `tbnz w20,#0x1c` at `:1bd0` — a fatal/debug flag).
- Runtime pin-down: lldb arg/retval dump on a macOS host or device
  session; a `mach_msg` port-name probe inside WS.

## Cross-check (existing permits — no overlap)

D198: zero permits/hooks at `0x33C63C`; SENDEROBS sites
(0x318464/0x3407ec/0x340da0) and IOMFB/QC permits are unrelated
offsets. The mach_msg* interpose proxies already installed emit the
legs this bracket contains. No duplicate work.

## Verdict

**Feasible**: exact symbol, clean 16B relocatable window, single
retab, pass-through args — an entry/exit observer covers every
reachable exit. The two caveats are (a) arg/return semantics are
inferred pending runtime pin-down, and (b) the fatal `__exit` path
is inherently unwrappable.
