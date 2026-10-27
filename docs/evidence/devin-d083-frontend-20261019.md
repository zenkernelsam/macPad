# D083 — frontend cross-review of D082 backend runner-v3 authorization text

Offline review only. Nothing executed; no device contact, credentials,
build, deploy, restart, source, or Git. **No execution is approved by
this memo.**

Subject: `docs/evidence/devin-d082-backend-20261019.md` — the user-facing
authorization block for `macws-policy-snapshot-v3`, verified against the
pinned runner bytes in `docs/evidence/devin-d080-backend-20261019.md`
(sha256 `8bb903994c741a89a47f6bfe57ed98ffe41ff99b33236292e54118e06b8a5bcc`)
and the D081 element review.

## Verified sound (meets evidence discipline)

- **Hash binding** — name + sha256, extraction convention declared
  ("fenced script bytes plus one trailing newline"), re-verification
  required before use; v3 hash distinct from v1/v2 ⇒ correctly demands
  a third authorization cycle.
- **Bounded kernel read set** — enumerated: magic 8 B, version 112 B,
  header 2×56 B, ≤0x40 entries, names ≤0x80 printable, two ops slots.
  Matches the script exactly; nothing else is dereferenced (KV range
  gate before every pointer).
- **Question bounded** — "is a `Quarantine`-named policy registered,
  and are `ops+0x388`/`ops+0x3a8` populated" is exactly the D069
  acceptance question; interpretation bound (epoch via `mac_late=1`,
  absent⇒no rerun, residual #103 attribution no-match-vs-foreign-veto)
  matches the agreed contract without overclaim.
- **Fail-closed** — 16 STOP terminals match the script 1:1;
  `physrw-unarmed → missing-authorization` explicitly forecloses the
  D069 violation class (helper binary / trustcache admission).
- **Credential handling** — `read -s SSHPASS`/`sshpass -e` removes the
  D069 argv-password defect; placeholders for device/port.
- **Identity-before-reads** — staticBase, base=SB+slide, magic, version
  all verified before the first policy-list read; torn-header
  double-read; zero-return declared to fail toward "absent" (the
  conservative direction for this discriminator).
- **Non-approval preserved** — "ready to present; still requires the
  user's named grant."

## Scope ambiguities

- **S1 — device-side dependency identity unpinned.** The script
  `CDLL`s `/var/jb/basebin/libjailbreak.dylib` at a fixed path with no
  hash/identity check. `sym-*` only catches *absent* exports, not
  changed semantics of an existing symbol. D069 pinned this dylib's
  sha256; the grant should too, or declare the acceptance. Same for the
  procursus `python3` binary (path named, identity not).
- **S2 — "read-only" covers kernel memory, not the arming act.**
  `jbclient_initialize_primitives` + `jbclient_root_get_physrw` are
  state operations (jailbreakd RPC + primitive arming) whose
  persistence scope is undeclared: process-scoped? jailbreakd-side?
  kernel bookkeeping? If arming outlives the python process, "one
  bounded read" underdescribes the run. One line declaring scope — or a
  post-witness — is missing.
- **S3 — "no syscall … is exercised" is overbroad.** Arming physrw and
  the sysinfo RPC necessarily exercise jailbreak machinery (at minimum
  a mach message to jailbreakd). The precise claim is "no MAC/TCC/
  setter syscall and no policy handler invocation" — tighten wording.
- **S4 — "Reads" enumerates kernel reads only.** The script also
  dlopens two dylibs, resolves `in_dll` symbols, and performs the
  sysinfo XPC RPC. Fine as mechanism, but label the list "kernel read
  set" so it isn't misread as total memory touched.
- **S5 — SSH principal unnamed.** `MACWS_DEVICE` placeholders hide the
  login account. `jbclient_root_*` implies an elevated context; root-
  over-ssh password auth is a materially larger grant than mobile and
  should be named explicitly.
- **S6 — no wall-clock bound.** `ConnectTimeout=8` bounds connect only.
  Loops are bounded (≤0x40×≤0x80); the primitive/RPC calls are not. A
  hung call has no timeout or kill plan.
- **S7 — mid-walk STOP semantics undeclared.** A STOP after some
  entry lines leaves partial sanitized output; the text doesn't say
  partial output is admissible or must be discarded. Declare: any STOP
  before the walk completes ⇒ the snapshot answers nothing (the STOP
  name itself is the only finding).

## Sanitization ambiguities

- **Z1 — residue-witness blind spots.** `find /var/jb/tmp /tmp
  -maxdepth 1 -type f` misses new *directories*, other scratch paths
  (`/var/jb/var/mobile/...`, CrashReporter/Jetsam dirs if python
  crashes), and is TOCTOU (create+delete between the two finds escapes
  detection). Expected residue is genuinely zero for a stdin-fed
  script, but the blind spots should be declared.
- **Z2 — host-key checking disabled without comment.**
  `StrictHostKeyChecking=no` + `UserKnownHostsFile=/dev/null` is
  prior-packet-consistent, but it hands SSHPASS to whatever answers.
  One acknowledged-risk line (or a pinned host key) is missing.
- **Z3 — stderr outside the stated contract.** "No raw KASLR-bearing
  pointers … in any emitted or saved line" — the script prints only to
  stdout; exception paths (D081 note 4) go to stderr as tracebacks.
  Verified low-risk (kernel VAs live only as python ints, never repr'd
  into exception messages; `o` is a userspace pointer), but stderr
  should be named inside the sanitization contract.
- **Z4 — SSHPASS env exposure scope.** `sshpass -e` puts the password
  in the process environment — readable by host-local env inspection
  and inherited by children. Standard for this tooling and prior
  packets; one scoping line ("host-local exposure only") closes it.
- **Z5 — emitted fields verified safe.** `entries_off` is
  slide-relative (= the static image offset, image-fixed, not a leak);
  counts, `*-ok` flags, names, presence flags carry no addresses.
  `ent<base` would print a negative offset — still safe.

## Witness ambiguities

- **W1 — `delta=0 … is valid` is too lenient.** Gen-10447 composites
  continuously (D070: seq 695→6055 same generation). Post-run delta=0
  is *anomalous*, not benign — "ready + delta=0" cannot distinguish a
  quiescent interval from a stalled pipeline. Require nondecreasing
  **and** flag delta=0 for investigation rather than unconditional
  pass.
- **W2 — no kernel-panic witness.** pid+lstart catches a reboot
  indirectly; an explicit panic-log/`last` check (or at minimum
  declaring reboot-detection as *the* panic witness) is missing. A
  partial device-level fault would be invisible to all current
  witnesses.
- **W3 — no process-lifecycle witness.** Nothing asserts the
  python3/ssh session exited (a lingering process could retain an armed
  physrw). A post-run `ps` for the runner closes it — ties to S2.
- **W4 — "absent" requires a completed walk.** The witnesses can't
  distinguish "walked 0x40 slots, no Quarantine row" from "walk
  truncated." Admissible-absence needs: `hdr:` line emitted + full
  entry list + no `entry-*`/`name-*`/`ops-*` STOP. Declare it —
  otherwise a silent partial walk reports a false absence.
- **W5 — per-entry reads not double-read.** Only the header is
  torn-checked; a policy unloading mid-walk could tear a single entry.
  Bounded by `mac_late=1` (no late load/unload expected) — worth one
  line rather than silence.
- **W6 — distinguishability matrix.** Current witnesses separate:
  not-run (no output) / read-failed (STOP name) / snapshot-produced /
  device-rebooted (pid+lstart). They do not separate: armed-primitive
  persistence (S2), lingering process (W3), panic-short-of-reboot (W2),
  residue outside two dirs (Z1).

## Authorization ambiguities

- **A1 — retry semantics.** "One bounded snapshot" + "no second run
  under a modified script" leaves open whether a STOP-terminated
  attempt may be retried with the *same* pinned bytes. Natural reading
  is one attempt total; say it.
- **A2 — userspace command set undeclared.** The witnesses themselves
  are device commands (`ps`, state-file read, `find`) outside the
  "kernel read set." Declare the complete userspace command whitelist
  (ssh plumbing, `ps -axo pid,lstart,comm`, the state read, the two
  `find`s, python3-via-stdin) so no additional diagnostics get
  improvised under the same grant.
- **A3 — desktop interaction exclusions implicit.** "No deployment,
  no restart …" covers the big items; add explicit "no event
  injection, no pixel capture, no sentinel file creation/removal" so
  desktop safety doesn't rest on inference.
- **A4 — presentation binding is correct.** Name+hash grant, void on
  byte mismatch, D069-precedent scope — sound. The grant should also
  name the **operator** (who runs it: user pastes vs agent executes)
  to close the "who holds the terminal" question.

## Verdict — do not approve as written

The packet is well-formed and far closer to contract than the D069
generation: the kernel read set is bounded, output is sanitized,
fail-closed terminals are real, and the interpretation bound is
honest. The gaps are *authorization-text* gaps, not script defects —
the pinned bytes can stay frozen. Smallest text changes before any
future presentation:

1. Pin `libjailbreak.dylib` (and python3) device-side identity or
   declare acceptance (S1); name the ssh principal (S5).
2. Declare arming persistence scope + a post-run process-exit witness
   (S2/W3); add a run wall-clock bound and kill plan (S6).
3. Widen the residue witness (directories, crash-log paths) or declare
   the blind spots (Z1); one acknowledged-risk line for disabled
   host-key checking (Z2); stderr inside the sanitization contract (Z3).
4. Strengthen the composite witness: nondecreasing-required,
   delta=0-flagged (W1); name reboot-detection as the panic witness or
   add a panic-log check (W2); define admissible-absence =
   completed walk (W4).
5. One attempt total (A1); userspace command whitelist (A2); explicit
   no-injection/no-capture/no-sentinel exclusions (A3); named operator
   (A4).

None of these requires executing anything; all are edits to the
authorization text around the frozen script.
