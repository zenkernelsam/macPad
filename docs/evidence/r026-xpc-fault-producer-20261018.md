# R026 — exact `xpc-fault` producer and current relevance

Date: 2026-10-18 (Asia/Hong_Kong). Packet:
`coordination/_relay/round-026-to-frontend.md`. This is offline analysis of
preserved exact binaries and prior runtime reports: no new device probes, no
generation, no package/build/source/Git change, no trustcache/TCC/policy
change. No credential or endpoint is recorded.

Inputs: R025 exact libxpc/libsystem_kernel bytes and fault function; the
preserved iOS launchd `790A3FDD-A2AA-3732-9A81-DD0F4B7194EB`
(SHA-256 `793c051b…fa4f`); `launchdchrootexec/main.m`
(SHA-256 `0e8d6504…110c`) and local arm64 build
(SHA-256 `1004c790…0f50`, UUID `685002B3-ECBD-3BCD-9D32-6384F5875472`);
the R010 codesign lightweight report; R012/R013 guard deltas.

## 1. The exact client-side chain is already fixed

From R025 (22F82 libxpc `F3F19227-…`, text SHA `af2bbe7c…0db5`):

```text
libxpc frame-1 function img+0x5E9C:
  img+0x5F78  "xpc-fault"
  img+0x5F80  dictionary value getter img+0x5954
  img+0x5F88  bl fault function img+0x1D5A8   (x0 = value)
fault function img+0x1D5A8:
  img+0x1D73C..0x1D754  os_fault_with_payload(7, 9, NULL, 0, value, 0)
frame 0: libsystem_kernel 34A49B54-… img+0xFB70 mov x16,#0x211;
         img+0xFB74 svc #0x80; img+0xFB78 return (syscall 529)
```

The reason string is the value of the `xpc-fault` key in the reply
dictionary. R026 resolves where that key can be produced.

## 2. Exact launchd `xpc-fault` set sites and branch preconditions

The iOS launchd binary contains exactly two literal `xpc-fault` set sites.
The focused exact disassembly is preserved at
`analysis/r025-libxpc-guard/disasm-launchd-xpc-fault-sites.txt`
(SHA-256 `433545d06c58cb476d5fe948126af96404182f97e6764b4a2520b5fe347124db`).

### Site A — `0x10002F4E4`

```text
0x10002F4C8  bl shared lookup helper 0x10003326C
0x10002F4CC  cbz w0, 0x10002F500        ; helper returned 0 -> normal reply
0x10002F4D4  adr x0, "System session daemon must not initiate XPC to the
                     User session (rdar://77349945): endpoint = %s"
0x10002F4DC  bl formatter 0x100015984
0x10002F4E4  adr x1, "xpc-fault"
0x10002F4EC  x0 = reply dictionary (x20)
0x10002F4F0  x2 = formatted message (x21)
0x10002F4F4  bl _xpc_dictionary_set_string
```

Exact preconditions: `xpc-fault` is set only in the branch where the shared
lookup helper returns nonzero (+ errors); the zero branch returns the normal
reply. The handler builds a reply dictionary with `port`/`bootstrap`/
`exception` fields and formats an endpoint (`x24`) into the message.

### Site B — `0x10002FB60`

```text
0x10002FB44  bl shared lookup helper 0x10003326C
0x10002FB48  cbz w0, 0x10002F774        ; helper returned 0 -> normal reply
0x10002FB50  adr x0, "System session daemon must not bootstrap_look_up User
                     session services (rdar://77349945): endpoint = %s"
0x10002FB58  bl formatter 0x100015984
0x10002FB60  adr x1, "xpc-fault"
0x10002FB68  x0 = reply dictionary (x21)
0x10002FB6C  x2 = formatted message (x20)
0x10002FB70  bl _xpc_dictionary_set_string
```

Exact preconditions: same nonzero-helper branch; this handler builds a reply
with `error`/`port`/`non-launching` fields, formats endpoint `x23`, and is
the explicit `bootstrap_look_up User session services` path.

The shared helper `0x10003326C`/log function `0x100033314` emits the exact
generic log fields:

```text
x1 = name, x2 = instance, x3 = handle, x4 = flags,
x5 = requestor, x6 = error
"%s: name = %s, " + optional "handle/instance/flags" +
"requestor = %s[%d], error = %d: %s"
```

## 3. Match with the codesign request

Exact launchd log (`/var/log/com.apple.xpc.launchd/launchd.log.1:64609-64610`):

```text
18:24:43.889040 launchdchrootex.67961 sent an invalid request
18:24:44.009730 (system) <warning>: failed lookup: name = com.apple.analyticsd,
                flags = 0x8, requestor = codesign[67961], error = 3: No such process
```

Match:

- **Domain/requestor:** launchd `(system)` + requestor `codesign` matches
  "System session daemon" in both messages.
- **Lookup:** `com.apple.analyticsd` is the name field; error 3 is `No such
  process`; the handle/instance fields are absent in the log, which is
  consistent with both handlers.
- **Type:** the observed operation is a service lookup. Site B explicitly
  names `bootstrap_look_up User session services`; Site A covers initiating a
  direct XPC connection to the User session. A Mach service lookup favors
  Site B.
- **Flags:** the log shows `flags = 0x8`; both sites pass a flags argument to
  the shared helper, so this alone does not disambiguate.

**Bounded attribution:** the producer is one of these two exact iOS launchd
cross-domain lookup guards; Site B (`bootstrap_look_up`) is the leading
candidate. The exact site and the substituted endpoint string are not proven
because the lightweight report does not preserve the value and the log does
not include the request operation type/handle. This is not the generic
"failed lookup" text: both set sites use the cross-domain format strings.

## 4. Adapter (`launchdchrootexec`) does not own the fault

Exact source `launchdchrootexec/main.m`:

- uses `POSIX_SPAWN_SETEXEC`, so the adapter is replaced by the target and the
  target's exit status is the shell's wait status;
- sets `DYLD_INSERT_LIBRARIES`, `HOME`, `TMPDIR`, `MallocNanoZone` and
  architecture selection;
- sets `CS_LAUNCH_TYPE_SYSTEM_SERVICE` only when `getppid()==1` and
  `XPC_SERVICE_NAME` is present; otherwise it preserves the ordinary launch
  type;
- does **not** set or rewrite the bootstrap port, session, audit token, XPC
  service name for the target, or any XPC request.

The codesign report's parent PID is 67477, not 1, so the system-service
launch-type branch did not apply. The `(system)` domain is inherited from the
host launch context, not produced by the adapter. The adapter is a request
executor, not the `xpc-fault` producer. Local build identity is recorded
above; the installed device adapter binary identity is not preserved in the
available evidence and is not needed for this source-level conclusion.

## 5. Fatal termination vs lightweight diagnostic event

Exact XNU code (local `8792.81.2`, device `8792.82.2`):

```text
os_fault_with_payload
  -> abort_with_payload_internal(..., OS_REASON_IFLAG_USER_FAULT)
  -> task_violated_guard(code, reason_code, exit_reason, TRUE)
  -> task_enqueue_exception_with_corpse(..., lightweight=TRUE)
```

`osfmk/corpses/corpse.c:563-620`: if `lightweight` is true and
`task_should_generate_lightweight_corpse(task, …)` finds an exception port
with `MACH_EXCEPTION_BACKTRACE_PREFERRED`, the kernel creates a
`KCDATA_OBJECT_TYPE_LW_CORPSE` backtrace object, enqueues it with
`thread_backtrace_enqueue`, and returns. This path does **not** generate a
full corpse task and does **not** kill the task itself. The exact codesign
report has `isLightWeightCorpse: 1`; the R012/R013 class reports are also
lightweight.

Therefore:

- The reports are **diagnostic lightweight os_fault/backtrace events**, not
  evidence of a kernel-forced fatal termination. The `termination` fields
  (`LIBXPC`, `XPC_EXIT_REASON_FAULT`, code 9, flags `0x206`) describe the
  attached exit reason, not a proven process death. R013's long-lived
  processes remaining alive are consistent with this.
- The guard is **not harmless**: the lookup itself failed (`error = 3: No
  such process`), so the requesting operation did not get analyticsd. What
  the caller does after the guard is not inferred.
- If the lightweight condition were not met, the full-corpse path is
  different; not observed here and not generalized.

## 6. Relevance to current bring-up

- The producer is iOS launchd's cross-domain lookup guard for a chroot
  system-session process asking for a user-session service. It is not the
  provider Identifier, publish/trustcache path, dyld identity, or the macOS
  `_xpc_bootstrap_services` PAC crash.
- The codesign event is an install-time analyticsd lookup failure; the
  lightweight guard does not kill the process, and the install exited 0. That
  does not prove the failed lookup had no effect, but no evidence links it to
  provider publish/trust or to the current WindowServer composite/input
  blockers.
- The same class recurs across many services and outside install windows;
  R013's displayd remained alive and its blocker was upstream composite
  production. Current bring-up priority remains the XPC PAC/display/input
  paths. Further rounds on this class are not warranted unless a current
  service logs the same guard **and** loses functionality; in that case the
  missing input is the actual `xpc-fault` value and the request operation
  type.

## 7. Remaining exact input (only if a current service needs it)

1. The actual `xpc-fault` value for incident
   `5AA40622-72B7-4995-AB7B-5F115A105EED` (non-lightweight os_reason or
   retained libxpc `"%s (backtrace follows)"` log), which would also select
   Site A vs Site B and reveal the endpoint.
2. The launchd request operation type/handle for the analyticsd lookup, to
   disambiguate the two set sites without the value.
3. The installed `launchdchrootexec` device binary SHA/UUID if binary-level
   adapter identity is required; source behavior is already exact.

No additional 22F82 cache/libxpc bytes are needed.

## 8. Limits

- Offline analysis only; no new device probes.
- Local XNU is `8792.81.2`; device is `8792.82.2`. The lightweight path and
  exact report field `isLightWeightCorpse=1` are corroborated by the device
  report; exact-build XNU disassembly is not available.
- The iOS launchd binary is exact for this device; its two literal set sites
  are exact, but the actual request operation/value is not preserved.
- No benignity, no fatal-kill, and no adapter-fault ownership is claimed.

(handover to codex now)
