# D068 backend result — probe readiness verified, mock passed

## Result

Both missing ops offsets are now **image-verified**: located `mac_syscall`
itself at `0xfffffe000865db70` via `copyinstr`-caller analysis, decoded the
precheck and dispatch loops, and pinned the literal `ENOPOLICY` preset
site. The D067 walker contract is complete; a synthetic-layout mock of the
walker logic passes (enumeration, per-policy hook detection, bounds,
fail-closed). Nothing executed — no KRW, no deployment, no syscalls.

## Verified on the exact image (kc_raw / T8103 / xnu-8792.82.2)

- `mac_syscall` = `0xfffffe000865db70`; uap = `policy`@0, `call`@8,
  `arg`@0x10; name buffer 32 B via `copyinstr`.
- `mpo_proc_check_syscall_mac` = ops+**0x388** (per-policy precheck,
  aggregated-error ladder: EAGAIN>EINVAL>ESRCH>ENOENT>EACCES>EPERM,
  last-nonzero fallback → immediate return).
- `mpo_policy_syscall` = ops+**0x3a8** (name strcmp match → `blraa`).
- `mpo_policy_init`/`initbsd` = ops+0x398/0x3a0 (register path, D067).
- `w21=0x67` preset @ `0x865de80` = the no-match ENOPOLICY=103 producer.
- Dual loop forms: `staticmax` bound (unlocked) vs `maxindex` under
  `mac_policy_busy` (dynamic-policy path) — matches
  `mac_policy_list_conditional_busy` semantics.
- Slide contract: `live = image_va − 0xfffffe0007004000 + kernbase_live`
  (kernbase from existing Dopamine primitives).
- Pointer contract: ops slots PAC-signed (pacia/blraa, discs `0x6827`/
  `0xfee3`/`0x2abe`); NULL-test via low-56 mask is sufficient; VA decode
  optional/range-checked.
- `mac_policy_list` storage is zeroed in-file → live read required; no
  file substitute possible.

## Probe readiness

Final walker contract (§3 of evidence doc): double-read header for
tear-detection, bounds `numloaded≤max≤0x400`, index cap 0x40, name cap
0x80 printable, per-entry reads = conf{+0 name,+0x20 ops} +
ops{+0x388,+0x3a8}. Mock: `analysis/devin-d068/walker_mock.py` — PASS
(5-policy layout, hook detection, garbage/​unmapped fail-closed).

## Limits stated

Snapshot proves the *instant's* registered set, not permanence; `mac_late=1`
verified → post-boot registration impossible → per-generation stability.
Cannot show hook *return values*, only presence.

## Recommended next (one)

Execute the walker once via existing KRW primitives (~6 header reads +
~3/policy). It alone closes the provider question; the WS gate observer
can't see kernel registration and can't substitute.

## Artifacts

- `docs/evidence/devin-d068-backend-20261019.md`
- `analysis/devin-d068/walker_mock.py`

(handover to codex now)
