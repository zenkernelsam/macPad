# D088 — user-facing authorization text for runner v3 (revision 4)

Offline document only; runner bytes and hash unchanged
(`8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`).
Applies all five D087 residuals (D087-1–5) to the D086 text.
**Status: NOT APPROVED** — valid only if granted by name and hash.

## Authorization request text (verbatim, revision 4)

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
> this run creates no new kernel state; kernel-side "clean
> bookkeeping" is a declared assumption while the post-`ps` witness
> covers the process side.
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
> stores `unavailable` rather than substituting another tool.
>
> **Kernel read set (declared — bytes touched).** Magic 8 B at
> `kernelConstant.base`; `version[]` 112 B; `mac_policy_list` header
> read twice — **32 B per pass** (6×`kread32` + 1×`kread64`), 64 B
> total; ≤0x40 entry slots (8 B each) + per populated entry 16 B
> (name/ops pointers) + name buffer reads **up to 0x80 `kread64`s
> (≤1024 B touched) to parse ≤0x80 printable bytes** + two 8 B ops
> slots. Nothing else is dereferenced; every pointer passes the
> kernel-VA range gate first.
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
> **Step order (numbered, exact).**
> 1. **Pre-witness** (device): `ps -axo pid,lstart,comm | grep -E
>    "WindowServer|tccd|python3"`;
>    `cat /var/mnt/rootfs/private/tmp/macws_final_composite.state`
>    (chroot-namespace state file in host spelling, witnessed `ready`
>    in D069/D070); `find /var/jb/tmp /tmp
>    /var/mobile/Library/Logs/CrashReporter -maxdepth 2 | sort`
>    (baseline; `sort` makes the byte-identical diff deterministic).
> 2. **Dependency identity** (device):
>    `sha256sum /var/jb/basebin/libjailbreak.dylib
>    /var/jb/usr/bin/python3` — recorded before the mechanism runs.
> 3. **Runner** (device): `/var/jb/usr/bin/python3 -` fed the pinned
>    script over ssh stdin, 120 s bound.
> 4. **Post-witness** (device): the three step-1 commands repeated
>    verbatim.
>
> **Command whitelists (complete).**
> - *Device userspace*: the four step blocks above — `ps|grep -E`,
>    `cat` of the one state path, `sha256sum` of the two binaries,
>    `find … | sort`, `python3 -` via stdin. **Nothing else.**
> - *Host side*: `read -s`/`export` for `SSHPASS`/`MACWS_DEVICE`/
>    `MACWS_DEVICE_PORT`; local `shasum -a256` re-verification of the
>    extracted runner bytes; `cat <script> | sshpass -e ssh …`;
>    `kill` of the local ssh process under the 120 s bound; saving the
>    session transcript to the evidence file. **Nothing else.**
>
> **Witnesses.** Pre and post, identical commands:
> - **Generation**: WindowServer and tccd pid **and** lstart must
>   match exactly — a change invalidates comparison (reboot-detection
>   is also the **panic witness**, since a panic changes pids; the
>   CrashReporter find-diff covers panic logs and any userspace
>   python crash report).
> - **Desktop**: `state=ready` required. Sequence must be
>   **nondecreasing**; `delta=0` with same pid is *flagged for
>   investigation*, not an unconditional pass.
> - **Process exit**: post `ps` must show **no lingering python3**
>   belonging to this run.
> - **Residue**: post `find | sort` byte-identical to baseline over
>   the declared set. Declared blind spots: scratch paths outside the
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
> the declared kernel read set; no command outside both whitelists;
> **no event injection; no pixel/screen capture; no sentinel-file
> creation or removal** (including `macws_frame_pace_diagnostics` or
> any other `/tmp` marker). Any deviation voids admissibility of
> everything the run produced.
>
> **Interpretation bound.** `Quarantine` absent on a completed walk ⇒
> handler-registration arm closed for the whole boot epoch
> (`mac_late=1`); residual #103 attribution remains
> no-match-vs-foreign-precheck-veto. Present ⇒ dispatch arm possible
> only. Userspace TCC legs belong to the separate frontend packet.

## Revision-4 delta over D086 (mapped to D087 findings)

| Finding | Change |
|---|---|
| D087-1 | `| sort` restored on both finds — byte-identical diff now deterministic |
| D087-2 | Header declared **32 B per pass / 64 B total** (6×kread32 + 1×kread64), matching `:84–85` |
| D087-3 | Whitelists split: device userspace set + explicit host-side set (shasum re-verify, `cat|ssh`, `kill`, `read -s`/`export`, transcript save) |
| D087-4 | Name reads declared as **≤1024 B touched → ≤0x80 printable parsed** |
| D087-5 | Steps numbered 1–4: pre-witness → sha256 → runner → post-witness |

## Residuals remaining (recorded, non-blocking)

- `sha256sum` device presence asserted from procursus convention —
  declared `unavailable` fallback, no substitution.
- `find … -maxdepth 2` adds ~1 directory level of output vs `-type f`;
  `sort` output is deterministic but baseline must be captured under
  the same `sort` invocation (declared).
- All carried items stand: physrw-arm entitlement question
  (fail-closed terminal), zero-return fails toward "absent",
  byte-at-a-time name reads (bounded, now declared).

## Provenance

D082 → D083-frontend (23) → D084 rev 2 → D085-frontend (4) → D086
rev 3 → D087-backend audit (5) → this revision. Runner v3 frozen
since D080; presentation still requires the user's named grant of
this exact packet.
