# D196 — frozen D193 cleanup/execution-test review

Frozen hashes (sha256):
- `d190_probe.c`      `b4dec55a3d3ec3694adbf784274313647cf7510b8c12c7a61eec982bd26cc185`
- `d190_probe.h`      `16317ebe945d02379b845cb16696b681cf0043ab9770dc7b47c6643e7ae7e126`
- `d190_probe_test.c` `ae4d82748396bb3144d8630104d61ce45ef485d86de5def58f2a62970f744368`
- `d190_main.c`       `d176774a164565acddc8634cc82d2a1ebbc566ad7a95307aa5fcff3a55d969fc`
  (unchanged since D194 — scanner/main repair pending, not reviewed)
- `README.md`         `1e2fb9995d40487d0fbacddc7b834dfd8874ebcfaf2fb832c3d17d32ee12c118`

Verdict: **ACCEPT** the D193 cleanup/decision/test sections.

## Decision function — exact D192 mapping (:153-161)

`d190_cleanup_class`: SUCCESS→(OK?NONE:DESTROY); kr∈{SEND_INVALID_
DATA, SEND_INVALID_DEST, SEND_INVALID_HEADER}→NONE; else→DEALLOC.
Matches the stub's `w8=kr−0x10000002` range + `&0x4003` mask domain
{offsets 0,1,14 = 02/03/10} exactly — in-range non-masked and
out-of-range kr both land DEALLOC, identical to the stub's `:4984`
convergence. Constants pinned verbatim in p7 (:345-348).

## Execution proof, not labels (task requirement)

Hook counters increment **inside** each switch arm (:199-216) — they
prove which branch ran, not what `cleanup_class` returned. p6 drives
three real scenarios:

- valid loopback → `none==1`, destroy/dealloc 0 (:276-278)
- bad-id reply → `destroy==1`, dealloc 0 (:287-289) — real
  `mach_msg_destroy` executed
- dead remote → real `SEND_INVALID_DEST` kr → `none==2`, dealloc 0
  (:293-301) — the class-3 regression the old code dealloc'd
- oversized reply (0x40 > rcv 0x34) → real `RCV_TOO_LARGE` →
  `dealloc==1` (:306-315) — actual `mig_dealloc_special_reply_port`
  on the thread SRP

p7 pins the full table including non-loopback-drivable classes
(SEND_INVALID_DATA/HEADER, TIMED_OUT, INTERRUPTED, PORT_DIED,
out-of-range 0x10000020).

## Records preserved on every error path

`fill_records` runs before classify (:191-194): dead-remote case
asserts `aux==SEND_INVALID_DEST`, RET msgid=0, CALL still recorded
(:297-304); malformed-reply case keeps the **actual received id**
`0x1234` in RET.msgid (:290) — verbatim, never fabricated.

## Saved-SRP correctness

`mig_dealloc_special_reply_port(rp)` passes the stored local (:209),
never the received header's local_port — matches D192's defensive-
branch note.

## Minor observations (non-blocking)

- Malformed-**complex** replies are decision-covered only (p7 pure
  table); no loopback injects a descriptor-bearing reply, so
  `mach_msg_destroy` is never observed against a complex buffer.
  Thin: class-2 execution is proven via the non-complex path, and
  destroy semantics are libSystem's.
- `fake_ws` correctly models the kernel's right translation
  (received remote = vendored send-once; reply moves it) — wire
  faithful.
- TOO_LARGE semantics consistent: send-once consumed by the reply
  send; the oversized message stays queued — exactly why class 4
  destructs the SRP.
