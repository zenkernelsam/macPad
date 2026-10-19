# D190 — analysis-only one-shot raw 0x74a7 probe sender

**Status: PENDING — standalone diagnostic. Never installed, invoked, or
linked into anything. Host test coverage only; no device/product
integration exists or is permitted for this milestone.**

Sends exactly ONE raw `_XGetWindowServerTimebase` (`0x74a7`) request per
process, captures client-side monotonic CALL/RET timestamps and the
verbatim `mach_msg` return, validates the reply per the real MIG
epilogue, and writes a D169 v3 probe-stream file. Wire shape is verbatim
from the **D188 exact 22F82 disassembly** (`_SLSGetWindowServerTimebase`
@ `0x185364890`, callsite `:4904-49bc`), gated by the frozen **D189
G1–G17** contract.

## UNVERIFIED ASSUMPTION (G15, verbatim)

Server-side side effects of `_XGetWindowServerTimebase` are
**UNVERIFIED** — the WindowServer handler binary is not in-tree;
"no server state change" is inferred from the request/reply shape
only, not from handler disassembly. This tool is a labelled
diagnostic, not a fix. Its output is **client-side probe data only**:
no WS handler instrumentation exists, no wait-vs-no-wait comparison
is performed, and the stream alone cannot decide wake adaptation.

## Files

- `d190_probe.h` — wire constants, reply layout, classification enum,
  API + contract comments
- `d190_probe.c` — LC_UUID dyld gate, request builder, reply
  classifier, record fill, stream writer, `d190_send_once`
- `d190_main.c` — one-shot entry point (never invoked)
- `d190_probe_test.c` — host suite (loopback ports only)
- `run_tests.sh` — ASan+UBSan, TSan, arm64 object + binary link

## Wire shape (D188 RE-confirmed)

| Field | Value |
|---|---|
| `msgh_bits` | `0x1513` (remote COPY_SEND, local MAKE_SEND_ONCE) |
| `msgh_id` | `0x74a7` |
| request | 24-byte header only — no body |
| option | `0x00304003` = SEND\|RCV\|SYNC_OVERRIDE\|PROPAGATE_QOS\|SYNC_WAIT (xnu `message.h:904–932`) |
| `send_size` / `rcv_size` | `0x18` / `0x34` |
| timeout / notify | `0` / `MACH_PORT_NULL` — verbatim |
| remote | runtime `SLSServerPort` via dlsym **C name** (D195/F1 — `"_SLSServerPort"` searched for `__SLSServerPort` and always missed; no wrapper reuse — G6) |
| local | `mig_get_special_reply_port()` — the **thread** special reply port: task holds receive+send rights, thread-bound, TSD-cached; `MAKE_SEND_ONCE` mints the send-once right INTO the outgoing message (D192-corrected G2 — the SRP is not itself a send-once right; the option's SYNC_WAIT/QoS handoff binds specifically to the thread SRP) |

## Identity binding (D195/F2) + bounded scanning (D195/F3)

Two gates run before any send: (1) the exactly-one-loaded-image LC_UUID
match (G1), and (2) `d190_addr_image_match(slp, uuid)` — `dladdr` on the
resolved pointer binds it to its *owning* image and requires that
image's LC_UUID to equal the pinned 22F82 UUID, so an interposed or
differently-versioned `SLSServerPort` cannot satisfy the gate by
coexistence.

`d190_image_lc_uuid_bounded(mh, hdr_bytes, out)` walks at most
`min(sizeofcmds, hdr_bytes − header)` bytes: requires
`cmdsize ≥ sizeof(load_command)`, no advance past the bound,
`LC_UUID cmdsize ≥ sizeof(uuid_command)`, exactly one LC_UUID
(duplicates reject), and every declared `ncmds` command inside the
region. The live variant `d190_image_lc_uuid` uses the dyld-mapped
`sizeofcmds` bound.

Reply valid iff ALL: `msgh_id==0x750b`, `msgh_size==0x2c`, complex bit
clear, `msgh_remote_port==0`, `retcode==0` → payload u64 `@+0x24`.

## SRP cleanup — the four exact stub classes (D193, per D192 :495c-49e0)

`d190_cleanup_class(kr, class)` is the single decision function;
`d190_send_once` executes exactly this mapping:

| class | condition | action |
|---|---|---|
| `NONE` | valid reply | nothing — send-once consumed by reply; SRP stays TSD-cached |
| `DESTROY` | SUCCESS, any validation failed | `mach_msg_destroy(reply)` — covers header-carried remote right + descriptors; **SRP retained** |
| `NONE` | `kr ∈ {SEND_INVALID_DATA, SEND_INVALID_DEST, SEND_INVALID_HEADER}` (`0x10000002/03/10`) | nothing — send-side reject, message never left; **SRP retained** |
| `DEALLOC` | any other non-success kr | `mig_dealloc_special_reply_port` — destructs ALL rights on the thread SRP + clears TSD |

The earlier two-class split (dealloc on any `kr!=SUCCESS`) was wrong:
it churned the pristine SRP on harmless send-side rejects and never
existed in the stub.

## Output — D169 v3 probe stream

`d190_write_stream` emits `d169_stream_hdr` + exactly 2 `d141_record`s:
`uuid_valid=1`, `image_uuid[16]` = gated SkyLight UUID
(**producer-declared** — hi64 not record-verifiable), `gen =
(pid<<32)^uuid_lo64`, `emitted=2`, `dropped=0`, `uncommitted=0`,
`n_records=2`, `reserved=0`, label `d190-probe`. Records: CALL(seq0,
`msgid=0x74a7`, `aux=option`) + RET(seq1, `msgid`=reply id iff
`kr==SUCCESS` else 0, `aux=kr` verbatim). A no-reply/failed send still
yields CALL+RET(msgid=0, aux=kr) — never fabricated (G13).

## Identity gate (G1)

`d190_check_skylight_uuid` walks dyld images' LC_UUID (no user string):
exactly ONE image matching `96676A53-B1E0-3D7E-B98B-B73873CD1880`
required; absent/ambiguous/mismatched → refuse before request
construction. No flag/env bypass exists.

## Tests (97 checks, `run_tests.sh`)

| p | Coverage |
|---|---|
| p1 | wire constants: 24B request, packed 0x2c reply, option-bit decode, builder fields, no body |
| p2 | LC_UUID extraction on fabricated Mach-O (found/32-bit/no-cmd); live gate consistency (fail-closed zeroing) |
| p3 | all reply classes: OK, KR, TRUNC, COMPLEX, BAD_ID, BAD_SIZE, REMOTE, RETCODE |
| p4 | record legs (seqs, ids, aux verbatim, same tid/pid/gen), kr-fail RET(msgid=0), gen encoding |
| p5 | stream write → accepted d169 `d169_stream_load` round-trip (header fields verbatim) → full `d169_decode` vs synthetic WS stream: 1 scored span, latency = handler_begin − call |
| p6 | **G17 loopback** + cleanup execution: fake-WS thread receives the 24B request on a test-owned port (verifies size/id/voucher verbatim, send-once right vended), injects `0x750b/0x2c` reply → OK + payload → `cleanup_none` ran; bad-id injection → `E_BAD_ID` → `mach_msg_destroy` **executed** (hook counter); dead remote → `SEND_INVALID_DEST` → **no** cleanup (class 3); oversized reply → `RCV_TOO_LARGE` → real `mig_dealloc_special_reply_port` **executed** (class 4) |
| p7 | `d190_cleanup_class` pure decisions — all four classes incl. `SEND_INVALID_DATA/HEADER` and out-of-range kr |
| p8 | **F1** dlsym C-name mechanism on a harmless fixture dylib (C-name resolves, `"_x"` does NOT — the exact bug shape); **F2** `d190_addr_image_match`: own image matches, fixture symbol owns a different image, both mismatch the pinned UUID |
| p9 | **F3** bounded scanner: valid, `cmdsize=0` (no hang), buffer < declared `sizeofcmds`, truncated `LC_UUID`, `ncmds` past region, oversized `cmdsize`, duplicate `LC_UUID`, `hdr_bytes < header` |

Note: the kernel translates `msgh_bits` on receive (remote becomes
`MOVE_SEND_ONCE`) — the p6 wire-shape assertions check the received
semantics; the `0x1513` literal is proven on the constructed buffer
(p1). The `d190_reply` struct is `packed` so `payload` sits at the
real wire offset `+0x24` (4-aligned; normal alignment would pad it
to `+0x28`).

## Boundaries

- Exactly one `mach_msg` per process (single callsite, no loop/retry).
- Never calls `_SLSGetWindowServerTimebase` — its once-cache would
  suppress the send (G6).
- `mach_msg` return recorded verbatim; never synthesized success.
- The compiled binary is never executed — compile/link is the
  acceptance artifact; device send is a separately authorized
  milestone (D189 class 2).
- No hooks, sentinel, permits, drain transport, GUI, product
  integration, or wake conclusion.

## Verification

`bash run_tests.sh`:

- ASan+UBSan: **97 checks, 0 failures** (9 scenarios)
- TSan: **97 checks, 0 failures** — the loopback helper thread is the
  only concurrency; clean
- arm64: `d190_probe.o`, `d190_main.o` — `Mach-O 64-bit object arm64`;
  `d190_probe` — `Mach-O 64-bit executable arm64` (link-level only,
  never run)
