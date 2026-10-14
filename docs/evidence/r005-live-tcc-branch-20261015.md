# R005: live WS branch — real TCC deny, not cached deny (2026-10-15)

Status: **the 1002 denial's actual branch is now runtime-confirmed**:
fresh-miss → `audit_token_check_tcc_access` → `TCCAccessPreflightWithAuditToken`
returns raw **w0=1** (deny at this callsite) → deny bits folded into the
per-process rights cache → gate returns 0 → reply 1002.  Input is still
NOT fixed; the grant producer remains unidentified.

## Setup

- Device: iPad13,11, iPadOS 16.3 (xnu-8792.82.2), chroot rootfs macOS 13.4.1
  (22F82).  Connection `<MACWS_DEVICE>:<MACWS_DEVICE_PORT>` via `macpad_ssh.py`
  (password auth, real exit codes in `coordination/artifacts/r005-*.json`).
- Debugger: Procursus `lldb-16` installed this session
  (`/var/jb/usr/lib/llvm-16/bin/lldb`, `debugserver` present, debugger
  entitlements verified).  tmux 3.4 installed for a task-owned interactive
  session `devin_r005` (unique name; legacy `ios_lldb_tmux.sh` NOT used —
  it kills sessions and echoes a password).
- WS generation: `macos_gui.sh start coexist --no-terminal`, WS pid 55506
  (`UIKitApplication:com.macwsguide.windowserver`, `-virtualonly -daemon`).
  No diagnostics sentinel armed (loader observer not needed).

## Identity / address validation (before any breakpoint)

- `image list`: SkyLight UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`
  (matches pinned `analysis/SkyLight_22F82`), runtime base
  `0x1974d1000` → slide `0x123ec000`.
- Instruction words verified against the file before breaking:

| file VA | runtime | expected word | observed |
|---|---|---|---|
| `0x1853fd464` gate entry | `0x1977e9464` | `0xd503237f` pacibsp | ✓ |
| `0x1853fd4e4` gate ret   | `0x1977e94e4` | `0xf85f83a8` | ✓ |
| `0x1854258b4` cache test | `0x1978118b4` | `0xf9400ec8` ldr x8,[x22,#0x18] | ✓ |
| `0x185425914` impl blraaz| `0x197811914` | `0xd63f0abf` | ✓ |
| `0x185425da0` impl entry | `0x197811da0` | `0xd503237f` pacibsp | ✓ |
| `0x185425e0c` post-call  | `0x197811e0c` | `0x7100001f` cmp w0,#0 | ✓ |

lldb resolves SkyLight symbols in-process — `WSAuditTokenHoldsCachedProcessRight`,
`audit_token_check_tcc_access`, `audit_token_has_entitlement`,
`CGXSenderCanSynthesizeEvents` all resolve at the expected file offsets.

## What ran (probe pid 0xe0af = 57519, ws_main_thread)

Probe: `/usr/local/bin/macos_ws_event_probe post` (canonical record
`v=0 len=248 type=5`).  NOTE: the probe's `post` mode performs a cursor
warp and an `SLPreflightPostEventAccess`/`SLPreflightListenEventAccess`
before the canonical record — those calls produced some of the gate
evaluations below; attribution is preserved per hit order.

Hit sequence (full pane captures: `artifacts/r005-lldb-pane{1,2}.txt`):

1. `event.monitor` right — miss (cached=0) → impl =
   `audit_token_has_entitlement` (0x197811ff0, file `0x185425ff0`) →
   deny folded (cached |= 4).
2. `kTCCServiceListenEvent` right — miss (cached=4, `4 & 0x300 == 0`) →
   `audit_token_check_tcc_access` → blraaz → **raw w0 = 1** → deny
   folded (cached |= 0x100 → 0x104).
3. `kTCCServicePostEvent` right (the canonical post) — **miss**
   (cached=0x104, `0x104 & 3 == 0`) → `audit_token_check_tcc_access`
   → blraaz → **raw w0 = 1** → impl 0 → deny folded
   (cached |= 1 → 0x105) → gate +128 **w0 = 0** →
   `SLSPostEventRecord → rc=1002`.

   LIMITS (per R005 review): raw w0=1 is proven AT THE TCC CALLSITE
   return — it is NOT yet proven that tccd received a request or
   returned a policy verdict; the value may be produced locally inside
   TCC.framework (input check, client cache, transport failure).
   The probe's `post` mode also issues preflight + warp calls during
   the same interval, so the observed evaluations are attributable to
   the probe process but not to a single isolated API call.  bp1's
   total hit count was 6429; the "141 hits" figure counts only the
   retained pane samples.
4. Second PostEvent evaluation — cached=0x105, `0x105 & 3 != 0` →
   known-hit, returns `(0x105 & 2) != 0` = 0, **no impl call** →
   gate w0 = 0.

(The other 141 bp1 hits in pane1: pid 0xd9ad = `com.macwsguide.display`,
cached=0x144 on a third descriptor — WS-internal traffic, unrelated.
bp1 was then conditioned `x20 != 0xd9ad` for the clean capture in pane2.)

## Answered / still open

ANSWERED:
- Branch taken: **live TCC evaluation**, raw return **1** → deny.
  Not a cached deny, not a failed checker resolution, not an early-bail.
- The checker pointer resolves fine and is called via `blraaz`; the
  TCC library function returned 1.  Whether that 1 was produced by a
  tccd policy reply or locally inside the framework is UNPROVEN —
  R006 scope.
- Deny bits persist in the per-process cache keyed `{pid|pidver<<32}`;
  repeat checks for the same sender skip TCC entirely.

OPEN (next bounded step — R006 candidate):
- WHY TCC returns non-zero for this sender's audit token: inspect the
  actual TCC request/response (service `kTCCServicePostEvent`,
  responsible/attributing process, session identity).  Candidates to
  verify live: sender's responsible-process resolution, missing
  `com.apple.private.SkyLight.loginwindow` effect on TCC's policy decision,
  tccd's target identity inside the chroot.  **No grant fabrication,
  no cache mutation, no entitlement patching** was performed or
  is proposed.

## Process hygiene

- lldb `process detach` clean; WS resumed (`Ss`), then stack stopped via
  `macos_gui.sh stop` (exit 0).  tmux session `devin_r005` killed; no
  lldb/tmux/WS/probe orphans.  Sentinels absent in both namespaces
  (`/tmp`, `/var/mnt/rootfs/tmp`).  Baseline `chroot echo HI` → HI.
- Packages installed this session on device: `lldb-16` (+
  `liblldb-16`, `debugserver-16`, `libclang-cpp16`), `tmux` (+
  `libutf8proc3`, `gettext-localizations`, `libevent-core-2.1-7`).
