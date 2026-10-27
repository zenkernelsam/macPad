# devin-060 backend result — exact responsibility branch audit

2026-10-19 · local-only · packet `coordination/_relay/devin-060-backend-task.md`
Evidence: `docs/evidence/devin-d060-responsibility-branch-audit-20261019.md`
Binary: `tccd_22F82` arm64e UUID `9845D17F-0409-3884-8A17-260C729636E5`
(live fingerprint). No device, no TCC, no setters, no source/build/Git
mutation.

## Decisive correction: stage order inverted

`initWithMessage:` (IMP `0x4ffa8`, w3=1) → `initWithMessage:
evaluateResponsibility:` (`0x4ffb0`, x4=0) → `…processInfo:` (`0x5001c`)
call order inside:

1. `getAuditToken:fromMessage:` `0x500a8` — kernel peer token, not
   dict-forgeable
2. `createProcessFromAuditToken:processInfo:` `0x500c0` — **PATH lookup
   FIRST** (`proc_pidpath_audittoken` @`0x4b9d8` under fn `0x4b8a0`)
3. `_evaluateResponsibleProcess` `0x5024c` — **#103 LAST**, gated on
   `evaluateResponsibility` arg (`cbz w22` `0x50244`)

→ D052's `#2 ENOENT` means the iOS sender died at PATH — responsibility
was never evaluated for it; "stage-1 pass" was never proven (frontend's
D058/D059 suspicion confirmed). R002's `#103` means the chroot sender's
path PASSED — consistent with D059's in-root relative-spelling proof.
Chroot senders' sole observed blocker = responsibility. All prior
"stage-1/stage-2" ordering references are corrected in the doc.

## Class + variants + legs (all exact-offset)

- Builder class `@0x8e420` = **TCCDAttributionChain** (ro.name
  `0x78c03`, method list `0x5d6d8`, 22 methods incl.
  `getAuditToken:fromMessage:` `0x4ffb8`,
  `createProcessFromAuditToken:processInfo:` `0x4ffc4`,
  `_evaluateResponsibleProcess` `0x4fac4`).
- `evaluateResponsibility:`-NO route has **zero `bl` callsites** — dead
  surface; the live entry always sets the flag.
- Leg structure: `serviceFromMessage:` → `0x6708` `tbnz` on
  `[TCCDServer macos_isSystemServer]` (IMP `0x10002d0e4` = `+0x29` bit0):
  flag=1 → attribution; flag=0 + service-prop `w28`=0 → direct dispatch;
  flag=0 + w28≠0 → early-bail send (`0x6710` leg). Service-fail leg
  `0x6824` re-checks the same flag.
- **Flag-write provenance unresolved** (stated, not guessed): zero
  `strb …,#0x29` outside the setter IMP and zero `bl` callsites of
  `setMacos_isSystemServer:` in this image — set either outside this
  image or via a path my scan missed. Per-instance vs per-connection
  open.

## Indirect admission — established

`do_TCCAccessRequestIndirect` (`0x11aa4`) rebuilds the **requester**
chain from the same message (`0x128e8`, same sender gates); subject
identity comes from dict fields (`target_*`/`indirect_object_*`,
`initWithIdentifier:type:executableURL:…` `0x12948` or the type==3
`initWithAttributionChain:` branch) — **subject never re-attributed
through a kernel token**. Sender-side entitlement gate found:
`com.apple.private.tcc.override-prompt-policy` via
`boolValueForEntitlement:` on `requestingProcess` (`0x13494`,
cfstring @`0x82b60`). Policy eval =
`evaluateOverridePolicyForAccessByIdentity:…` (`0x12984`).

## Discriminator delivered

#103 can only be emitted by the getter inside the builder — so every
observed #103 already proves "attribution leg entered + genuine
ENOPOLICY", not leg-selection confusion. The remaining open split is
**registration-level vs provider-level**. Smallest read-only
observation: `launchctl procinfo`/`proc_pidinfo`-family responsible-pid
read on WS `10447`, tccd `10300`, and an existing native user/501 job —
zero TCC, zero tccd traffic; absent-entry vs present-but-unresolvable
splits registration from provider semantics. Execution deferred to a
packet authorizing device reads.

Bounds kept: installed launchdchrootexec identity still unknown; no
broker implemented; no `TCCAccessSetPidResponsibleForPid` call; no
universal claims — all findings are for this binary/build.

(handover to codex now)
