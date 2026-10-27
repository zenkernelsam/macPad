# devin-061-frontend — responsibility inventory + order audit RESULT

**Doc:** `docs/evidence/devin-d061-frontend-20261019.md`
Local read-only on `tccd_arm64e` `039f64b7`/9845D17F + xnu 8792.81.2.

## Constructor order — verified (path-first, responsibility-LAST)

IMP chain decoded selref-exact: `initWithMessage:`(0x4ffa8, w3=1) →
`evaluateResponsibility:`(0x4ffb0, x4=0) → body `0x5001c` order:
`getAuditToken:`(xpc kernel token) → `createProcessFromAuditToken:`
(**PATH first**, #2 bail at `0x500fc`) → 2nd create → `cbz w22` gate →
`_evaluateResponsibleProcess`(**#103 last**, real SPI @0x4fb48).
D052 `#2` ⇒ died at PATH (responsibility never evaluated — stage-1
"pass" unproven); R002 `#103` ⇒ probe's path PASSED. Builder class =
**TCCDAttributionChain** (corrects my D060 label).

## Blind-spot audit — two backend "zero-callsite" claims disproven

1. `setMacos_isSystemServer:` HAS stub `0x59fe0` + caller `0x6108` —
   inside server bring-up: `argc≥2 && strcmp(argv[1],"system")==0` →
   global flag `0x8f888` → setter at init. **Per-instance, launch-arg
   driven** — `tccd system` ⇒ every connection attributes.
2. `evaluateResponsibility:NO` is LIVE — helper `0x3a8e4` called from
   `0x3bbcc`(w2=1) and **`0x1238c` inside do_TCCAccessRequestIndirect
   with `w2 = !service.macos_shouldAllowSameTeam`** — same-team
   services legitimately skip the responsibility getter.
Residual blind spots: no KVC/performSelector/NSInvocation stubs in
image; block-invoke `blraa`×8 theoretical only.

## API-vs-0xb6 relationship — the procinfo test can't split the question

- `p_responsible_pid`: KPI-written (`proc_set_responsible_pid`,
  Private.exports — in-kernel caller absent, writer is a MAC/kext
  client), fork-inherited, **no userspace read flavor exists**.
- SPI = `__mac_syscall("Quarantine",0xb6)` MAC-policy dispatch (R009
  exact-image FACT) — a different store than launchd's procinfo
  bookkeeping. Procinfo present/absent proves launchd's table only;
  `#103` remains 3-way ambiguous (precheck / no-policy / handler).
- **Provider-absent hypothesis stands**: native iOS tccd has zero
  responsibility imports; if iPadOS kernel lacks the Quarantine
  `mpo_policy_syscall`, EVERY sender #103s — registration can't fix.

## Next discriminator (exact)

SPI self/native-job audit-token query from owned native helper
(weak_import, no TCC): #103-for-all → provider absent; native-ok +
chroot-103 → registration-level. Missing inputs: device read
authorization, installed launchdchrootexec identity, kernel Quarantine-
policy existence evidence.

(handover to codex now)
