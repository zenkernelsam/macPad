# D101 — deferred small-reply send, event-loop/pacing relation, timing-label corrections

Continuation of D100 on the same frozen image: `analysis/SkyLight_22F82`
(UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`, base `0x1850e5000`,
sha256 prefix `13d7a0ca010a9808234c71c34fd650b2`). Offline static only.

## 1. `_push_out_message` @ `0x1854214fc` — the actual deferred send

```
0x185421524: adrp x21, #0x1d8ba6000
0x185421528: ldrb w8, [x21, #0x1f8]        ; _gOutMsgPending
0x18542152c: cmp  w8, #1
0x185421530: b.ne #0x1854215cc            ; nothing pending -> ret
0x185421534: adrp x22, #0x1d8ba6000
0x185421538: ldr  x0, [x22, #0x1f0]        ; _gOutMsg
0x18542153c: ldr  w2, [x0, #4]            ; send_size = gOutMsg->msgh_size
0x185421540: mov  w1, #1                  ; MACH_SEND_MSG
0x185421544..50: w3=w4=w5=w6=0
0x185421554: bl   #0x18547cc6c            ; === actual mach_msg send ===
0x185421558: cbz  w0, #0x1854215c8        ; success -> clear pending
0x185421560: mov  w8, #3
0x185421564: movk w8, #0x1000, lsl #16    ; 0x10000003 = MACH_SEND_INVALID_DEST
0x185421568: cmp  w0, w8
0x18542156c: b.ne #0x185421584           ; other errors -> log/classify path
0x185421570: ldr  x0, [x22, #0x1f0]
0x185421574: ldr  w8, [x0]
0x185421578: tbz  w8, #0x1f, #0x1854215c8 ; complex bit clear -> done
0x18542157c: bl   #0x18547cc7c            ; mach_msg_destroy
0x185421584: bl   #0x185150a44            ; os_log id
0x18542158c: ldr  x20, [x8, #0x940]
0x185421590: mov  x0, x20
0x185421594: mov  w1, #0x10
0x185421598: bl   #0x18547d27c            ; os_log_type_enabled
0x18542159c: cbnz w0, ...
0x1854215a0: mov  w8, #0xfff8
0x1854215a4: movk w8, #0xefff, lsl #16    ; decode known send-failure codes
0x1854215c4: b.eq #0x185421570
0x1854215c8: strb wzr, [x21, #0x1f8]      ; _gOutMsgPending = 0  (all exits)
```

Callers (exact 2, raw `bl`/`b` opcode scan over `__text` 0x1850e7618..0x185478d5c):

| site | context |
|---|---|
| `0x1854213d0` `bl` | inside `_CGXPostReplyMessage+0x2c` — flush a *previous* pending reply before stashing a new one |
| `0x185421cb4` `bl` | inside `_CGXRunOneServicesPass+0x678` — end-of-pass flush |

## 2. The event loop: `server_loop` @ `0x185422638`

```
0x185422668: bl #0x185420f60   ; register_for_port_data_in_session(_notify_handler 0x1854228c0)
0x18542266c: bl #0x1854227d0   ; _schedule_idle_timeout
0x185422688: bl #0x18533ed48   ; _SLSCurrentRealTime
0x18542268c: fmov d1, #30.0
0x185422690: fadd d0, d0, d1
0x1854226ac: bl #0x1853e29c8   ; _WSScheduleCallbackOnSession (30s watchdog-ish cb 0x18537562c)
0x1854226b8: fmov d0, d8       ; d8 = 3600.0  (@0x185481810)
0x1854226bc: mov  w0, #0
0x1854226c0: bl   #0x18542163c ; CGXRunOneServicesPass
0x1854226c4: b    #0x1854226b8 ; === infinite services loop ===
```

## 3. `_CGXRunOneServicesPass` @ `0x18542163c` — pass anatomy (extent `0x18542163c..0x185421d80`)

Order established by code layout + control flow:

1. prologue `blraaz [0x1dd4b3f50]` ctx call `0x18542167c`; pool push `0x1854216a0`
2. timeout calc: `d8` clamped by `run_timer_pass(0x1d8ba5de8)` `0x1854216d8`; `CGXRunOneEventPass` @ `0x18542173c` — **nonzero → `fcsel d8, d12, d8, ne` forces next pass nonblocking**; flag `[0x1d8ba5fd8]` → process-death reaper drain `0x1854217a0..` (`waitpid(-1,…,WNOHANG)` `0x1854217cc` + pending-note handler loop `blraaz` `0x18542180c`)
3. receive-buffer memset `0x1854218c8`; timer arm `[0x1d8ba61c8]`; timeout arg `w5 = d11*d15` ms `0x185421930`
4. **combined send+receive** (the deferred-send release point):
   ```
   0x185421938: ldrb w8, [x22, #0x1f8]    ; _gOutMsgPending
   0x18542193c: cmp  w8, #1
   0x185421940: b.ne #0x185421984
   0x185421948: ldr  x0, [x8, #0x1f0]    ; _gOutMsg as send buffer
   0x18542194c: ldr  w2, [x0, #4]
   0x185421954: ldr  w4, [x8, #0x1c0]    ; rcv port-set name
   0x185421958: mov  w8, #0x902
   0x18542195c: movk w8, #0x300, lsl #16 ; 0x3000903 = SEND|RCV|RCV_TIMEOUT|OVERWRITE
   0x185421960: add  w1, w8, #1
   0x18542196c: mov  w3, #0x8000         ; rcv size
   0x185421974: bl   #0x18547cc8c        ; mach_msg_overwrite — atomic send+rcv
   0x18542197c: strb wzr, [x22, #0x1f8]  ; clear pending
   0x185421984: ... w1 = 0x3000902 (RCV|RCV_TIMEOUT|OVERWRITE)
   0x1854219a4: bl   #0x18547cc6c        ; mach_msg — pure receive
   ```
5. receive-success path `0x185421b6c`: msg counter `[0x1d8ba626c]++`,
   `_voucher_mach_msg_adopt` `0x185421ba4`, then
   `ldr w0,[sp,#0x2c]` (received `msgh_local_port`) → **`bl 0x185421e48`
   = `_post_port_data`** `0x185421bb4` → unhandled → `mach_msg_destroy`
   `0x185421bcc`
6. `tbnz w20,#0x1c → 0x185421cb4` (timing-flag bit) else session work loop
   `0x185421bd4..`: iterate `[0x1d8ba4f20]` list → `post_event_tap_data`
   `0x1852d4024` / `release_event_buffer` `0x1852d4570` per armed tap
7. **`bl _push_out_message` @ `0x185421cb4`** — end-of-pass flush
8. elapsed budget check `bl _count_vm_regions` `0x185421cc4`,
   `subs x20,x0,x27; b.ls 0x185421bd4` — work loop repeats under budget

## 4. `_post_port_data` @ `0x185421e48` — port→handler dispatch

```
0x185421e7c: ldr  w8, [x8, #0x1c4]       ; no-handler sentinel port
0x185421e80: cmp  w8, w0
0x185421e88: ...bl #0x18547c34c          ; deallocate, return 0
0x185421ea0: ldr  x0, [x8, #0x1e8]       ; _gPortDataNotes (CFDictionary)
0x185421eac: mov  w1, w20                ; local_port key
0x185421eb0: bl   #0x18547982c           ; CFDictionaryGetValue
0x185421eb8: bl   #0x1854796bc           ; -> note array / count w26
0x185421f44: ldr  w0, [x24, #8]          ; note.session_id
0x185421f48: bl   #0x185351938           ; CGSessionControlSelectSessionWithID
0x185421f4c: cbnz w0, #0x185421f64       ; wrong session -> skip
0x185421f50: ldp  x8, x2, [x24, #-8]     ; note.handler, note.ctx
0x185421f54: mov  x0, x20                ; port
0x185421f58: mov  x1, x19                ; msg buffer
0x185421f5c: blraaz x8                   ; === handler invocation ===
0x185421f60: mov  w23, #1                ; handled
0x185421f84: tst  w23, #1
0x185421f88: mov  w8, #0x3e8
0x185421f8c: csel w0, w8, wzr, eq        ; unhandled -> 1000
```

Registration side confirmed: `register_for_port_data_in_session` callers
include `_server_loop+0x30` (notify port → `_notify_handler`),
`CGXEnableWindowServerPort` (session service port → `rendezvousHandler`),
`CGXEnableRootWindowServerPort` (→ `rootRendezvousHandler`),
`CGXNewConnectionPort` (per-connection port → `connectionHandler`).
Handlers have **no direct bl/b callers** — invoked only via this map.

## 5. Complete per-request chain (msgid `0x74a7`, `_XGetWindowServerTimebase`)

| # | boundary | site | semantics |
|---|---|---|---|
| R0 | client `mach_msg` send completes | kernel | request lands in WS port-set queue — *earliest possible residency point; not in image* |
| R1 | dequeue | `mach_msg[_overwrite]` `0x185421974`/`0x1854219a4` ret `0x185421978`/`0x1854219a8` | first SkyLight-visible receipt point |
| R2 | voucher adopt + dispatch | `0x185421ba4` → `_post_port_data` `0x185421bb4` → `blraaz` `0x185421f5c` | port→handler selection, session select |
| R3 | handler prelude | `rendezvousHandler` `0x1851eb4a8` → `CGXHandleMessage` `0x1853beaa0` → stamps `_current_message`/`_current_connection_port` `0x1853beb0c/18` → work block `0x1851eb4e0` | **post-receive prelude, NOT queue wait** |
| R4 | impl dispatch | `blraaz` `0x1851eb584` → `__XGetWindowServerTimebase` `0x18543e148` | service body |
| R5 | reply enqueue | `CGXPostReplyMessage` `0x1854213a4`: pre-flush `push_out_message` `0x1854213d0`, then `memcpy→_gOutMsg`+pending=1 `0x1854213e8..fc` | **enqueue only, not send** |
| R6 | actual send | whichever first: `push_out_message`→`mach_msg` `0x185421554` via pass-end `0x185421cb4`; or next pass `mach_msg_overwrite` `0x185421974` (send+rcv atomic); or a later `CGXPostReplyMessage` pre-flush | request leaves server |

Remaining gap (static): the interval R0→R1 — port-set queue residence —
is between kernel delivery and the receive trap returning; nothing inside
SkyLight observes it. It requires either kernel-side visibility or a
receive-side timestamp at 0x185421978/0x1854219a8 minus a client-stamped
send time.

## 6. Corrections to D100 labels (required)

- D100 "T0→T1 = server-side queue+prelude wait" is **wrongly merged**:
  R3→R4 (rendezvous entry → `blraaz` impl) is the **handler prelude**
  only — the request had already been dequeued by `mach_msg` at that
  point. Time queued *before* handler entry lives entirely in R0→R1
  (kernel port queue + whatever interval the services loop was busy).
- `_CGXPostReplyMessage` for small replies is **enqueue** (memcpy into
  `_gOutMsg`), not send. Actual send occurs at R6 sites. D100's reply
  row wording is corrected accordingly.

## 7. Flush ↔ event loop ↔ pacing relationship (what the code establishes)

- The deferred reply leaves the server **no later than the end of the
  current `CGXRunOneServicesPass`** (`0x185421cb4` flush) — or at the
  head of the next pass via the combined `mach_msg_overwrite`
  (`0x185421974`) — provided the pass reaches those instructions.
- The services loop `server_loop` runs `CGXRunOneServicesPass(0, 3600.0)`
  forever on one thread; its per-pass work list is event-tap delivery
  (`post_event_tap_data`/`release_event_buffer` on the `[0x1d8ba4f20]`
  armed list), the process-death reaper, session timer passes, and
  request dispatch — **all serialized on the same pass**.
- `CGXRunOneEventPass` `0x1853f7970` drains `eventQueue`
  (`CGXFilterEvent`/`CGXSetEventTarget`/key-event fixup) and returns
  nonzero → next pass nonblocking (`fcsel d8,0,d8` `0x185421744`). Its
  call list contains **no display/composite/render functions** — HID event
  pipeline only.
- **No static edge found** between the services pass / reply flush and
  the frame-completion or display-swap path (no `CARender*`/`IOSurface`/
  IOMFB calls inside `server_loop`, `CGXRunOneServicesPass`,
  `CGXRunOneEventPass`, `post_port_data`, `CGXHandleMessage`,
  `CGXPostReplyMessage`, `push_out_message`). The pacing relationship is
  therefore *indirect only*: a composite-blocked or slow services pass
  delays both the next request dequeue (R1) and pending-reply flush
  (R6) — single-threaded serialization, not a shared compositor queue.
- No runtime causal claim is made from this ordering; it is a static
  reachability result only.
