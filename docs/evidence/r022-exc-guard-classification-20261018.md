# R022 — classification of the `ExcUserFault_codesign` EXC_GUARD namespc=7/reason=9 report

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-022-to-frontend.md`. Read-only audit: no source,
test, build-input, package, device, SSH, installation or Git write. Existing
crash excerpts, postinstall sources and local XNU/libxpc material only.

## 1. Exact report facts

File:
`analysis/r010-provider-f1/crashes/ExcUserFault_codesign-2026-10-18-182444.ips`
(the same report is preserved in
`analysis/r012-xpc-fix/excuserfault-reports.raw:301-336`).

Header/corpse:

```text
app_name=codesign, procPath=/usr/bin/codesign, pid=67961, parentPid=67477
os_version=iPhone OS 16.3 (20D47), capture=2026-10-18 18:24:44.0107 +0800
coalition=com.openssh.sshd.14C1D353-...; isLightWeightCorpse=1
```

Exception and termination:

```text
exception.type      = EXC_GUARD
exception.subtype   = GUARD_TYPE_USER
exception.namespc   = 7
exception.reason    = 9
exception.codes     = 0x6000000000000007, 0x0000000000000009
termination.flags   = 518 (0x206)
termination.code    = 9
termination.namespace = LIBXPC
termination.indicator = XPC_EXIT_REASON_FAULT
```

Loaded images in the report:

```text
0  uuid BE59BB71-7B69-3EFC-86A8-5625A077AE2E  base 0x180000000   (22F82 shared cache)
1  uuid AF141C28-888C-32FE-A793-265F75E55E52  base 0x100258000   (codesign itself)
```

No `CydiaSubstrate`/provider image and no `libmachook` image appears in this
report. The faulting thread's backtrace starts in the shared cache
(`imageIndex 0` offsets `0x3CEB78`, `0x13BF8C`, `0x141928`, `0x1417B0`,
`0x1410E4`, `0x140C84`, then `0x7521684`, ...). Only the last caller frames
are in the codesign image (`imageIndex 1` offsets `0x3130`, `0x1B68`); the
final frame is shared-cache offset `0xA7F28`.

The report's `pc=0`, `lr=0x8f2b0a380` and `esr` text "Address size fault" are
the captured fault context for a simulated guard, not a normal instruction
fault. They must not be read as the triggering instruction.

## 2. Exact structural meaning: a user-namespace fault, not a kernel guard

**FACT (local XNU 8792.81.2 source; device kernel is 8792.82.2, so
version-adjacent):** `bsd/kern/kern_exit.c:1300-1355` implements
`abort_with_payload_internal`. When the caller entered through
`os_fault_with_payload` (`kern_exit.c:1385-1390`), which passes
`OS_REASON_IFLAG_USER_FAULT`, the kernel deliberately creates a *simulated*
EXC_GUARD:

```c
EXC_GUARD_ENCODE_TYPE(code, GUARD_TYPE_USER);
EXC_GUARD_ENCODE_FLAVOR(code, 0);
EXC_GUARD_ENCODE_TARGET(code, reason_namespace);
...
kr = task_violated_guard(code, reason_code, exit_reason, TRUE);
```

The encoding is defined in `osfmk/kern/exc_guard.h:100-115,171`:
bits `[63:61] = GUARD_TYPE_USER (3)`, `[60:32] = flavor 0`,
`[31:0] = reason_namespace`; the second code is the reason code.
`bsd/sys/reason.h:94` defines `OS_REASON_LIBXPC = 7`.

Therefore `code0 = 0x6000000000000007` decodes exactly as:

```text
type=3 (GUARD_TYPE_USER), flavor=0, namespace=7 (OS_REASON_LIBXPC)
code1=9 (reason code)
```

The termination flags corroborate the userspace abort path: `518 = 0x206 =
OS_REASON_FLAG_GENERATE_CRASH_REPORT (0x2) | OS_REASON_FLAG_FROM_USERSPACE
(0x4) | OS_REASON_FLAG_ABORT (0x200)`.

**FACT (adjacent 15.6 libxpc, UUID `1B8951EB-68DB-37BA-9581-240B796AA872`;
not the 22F82 libxpc):** `__XPC_MISUSE_FAULT` at `0x180189a88` is the
libxpc API-misuse fault handler. Its fault call sets the same tuple and
passes the misuse message as the reason string:

```text
0x180189bfc  mov w0, #0x7          ; reason_namespace = OS_REASON_LIBXPC
0x180189c00  mov w1, #0x9          ; reason_code = 9
0x180189c04  mov x2, #0x0          ; payload = NULL
0x180189c08  mov w3, #0x0          ; payload_size = 0
0x180189c0c  mov x4, x19           ; reason_string (misuse message)
0x180189c10  mov x5, #0x0          ; flags = 0
0x180189c14  bl   _os_fault_with_payload
```

The same library's string table lists the exit-reason names in order,
ending with `XPC_EXIT_REASON_UNMANAGED` and `XPC_EXIT_REASON_FAULT`
(offsets `0x3fdd9`-`0x3fece`). This adjacency corroborates that reason 9 is
the libxpc fault reason; the exact 22F82 report metadata already states
`indicator = XPC_EXIT_REASON_FAULT`, so no enum inference is needed for the
device verdict.

**Conclusion:** namespc 7/reason 9 is a *simulated user EXC_GUARD raised by
libxpc's own API-misuse fault path*, not a kernel guard on a file descriptor,
vnode, VM region or Mach port, and not a code-signing or trustcache denial.

## 3. The report is process-independent, not codesign-specific

The preserved R012 raw reports
(`analysis/r012-xpc-fix/excuserfault-reports.raw`) contain the same class for
`pkd`, `SystemUIServer` and `OSXvnc-server`: identical exception tuple,
identical `termination.namespace=LIBXPC`/`XPC_EXIT_REASON_FAULT`, identical
shared-cache frame prefix (`0x3CEB78`, `0x13BF8C`, `0x141928`, `0x1417B0`,
`0x1410E4`, `0x140C84`), and different only in the per-process image.

R012 records 15 such reports at 19:32-19:33 across many services; R013 records
29 at 19:50:23-19:51:36 with the "identical faulting image-offset signature"
and leaves `cause_attribution: none`. R013's long-lived named processes
(displayd 80405, Terminal 80562, OSXvnc-server 80670) remained alive; the
reports do not by themselves prove termination of those processes.

The 19:32/19:50 occurrences are outside any package-install or publisher
transaction (the provider was already installed/unchanged). Timing therefore
does not tie the class to the publisher, and the identical multi-process
signature is evidence of a shared libxpc path rather than a codesign-specific
or provider-specific condition.

## 4. Separate from the XPC PAC crash

| Field | EXC_GUARD report | XPC PAC crash (tccd/authd/systemstatusd) |
| --- | --- | --- |
| Exception | `EXC_GUARD`, `GUARD_TYPE_USER`, namespc 7, reason 9 | `EXC_BAD_ACCESS`/`SIGSEGV`, instruction abort |
| Termination | `LIBXPC` / `XPC_EXIT_REASON_FAULT`, code 9 | `SIGNAL` 11, by exception handler |
| Faulting frames | shared-cache backtrace, codesign caller frames | `_xpc_bootstrap_services` + `loadImageCallback` |
| Key far address | 0 | `0x2000000180ae1140` |
| libmachook image | absent | present |
| Provider image | absent | absent |

They are different reports with different mechanisms. R012 explicitly
recorded that the former `_xpc_bootstrap_services` crash did not recur and
preserved the `ExcUserFault` class as a separate delta.

## 5. Attribution matrix for the codesign report

| Candidate | Verdict | Evidence |
| --- | --- | --- |
| Publisher temp Identifier | **Not attributable** | The publisher signs with `ldid`, not `/usr/bin/codesign`; no `codesign` call targets the provider (`layout/DEBIAN/postinst:981-989`, `layout/usr/macOS/bin/postinst.sh:1389-1397` target `MacWSInteropService.app`). The provider is not in the crash images. The termination namespace is `LIBXPC`, not `CODESIGNING` (`OS_REASON_CODESIGNING = 3`). The same class occurs outside publisher/install windows. Timing alone is not causation. |
| Trustcache admission | **Not attributable** | Admission runs in `jbctl` (a different process) and both provider CDHashes were live; no `jbctl` report exists. A trust/code-signing denial would use the CODESIGNING namespace or an AMFI kill, not LIBXPC reason 9. |
| Codesign tool guard | **Partially implicated, exact trigger unknown** | The crashing process is codesign and codesign frames are in the backtrace, but the guard is libxpc's `__XPC_MISUSE_FAULT`, not a codesign-internal guard. The misuse message/call site is not preserved. |
| Unrelated / unknown | **Trigger unknown; class identified** | The report class is a libxpc API-misuse/user fault, process-independent and recurrent. Whether this particular codesign process hit it because of its own XPC use, the chroot, or another caller remains unproven. |

A plausible THEORY is that codesign's XPC service interaction in this chroot
triggers a libxpc API-misuse path; no local evidence identifies the misuse
string or the exact codesign call, so it is not promoted to fact.

## 6. Missing inputs needed to name the exact trigger

1. The `os_reason` reason string / payload. `__XPC_MISUSE_FAULT` passes
   `x4 = x19` as the misuse message; the lightweight corpse does not include
   it. Needed: the full non-lightweight report, `ReportCrash`/launchd reason
   line, or device `log`/`oslog` around 18:24:44 containing
   `xpc_fault`/`API Misuse`.
2. The exact codesign command line, its parent chain for PID 67477 and its
   exit status. The R010 install transcript contains no codesign line and
   `DPKG_EXIT=0`; the checked postinst steps use `|| exit 1`, so the crashing
   invocation is not identified by timing alone.
3. Exact 22F82 `/usr/bin/codesign` (UUID
   `AF141C28-888C-32FE-A793-265F75E55E52`) and exact 22F82
   `/usr/lib/system/libxpc.dylib` (UUID
   `F3F19227-FF8F-389C-A094-6F4C16E458AF`) to map frame offsets
   `0xA7F28`/`0x3130`/`0x1B68` and the `__XPC_MISUSE_FAULT` caller path.
   Only the libxpc bootstrap entry bytes are local.
4. Optional controlled A/B: run the same codesign bundle-sealing command with
   the provider publisher disabled or with the previous provider; if the same
   LIBXPC fault still appears, it confirms the independence already indicated
   structurally.

## 7. Limits

- No device/SSH, no re-install, no code or test change.
- The local XNU is `8792.81.2`; the device kernel is `8792.82.2`. The local
  libxpc is 15.6 (`1B8951EB-...`); the device libxpc is the exact 22F82
  `F3F19227-...`. The exact 22F82 crash report supplies the device tuple; the
  adjacent sources supply the mechanism, not an exact-build disassembly.
- No claim is made that the trigger is benign or fixed. The class remains an
  open libxpc fault observation; it is simply separate from the provider
  Identifier/trustcache and from the XPC PAC crash.

(handover to codex now)
