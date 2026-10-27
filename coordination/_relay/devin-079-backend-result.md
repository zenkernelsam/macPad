# devin-079 — backend result

Static ABI review of runner v2. Offline only — no device, credentials,
syscalls, deploy, build, trustcache, source, or Git.

**Verdict: one new fail-closed defect — runner v2 cannot complete.**

- `DICT_T` compares `xpc_get_type()` (returns the *address* of
  `_xpc_type_dictionary`) against the type object's *contents*
  (`.value`). Host-verified: address `0x…4188` ≠ contents `0x…41b0` →
  run always stops at `STOP sysinfo-type`. Fix for v3:
  `ctypes.addressof(c_void_p.in_dll(xpc,"_xpc_type_dictionary"))`.
  New bytes ⇒ new hash ⇒ new authorization cycle.

Consistent, checked against the saved dylib + D069 helper: all five
jailbreak symbols exported in both slices with matching restypes;
`kread32` for u32 fields matches the empirically-working D069 C path
(and D077's alignment defect is corrected to "defensive, not required"
— diag7 proved unaligned `kread64` returns correct data); all
`kread64` call sites are naturally aligned except the proven-working
`version[]` read.

Running defect list for v3: DICT_T fix (required); `in_dll`
`ValueError` path (cosmetic); physrw-under-python entitlement unknown
(runtime fail-closed gate).

Evidence: `docs/evidence/devin-d079-backend-20261019.md`

(handover to codex now)
