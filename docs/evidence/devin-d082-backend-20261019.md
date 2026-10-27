# D082 — user-facing authorization text for runner v3

Offline document only. The text below is the exact block to present to
the user. Authorization is valid **only** if granted to this packet by
name and hash; anything else is out of scope (D069 precedent).

## Authorization request text (verbatim)

> **Authorization requested: `macws-policy-snapshot-v3`**
>
> Runner v3, sha256
> `8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`
> (hash covers the fenced script bytes plus one trailing newline; the
> block is re-extracted and re-verified before use).
>
> **Scope.** One bounded, read-only snapshot of the kernel's
> `mac_policy_list` on iPad13,11 (T8103, iPadOS 16.3, kernel
> `xnu-8792.82.2~1/RELEASE_ARM64_T8103`), via the already-installed
> Dopamine `libjailbreak` primitives called from procursus
> `/var/jb/usr/bin/python3` fed over ssh stdin. It answers only:
> *is a `Quarantine`-named MAC policy registered at snapshot instant,
> and are its precheck (`ops+0x388`) / syscall (`ops+0x3a8`) slots
> populated?* No policy handler is invoked; no syscall, TCC, or setter
> is exercised.
>
> **Reads.** Kernel-base magic (8 B), `version[]` string (112 B),
> `mac_policy_list` header read twice (56 B each), up to `0x40`
> entry/name/ops-pointer slots (names capped at `0x80` printable
> bytes). Nothing else is read.
>
> **Output.** Sanitized only: `STOP <name>` terminals, `*-ok` flags,
> header counts, slide-relative `entries_off`, index, policy name,
> presence flags. No raw KASLR-bearing pointers, audit tokens, task
> ports, or other memory appears in any emitted or saved line.
>
> **Credentials.** `read -s SSHPASS; export SSHPASS` — never typed
> visibly, never in argv, history, or any file. `sshpass -e` only;
> `MACWS_DEVICE`/`MACWS_DEVICE_PORT` are placeholders filled at run
> time. ssh options: `ConnectTimeout=8`, `StrictHostKeyChecking=no`,
> `UserKnownHostsFile=/dev/null`, password-only auth.
>
> **Witnesses.** Before and after: `ps -axo pid,lstart,comm` for
> WindowServer and tccd (pid **and** lstart must match), the
> `macws_final_composite` state must be `ready` (sequence is
> informational — nondecreasing is expected, delta=0 with same pid is
> valid), and a `find /var/jb/tmp /tmp -maxdepth 1 -type f` baseline
> diff that must come back byte-identical. A pid/lstart change
> invalidates the comparison; the run is aborted on Step-0 failure.
>
> **Fail-closed terminals.** Every gate emits `STOP <name>` and exits:
> `dlopen`, `sym-*`, `init-prims`, `physrw-unarmed`, `sysinfo`,
> `sysinfo-type`, `base-slide`, `magic`, `version`, `torn-header`,
> `bad-header`, `entry-*`, `nameptr-*`, `name-*`, `name-nul-*`,
> `ops-*`. If `physrw-unarmed` fires — meaning python's entitlements
> can't arm the read primitive — the run ends as
> **`missing-authorization`**: the fallback (a staged helper binary)
> would need a trustcache admission, which this grant does not cover.
> No improvised workarounds.
>
> **Explicitly not covered.** No helper executable, no trustcache
> add/remove/clear, no deployment, no restart, no launchd action, no
> source or Git write, no second run under a modified script, no
> reads outside the declared set. Any deviation voids admissibility of
> everything the run produced.

## What authorization would and would not settle

- Granted: an admissible, bounded snapshot exists; a `Quarantine` row
  closes the handler-registration arm for the whole boot epoch
  (`mac_late=1`), leaving the historic #103 explained by no-match or a
  foreign precheck veto. Absence does not rerun — one snapshot only.
- Denied/deferred: the question stays open at D081 status; no further
  offline artifact can answer it (PAC-encoded stubs block caller
  enumeration — D072).

## Residual risks carried with the grant (from D081)

Physrw arm under procursus python entitlements is unverified
(fail-closed terminal defined); zero-return reads fail safe toward
"absent"; name reads are byte-at-a-time but bounded.
