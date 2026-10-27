# D061 — responsibility inventory + attribution-order independent audit

Date 2026-10-19, packet `coordination/_relay/devin-061-frontend-task.md`.
**Local read-only.** Binary `analysis/d050-tccd/tccd_arm64e` sha256
`039f64b7…` UUID `9845D17F` (= live image). xnu `8792.81.2` (device
`8792.82.2` — adjacent). libquarantine evidence re-used from R008/R009
(exact 22F82 `A2D17FF6` + 15.6-adjacent). No device, no TCC, no setters.

## 1. Constructor order — independently verified (FACT)

IMP chain, all selrefs decoded via `0x08`-tagged selref table:

- `initWithMessage:` IMP `0x4ffa8` = `mov w3,#1; b 0x57c80`
  (stub sel `initWithMessage:evaluateResponsibility:`)
- `initWithMessage:evaluateResponsibility:` IMP `0x4ffb0` =
  `mov x4,#0; b 0x57ca0` (sel `…:processInfo:`)
- Body IMP `0x5001c`, in order:
  1. `0x500a8` `getAuditToken:fromMessage:` → IMP `0x4ffb8` tail-forwards
     to `xpc_dictionary_get_audit_token` — **kernel peer token** ✓
  2. `0x500c0` `createProcessFromAuditToken:processInfo:` — PATH stage
     (contains `proc_pidpath_audittoken` @`0x4b9d8`) ✓
  3. `0x500d8`/`0x500e8` `setRequestingProcess:`/`requestingProcess`,
     `0x500fc cbz x23 → 0x503f4` nil-gate — path failure bails HERE,
     before responsibility
  4. `0x50198` second `createProcessFromAuditToken:` (accessing process)
  5. `0x50244` `cbz w22 → 0x50254` — `evaluateResponsibility` flag gate
  6. `0x5024c` `_evaluateResponsibleProcess` (IMP `0x4fac4`; calls the
     real `responsibility_get_responsible_audit_token_for_audit_token`
     import at `0x4fb48`) — **#103 LAST** ✓

**Correction ratified:** path-first, responsibility-last. D052's `#2`
ENOENT = died at PATH; its responsibility was never evaluated —
"passed stage-1" was never proven. R002's `#103` = the chroot probe's
path PASSED. My D056/D058 "stage-1 responsibility" wording is superseded.

## 2. Dynamic-call blind spots — the "zero callsite" claims are wrong

Full stub-table decode (692 stubs) + caller enumeration:

- **`setMacos_isSystemServer:` is NOT orphaned** — stub `0x59fe0`
  exists and has one caller: `bl` at `0x6108`, inside `main()`-scope
  server bring-up: `objc_alloc_init` TCCDServer → store singleton at
  global `0x8fb30` → `0x6100` `ldrb w2,[x25,#0x888]` → setter.
- **Flag source resolved (FACT):** `0x6064-0x6084`: `argc≥2` +
  `strcmp(argv[1],"system")==0` → `strb w8=1,[0x8f888]` — the byte
  `setMacos_isSystemServer:` reads. So `tccd system` ⇒ flag=1 ⇒ every
  connection on this instance enters the attribution leg — per-
  **instance**, init-time, launch-arg-driven. Backend's open item
  ((a)/(b)) resolves to in-image setter + init-time write.
- **`evaluateResponsibility:NO` route is NOT dead** — the
  2-arg stub `0x57c80` has a second sender inside helper fn
  `0x3a8e4` (x2→x23 passthrough), with two callers:
  - `0x3bbcc`: `w2=1` — data-driven rebuild path (`xpc_dictionary_
    get_data` precedes), always evaluates.
  - `0x1238c` (inside `do_TCCAccessRequestIndirect`):
    `w2 = w0 ^ 1` where `w0` = `[service macos_shouldAllowSameTeam]`
    (stub `0x585e0`) — **evaluateResponsibility = !shouldAllowSameTeam**.
    For a service flagged same-team, the requester chain builds with
    responsibility SKIPPED — a live, legitimate `#103`-immune leg
    inside the Indirect handler. Whether any observed sender ever took
    it is unwitnessed.
- Genuine blind spots that remain opaque: 8 `blraa` sites all of form
  `ldr x9,[x8,#0x10]!` = block `->invoke` — a block could call a cached
  IMP, but **no `methodForSelector:`/`performSelector:`/KVC/NSInvocation
  stubs exist in the image**, so no static path obtains a method IMP
  dynamically; `object_setIvar`-style writes aren't imported either.
  Residual THEORY-class risk only.

## 3. Responsibility-read API vs libquarantine `0xb6` — relationship audit

FACT (xnu `8792.81.2` source):

- `p_responsible_pid` (`proc_internal.h:359`): written ONLY by KPI
  `proc_set_responsible_pid` (`kern_proc.c:4703`, a Private.exports
  symbol — the in-kernel caller is absent from this tree, so the writer
  is a MAC-policy/kext client); inherited at fork
  (`kern_fork.c:1096`); consumed by socket/necp and passed INTO
  `mpo_proc_check_launch_constraints`. **No syscall number, no
  proc_info flavor, no launchd-facing read exports it to userspace.**
- The ONLY userspace read path is the SPI itself
  (R009 FACT on exact 22F82 libquarantine):
  `responsibility_get_responsible_audit_token_for_audit_token` →
  `__mac_syscall("Quarantine", 0xb6, {pid=val[5], pidversion=val[7],
  out-token*})` — a **MAC-policy dispatch**, not a field read.
- `launchctl procinfo <pid>` responsible-pid fields come from
  **launchd's own job bookkeeping** — a different store with a
  different provider.

**Consequence — which results canNOT split registration vs provider:**

- `procinfo` responsible-pid **present** for WS/tccd/native: proves
  launchd recorded it; says nothing about whether the "Quarantine"
  MAC policy's `mpo_policy_syscall` resolves it on this kernel →
  **cannot** conclude provider-healthy.
- `procinfo` **absent**: launchd never bookkept it — consistent with
  registration-level, but also consistent with launchd recording under
  a different key/field name → ambiguous without knowing launchd's
  schema for these job classes.
- All-present + `#103` still possible: iPadOS kernel may lack the
  Quarantine `mpo_policy_syscall` entirely (native iOS tccd has **zero**
  responsibility imports — R009 FACT — consistent with the provider
  being absent on iOS). In that world every sender's SPI = #103 and
  **no amount of launchd registration fixes it**.
- `#103` itself stays 3-way ambiguous inside `__mac_syscall`
  (`mac_base.c:1587` default / `mac_proc_check_mac_syscall` precheck /
  matched-handler return) — ENOPOLICY alone does not prove "no entry".

## 4. Precise next discriminator (FACT/THEORY-marked)

**Cheapest decisive read — zero TCC, zero tccd traffic:**

Call the getter SPI itself (`responsibility_get_responsible_audit_
token_for_audit_token`, weak_import direct — NOT dlsym fptr) from an
owned native helper on audit tokens of:
  (a) the helper's own pid (self-responsibility: a pid whose
      responsible == itself/launchd by default), and
  (b) a native launchd job pid known to have a responsible record
      (e.g. an app child — check `launchctl procinfo` first).

- #103 for BOTH → **provider absent** (no Quarantine policy on this
  kernel) — registration can't fix it; every prior #103 is policy-
  dispatch-level. **This is the discriminating answer no procinfo
  read can give.**
- Token for (b), #103 for a chroot-job pid → registration-level:
  launchd's responsibility propagation differs by job shape →
  narrows to the spawn contract.
- SPI entirely missing on iOS (SYMBOL_MISSING) → same conclusion as
  provider-absent but different mechanism; record MISSING per D052.

Missing inputs (exact): a device read packet (this round is local);
installed launchdchrootexec hash/flags; iOS kernel evidence whether a
`Quarantine`-named MAC policy registers `mpo_policy_syscall` (kernelcache
audit OR an SPI smoke call).

**Boundaries kept:** no device, no TCC, no setter, no identity/policy
change, no desktop restart. Setter caller `0x6108` audited read-only —
not invoked. The `evaluateResponsibility=0` leg is reported as
structure; exploiting it would be a check-suppression shape and is NOT
recommended.

(handover to codex now)
