# D107 — kernelcache identity reconciliation + bounded mac_syscall audit

Offline static audit only. No device, KRW, snapshot, syscalls, source/build,
or Git. Raw artifact:
`analysis/kc-d107/mac_syscall-0xfffffe000865db70.txt`
(sha256 `60122e941389a297762027b09362bf66c13845be53306a7c5e23117c0be0cfb5`).

## 1. Artifact inventory — re-verified

| Artifact | sha256 / identity | Status |
|---|---|---|
| `analysis/kernelcache_16.3_T8112.img4` | `78959dd973ed3584532d51424274e183d843a2add801bd017f37d64e7b62dcc2` (re-verified) | IMG4/IM4P `krnl`, manifest CHIP=`0x8103` BORD=`0x1e` `T8103-TssLive-ManifestKey` — **T8103/M1-personalized; the `T8112` basename is mislabeled**. Byte-identical to the deployed device kernelcache (D064 live `sha256sum` of `/private/preboot/…/kernelcache`, 21,845,088 B). |
| `analysis/kc_raw_16.3_T8112.bin` | `6673ce2889604cbfb7a9dc938000be0c48486874c9aebae0cdd4870c514f2b38` (re-verified), 80 MB | Thin arm64 `MH_FILESET` Mach-O (magic `cffaedfe` BE = `0xfeedfacf`), 7 segments + ~270 `LC_FILESET_ENTRY`; embedded `xnu-8792.82.2~1/RELEASE_ARM64_T8103` = live `uname` string. Same T8103/T8112 shared driver set. |
| IDA sidecars | `.i64/.id0/.id1/.id2/.nam/.til` | prior interactive analysis, unchanged |

## 2. Contradiction resolved

`devin-d094-layered-acceptance-20261019.md` line ~28–30 says *"matching
Ventura cache/fixup records and target T8103 kernelcache are absent"* — **the
kernelcache part is incorrect as written** and needs correction. Both the img4
and kc_raw exist locally and are hash-stable. What is genuinely absent:

1. **bvx2/RAWPack2 decompressor** — `kc_raw`↔img4 *payload* byte-linkage was
   never run (img4tool/bvxdec/lzfse/librawpack all absent). kc_raw's identity
   rests on embedded build-string + driver-set consistency, not bytes.
2. **Ventura dyld cache / fixup records** — needed to name the libquarantine
   stub provider (`braa` GOT slot `0x1dfef8f88` outside all sparse segments,
   D106). Still absent.
3. **kc_raw symbol table / export trie** — no `LC_SYMTAB`/`LC_DYLD_INFO`;
   `LC_FILESET_ENTRY` names only. Kernel functions resolvable only via
   callsite/string work, not by name.

**Missing extraction stage named**: stage = img4→payload decompression (bvx2).
Not a missing kernelcache.

## 3. Bounded `mac_syscall` audit — image-verified (kc_raw `__TEXT_EXEC`)

`mac_syscall` at `0xfffffe000865db70` (fileoff `0x1659b70`; `__TEXT_EXEC` va
`0xfffffe0007e60000` ↔ fo `0xe5c000`), three arms confirmed verbatim:

- **copyinstr arm** `dbb8–dcd0`: `uap.policy` → `copyinstr` (32B name buffer
  `sp+0x10`); fault → EFAULT, not 103.
- **Precheck arm** `dbd4–dd84`: per-entry `mpc_ops+0x388`
  (`mpo_proc_check_syscall_mac`) via `blraa` diversifier `0x6827`; aggregated
  error ladder (priority selects among `0xb,0x16,3,2,0xd,1`, `csel` keeps
  last-nonzero) → `cbnz w21` returns immediately. **A foreign hook CAN return
  ENOPOLICY — this producer stays open.**
- **Dispatch arm** (two compiled copies — staticmax fast path `dd84–de00` and
  `mac_policy_busy`-locked path `de10+`): inline strcmp `mpc->mpc_name` vs
  `sp+0x10`; match → `mpc_ops+0x3a8` (`mpo_policy_syscall`), `cbz`-skip if
  NULL, `blraa` diversifier `0xfee3`; result `w21=x0`.
- **Two ENOPOLICY preset sites** — `mov w21,#0x67` at **`de80`** (busy-path
  bound exceed) and **`dfbc`** (fast-path `numloaded ≤ staticmax` exhaustion).
  Return `w0=w21` at `dfd8` post-stack-cookie check.

Zero `Quarantine\0` bytes in the entire 80 MB image (re-verified) — no
in-image code can register that literal name. Image storage at
`mac_policy_list` is zeroed → runtime-initialized; file-side state proves
nothing about live registration.

## 4. mac_late vs unload — scoped answer

Source xnu-8792.81.2 `mac_base.c` (adjacent build — drift caveat) + D068's
image-verified EPERM arm:

- `mac_late` is set once at end of `mac_policy_init` **during boot**
  (`:377`). `mac_policy_register` then rejects only
  `MPC_LOADTIME_FLAG_NOTLATE` policies with EPERM (`:638`) — late
  registration is **conditionally** blocked, not universally; non-NOTLATE
  policies can still register dynamically (which is exactly why the
  busy-locked dispatch copy exists).
- `mac_policy_unregister` returns EBUSY unless
  `MPC_LOADTIME_FLAG_UNLOADOK` (`:807`) — a non-UNLOADOK policy, once
  registered, **cannot unload for the boot epoch**.

**So: static code proves *persistence* (no-unload for non-UNLOADOK) and
*conditional* late-registration prohibition (NOTLATE only) — it does NOT
prove what was actually registered.** For `Quarantine` specifically, epoch
absence requires the conjunction: (a) snapshot absence, (b) zero in-image
registrant (verified), (c) no post-boot kext vector (SLE absent, D064 §3c).
The snapshot remains the missing witness; static code alone gives neither
presence nor absence.

## 5. Uncertainty preserved

- Deployment-hash + build-string match do NOT exclude runtime kernel patches
  (jailbreak present); kc_raw↔live-mapped-kernel rests on consistency, not
  bytes. The frozen v3 snapshot reads live state, so it stays valid
  regardless.
- errno `103` is a producer *class* — precheck / no-match-preset / matched-
  handler all remain possible until a snapshot + optional dispatch-boundary
  trace decides. Nothing here infers no-match from `#103`.
- D105's historical causality (msgids `48891.17/.19` → `#103` → `count=0` →
  `1002`) is unchanged and unaffected.
