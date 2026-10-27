# D084 — user-facing authorization text for runner v3 (revision 2)

Offline document only; runner bytes and hash unchanged from D080/D082.
Applies every D083 frontend finding (S1–S7, Z1–Z5, W1–W6, A1–A4).
**Status: NOT APPROVED** — authorization is valid only if granted to
this packet by name and hash.

## Authorization request text (verbatim, revision 2)

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
> is invoked; no MAC/TCC/setter syscall is exercised. (The jailbreak
> arming RPCs and the two dlopens are the *mechanism*, not additional
> scope: `jbclient_*` calls are process-scoped to the python process
> and leave no kernel bookkeeping after process exit.)
>
> **SSH principal: `root` over password auth** — named explicitly as
> the elevated context this requires. `MACWS_DEVICE`/`MACWS_DEVICE_PORT`
> are operator-filled placeholders.
>
> **Device-side dependency identity — declared acceptance.** The grant
> accepts `/var/jb/basebin/libjailbreak.dylib` and
> `/var/jb/usr/bin/python3` as-installed by the Dopamine/procursus
> stack (no verified baseline hash exists in evidence — only a
> truncated `e249e475…` for the dylib). The run captures each file's
> `sha256` into the record for post-hoc identity, inside the declared
> command whitelist — it does not gate execution on a preset value.
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
> any emitted **or saved** line — this covers stderr too. A Python
> traceback on stderr is an anomaly: it is captured, reviewed for
> address material before retention (verified low-risk: kernel VAs
> exist only as ints, never repr'd), and does not count as a snapshot.
>
> **Credentials.** `read -s SSHPASS; export SSHPASS` — never typed
> visibly, never in argv, history, or files. `sshpass -e` places it in
> the ssh process environment: **host-local exposure only**, inherited
> by its children — declared and accepted.
> `StrictHostKeyChecking=no`+`UserKnownHostsFile=/dev/null` hands
> SSHPASS to whatever answers on that port: **acknowledged risk**,
> accepted for the authorized host on the local network only.
>
> **Wall-clock bound.** 120 seconds for the runner step.
> `ConnectTimeout=8` bounds connect; the operator kills the local ssh
> process if no output appears within 120 s (remote python exits on
> stdin/channel close). No part may hang unobserved.
>
> **Userspace command whitelist (complete).** ssh plumbing;
> `ps -axo pid,lstart,comm | grep -E "WindowServer|tccd|python3"`;
> `cat` of `macws_final_composite.state`; `sha256` of the two pinned
> device binaries; `find /var/jb/tmp /tmp /var/mobile/Library/Logs/
> CrashReporter/Panics -maxdepth 1` (all types, for both baseline and
> post); `/var/jb/usr/bin/python3 -` fed the pinned script over stdin.
> **Nothing else** — no improvised diagnostics under this grant.
>
> **Witnesses.** Pre and post, identical commands:
> - **Generation**: WindowServer and tccd pid **and** lstart must match
>   exactly — a change invalidates comparison (reboot-detection is
>   also the **panic witness**, since a panic changes pids; the Panics
>   find-diff catches panic-logs-short-of-reboot).
> - **Desktop**: `state=ready` required. Sequence must be
>   **nondecreasing**; `delta=0` with same pid is *flagged for
>   investigation*, not an unconditional pass (gen-10447 composites
>   continuously — a flat sequence is anomalous).
> - **Process exit**: post `ps` must show **no lingering python3**
>   belonging to this run — closes the armed-primitive persistence
>   question (arming is process-scoped and dies with it).
> - **Residue**: post-run `find` must be byte-identical to baseline
>   over the three declared directories, all file types. Declared
>   blind spots: other scratch paths, and create+delete entirely
>   between the two finds (TOCTOU) — both accepted.
>
> **Admissible-absence rule.** "Quarantine absent" is a finding **only
> on a completed walk**: `hdr:` line emitted + every slot `0..min(
> maxindex,0x40)-1` printed + no `entry-*`/`name-*`/`ops-*` STOP. Any
> STOP mid-walk means the snapshot answers nothing — the STOP name is
> the only finding, and the attempt is consumed. Per-entry tearing is
> bounded by `mac_late=1` (no late load/unload expected); header
> double-read remains the consistency gate.
>
> **Fail-closed terminals.** `dlopen`, `sym-*`, `init-prims`,
> `physrw-unarmed`, `sysinfo`, `sysinfo-type`, `base-slide`, `magic`,
> `version`, `torn-header`, `bad-header`, `entry-*`, `nameptr-*`,
> `name-*`, `name-nul-*`, `ops-*` — exit 9 on all. If
> `physrw-unarmed` fires, the run ends as **`missing-authorization`**:
> the helper-binary fallback would need a trustcache admission this
> grant does not cover. No improvised workarounds.
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

## Revision-2 delta over D082 (each mapped to a D083 finding)

| D083 | Change |
|---|---|
| S1 | Dependency identity declared as acceptance + sha256 capture in whitelist (no offline baseline exists) |
| S2/W3 | Arming declared process-scoped; post-`ps` python3-exit witness added |
| S3/S4 | "no MAC/TCC/setter syscall" wording; list relabeled "kernel read set"; mechanism (RPC/dlopen) named |
| S5 | `root`-over-ssh principal named |
| S6 | 120 s wall-clock bound + kill plan |
| S7/W4 | Admissible-absence = completed walk; one attempt total |
| Z1 | find widened to all types + Panics dir; TOCTOU/other-path blind spots declared |
| Z2 | Host-key-disabled risk acknowledged in text |
| Z3 | stderr folded into the sanitization contract |
| Z4 | SSHPASS env exposure scoped "host-local only" |
| W1 | Sequence nondecreasing-required, delta=0 flagged anomalous |
| W2 | Panic witness = pid+lstart reboot-detection + Panics find-diff |
| W5 | Per-entry tear bounded by `mac_late=1`, stated |
| W6 | Distinguishability matrix satisfied by new witnesses above |
| A1 | "One attempt total" explicit |
| A2 | Complete userspace command whitelist |
| A3 | No-injection/no-capture/no-sentinel exclusions explicit |
| A4 | Operator named (user's terminal; agent does not execute) |

## Provenance

D082 authorization text → D083 frontend cross-review (23 findings) →
this revision. Runner v3 bytes/hash frozen since D080; D081 element
review; D069 precedent supplies every boundary clause.
