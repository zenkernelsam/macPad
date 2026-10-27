# Round 005 — frontend result: responsibility/audit contract audit

Date: 2026-10-18. Read-only source/binary audit; no device, source/test/build
change, build, package or Git write. Only this file and
`docs/evidence/r005-responsibility-audit-20261018.md` were added.

Candidate HEAD: `7e5d771`. Backend R002's live generation is not touched.

## Inspected identities (SHA-256)

| Object | Identity |
| --- | --- |
| `launchdchrootexec/main.m` | `0e8d6504189188db4c8993194381abd6d459bbf01113374ef5ae010f606c110c` |
| `libmachook/mac_hooks.m` | `e8ae9dadfa5bb40701a681661cb7677e729f94b35807355f5ea5ae0af71eb08d` |
| `analysis/TCC_22F82` | UUID `1238D7CA-719A-36A4-94B1-2F387E6B3BF1`, `09b84137…f2de` |
| `analysis/SkyLight_22F82` | UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`, `13d7a0ca…6c61` |
| share `tccd-arm64e` (24G90) | UUID `8D17A864-8554-3BA0-9CC9-B92BD597F6AE`, `b616c04d…b057` |
| Ventura tccd (backend) | UUID `9845D17F-0409-3884-8A17-260C729636E5`, `42c388e8…c828d` |
| XNU iOS 16.3 source | `kern_proc.c` `3ec596e3…f6ca`, `kern_fork.c` `1cac9e63…677f` |

## Contract (FACT, with version scope)

- 24G90 tccd call site at `0x10005c8a8` (`__TEXT` vmaddr `0x100000000`,
  fileoff 0 → file offset `0x5c8a8`): x0 = input token pointer, x1 = output
  token pointer, x2/x3 = 0; return `w0`, 0 = success.
- Failure path reads `errno`; `#103` is `ENOPOLICY` (`sys/errno.h:256`), and
  `strerror(103)` is `Policy not found`. The Ventura log's
  `(#103) Policy not found` is therefore the responsibility API's errno, not a
  TCC policy denial.
- Failure leaves attribution nil; the backend's Ventura path creates the reply
  at `0x100006500` and sends it unchanged at `0x1000069cc..e0`, yielding
  `count=0`.
- XNU source: `p_responsible_pid` (`proc_internal.h:359`), inherited on fork
  (`kern_fork.c:1096`), setter `proc_set_responsible_pid` (`kern_proc.c:4698-4706`).
  No in-tree setter caller and no open-source proc_info responsibility flavor.
  The field feeds MAC launch constraints/socket policy; it is a real kernel
  policy input.
- **THEORY:** the macOS libSystem wrapper queries that policy and returns
  `ENOPOLICY` when absent. The exact Ventura wrapper implementation is not in
  the local extracts (only 15.6 stubs).

## Project adaptations

- **FACT:** project source contains **0** references to
  `responsibility_get_*`, `responsibility_set_*`,
  `responsibility_spawnattrs_*` and `setaudit*`. It does not create or override
  responsibility.
- **FACT:** `audit_token_to_asid/auid` are declared **by value**
  (`mac_hooks.m:1685-1686`) and implemented at `13565-13575`; C semantics mean
  the interposes cannot mutate the caller's token. The observed
  `sender_auid=501` is the accessor return, not a rewritten token. They cannot
  change the token passed to the responsibility API, and they do not register a
  policy. Do not attribute `ENOPOLICY` to the auid mismatch alone.
- **FACT:** `launchdchrootexec` uses `POSIX_SPAWN_SETEXEC`
  (`main.m:119,141`) and preserves the launcher's launchd responsibility; it
  only requests `CS_LAUNCH_TYPE_SYSTEM_SERVICE` when
  `getppid()==1 && XPC_SERVICE_NAME` (`103-113`). It has no responsibility
  declaration. SSH-spawned probes are not launchd jobs.
- Other project responsible-process usage (`macwshostd/main.m:3047-3056`,
  `MacWSHost/main.m:6379-6385`, `macos_gui.sh:1504`) relies on the genuine
  UIKit/FrontBoard owner spawn; it is not a direct responsibility API and is
  not reusable for chroot tccd clients.

## Share-package comparison

Share `launchdchrootexec/main.m:133,146,178` uses the same SETEXEC/launch-type
contract and no responsibility setup; `libmachook/mac_hooks.m:12504-12508,
14203-14204` has the same audit accessors; `mac_hooks.m:5689` still installs
the constant-true `_audit_token_check_tcc_access` hook. Its success bypasses
the checker rather than fixing responsibility; not a reusable correction.

## Narrow repair candidates

1. **Preferred (no source change):** launch TCC-attributed clients through a
   genuine launchd job. Use a one-shot launchd-hosted probe instead of the SSH
   path; keep WS on its existing launchd job. This reuses launchd's real
   responsibility producer and changes no TCC code.
2. **Conditional source candidate:** only if a genuine launchd job also has no
   policy and the API succeeds, call
   `responsibility_set_pid_responsible_for_pid(getpid(), getpid())` in
   `launchdchrootexec`'s genuine-job branch (`getppid()==1 &&
   XPC_SERVICE_NAME`) before `POSIX_SPAWN_SETEXEC`. This is a real
   self-responsibility policy, not a fake token; never apply it to SSH/ad-hoc
   probes. If it returns `EPERM/ENOTSUP`, report the platform gap instead.
3. **Job config:** adjust the job domain/uid so launchd establishes the policy
   (plist/experiment, not a source patch).
4. **Rejected:** fabricated tokens/sessions, setter calls for arbitrary probes,
   constant checker, TCC grant/DB edits, or copying the share hook.

## Discriminating device A/B (backend owns)

1. Same `macwstccdprobe preflight` from SSH vs a one-shot launchd job (same
   domain/uid); capture tccd `responsibility_get_responsible...`, attribution,
   reply keys/count, plus a read-only
   `responsibility_get_pid_responsible_for_pid(getpid(), &rpid)` return/errno.
   Success in the launchd case isolates the launch context.
2. Fresh WS generation: capture the first TCC request; if it also logs
   `#103 Policy not found`, the WS launch contract is the common cause. If not,
   the probe failure is probe-only.
3. Only if 1/2 show genuine jobs also lack a policy: disposable
   `launchdchrootexec` with the gated self-responsibility call; A/B the reply.
   Do not ship without this witness.

## Missing evidence

- Exact Ventura libSystem/libsystem_kernel wrapper implementation and UUID
  (local 15.6 extracts are stubs; no responsibility symbol in the extracted
  kernel lib).
- Exact Ventura tccd local copy/disassembly at the responsibility call site
  (backend has the device copy; this round has the 24G90 structural match and
  backend offsets).
- Launchd-hosted probe and fresh WS responsibility result/reply.
- Whether iPadOS launchd sets `p_responsible_pid` for MacWS jobs.
- Whether the setter API is permitted/functional in `launchdchrootexec`.
- Probe/WS `XPC_SERVICE_NAME`, launch type, job domain and responsible PID.

## Commands / exits

`sha256sum`, `otool -l`, `llvm-objdump -d`, `dyld_info -exports`,
`nm/strings` and targeted `grep | wc -l` all exited 0. The `strerror(103)`
control compiled and ran (exit 0, `103=Policy not found`). Project API counts:
responsibility_get 0, responsibility_set 0, responsibility_spawnattrs 0,
setaudit 0. No test, build, package or device command was run; no Git
commit/reset/stash/push.

Full detail: `docs/evidence/r005-responsibility-audit-20261018.md`.

(handover to codex now)
