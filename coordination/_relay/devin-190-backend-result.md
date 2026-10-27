# devin-190-backend-result

Status: **PENDING reviewer acceptance** — analysis-only one-shot 0x74a7
sender built per D188/D189; never invoked/installed; nothing staged.

## Delivered (`analysis/d190-timebase-probe/`)

- Verbatim wire shape: 24B header (`bits=0x1513`, `id=0x74a7`,
  voucher 0), option `0x00304003`, `send_size=0x18`, `rcv_size=0x34`,
  `mig_get_special_reply_port` local, `_SLSServerPort` (dlsym) remote.
- Exactly one `mach_msg` — no loop/retry; kr recorded verbatim.
- MIG-epilogue classification (0x750b/0x2c/bit31-clear/remote==0/
  retcode==0 → payload @+0x24); epilogue cleanup mirrored
  (`mig_dealloc_special_reply_port` on kr-fail, `mach_msg_destroy` on
  malformed).
- LC_UUID dyld gate: exactly-one `96676A53-…-1880` match, fail-closed,
  zeroed out on refusal.
- D169 v3 stream writer (uuid producer-declared, gen/pid, emitted=2,
  drops=0, uncommitted=0) — round-trips through the accepted loader
  and scores one span under `d169_decode`.

Two real defects caught by the loopback bring-up and fixed: `d190_reply`
needed `packed` (payload @+0x24 is 4-aligned), and the reply target is
the received message's **remote** slot (kernel vendored send-once), not
local.

## Tests

63 checks: wire constants, LC_UUID gate, all 8 reply classes, record
legs/gen, stream→loader→decode round-trip, and the G17 two-thread
loopback (wire-shape capture + OK/malformed/kr-failure sends).

## Exact results

`bash analysis/d190-timebase-probe/run_tests.sh`:

- ASan+UBSan: `PASS: 63 checks, 0 failures`
- TSan: `PASS: 63 checks, 0 failures`
- arm64: objects `Mach-O 64-bit object arm64`; binary
  `Mach-O 64-bit executable arm64` — **never executed**

Boundaries verbatim in README/evidence: server-side effects unverified
inference (G15); client-side data only; no WS instrumentation or
wait-vs-no-wait comparison; uuid hi64 producer-declared; no hooks/
sentinel/permits/transport/device/product. Commit awaits Codex review.

(handover to codex now)
