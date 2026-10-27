# D096 — offline exact-RE audit: responsibility/TCC upstream split

Scope: registration absence vs provider/query-resolution, post-D060.
Artifacts inspected: `analysis/SkyLight_22F82` (UUID
96676A53-B1E0-3D7E-B98B-B73873CD1880, exact image) and
`analysis/TCC_22F82` (UUID 1238D7CA-719A-36A4-94B1-2F387E6B3BF1, exact
image). No device, no execution, no edits.

## FACT — SkyLight 22F82 has no responsibility API surface

- `nm` import table: **zero** `responsibility_get_*` /
  `responsibility_set_*` / `responsibility_spawnattrs_*` /
  `setaudit*` symbols. Only libbsm `audit_token_to_*` scalar
  accessors (`_to_pid/_to_auid/_to_asid/…`) are imported — accessor
  reads, not registration.
- The only responsibility-named surface is the objc selector pair
  `responsibleProcess` / `setResponsibleProcess:` (`__objc_methname`
  `0x185504604`/`0x185505758`; stub `objc_msgSend$setResponsibleProcess:`
  at `0x18550f600`, four callsites `0x185136128`, `0x1854036b4`,
  `0x185405130`, `0x1854095ec`). Receivers are register-carried
  (x0←x27/x1); no implementation exists inside SkyLight — it is a
  session-bookkeeping property (SLSessionOwner/WSSession family),
  not the kernel responsibility contract. **WS never performs a
  responsibility lookup or registration.**

## FACT — the userspace registration leg is client→tccd XPC

- `TCC_22F82` exports `TCCAccessSetPidResponsibleForPid` at
  `0x1850d0574`: resolves the `_tccd` connection helper
  (`bl 0x1850c9584` at `0x1850d062c`), builds the message with
  `__TCCAccessSetPidResponsibleForPid_block_invoke` (`0x1850d0718`)
  and sends via the tccd channel. That is the upstream "provider"
  call — a client asks tccd to mark a pid responsible; tccd applies
  it kernel-side. Nobody calls the kernel directly.
- Callers: **none** in the project stack (R005 §3.1 — zero
  `responsibility_*`/`TCCAccessSet*` references across
  `launchdchrootexec`/`libmachook`/host/layout/misc) and **none** in
  SkyLight itself (no import). The registration leg exists in the
  exact binaries but is never exercised for our target processes.

## FACT — the kernel-side provider is the "Quarantine" MAC policy

- Per the D050–D068 chain: `responsibility_get_responsible_audit_
  token_for_audit_token` in tccd routes through
  `__mac_syscall("Quarantine", …)`; `mac_syscall` at
  `0xfffffe000865db70` strcmp-matches `mac_policy_list` entries and
  the **literal ENOPOLICY producer** is `w21=0x67` at `0x865de80` on
  no-match. The policy module itself is the provider — if it isn't
  registered there is no query-resolution step to fail separately.

## The upstream split resolves to registration absence at both levels

- **Kernel level**: #103 is produced iff no `Quarantine`-named policy
  is in `mac_policy_list` — the open question, measured only by the
  frozen v3 snapshot (D069 output inadmissible).
- **Userspace level**: even if the kernel policy existed, no caller
  ever registers a responsible pid — no `TCCAccessSetPidResponsible
  ForPid` caller in WS or the project; `launchdchrootexec`'s
  SETEXEC preserves launch ancestry but creates no responsibility
  record; R003 disproved "launchd hosting establishes it".
- Provider/query-resolution is therefore **not** a distinct open arm
  for this path: the provider is the (possibly absent) kernel policy,
  and the registration caller is absent regardless.

## Required binaries ABSENT locally (stated, not inferred)

- Ventura `libsystem_kernel.dylib` — only stubs in local extracts
  (R006 §2); the wrapper's exact implementation/ABI is unverifiable
  offline.
- Ventura `launchd` — spawn-attrs/responsibility-claim caller for
  real jobs; whether iPadOS launchd ever sets `p_responsible_pid` for
  our jobs is untestable from these artifacts.
- Ventura `tccd` 22F82 — device copy only (SHA `42c388e8…`, UUID
  `9845D17F-…`); callsite known (`0x10004fb48 bl …`) but local RE
  not possible.

## THEORY (bounded)

`p_responsible_pid` inheritance is kernel-real (fork copies it,
`kern_fork.c:1096`); the setter path exists in XNU but has no in-tree
caller — consistent with all observed #103s. Whether iPadOS launchd
populates it for launchd jobs remains unverified (only SSH-spawned and
`launchdchrootexec`-mediated jobs observed).

## Smallest next discriminator

**The v3 bounded snapshot remains the single discriminator**: presence
of `Quarantine` in `mac_policy_list` (with `ops+0x388`/`0x3a8`
populated) separates "kernel provider absent ⇒ every responsibility
query is structurally #103" from "provider present but never invoked
with a registered pid". Nothing in the two local binaries can decide
it further. Secondary (free): any future tccd RE should disasm the
`0x10004fb48` callsite's parameter setup — but the binary is
device-resident, so that waits for an authorized read.

(handover to codex now)
