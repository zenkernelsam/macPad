# devin-057 backend result — launch-premise correction + Indirect audit

2026-10-19 · local-only · packet `coordination/_relay/devin-057-backend-task.md`
Evidence: `docs/evidence/devin-d057-launch-indirect-audit-20261019.md`
No device contact, no TCC traffic, no trust/identity writes, no
source/build/Git changes. Binary audited: `analysis/d050-tccd/tccd_22F82`
arm64e, UUID `9845D17F-0409-3884-8A17-260C729636E5` (live fingerprint).

## Retractions applied (D056 docs updated in place, history preserved)

- `POSIX_SPAWN_SETEXEC` is ACTIVE — `main.m:119`/`setflags:133`; the :114
  call is a dead comment. Disasm-verified in all 3 preserved binaries:
  `mov w1,#0x40` unconditional at the setflags site (`0xc0` only under
  `MACWS_SUSPEND_AT_EXEC`). `posix_spawn` execs in place — the macOS
  process IS the launchd job's tracked pid; `return 1` is the failure
  path. No orphan child exists.
- Retracted: "orphaned stray", "spawn-die explains #103", "SETEXEC is the
  missing fix" (already present), "architecturally unreachable"
  (over-broad; single-mechanism universal claim per share-review
  warning). Stage-1's #103 cause is RE-OPENED: a tracked pid under
  SETEXEC has the same launchd relationship D052's native helper had, so
  the stage-1 discriminator is unexplained — launchd responsibility
  registration semantics remain the open question (kernel internals
  kept source-informed, not live-RE'd).
- Device-installed binary identity: no preserved pull hash found → left
  explicitly unknown, no inferred orphan.
- Unretracted: stage-2 caller-namespace finding (D055) — proven by a
  namespace-only variable, independent of launch mechanics.

## Indirect admission audit (exact offsets, verified CFG)

- **Order**: `create_reply(x23)` 0x6508 → `[obj+0xb30
  macos_isSystemServer]` gate 0x6828 (IMP 0x10002d0e4 = flag byte +0x29
  bit0; 0 → skip attribution → straight to dispatch 0x6ba0) →
  `initWithMessage:` builder 0x6848 → nil → 0x69ac failure tail →
  unconditional empty x23 at 0x69cc → `"function"` strcmp dispatch at
  0x6ba0.
- **Attribution precedes dispatch** for system-server connections — the
  Indirect handler cannot rescue a sender-level bail (coordinator's
  warning confirmed structurally).
- Sender identity is genuine: builder input is the XPC message — the
  peer audit token is kernel-provided, not dict-forgeable.
- `TCCAccessRequestIndirect` → `0x6fc0` → `do_TCCAccessRequestIndirect`
  @`0x100011aa4` (self-named via os_log `"do_TCCAccessRequestIndirect"`,
  `0x5ee56`). Consumes `target_csreq`, `target_identifier`,
  `target_path` — **subject** identity from message fields; sender
  attribution already done.
- Selector family proves a responsibility-skipping chain exists:
  `initWithMessage:evaluateResponsibility:` /
  `:evaluateResponsibility:processInfo:` — call sites unmapped.
- `TCCAccessSetPidResponsibleForPid` → `0x7b30` exists in the operation
  table (re-checks isSystemServer) — the protocol natively contemplates
  legitimate responsibility wiring; admission unknown.
- `proc_pidpath_audittoken` IMP site `0x4b9d8` ⊂ fn `0x4b8a0`, reached
  via ObjC dispatch (no direct bl callers).

## Explicit gaps (not guessed)

Entitlement gates inside `do_Indirect` (string presence ≠ use); whether
the subject's own chain is re-derived (decides broker viability);
`0x8e420` class identity + which init variant each site uses; flag
setter/per-instance-vs-per-connection semantics of `+0x29` bit0.

## Smallest discriminating next experiments (all local)

1. Enumerate `initWithMessage*` call sites + class → which token source
   each variant attributes.
2. Inside `do_Indirect`: does the subject hit the `0x4b8a0`
   identity-ctor (chain re-derived → chroot subjects still fail) or is
   it evaluated from message identity fields (broker viable)?
3. Locate `+0x29` setter → per-instance vs per-connection.

No device experiment proposed this round; (2) is the single branch that
decides whether any broker design is alive.

(handover to codex now)
