# D100 — Offline exact-SkyLight service-dispatch timing-boundary audit

Scope: offline static RE only on `analysis/SkyLight_22F82` (Mach-O UUID
`96676A53-B1E0-3D7E-B98B-B73873CD1880`, image base `0x1850e5000`, sha256
prefix `13d7a0ca010a9808234c71c34fd650b2`). No device, no instrumentation, no
source edits. All addresses are unslid image VAs; file offset of a __text VA
is `VA - 0x1850e5000` (section `__TEXT,__text` VA `0x1850e7618`, fileoff
`0x2618`, size `0x391744`; arm64e, all code pointers PAC-signed).

Goal: identify a read-only ordinary service request's
receive → dispatch → reply boundaries that could later be correlated with
the existing frame-pacing wait — without claiming client elapsed time ==
server queue delay.

## 1. Chosen request: `_XGetWindowServerTimebase` (msgid `0x74a7`)

Ordinary read-only query in the `CGXWindowServer` mig_subsystem. No state
mutation, no injection path. Same class also covers `_XGetDebugInfo`
(`0x74a8`), `_XGetSessionReadOnceData` (`0x7480`), `_XConnectionsForPid`
(`0x74a0`), `_XGetEventTapList` (`0x748b`). Timebase is the most natural
correlate for a pacing investigation.

## 2. Receive boundary

### 2a. Registration (static setup)

`_CGXEnableWindowServerPort` @ `0x1851eb470` registers the port-level
message handler:

```
0x1851eb470: adrp x8, #0x1d8ba5000        ; server-state global
0x1851eb474: ldr  x8, [x8, #0x460]
0x1851eb478: ldr  x8, [x8, #0x20]         ; port-record object
0x1851eb47c: cbz  x8, #0x1851eb488
0x1851eb480: ldr  w0, [x8, #0xc]          ; mach port name
0x1851eb484: b    #0x1851eb48c
0x1851eb488: mov  w0, #0
0x1851eb48c: ldr  w3, [x8]
0x1851eb490: adrp x16, #0x1851eb000
0x1851eb494: add  x16, x16, #0x4a8        ; = rendezvousHandler
0x1851eb498: paciza x16                    ; sign handler fn ptr
0x1851eb49c: mov  x1, x16
0x1851eb4a0: mov  x2, #0                  ; ctx
0x1851eb4a4: b    #0x185420f60           ; _register_for_port_data_in_session
```

`_register_for_port_data_in_session` @ `0x185420f60` inserts
`{port_name → (handler=rendezvousHandler, ctx)}` into the global map
`_gPortDataNotes` @ `0x1d8ba61e8` (lazy-created via its first branch).

Kernel delivery → dispatch-source wake → `rendezvousHandler` invocation
happens inside libdispatch's mach-receive machinery and is NOT visible in
this image (see §7 missing inputs). Inside SkyLight, the earliest
attributable server-side code point is `rendezvousHandler` entry.

### 2b. Handler entry

`rendezvousHandler(cookie, req_hdr, ctx)` @ `0x1851eb4a8`:

```
0x1851eb4a8: pacibsp
0x1851eb4ac: stp  x29, x30, [sp, #-0x10]!
0x1851eb4b0: mov  x29, sp
0x1851eb4b4: mov  x0, x1                  ; req header
0x1851eb4b8: adrp x1, #0x1dd490000
0x1851eb4bc: add  x1, x1, #0xdf0         ; work block literal .147
0x1851eb4c0: adrp x2, #0x1dd490000
0x1851eb4c4: add  x2, x2, #0xe10         ; reply block literal .151
0x1851eb4c8: bl   #0x1853beaa0           ; _CGXHandleMessage
0x1851eb4cc: mov  w0, #0
0x1851eb4d0: ldp  x29, x30, [sp], #0x10
0x1851eb4d4: retab
```

So the request is funnelled into `_CGXHandleMessage(req, work_block,
reply_block)` — the serializer + reply gate. `rootRendezvousHandler` @
`0x1851eb94c` is the sibling path for the root port.

## 3. Dispatch boundary

`_CGXHandleMessage` @ `0x1853beaa0` — stamps in-flight globals, allocates a
0x8000-byte reply buffer on-stack, invokes the work block, then gates the
reply block on the filled `RetCode`:

```
0x1853beab8: mov  w9, #0x8010
0x1853beabc: adrp x16, #0x1dd4b3000
0x1853beac0: ldr  x16, [x16, #0xf50]
0x1853beac4: blraaz x16                   ; context/audit prologue call
0x1853beac8: sub  sp, sp, #8, lsl #12     ; 32 KiB reply buffer
0x1853beaec: add  x8, sp, #8
0x1853beaf0: add  x0, x8, #0x18
0x1853beaf4: mov  w1, #0xaa
0x1853beaf8: mov  w2, #0x7fe8
0x1853beafc: bl   #0x18547ce5c            ; memset poison
0x1853beb0c: ldr  w8, [x19, #0x14]        ; req.msgh_id
0x1853beb14: str  w8, [x22, #0xb88]       ; -> _current_message (0x1d8ba5b88)
0x1853beb18: ldr  w8, [x19, #0xc]         ; req.msgh_local_port
0x1853beb20: str  w8, [x23, #0xb98]       ; -> _current_connection_port (0x1d8ba5b98)
0x1853beb24: mov  x8, x21                 ; work block
0x1853beb28: ldr  x9, [x8, #0x10]!        ; block->invoke
0x1853beb2c: add  x2, sp, #8              ; reply buffer
0x1853beb30: mov  x0, x21
0x1853beb34: mov  x1, x19                 ; request
0x1853beb38: blraa x9, x8                 ; === dispatch into work block ===
```

Work block `__block_invoke` @ `0x1851eb4e0` (of
`__block_literal_global.147` @ `0x1dd490df0`):

```
0x1851eb4f8: mov  x0, x1
0x1851eb4fc: mov  x1, x2
0x1851eb500: bl   #0x1854477d0            ; _CGXRendezvous_server (pre-dispatch)
0x1851eb504: cbz  w0, #0x1851eb518        ; not a rendezvous msg -> CGX table
--- reply header construction (0x1851eb518..0x1851eb544) ---
0x1851eb518: str  wzr, [x19, #0x20]       ; reply+0x20 = 0 (RetCode preset)
0x1851eb51c: adrp x8, #0x185480000
0x1851eb520: ldr  d0, [x8, #0xce8]        ; NDR_record
0x1851eb524: stur d0, [x19, #4]
0x1851eb528: ldr  w8, [x20]
0x1851eb52c: and  w8, w8, #0x1f           ; reply.msgh_bits = req bits & 0x1f
0x1851eb530: str  w8, [x19]
0x1851eb534: ldr  w8, [x20, #8]           ; reply.msgh_remote = req.msgh_remote_port
0x1851eb538: ldr  w9, [x20, #0x14]
0x1851eb53c: add  w9, w9, #0x64           ; reply.msgh_id = req.msgh_id + 100
0x1851eb540: stp  w8, wzr, [x19, #8]      ; remote_port, reserved
0x1851eb544: stp  wzr, w9, [x19, #0x10]   ; local_port=0, msgh_id
--- CGXWindowServer table lookup ---
0x1851eb548: ldr  w8, [x20, #0x14]        ; req.msgh_id
0x1851eb54c: mov  w9, #-0x74a9
0x1851eb550: add  w9, w8, w9
0x1851eb554: cmn  w9, #0x41
0x1851eb558: b.lo #0x1851eb58c            ; out of range -> secondary table
0x1851eb55c: mov  w9, #-0x7468
0x1851eb560: add  w8, w8, w9              ; index = id - 0x7468
0x1851eb564: adrp x9, #0x1dd49f000
0x1851eb568: add  x9, x9, #0x2c8          ; CGXWindowServer_subsystem
0x1851eb56c: mov  w10, #0x28
0x1851eb570: umaddl x8, w8, w10, x9       ; &routine[idx] (header is 0x28)
0x1851eb574: ldr  x8, [x8, #0x28]         ; signed impl ptr
0x1851eb578: cbz  x8, #0x1851eb58c
0x1851eb57c: mov  x0, x20                 ; req
0x1851eb580: mov  x1, x19                 ; reply
0x1851eb584: blraaz x8                    ; === impl dispatch ===
0x1851eb588: b    #0x1851eb508            ; return 1
--- secondary subsystem (msgids 0x7d00..0x7d09, table 0x1dd4a62f0) ---
0x1851eb5d8: mov  w10, #-0x7d00
0x1851eb5dc: add  w9, w9, w10
0x1851eb5e0: adrp x10, #0x1dd4a6000
0x1851eb5e4: add  x10, x10, #0x2f0
0x1851eb5e8: mov  w11, #0x28
0x1851eb5ec: umaddl x9, w9, w11, x10
0x1851eb5f0: ldr  x9, [x9, #0x28]
0x1851eb5f4: cbz  x9, #0x1851eb608
0x1851eb600: blraaz x9
--- no handler: error reply ---
0x1851eb608: mov  w0, #0
0x1851eb614: mov  w8, #-0x12f             ; -303 MIG_BAD_ID -> reply RetCode
0x1851eb618: str  w8, [x19, #0x20]
```

(`b.lo` at 0x1851eb558 is taken when `id-0x74a9` is out of `[0,0x41)` —
i.e., invalid ids fall through to the secondary table; the earlier demux
`CGXWindowServer_server_routine` @ `0x1854439a8` is the standalone
`*_server_routine` form of the same table, used by `dispatch_mig_server`
registrations elsewhere.)

`CGXWindowServer_subsystem` @ `0x1dd49f2c8` (AUTH_CONST): header record
`{signed server_routine=0x8028..054439a8, ids 0x7468|0x74a9, maxsize
0x1034, reserved×2}` then `0x41` descriptors of stride `0x28`; routine
field = first qword of each descriptor, PAC-signed. Verified entries:

| msgid | impl VA | symbol |
|---|---|---|
| 0x74a7 | 0x18543e148 | `__XGetWindowServerTimebase` |
| 0x74a8 | 0x18543dfc0 | `__XGetDebugInfo` |
| 0x7480 | 0x185441ddc | `__XGetSessionReadOnceData` |
| 0x74a0 | 0x18543eb00 | `__XConnectionsForPid` |
| 0x7493 | 0x18543ff80 | `__XWindowListCreate` |
| 0x748b | 0x1854409d0 | `__XGetEventTapList` |

## 4. Service impl (`_XGetWindowServerTimebase` @ `0x18543e148`)

```
0x18543e170: ldr  w8, [x0]
0x18543e174: tbnz w8, #0x1f, #0x18543e1bc  ; reject MACH_MSGH_BITS_COMPLEX
0x18543e178: ldr  w8, [x0, #4]
0x18543e17c: cmp  w8, #0x18
0x18543e180: b.ne #0x18543e1bc            ; req.msgh_size must == 0x18
0x18543e184: movi v0.8b, #0xaa
0x18543e188: str  d0, [sp]
0x18543e18c: mov  x0, sp
0x18543e190: bl   #0x18547cd6c            ; _mach_timebase_info
0x18543e194: ldr  d0, [sp]
0x18543e198: stur d0, [x19, #0x24]        ; reply payload @+0x24
0x18543e19c: str  wzr, [x19, #0x20]       ; RetCode = 0 (KERN_SUCCESS)
0x18543e1a4: ldr  x8, [x8, #0xa18]
0x18543e1a8: ldr  x8, [x8]
0x18543e1ac: str  x8, [x19, #0x18]        ; NDR
0x18543e1b0: mov  w8, #0x2c
0x18543e1b4: str  w8, [x19, #4]           ; reply.msgh_size = 0x2c
0x18543e1b8: b    #0x18543e1d4
0x18543e1bc: mov  w8, #-0x130             ; -304 MIG_BAD_ARGUMENTS
0x18543e1c0: str  w8, [x19, #0x20]
```

Impl writes only into the reply buffer; it does not send.

## 5. Reply boundary

Back in `_CGXHandleMessage` after the work-block `blraa` returns
(`0x1853beb3c`+): clears `_current_message`/`_current_connection_port`,
reads `reply RetCode` (`[sp+0x28]`), and the predicate at
`0x1853beb58..0x1853beb8c` runs `reply_block` (`__block_literal_global.151`
→ `__block_invoke_2` @ `0x1851eb4d8`, tail `b _CGXPostReplyMessage`) only
when the RetCode indicates a sendable reply (negative-range check vs
`-0x131`, i.e. the MIG "no reply" convention).

`_CGXPostReplyMessage` @ `0x1854213a4`:

```
0x1854213d0: bl   #0x1854214fc            ; _push_out_message (flush gOutMsg first)
0x1854213d4: ldp  w22, w2, [x19]          ; reply.msgh_bits, msgh_size
0x1854213d8: cmp  w2, #8, lsl #12         ; > 32 KiB?
0x1854213dc: b.hi #0x185421404
0x1854213e0: tbnz w22, #0x1f, #0x185421404 ; complex msg?
0x1854213e4: adrp x8, #0x1d8ba6000
0x1854213e8: ldr  x0, [x8, #0x1f0]        ; _gOutMsg buffer
0x1854213ec: mov  x1, x19
0x1854213f0: bl   #0x18547ce3c            ; memcpy -> batch slot
0x1854213f4: mov  w8, #1
0x1854213fc: strb w8, [x9, #0x1f8]        ; _gOutMsgPending = 1
0x185421400: b    #0x18542148c
0x185421404: mov  x0, x19
0x185421408: mov  w1, #1                  ; MACH_SEND_MSG
0x18542140c: mov  w3, #0
0x185421410: mov  w4, #0
0x185421414: mov  w5, #0
0x185421418: mov  w6, #0
0x18542141c: bl   #0x18547cc6c            ; _mach_msg send
```

Small simple replies (≤32 KiB, non-complex — exactly our
`_XGetWindowServerTimebase` case, size `0x2c`) are batched into `_gOutMsg`
(`0x1d8ba61f0`) with `_gOutMsgPending=1` for a later flush instead of an
immediate `mach_msg` send. Large/complex replies go straight to
`mach_msg(MACH_SEND_MSG)` on `reply.msgh_remote_port` = the client's reply
port (copied from `req.msgh_remote_port`).

## 6. Boundary summary + bounded timing-observation attach points

| stage | static location | what a future observer could timestamp |
|---|---|---|
| kernel delivery → source wake | libdispatch/kernel, not in image | (missing input) arrival→wake latency |
| handler entry | `rendezvousHandler` `0x1851eb4a8` | T0 server receipt (first SkyLight insn) |
| in-flight stamp | `_CGXHandleMessage` `0x1853beb0c..20` | `_current_message`/`_current_connection_port` writes — serial-queue admission |
| impl dispatch | `blraaz x8` `0x1851eb584` | T1 demux→impl edge |
| impl service | `0x18543e148` (timebase) | T2 handler body |
| reply gate | `0x1853beb58..8c` | RetCode classification |
| reply send | `_CGXPostReplyMessage` `0x1854213a4`/`0x18542141c` | T3 mach_msg send (or `_gOutMsg` enqueue) |

A bounded observation would bracket `rendezvousHandler` entry vs the
`blraaz` into the impl (T0→T1 = SkyLight-side queue+prelude wait inside the
server, closest measurable proxy for "time the request waited in the
server") and T1→T3 for service+reply cost. This brackets the **server-side
serial-queue + dispatch span**, not a client-visible latency.

**Interpretation guardrail (required):** a client's `mach_msg` send→reply
elapsed additionally contains kernel IPC delivery, dispatch-source
scheduling delay, reply-port wake, and the `_gOutMsg` batching deferral
(small replies are deliberately deferred — elapsed may include an
unrelated flush interval). Client elapsed ≠ server queue delay; only the
T0→T1/T1→T3 in-server brackets are server-attributable.

## 7. Missing inputs (explicit, not guessed)

- **libdispatch**: `dispatch_mig_server`/mach-receive source internals —
  the wake→`rendezvousHandler` invocation and any queuing between kernel
  delivery and handler entry are outside `SkyLight_22F82`. Exact dyld
  cache entry not present in `analysis/`.
- **kernel (xnu 8792.82.2)**: `mach_msg` delivery/send timing semantics —
  the policy snapshot analyzed the kernel for MAC ops, not IPC latency.
- **flush site for `_gOutMsg`**: `_push_out_message` @ `0x1854214fc` is
  the flush helper; its caller cadence (end of `CGXHandleMessage` pass /
  run-loop drain) was not fully traced — bounded follow-up if needed.
- **who calls `CGXEnableWindowServerPort`**: no direct `bl/b` callers in
  `__text` — invoked indirectly (session-init path); static attribution of
  the enabling moment is open but does not affect the request-path
  boundaries.

## 8. Reply-to-request linkage (wire facts)

- `reply.msgh_id  = req.msgh_id + 0x64` (`0x1851eb53c`)
- `reply.msgh_remote_port = req.msgh_remote_port` (`0x1851eb534`)
- `reply.msgh_bits = req.msgh_bits & 0x1f` (`0x1851eb52c`)
- `reply RetCode @ +0x20`, payload from `+0x24`, `msgh_size` per impl.
- Per-request context: `_current_message` (msgid) +
  `_current_connection_port` (local port) stamped before dispatch, cleared
  after — the audit-token-adjacent request identity window (cf.
  `gCurMsgAudit` family noted in prior input-transport RE).

No claims beyond static code; all addresses verified against the exact
frozen binary.
