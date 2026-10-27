# D069 frontend — independent review of the policy-snapshot acceptance contract

Local read-only. No device access (backend owns the single KRW read —
not contacted), no syscalls/TCC/setters, no deployment/restart, no
production/Git changes. The backend D069 result had not landed at
write time; per task, this document defines the acceptance contract
and the missing-input checklist rather than reviewing a result.

## 1. Audit of the D068 interpretation matrix (self-review)

The D068 frontend decision table survives audit with two sharpenings:

- **Correct as written**: hook-install proof + zero-drop accounting are
  preconditions for reading *any* absent-record row; `lazy_post` splits
  unresolved-symbol from reached-call; `impl_rc=0` remains
  "nonzero-or-unresolved", never a verdict by itself.
- **Sharpening 1**: even a live snapshot proving `"Quarantine"`
  *registered* does not by itself clear the userspace side — the impl
  record `lazy_post`/`rc` still decides whether TCC was asked and what
  it answered. The snapshot and the observer answer *different layers*:
  kernel registration vs sender-side check outcome. Neither substitutes.
- **Sharpening 2**: a `#103` observed in any userspace channel has
  *three* candidate producers (R009): no-match default
  (`w21=0x67` preset @ `0x865de80`, image-verified by backend),
  per-policy `mpo_proc_check_syscall_mac` precheck veto (ops+0x388 —
  *any* registered policy can produce it, Quarantine-named or not),
  or a dispatched handler's internal return. The snapshot resolves
  **only whether a Quarantine-named handler can exist**, not which arm
  historically fired.

## 2. Acceptance criteria for a saved D069 snapshot

A snapshot is *acceptable as evidence* only if every row passes. Any
single row failing → the corresponding conclusions are void, not
degraded.

| # | Criterion | Contract |
|---|---|---|
| A1 | **Kernel identity** | Live `kernbase` derived from the existing trusted Dopamine primitive (not a constant); `uname`/build string == `xnu-8792.82.2~1/RELEASE_ARM64_T8103`; boot kernelcache identity already closed (SHA `78959dd9…` == repo img4, D064-backend) — the snapshot must reference *that* image's offsets, no re-derivation from another build |
| A2 | **Slide proof** | `live_va = image_va − 0xfffffe0007004000 + kernbase_live`; the computed `mac_policy_list` address must be independently sanity-read (e.g., a known-immediate field like `staticmax` ≥ expected count) before trusting entries — a mis-slid read must fail closed, not parse garbage |
| A3 | **Header consistency** | `mac_policy_list` read **twice**; identical `numloaded/max/maxindex/staticmax/freehint` across reads (tear check). Bounds: `numloaded ≤ max ≤ 0x400`, `numloaded` expected ≤ ~20, `entries` pointer low56-nonzero and in kernel range |
| A4 | **Entry bounds** | walk capped at `index ≤ 0x40`; per-entry `mpc*` pointer low56-nonzero + kernel-range; conf pointer same checks; name read capped `0x80`, must be printable + NUL-terminated before the cap |
| A5 | **Pointer/PAC** | ops slots are PAC-signed — NULL-test via low-56 mask only; **no PAC decoding needed for presence**; any VA decoding is optional and range-checked; never follows a pointer that fails range |
| A6 | **Names + ops slots** | for every entry: `conf.name` string, `ops+0x388` (precheck) presence flag, `ops+0x3a8` (policy_syscall) presence flag — presence is the deliverable, **never** invocation |
| A7 | **Generation witness** | WS pid/lstart + tccd pid + `macws_final_composite.state` seq captured before AND after; identical ⇒ desktop generation preserved |
| A8 | **Completeness** | command exits + helper/job hashes saved; helper artifacts removed; no leftover owned processes |

## 3. Interpretation contract — what the snapshot can and cannot say

**If `"Quarantine"` is absent from the list:**

- PROVES: at snapshot instant, no policy named `"Quarantine"` is
  registered → the `mpo_policy_syscall` dispatch arm for that name
  cannot exist **at that instant**. Combined with `mac_late=1`
  (post-boot registration impossible, D068-verified), the arm is closed
  for the whole boot epoch of that kernel.
- DOES NOT PROVE: (a) that any *historical* `#103` came from the
  no-match arm — a foreign policy's precheck veto remains a live
  candidate; (b) what a Quarantine handler *would* return; (c) the
  list on any other build/boot; (d) anything about userspace legs —
  the WS-side question (does the denial even reach TCC) stays open
  and belongs to the D068 observer plan, not this snapshot.

**If `"Quarantine"` is present with `ops+0x3a8` non-NULL:**

- PROVES: a registered handler for that name existed at snapshot → the
  dispatch arm is *possible*; provider-absence hypothesis falsified.
- DOES NOT PROVE: (a) that the historical call ever dispatched to it —
  the precheck loop runs *before* name-matching and any other policy's
  veto can still produce `#103` first; (b) that `0xb6` is a valid
  selector in its table (presence of the hook ≠ selector semantics);
  (c) its return values — presence evidence only, never a call.

**Either way:** a snapshot is a point-in-time read. `mac_late=1` gives
per-boot-epoch stability, not permanence across reboots/updates.

## 4. Sensitive-data hygiene for saved evidence

- **Kernel addresses are KASLR-bearing**: publishing raw `entries`,
  `conf`, `ops`, or name pointers leaks the live slide. Acceptable:
  slide-relative offsets or low-56 presence flags only. Raw VAs must be
  elided or stored as `va − kernbase` (kernbase itself needn't appear).
- **No audit tokens / process creds** beyond needed: WS/tccd pid+lstart
  are legitimate witnesses; no token bytes, no task ports, no
  entitlement blobs.
- **No unrelated reads**: only `mac_policy_list` header + bounded
  entries + name strings. No `task_for_pid` dumps, no arbitrary-VA CLI.
- Command transcripts: exits + stderr ok; sanitize host/user specifics
  per repo convention (placeholders for addresses/ports).

## 5. Missing-input checklist (since no result landed)

For the snapshot to land as acceptable evidence, backend still owes:

- [ ] A1/A2 identity + slide proof lines (uname string, kernbase
      derivation method name — not the value)
- [ ] A3 double-read header transcript (two dumps, fields compared)
- [ ] A4/A5 walk log: per-index pointer checks + name strings + the
      two ops flags, with caps honored (0x40/0x80)
- [ ] A7 before/after generation witness (pids, lstart, state seq)
- [ ] A8 helper hash + cleanup proof (files removed, no owned procs)
- [ ] §4 sanitization applied — no raw KASLR addresses or tokens in
      the saved artifact

If slide/base/list-pointer couldn't be independently verified, the
task's own contract says STOP without the read — a "missing input"
report is the correct terminal, not a degraded snapshot.

## Provenance

Reviewed: `devin-068-backend-result.md`, `devin-069-backend-task.md`,
`devin-d068-frontend-20261019.md`, R009 three-producer model, D067
offset table (`0x79e1750` list, conf/ops layout, `0x388/0x3a8` slots,
`0x865de80` ENOPOLICY preset). No device, no kernel reads, no Git.
