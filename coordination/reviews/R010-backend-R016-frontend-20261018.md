# Review: Backend R010 and Frontend R016

Date: 2026-10-18 Asia/Hong_Kong. Frozen candidate `c97e219`.

## Backend R010 — accepted with limits; merged generation rejected

The full package was built from a clean frozen worktree, real `.deb` payload
verification passed, device installation returned 0, and the postinst-published
provider's arm64/arm64e identities and trustcache entries were read back.
Disposable device probes show the installed arm64e provider replacement runs
and its original IMP remains callable. The pure F1 header contract passes 9/9
device probe cases. These are useful component-level acceptances.

The combined generation is **not accepted**: tccd/authd/systemstatusd crashed,
the managed restart failed, and no WindowServer or loaded-provider map exists.
There are no final-composite pixels, no live production AppInput/F1 witness,
and TCC/input were not re-tested. The package remains installed and the prior
provider/libmachook files are preserved; the worker completed cleanup without
claiming rollback or success.

The report's crash stack and invalid address support a runtime-confirmed
failure in `_xpc_bootstrap_services`, but the attribution to PAC/ABI mismatch
is still **THEORY**: the exact 22F82 `libxpc` implementation/prototype,
faulting instruction/register arguments, and caller-side ABI have not been
shown together. The proposed role/identity gate is not approved as a fix yet;
first identify what the callback is supposed to initialize and why the call
faults. Preserve the crash logs and package fingerprints for that read-only
investigation.

Secondary finding: the publisher's final Mach-O Identifier reflects its temp
basename. Track it separately; it does not explain the observed XPC crashes.

## Frontend R016 — accepted as a source-level acceptance plan

The audit correctly separates the strict `RequestApplicationReopen` path from
the generic visible-window shortcut, which performs no reopen request. It
also records the document-process preservation rule and provides a bounded
Terminal witness requiring valid `G0 > 0`, a new Host request/result, `G1 > G0`,
a visible metrics entry, and the same PID/executable/start time. It correctly
states that metrics freshness is not pixel or input acceptance and that the
current source path has no device-runtime witness in the saved evidence.

The generation comparison and failure cases have no focused tests. Do not
reimplement the existing mechanism. A future owned WindowServer run should
reuse the R016 witness after the startup regression is understood and a live
generation is available.
