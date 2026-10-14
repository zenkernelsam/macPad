# R006 — the producer of TCC raw `1`: transport failure, not a daemon reply

Date: 2026-10-15. Scope: identify what actually produces the `w0=1` that
`audit_token_check_tcc_access` returns to `WSAuditTokenHoldsCachedProcessRight`
(observed live in R005). Result: **every `TCCAccessPreflightWithAuditToken`
call inside WindowServer fails at `xpc_connection_send_message_with_reply_sync`
with `_xpc_error_connection_invalid`; no request ever reaches a tccd, no reply
dictionary is ever parsed, and the `1` is the preflight function's
pre-initialized default result.** The raw `1` is a *transport-failure*
artifact, not a policy decision.

## Exact images (runtime-verified)

| image | UUID | runtime base | source |
|---|---|---|---|
| SkyLight | `96676A53-B1E0-3D7E-B98B-B73873CD1880` | `0x1974d1000` (slide `0x123ec000`) | `analysis/SkyLight_22F82` |
| TCC | `1238D7CA-719A-36A4-94B1-2F387E6B3BF1` | `0x1974b3000` | `analysis/TCC_22F82` |

`analysis/TCC_22F82`: sha256
`09b841378285a120a04b1c8718dd5c7e3b98b4e59849eea0d8767f845d02f2de`,
extracted on-device from the rootfs Ventura dyld cache
(`/System/Cryptexes/OS/System/Library/dyld/dyld_shared_cache_arm64e`)
via `misc/dsc_extract` (built arm64e, signed `ldid -S`×2 with
`libmachook_ent.plist`, trustcache'd, ran under `launchdchrootexec`).
Its UUID was then confirmed identical in the live WS `image list`
— the analyzed file IS the loaded image.

Instruction words verified at runtime before breakpoints:
`pacibsp` at `tccd_send_message` (0x1974b5740),
`__TCCAccessSetPidResponsibleForPid_block_invoke_2` (0x1974bc7b0 —
labeled "shared reply parser" here originally; corrected under R007
review: it is a different API's completion, not a shared get_bool
parser, and its 0-hit count below is expected rather than meaningful),
`TCCAccessRequest` (0x1974b4d9c), `tccd` (0x1974b5584);
`cmp w0,#0` (`0x7100001f`) at SkyLight `audit_token_check_tcc_access+108`
(0x197811e0c).

## Live transaction (one `postmin` canonical post, WS pid 61460)

Probe mode `postmin` added for this round (source change):
`SLSMainConnectionID` + ONE `SLSPostEventRecord` — no warp, no
CGEventPost witness, no inventory phases, no session mutation.
`SLSPostEventRecord -> rc=1002` as before.

Captured sequence per TCC request in WS (pane:
`coordination/artifacts/r006-lldb-pane.txt`):

```
tccd(flags=1, audit)                                  → conn = xpc_connection_create_mach_service("com.apple.tccd.system")
tccd_send_message(conn, 0, msg, handler, 7, attempt=0)
  xpc_connection_send_message_with_reply_sync
      → x0 = 0x1ef528980  libxpc`_xpc_error_connection_invalid   ← EVERY attempt
  (error != interrupted → no retry loop; == connection_invalid)
  → handler(block=0x16b4aa830, reply=connection_invalid)          (send+1564)
      → completion block impl 0x1850ccebc:
          xpc_get_type(reply) == XPC_TYPE_ERROR → error path
      → caller's mini-handler (0x1850d0560) invoked with w1=0
          → cbz w1 → SKIPS the write → result stays 1
SkyLight audit_token_check_tcc_access+108: w0 = 1 → cset eq → 0 → deny
```

Breakpoint tallies (whole session): `tccd`=13, `tccd_send_message`=8,
post-sync-send=8 (**all** `_xpc_error_connection_invalid`),
interrupted-path=0, handler-invoke=4 (always with the error object),
`__TCCAccessSetPidResponsibleForPid_block_invoke_2`=**0** (originally
labeled "shared `get_bool("result")` parser" — mislabel corrected
under R007 review; it is not on this path), mini-handler(0x1850d0560)=2
(both `w1=0`).

## Static chain (exact-file LLDB, `analysis/TCC_22F82`)

- `TCCAccessPreflightWithAuditToken` (0x1850d03bc): `mov w8,#1;
  str w8,[sp+0x70]` at +68 — result field initialized to **1**.
- `TCCAccessRequest` (0x1850c8d9c): `_os_activity_initiate
  ("TCCAccessRequest() IPC")` → `dispatch_async` → work block calls
  `tccd_set_message_number` + `tccd_send_message` (sync mode).
- `tccd` (0x1850c9584): `xpc_connection_create_mach_service(name,0,2)`
  with `name = flags ? "com.apple.tccd.system" : "com.apple.tccd"`;
  all observed calls had `flags=1` → **`.system`**.
- `tccd_send_message` (0x1850c9740): sync path at +796 when flags==0 →
  `xpc_connection_send_message_with_reply_sync` → `cmp` vs error
  singletons at +952/+968 → interrupted → recursive retry (attempt+1,
  max w4=7); `connection_invalid` → invoke caller's handler with the
  error object (+1108..+1124 → +1564).
- Completion handler impl `0x1850ccebc` (block ptr `[x3+0x10] =
  0x1974b8ebc` read live): on `XPC_TYPE_ERROR` skips all
  `result/restricted/preflight_unknown` parsing and forwards failure.
- Preflight's two mini-handlers (0x1850d0548/0x1850d0560): `cbz w1` —
  invoked with `w1=0` they write nothing; the initialized `1` survives.

## The upstream invariant (producing operation)

`xpc_connection_create_mach_service("com.apple.tccd.system")` inside
WindowServer yields a connection whose peer does not exist:

- The name is real on Ventura:
  `/System/Library/LaunchDaemons/com.apple.tccd.system.plist` →
  `tccd system`, `MachServices{com.apple.tccd.system:true}`.
- In the chroot that job is **not loaded** — the only
  `TCC.framework/Support/tccd` on the device is the iOS daemon
  (pid 23321, `user/501/com.apple.tccd`), a different service name.
- `bootstrap_look_up("com.apple.tccd")` from a chroot process succeeds
  (rc=0, port=3343) — the chroot bootstrap resolves the iOS user tccd's
  name but nothing serves `com.apple.tccd.system`.

So the denied `1002` is produced by **missing TCC system-daemon
transport**, not by a policy lookup, not by a client cache, not by a
local identity check. R005's `w0=1` is `TCCAccessPreflightWithAuditToken`'s
fail-closed default surviving an unreachable daemon.

Ruled out by the same trace: cached-deny shortcut (R005 proved the miss
path), local identity/argument check (none exists in this path — the
error arrives at the XPC layer), daemon policy reply (no request left
the client; the reply parser never ran), entitlement evaluation
(`audit_token_has_entitlement` is the sibling `event.monitor` impl,
not this one).

## What this is NOT

- Not a fix. Nothing was patched, granted, or mutated; every check ran.
- Not proof that starting `tccd system` is *sufficient* for input —
  it is the evidenced *necessary* upstream producer for the current
  deny; what the daemon would answer once reached is still unknown.
- Not a same-pid proxy suggestion; the transport must be a genuine
  `com.apple.tccd.system` endpoint or the invariant stays broken.

## Environment discipline

Single WS generation via `macos_gui.sh start coexist` (watchdog pid
60940, GUI_RC=0); unique tmux session `devin_r006`; lldb Procursus 16
attached to WS 61460 only; `expr` unusable on this target ("couldn't
install checkers") — all captures via `register read`/`memory read`
breakpoint commands only; explicit `process detach`, `breakpoint
disable` (14), tmux killed, stack stopped via `macos_gui.sh stop`;
zero orphan processes, backboardd alive, `echo HI` passes, no
sentinels left in either /tmp namespace.
