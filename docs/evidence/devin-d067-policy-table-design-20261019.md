# D067 — MAC policy table read layout, verified against the exact image

Date: 2026-10-19. Backend. **Local-only RE on `analysis/kc_raw_16.3_T8112.bin`.**
No KRW executed, no helper deployed, no MAC syscalls, no device contact.

## 0. Corrections (per task, history preserved)

- R004's native-token 103 (native pid 51948, alive during call) does **not**
  exclude all namespace/ABI/provider factors — the getter ran in the chroot
  context for both tokens, so a chroot-side ABI/copyin quirk could produce
  both failures. It does exclude *sender-identity* causes.
- A nonsense-name control returning 103 would only calibrate the default
  arm's errno; it cannot identify which arm produced the real
  `"Quarantine"` call's 103 (precheck vs no-match vs handler). Withdrawn as
  a discriminator for the observed call.
- Enumerating a handler's presence/absence alone cannot prove which arm
  returned a given 103 — it only bounds which arms remain possible.

## 1. Image identity (FACT)

`kc_raw_16.3_T8112.bin`: Mach-O, segs `__TEXT` va `0xfffffe0007004000`,
`__PRELINK_TEXT` +`0x8000` (0x890000 B), `__DATA_CONST` va
`0xfffffe000789c000` fo `0x898000`, `__TEXT_EXEC` va `0xfffffe0007e60000`
fo `0xe5c000` (0x29b0000 B), `__PRELINK_INFO`, `__DATA` va
`0xfffffe000a9e4000` fo `0x39e0000`, `__LINKEDIT` fo `0x3bec000`
(0x106c000 B). Mislabeled filename: manifest CHIP `0x8103` (T8103/M1),
embedded build `xnu-8792.82.2~1/RELEASE_ARM64_T8103`. D064: deployed boot
kernelcache SHA-256 `78959dd9…` byte-identical to the repo img4.
VA rule: `va = fileoff + (seg_va - seg_fo)`.

## 2. Symbol resolution method (FACT)

`__LINKEDIT` contains the kernel export symtab in kcgen form:
nlist64 `{u32 strx; u8 type; u8 sect; u16 desc; u64 value}` at stride 16;
strtab base fileoff `0x40e0340`. `type==0x0f` (N_SECT|N_EXT) entries are
definitions carrying kernel VAs — 103,461 resolved. Non-exported globals
(`mac_policy_list`, `sysent`, `mac_late`) are absent from it.

## 3. `mac_policy_list` — address and layout (instruction-verified)

`_mac_policy_register` export = **`0xfffffe000865b770`**. Its disasm
directly references the global:

```
b860  adrp x8,#0xfffffe00079e1000 ; add x8,x8,#0x750
b868  ldp w9,w8,[x8]              ; numloaded(+0), max(+4)
b86c  cmp w9,w8 ; b.hs            ; numloaded>=max -> w19=0xc ENOSPC
b878  adrp x22,#...79e1000 ; add x22,#0x758   ; +8 = maxindex
b880  ldr w9,[x22]                ; loop bound = maxindex
b884  ldr x10,[x22,#0x10]         ; entries ptr at +0x18
b898  ldr x11,[x10,w8,uxtw #3]    ; entries[i].mpc — element stride 8
b8a0  ldr x11,[x11] ; ... inline strcmp vs [x20]=mpc->mpc_name (+0)
bb34  base+#0x754: ldr [x10,#0x10] (+0x14 freehint scan) ; ldur +0x18 entries
bb80  str w8,[x9,#0x764]          ; freehint = handle+1 at +0x14
bb9c/bbb4/bbc0  +0xc staticmax read/write (late-load accounting)
bbe8  str w9,[x10,#0x758]         ; maxindex update
bbf0/bbf8  +0x750 numloaded++
bbf4  ldr x9,[x20,#0x20] ; ldr x9,[x9,#0x398] ; blraa  ; ops->policy_init
bc20  ldr x8,[x8,#0x3a0] ; blraa            ; ops->policy_initbsd
b934  ldr x8,[x20,#0x30]          ; mpc_field_off
b964  ldr/str [x20,#0x38] orr #1  ; mpc_runtime_flags |= MAC_POLICY_SET
b98c  str x0,[x20,#0x48]          ; mpc_data
b7cc  ldrb w8,[x20,#0x28] ; tbnz #0  ; mpc_loadtime_flags bit0=NOTLATE -> w19=1
b7c4/bb84  ldr w8,[x25,#0x248]     ; mac_late @ 0xfffffe000aa5e248
b800-… casa/sleep-lock complex    ; mac_policy_mtx @ 0xfffffe000aa8cfe8..ff8
```

**`mac_policy_list` VA = `0xfffffe00079e1750`** (`__DATA_CONST`, fo
`0x1657750`; consistent with `SECURITY_READ_ONLY_LATE`). Layout verified:

| off | field | evidence |
|---|---|---|
| +0x00 | numloaded | `ldp` count-check, incremented at bbf4 |
| +0x04 | max | same `ldp`, ENOSPC bound |
| +0x08 | maxindex | loop bound + post-insert update |
| +0x0c | staticmax | late-load accounting |
| +0x14 | freehint | written `handle+1` after slot claim |
| +0x18 | entries | `ldr x10,[+0x18]`, element stride 8 (`uxtw #3`) |

`MAC_POLICY_LIST_CHUNKSIZE=8` (source) bounds: static entries[8] initially,
grown by chunks — total `numloaded` expected ≤ ~20.

**`struct mac_policy_conf` cross-verified on AMFI's real conf** (built
inline at VA `0xfffffe0007bac090` inside `amfi_register_mac_policy` →
`0x92a3044`, `bl mac_policy_register` at `0x92a349c`):
`mpc_name`@+0 → `"AMFI"` (va `0xfffffe0007352f19`), `mpc_fullname`@+8 →
`"Apple Mobile File Integrity"`, `mpc_labelnames`@+0x10, count@+0x18,
`mpc_ops`@+0x20 (PAC'd pointer table), `loadtime_flags`@+0x28=0,
`field_off`@+0x30, `runtime_flags`@+0x38.

## 4. Walker design (read-only, bounded)

```
KRW init (existing libjailbreak kread64/kread32 pattern):
  base = kc va of mac_policy_list + (runtime kernel slide)
  slide: kcraw base va 0xfffffe0007004000 vs live base
         (from libjailbreak primitives / jbinfo — derivation only)
  l1 = kread(list): l1.numloaded in [0..0x40]  else abort
                    l1.max in [8..0x400]       else abort
                    l1.entries != 0            else abort
  consistency: re-read list header; require identical
               numloaded/max/maxindex/entries — else retry (≤3)
               (post-boot mac_late=1 blocks new registration;
                unregister requires MPC_LOADTIME_FLAG_UNLOADOK,
                so the snapshot is stable in practice)
  for i in [0 .. min(l1.maxindex+1, 0x40)):
      e = kread64(entries + i*8); if e==0 continue
      conf = kread(e): name = kread-str(conf+0, bound 0x80, stop@non-printable)
                       ops  = kread64(conf+0x20)
      record {i, conf va, name, ops va}
  report names list + which entries have non-NULL ops
```

Bounds: index cap 0x40, name length cap 0x80, no writes, no locks taken
(snapshot + verify-stability instead of locking — safe against a torn read).

## 5. What the observation proves — and cannot prove

- **"Quarantine" absent from names** ⇒ no Quarantine handler can run ⇒ the
  handler arm is closed for any subsequent call; producer ∈ {no-match,
  foreign-policy `mpo_proc_check_syscall_mac` veto}. Combined with the
  existing live-103 evidence, this is the discriminating fact.
- **"Quarantine" present** ⇒ handler arm becomes possible; next question
  becomes whether its ops syscall slot is non-NULL (needs the offset, see
  missing inputs) and what its 0xb6 op does.
- **Cannot prove**: which arm produced *historical* 103s; per-call precheck
  vetoes (a registered Sandbox-type hook could still return ENOPOLICY for a
  foreign name — the walker shows which policies *exist*, not what their
  precheck does); list state at past instants.
- `mac_late`=1 post-boot ⇒ registrations are EPERM'd (`NOTLATE`/`tbnz` path
  verified in-register code) ⇒ snapshot is durable for the generation.
- Versus the dedicated WS observer's gate evidence: orthogonal — the gate
  telemetry sees the *userspace denial path*; this walker sees *kernel
  ground truth*. One does not substitute for the other.

## 6. Missing inputs (fail-closed, no speculative offsets)

1. **`mpo_policy_syscall` field offset in THIS image**: source count gives
   0x670 but binary-verified `policy_init/initbsd` sit at ops+`0x398`/`0x3a0`
   vs source-count 0x650/0x660 — layout drift between the 8792.81.2 tree and
   this build makes source offsets unusable. Recover by disassembling the
   `mac_syscall` handler — requires locating it (sysent pointers in this
   kcgen image are fixup/PAC-encoded; plain-VA scan cannot find sysent —
   extraction-format gap, not an absence).
2. Live kernel base/slide (from jailbreak primitives at run time).
3. Authorization to run the read-only walker (KRW helper deploy).

## 7. One recommended causal experiment

Run the bounded walker once (§4) against the live kernel: enumerate
`numloaded`, each `mpc_name`, `ops` non-NULL-ness. If `"Quarantine"` is
absent → handler arm closed permanently; remaining producer ambiguity
shrinks to no-match vs a foreign precheck veto, and the earlier
nonsense-name errno calibration becomes the finishing step. If present →
the next RE target is that policy's syscall slot + `0xb6` op semantics.
Either outcome advances; it is strictly stronger than repeating getter
calls, and no alternative reads cheaper.

(handover to codex now)
