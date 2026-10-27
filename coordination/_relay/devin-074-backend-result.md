# devin-074 — backend result

Operator runbook written. Documentation only — no device, SSH,
credentials, syscalls, deploy, build, trustcache, source, or Git
action, and the packet is explicitly marked **NOT APPROVED FOR
EXECUTION** until the user authorizes it by name.

Runbook contents (`docs/evidence/devin-d074-backend-20261019.md`):

- Credential-safe shell convention: `MACWS_DEVICE`,
  `MACWS_DEVICE_PORT`, `SSHPASS` placeholders; `sshpass -e` mandatory;
  password never on argv or in docs.
- Zero-binary runner: procursus `python3` + ctypes over the SSH
  channel — no new executable, no trustcache change.
- Fail-closed gates: `get_physrw` rc, `staticBase`/`base=static+slide`
  consistency, `MH_MAGIC_64` at base, literal `version[]` string at
  `0xfffffe0007e95ee5+slide` — each printed by name, nothing else on
  failure.
- Bounded read: header double-read, `numloaded≤max≤0x400`,
  `maxindex≤0x40`, names ≤0x80 printable+NUL, ops `+0x388`/`+0x3a8`
  presence via low-56 mask only.
- Sanitized output contract: slide-relative offsets + booleans; no raw
  KASLR in any transcript.
- Pre/post generation witnesses; `find`-based cleanup (no globs —
  D069's abort bug recorded); explicit `STOP missing-authorization`
  if the run would ever need helper signing/trustcache admission.
- Interpretation bounds restated so a future run can't overclaim.

(handover to codex now)
