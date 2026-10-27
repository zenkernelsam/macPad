# D062 — Local verification: responsibility interfaces, #103 provenance, independent CFG audit

Date: 2026-10-19. Backend. **Local-only**: xnu source + extracted binaries + kernelcache.
No device contact, no TCC requests, no setter, no deployment, no restart.

Inputs: `tccd_arm64e` `039f64b7` (UUID 9845D17F), Ventura libquarantine slice
`f5989c8f` (UUID A2D17FF6), iOS pulls (libquarantine UUID `8CE8DAD0`,
libsystem_kernel, libSystem.B), `xnu-xnu-8792.81.2`, `kc_raw_16.3_T8112.bin`.

## 1. Interface verification — where the query lives on each platform

| Fact | Evidence | Class |
|---|---|---|
| Ventura tccd imports `_responsibility_get_responsible_audit_token_for_audit_token` | R005 metadata (exact-image, UUID-verified) | FACT |
| Ventura libquarantine getter = `__mac_syscall("Quarantine", 0xb6, &args)` | in-slice disasm: stub `0x18b321bc0` (adrp+ldr+braa import thunk), policy-name constant `0x18b321e0b` = literal `"Quarantine"`, `w1=0xb6` | FACT |
| iOS libquarantine (Quarantine-181.120.2, UUID 8CE8DAD0, 36 KB) contains **zero** `responsib*`/`mac_syscall` strings; contains "Quarantine is unavailable", "Failed to load a compatible version of libMatch" | `strings` count=0 on device pull | FACT |
| iOS libsystem_kernel + libSystem.B pulls: zero `responsib*` strings | same | FACT (subject to slice completeness caveat, §4) |
| iOS native tccd (UUID 0120CE08): zero responsibility imports | R005 exact-image check | FACT |
| D052 device dlsym of the getter → SYMBOL_MISSING | runtime-verified | FACT |

## 2. #103 error provenance — exact kernel path (xnu-8792.81.2)

`bsd/sys/errno.h:256`: `ENOPOLICY = 103` ("No such policy registered").
`security/mac_base.c:1567` `__mac_syscall`:

1. `copyinstr` policy name → copyin errors only
2. `mac_proc_check_mac_syscall` — MAC precheck (`mpo_proc_check_syscall_mac`, enforce-gated) — failure returns that error, not 103
3. `error = ENOPOLICY` preset (line 1587)
4. Walk `mac_policy_list`: `strcmp(mpc->mpc_name,"Quarantine")==0 && mpc_ops->mpo_policy_syscall != NULL` → invoke handler, return its code
5. **No matching named policy with a syscall hook → returns ENOPOLICY(103)**

So `#103` means precisely: **no MAC policy registered under the exact name
"Quarantine" that implements `mpo_policy_syscall`**. It is *not* a generic
"process not registered" code and says nothing about launchd bookkeeping.

`p_responsible_pid` is a **different store** (frontend's distinction confirmed
in source): written only via `proc_set_responsible_pid` KPI
(`bsd/kern/kern_proc.c:4700` — **zero in-xnu callers**; client is a MAC/kext
component), fork-inherited (`kern_fork.c:1096`), consumed in-kernel only
(necp/uipc_socket/kcdata). `proc_get_responsible_pid` does not exist in this
xnu; no userspace read flavor exists — D061's MISSING is structural, not
a tooling gap that a different CLI would fix.

## 3. Provider absence — kernelcache evidence

`kc_raw_16.3_T8112.bin` (iPadOS 16.3, decompressed 80 MB):

- `strings`: NUL-terminated `"Quarantine"` count = **0** (policy names are
  compile-time C strings in the registrant's rodata — a registered
  "Quarantine" policy is impossible without the string)
- Registered-name strings present: `Sandbox`, `AppleMobileFileIntegrity`/`AMFI`,
  `AppleImage4`, `vnguard` (in-xnu, `kern_guarded.c:1513`)
- `qtn-user` (0x833f37) sits inside the **sandbox op-name table** (adjacent to
  `fs-quota-*`, `iokit-*`, `ipc-posix-*`) — a sandbox operation name, not a MAC
  policy name

→ `__mac_syscall("Quarantine",·)` returns ENOPOLICY for **every** caller on
this kernel — native, chroot, root — independent of registration, identity,
or entitlements. **No setter or registration can produce non-#103.** This
also retroactively explains R005's FACT (native tccd has zero imports): Apple
knows the API is dead on iOS.

Caveat (THEORY→FACT-pending): verified on the T8112 (iPad14,5/M2) image —
the only kernelcache in repo. The T8103 (iPad13,6/M1, actual device) image is
absent; MAC-policy set is a shared iOS build config so cross-SoC divergence is
unlikely, but the exact-image confirmation for T8103 is a missing input.
The macOS Quarantine policy lives in a kernel component not loadable on iPadOS.

## 4. Independent CFG audit — both D061-frontend corrections CONFIRMED

**Correction 1 — `setMacos_isSystemServer:` has a live caller.** Verified:

```
0x6064 cmp w0,#2 / b.lt 0x6088        ; argc>=2
0x606c ldr x0,[x1,#8]                 ; argv[1]
0x6070 adr x1,#0x5deff → "system"     ; strcmp stub
0x607c cbnz → skip
0x6080 mov w8,#1 / strb w8,[x25,#0x888]  ; global 0x8f888=1
…
0x6100 ldrb w2,[x25,#0x888]
0x6104 ldr x0,[x24,#0xb30]            ; server object (same +0xb30 slot as 0x6828 getter)
0x6108 bl 0x59fe0                     ; stub → selref 0x8de38 = setMacos_isSystemServer:
```
`0x59fe0` decoded: `adrp x1,#0x8d000; ldr x1,[x1,#0xe38]` → selref 0x8de38.
Single writer `0x6084`, three readers (`0x6100`, `0x61ac`, `0x6260`), all in the
bring-up function. **Per-instance, launch-arg driven — `tccd system` ⇒ every
connection takes the attributed leg.** D060's "zero callsites / provenance
unknown" is RETRACTED.

**Correction 2 — `evaluateResponsibility:NO` is live.** Verified:

```
0x12368 bl 0x585e0   ; selref 0x8d670 = macos_shouldAllowSameTeam (decoded)
0x12370 eor w2,w0,#1 ; w2 = !shouldAllowSameTeam
0x1238c bl 0x3a8e4   ; shared chain-helper (x0=obj, x1=0, w2=eval-flag, x3/x4=out-ptrs)
```
plus fixed `w2=1` callsite at `0x3bbcc`. Same-team services legitimately skip
the responsibility getter on the **subject** side inside do_Indirect. D060's
"zero callsites / dead variant" is RETRACTED.

**Methodological root cause of both misses** (recorded for future audits): the
D060 caller scans matched only `bl` sites into named msgSend *stubs*. Both live
paths call the shared helper `0x3a8e4` / the setter stub `0x59fe0` via plain
`bl` that I never target-scanned. Correct procedure: enumerate all `bl`
targets in `__text` first, then attribute.

## 5. Net consequence for the empty-reply problem

- Sender chain (`initWithMessage:` at `0x6848`) hardwires evaluate=YES → the
  `0xb6` query runs → provider absent → `#103` for **every** sender identity,
  forever, on this kernel. Registration, launchd metadata, and identity are
  irrelevant to this outcome — the API endpoint does not exist.
- The `shouldAllowSameTeam`/`evaluateResponsibility=NO` legs only skip the
  getter on the *subject* side inside do_Indirect — the sender bail at `0x69ac`
  precedes dispatch, so Indirect cannot reach them for a `#103`-class sender.
- A non-system tccd (`isSystemServer=0`) is the only leg reaching dispatch
  without attribution (`0x6834`) — and only for non-`macos_isPerSystem`
  services (flag=0+prop≠0 → distinct early bail). Whether any consumer uses
  such an instance is an open question, not asserted here.

## 6. Missing inputs

- T8103 (iPad13,6) kernelcache to close the SoC caveat
- Export-trie-level confirmation from the full iOS 16.3 cache `.symbols`/
  complete subcache set (local pulls have datasize=0 tries; dlsym MISSING is
  the live fallback proof)
- Which real TCC ops the desktop's consumers actually send (determines whether
  any non-attributed leg is reachable in practice)

(handover to codex now)
