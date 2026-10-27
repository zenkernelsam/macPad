# D067 backend result — MAC policy table layout verified on exact image

## Result

`mac_policy_list` located through actual register-code references (not
string proximity): **`0xfffffe00079e1750`** in `__DATA_CONST`. Field
offsets instruction-verified; `mac_policy_conf` layout cross-verified
against AMFI's real conf (`"AMFI"`/`"Apple Mobile File Integrity"`).
Bounded read-only walker designed; one missing offset recorded
fail-closed.

## Corrections applied (task-required)

- R004 native-token 103 does not exclude all namespace/ABI factors — the
  getter ran in-chroot for both tokens; it excludes sender-identity causes
  only.
- A nonsense-name 103 calibrates errno; it cannot identify which arm
  produced the real call's 103.
- Handler-presence enumeration alone cannot prove the producer either —
  it bounds which arms remain possible.

## Verified (instruction-exact, `mac_policy_register` @ 0xfffffe000865b770)

- `mac_policy_list` = `0xfffffe00079e1750`; `+0`numloaded `+4`max
  `+8`maxindex `+0xc`staticmax `+0x14`freehint `+0x18`entries; element
  stride 8 = `mpc*`.
- `mac_late` = `0xfffffe000aa5e248`; `mac_policy_mtx`/busy complex =
  `0xfffffe000aa8cfe8..ff8` (casa+sleep-lock).
- conf layout via AMFI's inline conf @ `0xfffffe0007bac090`:
  name@0 fullname@8 labelnames@0x10 count@0x18 ops@0x20 flags@0x28
  field_off@0x30 runtime_flags@0x38 data@0x48.
- `mpo_policy_init`@ops+`0x398`, `mpo_policy_initbsd`@+`0x3a0` (PAC `blraa`).
- Export symtab decoded: kcgen nlist64 + strtab at linkedit fo
  `0x40e0340`, 103,461 definitions resolved.
- `mac_late`/`NOTLATE` semantics verified: post-boot registrations
  EPERM → snapshot is stable; unregister requires `UNLOADOK`.
- Bounds: CHUNKSIZE=8; expected numloaded ≤ ~20; walker caps index 0x40,
  name len 0x80, double-reads header for tear-detection, takes no locks.

## Missing inputs (no speculative offsets)

- `mpo_policy_syscall` ops offset in THIS image: source count 0x670
  conflicts with binary-verified init offsets (+0x398 vs source 0x650) —
  must come from `mac_syscall` handler disasm; sysent not locatable by
  plain-VA scan because this image's pointers are fixup/PAC-encoded.
- Live kernel slide + KRW-run authorization.

## What it proves

"Quarantine" absent from the live list ⇒ the handler arm is closed for
any future call (producer ∈ no-match or foreign precheck veto).
Present ⇒ the handler question opens (needs the syscall-slot offset).
Cannot prove: which arm fired historically, foreign-policy precheck
vetoes, or past list states. Complementary to the WS gate observer —
kernel ground truth vs userspace denial telemetry.

## Recommended next experiment (one)

The §4 walker: one bounded KRW read of the list + names + ops-nonNULL —
resolves the provider question outright; cheaper than any further
syscall probing.

## Artifacts

`docs/evidence/devin-d067-policy-table-design-20261019.md` —
offsets table, walker pseudocode, proof/limits analysis.

(handover to codex now)
