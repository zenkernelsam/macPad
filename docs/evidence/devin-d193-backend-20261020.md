# D193 — D190 SRP cleanup correction against D192 exact-stub classes

Task: `coordination/_relay/devin-193-backend-task.md`. D190 was not
accepted: its G9 two-class cleanup was wrong against the D192 audit of
the real 22F82 stub epilogue (`:495c-49e0`) and vendored xnu
(`mach_port.c:441-478`, `ipc_tt.c:1451-1474`, `message.h:328,1182-1243`).
Uncommitted candidate repaired; no invocation/device/commit.

## What was wrong

- `kr != SUCCESS → mig_dealloc_special_reply_port` unconditionally —
  this destructs the thread SRP on **send-side rejects**
  (`SEND_INVALID_DATA/DEST/HEADER`) where the stub does nothing and the
  port never participated.
- The G2 wording described the SRP as "a send-once right" — inverted:
  `thread_get_special_reply_port` gives the task **receive+send**
  rights on a thread-bound, TSD-cached port; `MAKE_SEND_ONCE` mints the
  send-once right *into the message*.

## Repair

- New pure decision function `d190_cleanup_class(kr, reply_class)` →
  `NONE | DESTROY | DEALLOC`; `d190_send_once` executes exactly that
  mapping. Header documents all four stub classes with values.
- Class 1 valid → nothing (send-once consumed; SRP TSD-retained).
- Class 2 SUCCESS + validation fail → `mach_msg_destroy(reply)`; SRP
  retained.
- Class 3 `kr ∈ {0x10000002, 0x10000003, 0x10000010}` → nothing; SRP
  retained.
- Class 4 all other kr → `mig_dealloc_special_reply_port(rp)`.
- SRP ownership comments corrected in header, impl, and README.

## Tests — deterministic cleanup verification (not just labels)

- `D190_TEST_HOOKS` counters (`none/destroy/dealloc`) inside
  `send_once` prove which branch actually executed.
- **p7** (new): `d190_cleanup_class` pure table — all four classes
  incl. `SEND_INVALID_DATA/HEADER` (not loopback-drivable), out-of-range
  kr, and the verbatim constant check.
- **p6** extended: valid loopback → `cleanup_none==1`; bad-id
  injection → `cleanup_destroy==1`, `dealloc==0`; dead remote
  (`SEND_INVALID_DEST`) → `cleanup_none==2`, `dealloc==0` (the class-3
  regression the old code would have dealloc'd); **oversized reply
  injection** (`msgh_size=0x40` > `rcv_size 0x34`) → real
  `MACH_RCV_TOO_LARGE` → `cleanup_dealloc==1` — the actual
  `mig_dealloc_special_reply_port` ran on the thread SRP (it recreates
  via TSD on next use; verified safe in-harness).

## Exact test results

`bash analysis/d190-timebase-probe/run_tests.sh`:

- ASan+UBSan: `PASS: 81 checks, 0 failures` (7 scenarios)
- TSan: `PASS: 81 checks, 0 failures`
- arm64: `d190_probe.o` + `d190_main.o` → `Mach-O 64-bit object arm64`;
  `d190_probe` → `Mach-O 64-bit executable arm64` (never executed)

## Scope

Changed: `analysis/d190-timebase-probe/{d190_probe.h,d190_probe.c,
d190_probe_test.c,run_tests.sh,README.md}` + this report. Nothing
staged; no main/sender invocation, no SkyLight/WS/device/product
contact, no commit until Codex review.
