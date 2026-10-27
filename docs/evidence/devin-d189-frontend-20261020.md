# D189 — frozen acceptance contract: analysis-only one-shot 0x74a7 sender

Scope: minimal acceptance gates for a standalone raw-Mach
`_XGetWindowServerTimebase` diagnostic sender, built from D188's
exact 22F82 callsite (`_SLSGetWindowServerTimebase` @`0x185364890`,
SkyLight UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`). This milestone
authorizes **source + host-side test only** — no device invocation,
no product integration, no retries/multi-send.

## P — preconditions (binary-faithful)

- **G1 UUID gate**: before request construction, verify the loaded
  SkyLight image UUID == `96676A53-…-1880` (dyld image walk). Absent
  or mismatched → refuse to send, nonzero exit. Fail-closed; no
  flag/env bypass.
- **G2 Ports**: remote via `_SLSServerPort()` or a documented
  equivalent bootstrap route — route named in output, nonzero
  asserted. Local = `mig_get_special_reply_port()` — a send-once
  right; never a caller-allocated receive right.
- **G3 Request byte-shape** (callsite `:4904-4938`): 24-byte header
  only — `msgh_bits=0x1513` (remote COPY_SEND, local MAKE_SEND_ONCE),
  `send_size=0x18`, `voucher=0`, `msgh_id=0x74a7`. No body, no extra
  fields.

## S — send semantics

- **G4**: `option=0x00304003` verbatim (SEND|RCV|SYNC_OVERRIDE|
  PROPAGATE_QOS|SYNC_WAIT); `timeout=0`, `notify=0`, `rcv_size=0x34`,
  receive buffer ≥52B. Any deviation (e.g. finite timeout) is a
  separately reviewed variant, never a silent change.
- **G5 one-shot**: exactly one `mach_msg` per process lifetime —
  structurally enforced (single callsite, no loop/retry branch;
  failure exits nonzero, never reissues). Repeat/multi-send tooling
  is a different milestone.
- **G6 no wrapper reuse**: must NOT call
  `_SLSGetWindowServerTimebase` or any SkyLight wrapper — its
  once-per-process cache would silently suppress the send. The wire
  send is the sender's own `mach_msg`.
- **G7**: the actual `mach_msg` return value recorded verbatim — no
  success/fail collapse before recording.

## R — reply validation + cleanup (mirror `:4988-49e0`)

- **G8** a valid reply requires ALL: `msgh_id==0x750b`,
  `msgh_size==0x2c`, `msgh_bits` bit31 clear, `msgh_remote_port==0`,
  `retcode==0` → payload u64 @`+0x24`.
- **G9** malformed reply → `mach_msg_destroy` before exit;
  `kr != MACH_MSG_SUCCESS` → `mig_dealloc_special_reply_port`;
  valid reply consumes the send-once right — no manual dealloc on
  the success path (mirror the stub).
- **G10** non-success/malformed → nonzero exit + failure class;
  no retry, no fallback, no fabricated result.

## O — output (D169 v3 probe stream)

- **G11**: stream header carries `image_uuid[16]` = the SkyLight
  UUID (producer-declared) + `uuid_valid=1`; pid/gen per D150b.
- **G12**: records per the accepted D150b contract — CALL emitted
  pre-call with raw `msgid=0x74a7`, `aux=option` verbatim;
  RET `msgid`=reply id iff `kr==SUCCESS` else 0, `aux=kr` verbatim.
- **G13**: a no-reply/failed send still yields CALL + RET(msgid=0,
  aux=kr) — never a fabricated pair, never silent.
- **G14**: one stream per process, ≤1 CALL+RET pair; a stream
  containing >1 `0x74a7` CALL is malformed by construction.

## A — assumptions & milestone boundaries

- **G15 assumption shipped**: the tool's header/doc must state
  verbatim that server-side side effects are **unverified** (the WS
  handler binary is not in-tree; read-only is inferred from request/
  reply shape, not disasm). Labelled diagnostic, not a fix.
- **G16 isolation**: analysis-only directory; no product hook,
  sentinel, transport/daemon, GUI, permit, or env-gated bypass.
- **G17 milestone boundary**: this milestone = source + host test
  only. Host acceptance is achievable deterministically **without
  WS**: bind a local receive right, point `remote` at it, capture the
  outgoing 24B request (asserts G3/G4 bytes), inject a synthetic
  `0x750b/0x2c` reply (asserts G8/G9) — all loopback, zero device.
  Device measurement = separately authorized future milestone.

## Acceptance classes (frozen)

1. **Compile/harness**: builds clean; loopback host test passes
   G3/G4/G8/G9/G11-G14 against a fake port — no WS needed.
2. **Device send**: one real send against live WS — out of scope
   here; requires separate review of G15's assumption status.
