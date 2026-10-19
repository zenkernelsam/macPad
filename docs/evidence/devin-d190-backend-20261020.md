# D190 — analysis-only one-shot raw 0x74a7 probe sender

Task: `coordination/_relay/devin-190-backend-task.md`. Implements a
standalone diagnostic sender under `analysis/d190-timebase-probe/`
per the D188 exact 22F82 callsite and the frozen D189 G1–G17 contract.
**Never invoked, installed, or integrated — source + host tests only.**

## Implementation

- `d190_probe.c/h` — wire constants verbatim (`msgh_bits=0x1513`,
  `msgh_id=0x74a7`, option `0x00304003` = SEND|RCV|SYNC_OVERRIDE|
  PROPAGATE_QOS|SYNC_WAIT, `send_size=0x18`, `rcv_size=0x34`), LC_UUID
  dyld gate (exactly-one match for `96676A53-…-1880`, fail-closed),
  request builder, MIG-epilogue reply classifier (id `0x750b`, size
  `0x2c`, bit31 clear, remote==0, retcode==0 → payload `@+0x24`),
  D150b-contract record legs, D169 v3 stream writer, and
  `d190_send_once` — a single `mach_msg`, no loop/retry.
- `d190_main.c` — dlopen→symbol→gate→`_SLSServerPort`→send→write;
  nonzero exit class per failure, never fabricated. Never run.
- Real defects found during bring-up:
  - `d190_reply` needed `packed` — payload `@+0x24` is only 4-aligned;
    normal padding put it at `+0x28`.
  - Received-message `msgh_bits`/ports are kernel-translated (remote →
    `MOVE_SEND_ONCE` vendored send-once); the first loopback reply
    targeted the received `local` slot and died `INVALID_DEST` — reply
    goes to the received **remote** slot (verified by round-trip).

## Tests — 63 checks (deterministic, no sleeps)

p1 wire constants/builder; p2 LC_UUID + live gate consistency; p3 all
reply classes; p4 record legs/gen; p5 stream write → accepted d169
loader round-trip → full `d169_decode` (1 scored span, latency =
handler_begin − call); p6 G17 two-thread loopback — wire-shape capture,
synthetic-reply injection (OK + payload), bad-id injection (`E_BAD_ID`),
dead remote (`E_KR`, RET msgid=0 aux=kr verbatim — never fabricated).

## Exact results

`bash analysis/d190-timebase-probe/run_tests.sh`:

- ASan+UBSan: `PASS: 63 checks, 0 failures`
- TSan: `PASS: 63 checks, 0 failures`
- arm64: `d190_probe.o` + `d190_main.o` → `Mach-O 64-bit object arm64`;
  `d190_probe` → `Mach-O 64-bit executable arm64` (link-level only —
  the binary was NEVER executed)

## Boundary statements (verbatim)

- Server-side side effects are **unverified inference** (G15): the WS
  handler binary is not in-tree; "read-only" is inferred from message
  shape. Client-side data only — no WS instrumentation, no
  wait-vs-no-wait comparison, no wake conclusion.
- Exactly one `mach_msg` per process; no retries, loops, or
  same-process re-send. Never routes through
  `_SLSGetWindowServerTimebase` (once-cache suppression, G6).
- `image_uuid[16]` high64 is **producer-declared** — not
  record-verifiable (D175/D184 wording preserved).
- No product hooks, sentinel, permits, transport, device, or GUI.

## Scope

`analysis/d190-timebase-probe/{d190_probe.h,d190_probe.c,d190_main.c,
d190_probe_test.c,run_tests.sh,README.md}` + this report. Uncommitted
pending Codex review; unrelated WIP preserved.
