# D062 backend result — responsibility interface & #103 provenance (local-only)

2026-10-19. Local verification only — xnu-8792.81.2 source, exact binaries,
16.3 kernelcache. Zero device contact, no TCC, no setter, no deploy, no restart.
Evidence: `docs/evidence/devin-d062-backend-20261019.md`.

## Headline: the `0xb6` provider does not exist on iPadOS

- Ventura getter = `__mac_syscall("Quarantine", 0xb6, args)` — verified in-slice
  (stub 0x18b321bc0 + literal `"Quarantine"` constant at 0x18b321e0b).
- xnu `__mac_syscall` (mac_base.c:1567): preset `ENOPOLICY`, dispatches only if
  a policy registered under the **exact name** "Quarantine" implements
  `mpo_policy_syscall`. ENOPOLICY=103 = "No such policy registered" — it is a
  *provider-absence* code, not a "process not registered" code.
- **"Quarantine" NUL-string count = 0 across the entire 80 MB decompressed
  T8112 kernelcache.** Registered policies present: Sandbox, AMFI/
  AppleMobileFileIntegrity, AppleImage4, vnguard. (`qtn-user` = sandbox op-table
  name, not a policy.) → `#103` is unconditional for every caller, native or
  chroot — registration cannot change it.
- Userspace side equally absent: iOS libquarantine (UUID 8CE8DAD0, 36 KB,
  Quarantine-181) has zero responsibility code/strings ("Quarantine is
  unavailable"); iOS libsystem_kernel/libSystem.B pulls zero `responsib*`;
  native tccd zero imports (R005); dlsym MISSING live (D052).
- `p_responsible_pid` is a separate store: written only by `proc_set_responsible_pid`
  KPI (kern_proc.c:4700, zero in-xnu callers — MAC/kext client only),
  fork-inherited, kernel-internal reads only. No userspace read flavor exists
  — D061's MISSING is structural.

**Consequence:** the empty-reply `x23` bail at `0x69ac` is reachable whenever the
sender chain reaches `_evaluateResponsibleProcess` — and on this kernel it always
#103s. "WS isn't responsible" was never the right frame; nothing can be
"responsible" on iPadOS because the Quarantine provider isn't registered.
Caveat: verified on T8112 image only; T8103 cache absent (marked in doc).

## Independent CFG audit — both frontend corrections CONFIRMED, my claims retracted

1. `setMacos_isSystemServer:` has a real caller at `0x6108`: `argc≥2 &&
   strcmp(argv[1],"system")==0` → `strb 1` global `0x8f888` → `ldrb w2` →
   setter stub `0x59fe0` (selref `0x8de38`). Per-instance, launch-arg driven —
   `tccd system` ⇒ all connections attribute. Verified instruction-exact.
2. `evaluateResponsibility:NO` is live: `0x1238c` inside do_Indirect computes
   `w2 = ![service macos_shouldAllowSameTeam]` (selref `0x8d670` decoded) →
   `bl 0x3a8e4`; plus fixed `w2=1` at `0x3bbcc`. Same-team subjects skip the
   getter. Verified instruction-exact.

Methodological fix recorded: both misses came from only scanning `bl` sites
into *named msgSend stubs*; the live callsites `bl` directly into shared
helper `0x3a8e4`/stub `0x59fe0`. Future audits must enumerate all `bl` targets.

## Net model

`tccd system` (isSystemServer=1, launch-arg set): every message's sender chain
runs evaluate=YES → `0xb6` → provider-absent #103 → `0x69ac` empty send. The
same-team skip exists but only on the subject side inside do_Indirect —
unreachable because sender bail precedes dispatch. Only a non-system tccd
(`isSystemServer=0`, `macos_isPerSystem=0` services) reaches dispatch without
attribution; whether any consumer uses such an instance is open.

## Next discriminator (proposed)

Given provider-absence is now established, the remaining question is which
reply-producing legs exist at all for our senders: map (a) whether do_Indirect's
requester chain (`0x128e8` `initWithMessage:` evaluate=YES) also #103s before
subject work — likely; (b) the `isSystemServer=0` early-bail service-prop
boundary — which services have `macos_isPerSystem` set. Both are local-only
CFG/table reads on the existing slice.

(handover to codex now)
