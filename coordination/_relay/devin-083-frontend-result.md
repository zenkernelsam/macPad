# devin-083 — frontend result

Offline cross-review of D082 backend's runner-v3 authorization text
complete. No execution, device, credentials, build, deploy, restart,
source, or Git. **Not approved.**

- `docs/evidence/devin-d083-frontend-20261019.md`: verified the auth
  text against the pinned v3 bytes (D080, sha256 `8bb90399…`) and the
  D081 element review — line-checked the script's emits vs the
  sanitization contract and the witnesses vs current desktop safety.

## Sound

Hash binding + third-cycle naming, bounded kernel read set, bounded
question, 16 fail-closed STOP terminals incl. `physrw-unarmed →
missing-authorization` (forecloses the D069 helper/trustcache class),
`read -s SSHPASS`/`sshpass -e`, identity-before-reads ordering,
zero-return failing toward "absent", non-approval status preserved.

## Ambiguities found (all authorization-text, not script, defects)

- **Scope**: libjailbreak.dylib/python3 device identity unpinned;
  "read-only" doesn't cover physrw-arming persistence; "no syscall"
  overbroad (arming + sysinfo RPC are operations); ssh principal
  unnamed; no wall-clock/kill bound (`ConnectTimeout=8` is connect
  only); mid-walk STOP ⇒ partial-output admissibility undeclared.
- **Sanitization**: residue `find` misses dirs/other paths/TOCTOU and
  crash-report locations; `StrictHostKeyChecking=no` accepted without
  an acknowledged-risk line; stderr (traceback channel) outside the
  stated output contract; SSHPASS env-exposure scope unstated. Emitted
  fields themselves verified clean (`entries_off` is image-fixed).
- **Witness**: `delta=0 valid` too lenient — gen-10447 composites
  continuously, so zero seq delta is anomalous, not benign; no
  kernel-panic witness beyond indirect pid/lstart; no post-run
  process-exit check (a lingering python could hold armed physrw);
  "absent" is only admissible on a *completed* walk — undeclared;
  per-entry reads not torn-guarded (bounded by `mac_late=1`, say so).
- **Authorization**: retry-of-same-bytes semantics unstated; userspace
  witness-command whitelist undeclared (ps/state/find ARE device
  commands outside the kernel read set); no explicit no-injection/
  no-capture/no-sentinel exclusions; operator identity unnamed.

## Verdict

**Do not approve as written.** The frozen script needs no change; the
authorization text needs the five edit groups listed in the evidence
doc (identity pins + principal, arming-scope + process-exit + run
timeout, residue/host-key/stderr declarations, stronger composite +
panic witnesses + completed-walk admissibility, one-attempt + command
whitelist + explicit exclusions + named operator). Symmetric to the
backend's D083 finding on my plan: text-level, correctable, no
execution implied.

(handover to codex now)
