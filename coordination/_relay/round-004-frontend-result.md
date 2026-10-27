# Round 004 — frontend result: XPC empty-reply source audit

Date: 2026-10-18. Read-only audit; no source/test/build edit, no build, no
device command, no Git write. Only this file and
`docs/evidence/r004-xpc-empty-reply-source-audit-20261018.md` were added.

Candidate HEAD: `7e5d771`. Backend WIP in the working tree was not used as
audit input.

## Inspected identities

| File | SHA-256 |
| --- | --- |
| `libmachook/mac_hooks.m` | `e8ae9dadfa5bb40701a681661cb7677e729f94b35807355f5ea5ae0af71eb08d` |
| `libmachook/Metal_hooks.x` | `a49c52c14319b7991977c2ff1a5e39c353ef2f3a6824e82bec9f8b7335291e4a` |
| `macwstccdprobe/main.c` | `54fe253c2810704eb6286124e9878f2bf5cf63d51844e35aad9ce218ba8d03cd` |
| `analysis/TCC_22F82` (client framework) | `09b841378285a120a04b1c8718dd5c7e3b98b4e59849eea0d8767f845d02f2de` |

## Conclusion (FACT, source scope)

No inspected source mechanism constructs or sends a TCC reply dictionary,
substitutes the server event handler, drops XPC dispatch, or suppresses the
real handler:

- libmachook has **0** `xpc_dictionary_create_reply` calls and **no**
  interpose/hook of `xpc_connection_set_event_handler`; its 10 handler calls
  are ordinary installs on libmachook-owned connections.
- `xpc_connection_send_message_with_reply_sync_new`
  (`mac_hooks.m:16946-16977`) calls the original first and returns its reply;
  tracing is `MACWS_LSXPC_TRACE`-only.
- `macws_xpc_main` (`mac_hooks.m:16847-16891`) adapts six named roles, **not
  tccd**, and otherwise calls the raw `xpc_main(handler)`.
- libxpc creation hooks (`Metal_hooks.x:24722-24841`) change service names and
  clear the privileged lookup bit, then call the originals. They do not touch
  messages, replies or handlers.
- `mach_msg`/`mach_msg2_internal`/`dispatch_mig_server` wrappers call the
  originals; the only non-passthrough cases are narrow CoreServices MIG
  10054 / Crashpad / locationd diagnostics.
- The 12 empty-dictionary constructors in libmachook are private MacWS
  protocol requests (Steam, keychain, hostd, DNS, Metal, AppInput, AGX), not
  TCC replies.

The only TCC-specific adaptation is the `com.apple.tccd.system` →
`com.apple.macosbooter.tccd.system` name map plus privileged-bit clearing
(`mac_hooks.m:15713-15732`, `Metal_hooks.x:24477-24488`), which is required
for transport and cannot change the reply schema after connection.

## Observed data (cited, not re-run)

R008 probe-context capture: 7-key AccessRequest, real `OS_xpc_dictionary`
reply with `count=0`, no `result`/`preflight_unknown`/`restricted`/
`do_not_cache`, SPI result 1. The request is built by the real
`TCCAccessPreflightWithAuditToken` SPI (`macwstccdprobe/main.c:134-158`), not
by the probe. The caller is the probe, not WindowServer; WS-context tracing
is blocked by the production preflight.

## THEORY candidates (ranked)

1. **tccd server-side early empty reply.** Verify with a tccd breakpoint on
   reply creation/send correlated to the probe's `TCCD_MSG_ID`; record keys at
   send and the handler early-return. Refute if keys exist at send but the
   client sees empty.
2. **Client authorization/scope difference.** Probe lacks WindowServer's TCC
   client identity/entitlements. Verify with a bounded WS-context capture or a
   disposable signed probe carrying the same project profile; refute if both
   are empty.
3. **Audit-token adaptation.** `audit_token_to_auid` forces 501 and
   `audit_token_to_asid` synthesizes a shared ASID, unconditionally
   interposed (`mac_hooks.m:13565-13645`, 15293-15296). Verify with
   `MACWS_AUDIT_TRACE=1` in the tccd job during one preflight and correlate
   inbound-token calls. If proven, narrow the interposes by role for tccd's
   inbound authorization; do not fabricate replies.
4. **Request options/purpose difference.** Probe passes NULL options; real WS
   may include purpose/target identity. Verify by capturing the WS request or
   using `TCCAccessRequestIndirectWithOptions`.

## Share-package comparison

Reference source `.../sequoia-hook-source/libmachook/mac_hooks.m:5689` still
installs the constant-true `_audit_token_check_tcc_access` hook; the packaged
binary retained it in the prior audit. No tccd name mapping or XPC
reply/handler adaptation was found in that reference source. It bypasses the
checker rather than fixing the reply; it is not a reusable upstream
correction and must not be copied.

## Next discriminating experiment

Under backend device ownership: correlate one probe preflight in the running
tccd by `TCCD_MSG_ID`, breakpoint reply creation/send and record the
reply-key set plus handler early return, while `MACWS_AUDIT_TRACE=1` records
inbound audit-token calls. This distinguishes candidates 1 and 3 and gives
context for 2.

## Limits

- No device/WindowServer/tccd run; R008 facts are cited, not re-verified.
- No local tccd server binary was found; only the client framework is local.
- WS-context reply capture remains unavailable.
- Static audit proves absence in source, not runtime non-execution; causal
  statements after the source boundary are labeled THEORY.

## Read-only command exits

`sha256sum`, `file/nm/strings`, targeted `grep | wc -l` and
`git status --short | wc -l` all exited 0. Counts: reply/handler hooks 0,
direct handler installs 10, empty-dict request constructors 12. No build,
test, package or device command was run; no Git commit/reset/stash/push.

Full detail: `docs/evidence/r004-xpc-empty-reply-source-audit-20261018.md`.

(handover to codex now)
