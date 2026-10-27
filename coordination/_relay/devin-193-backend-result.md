# devin-193-backend-result

Status: **PENDING reviewer acceptance** — D190 cleanup repaired per
D192's exact four-class audit; nothing staged, no commit.

## Fix

`d190_cleanup_class(kr, reply_class)` — single decision function
executed by `send_once`: valid→NONE; SUCCESS+malformed→
`mach_msg_destroy` (SRP retained); `kr ∈ {SEND_INVALID_DATA/DEST/
HEADER}` (0x10000002/03/10)→NONE (send-side reject, SRP retained);
all other kr→`mig_dealloc_special_reply_port`. The old two-class
split wrongly dealloc'd the pristine SRP on send-side rejects. G2
wording corrected everywhere: SRP = thread special reply port
(receive+send rights, TSD-cached); MAKE_SEND_ONCE mints the send-once
into the message.

## Tests — cleanup execution, not just labels

`D190_TEST_HOOKS` counters prove which branch ran. p7 covers all four
classes pure (incl. DATA/HEADER/out-of-range + verbatim constants).
p6 extended: valid→none=1; bad-id→destroy=1,dealloc=0; dead remote
(SEND_INVALID_DEST)→none=2,dealloc=0; oversized reply (0x40>0x34)→
RCV_TOO_LARGE→**real** `mig_dealloc_special_reply_port` executed
(dealloc=1). Loopback + kr-verbatim records retained.

## Exact results

`bash analysis/d190-timebase-probe/run_tests.sh`:

- ASan+UBSan: `PASS: 81 checks, 0 failures` (7 scenarios)
- TSan: `PASS: 81 checks, 0 failures`
- arm64: objects `Mach-O 64-bit object arm64`; binary
  `Mach-O 64-bit executable arm64` — never executed

Changed: `analysis/d190-timebase-probe/{d190_probe.h,d190_probe.c,
d190_probe_test.c,run_tests.sh,README.md}` +
`docs/evidence/devin-d193-backend-20261020.md`. No invocation,
installation, device, product contact, or commit.

(handover to codex now)
