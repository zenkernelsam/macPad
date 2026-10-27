# D096 independent review — responsibility/TCC attribution limits

Read-only review of `docs/evidence/devin-d096-backend-20261019.md` against
D060, D069, D057 and R014. No device access, probes, or source changes.

## Accepted facts from D096

- Exact SkyLight 22F82 has no imported `responsibility_*` or `setaudit*`
  symbols; its `setResponsibleProcess:` Objective-C selector is session
  bookkeeping, not proof of kernel responsibility registration.
- Exact TCC 22F82 exports `TCCAccessSetPidResponsibleForPid` and sends a
  request to tccd. D096's local callsite search found no caller in SkyLight
  or the project-owned sources reviewed.
- Exact Ventura `launchd`, `tccd`, and `libsystem_kernel` implementations
  needed to establish external callers/ABI are absent locally, as D096 says.

## Claims not established by these facts

1. **“No caller in SkyLight/project” does not prove no responsibility
   registration for the target PID.** The launchd caller and Ventura tccd
   binaries are absent; external callers and job-spawn behavior remain
   unknown. Do not promote this to “registration absent at userspace level.”
2. **The Quarantine policy is not shown to be the only provider/resolution
   path.** D060/D069/R014 retain three `#103` producers: any policy's
   `mac_proc_check_mac_syscall` precheck veto, no-match default, or a matched
   handler return. A `Quarantine`-named policy snapshot addresses only whether
   that policy dispatch is possible; presence does not prove invocation, and
   absence does not exclude foreign precheck veto or no-match.
3. **The proposed snapshot cannot collapse the open registration-vs-query
   split.** If Quarantine is present, registration/query semantics remain
   unmeasured. If absent, only its matched-handler arm is excluded for the
   boot epoch; it does not establish why the responsibility query returned
   `#103`.

## Correct status and next discriminator

Observed `#103` remains localized by exact tccd disassembly to the final
responsibility lookup after the path check (D060). The launchd registration
state vs provider/query-resolution distinction remains unresolved. A
read-only `launchctl procinfo`/equivalent comparison of existing target PIDs
was the smallest proposed userspace discriminator in D060 §6, but no result
is present and its availability/semantics on the target are unverified. The
named `macws-policy-snapshot-v3` remains a separate kernel-policy fact and
still requires explicit authorization; it cannot answer the responsibility
registration question by itself.

Do not state a root cause until an exact runtime or disassembly artifact
observes the responsible identity for the target sender and the matching
query result/branch. Current accepted conclusion: the failure occurs at
responsibility lookup; its upstream producer is still open.
