# D086 — user-facing authorization text for runner v3 (revision 3)

Offline document only; runner bytes and hash unchanged
(`8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`).
Applies all four D085-frontend residuals (R1–R4) to the D084 text.
**Status: NOT APPROVED** — valid only if granted by name and hash.

## Authorization request text (verbatim, revision 3)

> **Authorization requested: `macws-policy-snapshot-v3`**
>
> Runner v3, sha256
> `8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`
> (hash covers the fenced script bytes plus one trailing newline;
> re-extract and re-verify before use — byte mismatch voids the run).
>
> **Operator.** The user (or a user-named operator) types and runs
> every command at their own terminal. The agent does not execute,
> relay output, or hold credentials.
>
> **Scope.** **One attempt total** — including one that ends in a STOP.
> A single bounded, read-only snapshot of the kernel's `mac_policy_list`
> on iPad13,11 (T8103, iPadOS 16.3, kernel
> `xnu-8792.82.2~1/RELEASE_ARM64_T8103`), via the already-installed
> Dopamine `libjailbreak` primitives called from procursus
> `/var/jb/usr/bin/python3` fed over ssh stdin. It answers only: *is a
> `Quarantine`-named MAC policy registered at snapshot instant, and
> are its `ops+0x388`/`ops+0x3a8` slots populated?* No policy handler
> is invoked; no MAC/TCC/setter syscall is exercised. Mechanism note:
> `jbclient_*` calls and the two dlopens are the mechanism, not added
> scope — arming activates a **pre-existing jailbreakd primitive**, so
> this run creates no new kernel state; the post-`ps` witness verifies
> the process side (no lingering python3), and kernel-side
> "clean bookkeeping" is a declared assumption, not an evidenced claim.
>
> **SSH principal: `root` over password auth** — named explicitly.
> `MACWS_DEVICE`/`MACWS_DEVICE_PORT` are operator-filled placeholders.
>
> **Device-side dependency identity — declared acceptance.** The grant
> accepts `/var/jb/basebin/libjailbreak.dylib` and
> `/var/jb/usr/bin/python3` as-installed (no verified baseline hash
> exists in evidence). **Before the python step**, the operator
> captures `sha256sum` output for both paths into the record —
> procursus `coreutils` spelling; if that binary is absent the record
> stores `unavailable` rather than substituting another tool. This
> binds the recorded identity to the mechanism that ran.
>
> **Kernel read set (declared).** Magic 8 B at `kernelConstant.base`;
> `version[]` 112 B; `mac_policy_list` header ×2 (56 B each); ≤0x40
> entry/name/ops-pointer slots (names ≤0x80 printable bytes). Nothing
> else is dereferenced; every pointer passes the kernel-VA range gate
> first.
>
> **Output / sanitization contract (stdout AND stderr).** Emitted:
> `STOP <name>` terminals, `*-ok` flags, header counts, slide-relative
> `entries_off`, indices, policy names, presence flags. No raw
> KASLR-bearing pointers, audit tokens, task ports, or other memory in
> any emitted **or saved** line — stderr included. A Python traceback
> is an anomaly: captured, reviewed for address material before
> retention, and does not count as a snapshot.
>
> **Credentials.** `read -s SSHPASS; export SSHPASS` — never typed
> visibly, never in argv, history, or files. `sshpass -e` places it in
> the ssh process environment: **host-local exposure only** —
> declared and accepted. `StrictHostKeyChecking=no` +
> `UserKnownHostsFile=/dev/null` hands SSHPASS to whatever answers on
> that port: **acknowledged risk**, accepted for the authorized host
> on the local network only.
>
> **Wall-clock bound.** 120 seconds for the runner step;
> `ConnectTimeout=8` bounds connect. If no output appears within
> 120 s the operator kills the local ssh process (remote python exits
> on channel close). No part may hang unobserved.
>
> **Userspace command whitelist (complete).** ssh plumbing;
> `ps -axo pid,lstart,comm | grep -E "WindowServer|tccd|python3"`;
> `cat /var/mnt/rootfs/private/tmp/macws_final_composite.state`
> (exact path — the chroot-namespace state file in host spelling, the
> same path witnessed `ready` in D069/D070);
> `sha256sum /var/jb/basebin/libjailbreak.dylib
> /var/jb/usr/bin/python3`;
> `find /var/jb/tmp /tmp /var/mobile/Library/Logs/CrashReporter
> -maxdepth 2` (all types; `maxdepth 2` so both userspace crash
> reports in the CrashReporter root **and** panic contents under
> `Panics/` are covered — baseline and post);
> `/var/jb/usr/bin/python3 -` fed the pinned script over stdin.
> **Nothing else** — no improvised diagnostics under this grant.
>
> **Witnesses.** Pre and post, identical commands:
> - **Generation**: WindowServer and tccd pid **and** lstart must
>   match exactly — a change invalidates comparison (reboot-detection
>   is also the **panic witness**, since a panic changes pids; the
>   CrashReporter find-diff now covers both panic logs and any
>   userspace python crash report).
> - **Desktop**: `state=ready` required. Sequence must be
>   **nondecreasing**; `delta=0` with same pid is *flagged for
>   investigation*, not an unconditional pass.
> - **Process exit**: post `ps` must show **no lingering python3**
>   belonging to this run.
> - **Residue**: post `find` byte-identical to baseline over the
>   declared set. Declared blind spots: scratch paths outside the
>   three roots, and create+delete entirely between the two finds
>   (TOCTOU) — both accepted.
>
> **Admissible-absence rule.** "Quarantine absent" is a finding **only
> on a completed walk**: `hdr:` line emitted + a line printed for
> every index **0 through `min(maxindex, 0x3f)` inclusive** + no
> `entry-*`/`name-*`/`ops-*` STOP. Any STOP mid-walk means the
> snapshot answers nothing — the STOP name is the only finding, and
> the attempt is consumed. Per-entry tearing is bounded by
> `mac_late=1`; header double-read remains the consistency gate.
>
> **Fail-closed terminals.** `dlopen`, `sym-*`, `init-prims`,
> `physrw-unarmed`, `sysinfo`, `sysinfo-type`, `base-slide`, `magic`,
> `version`, `torn-header`, `bad-header`, `entry-*`, `nameptr-*`,
> `name-*`, `name-nul-*`, `ops-*` — exit 9 on all. If
> `physrw-unarmed` fires, the run ends as **`missing-authorization`**:
> the helper fallback would need a trustcache admission this grant
> does not cover. No improvised workarounds.
>
> **Explicitly not covered.** No helper executable; no trustcache
> add/remove/clear; no deployment; no restart or launchd action; no
> source or Git write; no second or modified run; no reads outside
> the declared kernel read set; no userspace command outside the
> whitelist; **no event injection; no pixel/screen capture; no
> sentinel-file creation or removal** (including
> `macws_frame_pace_diagnostics` or any other `/tmp` marker). Any
> deviation voids admissibility of everything the run produced.
>
> **Interpretation bound.** `Quarantine` absent on a completed walk ⇒
> handler-registration arm closed for the whole boot epoch
> (`mac_late=1`); residual #103 attribution remains
> no-match-vs-foreign-precheck-veto. Present ⇒ dispatch arm possible
> only. Userspace TCC legs belong to the separate frontend packet.

## Revision-3 delta over D084 (mapped to D085-frontend residuals)

| Finding | Change |
|---|---|
| R1 | Replaced "process-scoped … no kernel bookkeeping" with: arming activates a **pre-existing jailbreakd primitive**; this run creates no new kernel state; kernel-side cleanliness is a declared assumption while the post-`ps` witness covers the process side |
| R2 | Whitelist now spells `sha256sum` (procursus coreutils; `unavailable` recorded if absent — no substitution), the exact state-file path `/var/mnt/rootfs/private/tmp/macws_final_composite.state`, and states the capture runs **before** the python step |
| R3 | Completed-walk formula corrected to "indices 0 through `min(maxindex, 0x3f)` inclusive" — exact match to the script's `range(min(midx+1, 0x40))` |
| R4 | CrashReporter residue witness widened: `…/CrashReporter` at `-maxdepth 2` covers userspace crash reports (root level) and panic contents (`Panics/` subdir) |

## Residuals remaining (recorded, non-blocking)

- `sha256sum` availability on the device is asserted from procursus
  convention, not verified offline — mitigated by the declared
  `unavailable` fallback.
- Whether a userspace python crash would also surface as a Python
  traceback on stderr (sanitization contract covers it) or only as a
  CrashReporter file (now witnessed) — both paths covered, no gap.
- All D084-carried residuals stand: physrw-arm entitlement question
  (fail-closed terminal), zero-return fails toward "absent",
  byte-at-a-time name reads (bounded).

## Provenance

D082 → D083-frontend (23 findings) → D084 rev 2 → D085-frontend (4
residuals) → this revision. Runner v3 frozen since D080; presentation
still requires the user's named grant of this exact packet.
