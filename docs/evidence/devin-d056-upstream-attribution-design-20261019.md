# D056 — upstream attribution compatibility design (local-only)

> **CORRECTION (D057, same day):** the "Launch contract" finding below is
> wrong. `launchdchrootexec/main.m:119` (`spawn_flags = POSIX_SPAWN_SETEXEC`)
> and `:133` (`posix_spawnattr_setflags`) are ACTIVE code — the commented
> `setflags` at :114 is dead. `posix_spawn` + SETEXEC execs in place: the
> launchd-tracked pid becomes the macOS process; `return 1` after
> `posix_spawn` is the failure path. Verified in three preserved binaries
> (`mov w1,#0x40` at the setflags site; `0xc0` only under
> `MACWS_SUSPEND_AT_EXEC`). The "orphaned stray" claim and the
> "(a) SETEXEC is the missing fix" design are retracted — SETEXEC is already
> present. The "architecturally unreachable" verdict is also retracted as
> over-broad: it rested partly on the retracted premise, and per the
> share-review warning a single mechanism does not support a universal
> impossibility claim. Stage-1's ENOPOLICY cause is re-opened. See
> `docs/evidence/devin-d057-launch-indirect-audit-20261019.md`.

2026-10-19, packet `coordination/_relay/devin-056-backend-task.md`.
Local analysis only — no device actions, no TCC traffic, no trust-list
writes, no source/Git changes. D053–D055 runtime facts, R004/R005
artifacts, and local disasm are the inputs.

## 1. Facts assembled (all RE/runtime-confirmed, this round verified)

**Launch contract** — `launchdchrootexec/main.m` (repo source):
- `chroot(rootPath)` + `chdir` happen in the *wrapper process*, then
  `posix_spawn(&child_pid, execPath, …)` spawns the macOS binary as a
  **new pid**, then the wrapper `return 1` — the launchd job's tracked
  process **exits**; the macOS child is orphaned/reparented to launchd
  as an untracked stray.
- `posix_spawnattr_set_launch_type_np(attr, CS_LAUNCH_TYPE_SYSTEM_SERVICE)`
  is propagated only when `getppid()==1 && XPC_SERVICE_NAME` — that
  attribute carries the **AMFI launch-constraint class**, NOT
  responsibility registration.
- `POSIX_SPAWN_SETEXEC` exists in the file as a commented-out flag —
  the in-place-exec variant was considered and dropped.

**Responsibility getter** — Ventura `libquarantine.dylib`
(`A2D17FF6…`, extracted slice, disasm at `0x18b320b00`):
- Wrapper copies the 32-byte token, extracts `val[5]=pid` and
  `val[7]=pidversion`, calls a dispatcher helper with selector
  `w1=0xb6` — a kernel/provider query keyed on the **pid identity**,
  not on any message-supplied field. ENOPOLICY(103) = no
  responsibility-policy entry for that pid.
- Confirmed matching behavior: launchd-job-tracked iOS sender passed
  stage-1 (D052); chroot/forked descendants and R004's non-job tokens
  all returned 103. **Responsibility entries follow launchd job
  tracking of the exact pid — not inherited through posix_spawn.**

**Stage-2 lookup** — xnu-8792 (D054-FE review): `proc_pidpath_audittoken`
builds a *global* namecache path, then re-resolves it under
`vfs_context_current()` — the caller's root. Existence-only check,
`vnode_put` immediately — **the looked-up vnode is never compared to
the sender's real textvp.**

**Double-root consequence (this round's correction to my D055
"next experiment")**: a sender exec'd inside the rootfs has global
path `/var/mnt/rootfs/…`; tccd (root=`/var/mnt/rootfs`) re-resolves it
as `/var/mnt/rootfs/var/mnt/rootfs/…` → ENOENT anyway. Frontend's
double-root warning is correct — placing the helper inside the rootfs
is NOT a remedy. Stage-2 passes only when the sender's *global* exec
path spells a name that also exists at rootfs top level.

**Interposition audit** — `mac_hooks.m` DYLD_INTERPOSE list: `mach_msg`,
`mach_msg_overwrite`, `mach_msg2_internal`, `mach_port_construct`,
`mach_vm_map`, `mach_make_memory_entry_64`, `vproc_swap_string`,
`audit_token_to_asid`, `audit_token_to_auid`, `auditon`,
`getaudit_addr`. **No `proc_pidpath*`, `responsibility_*`,
`task_info`, or `getpid` interposes exist** — tccd's stage-1/stage-2
calls reach genuine kernel APIs; the errors are real.
(Recorded honestly: `audit_token_to_auid_new` writes `val[0]=501`
and the `auditon`/`getaudit_addr` fills synthesize auid/asid — a
diagnostic-era scaffold that forges *logged* audit identity; it does
not touch responsibility registration but it does mean
`sender_auid=501` in tccd logs is interpose-forged, per D053.)

**Protocol surface** — tccd contains the full indirect schema:
`TCCAccessRequestIndirect`, `indirect_object_token`,
`indirect_object_identifier(_type)`, `indirect_object_type`,
`AUTHORIZATION_SUBJECT_CREDENTIAL_DICTIONARY_KEY`,
`AUTHORIZATION_SUBJECT_IDENTITY_DICTIONARY_KEY`,
`responsible_pid/path` keys, and `responsibility.set-arbitrary`
entitlement strings — a protocol-native brokered-request path exists;
its admission gates are not yet enumerated.

## 2. Design comparison

### (a) Authentic launchd-owned macOS process (spawn-side fix)

Change the wrapper to exec-in-place (`POSIX_SPAWN_SETEXEC`): the
launchd job's tracked pid **becomes** the macOS process — same pid,
same pidversion, genuine vnode — responsibility registration then
legitimately survives to the macOS binary. Real fix for stage-1;
no identity fabrication.

- Cost/gap: changes the launch contract — the job currently exits
  `return 1` after spawn; under SETEXEC launchd tracks the (chrooted)
  process directly, which alters KeepAlive/exit-status semantics for
  every existing job plist. Per-job review required; this is the only
  legal propagation but is NOT deployable without a job-contract audit.
- **Does not fix stage-2**: the macOS sender's exec path is still
  `/var/mnt/rootfs/…` globally → double-root ENOENT.

### (b) Scoped native-side identity/path broker

An iOS-native launchd daemon that vouches for chroot clients via
`TCCAccessRequestIndirect` + subject credential dict:

- Broker stage-1: passes (launchd-tracked pid — D052 proves).
- Broker stage-2: needs the broker's *global* exec path to exist in
  tccd's root → only if placed at a **namespace-symmetric canonical
  path** (same binary bytes at `/usr/libexec/macwsbroker` iOS-side and
  `rootfs/usr/libexec/macwsbroker`). Honest variant, not path-aliasing
  a foreign binary: the file tccd validates IS the program that sent
  the request. BUT `/usr/libexec` is on the read-only iOS system
  volume — no legit writable iOS path currently verified to both
  exec-admit AND collide with a rootfs-canonical spelling. Executable
  viability unproven.
- Admission gates unknown: whether the Indirect handler demands
  private entitlements (`responsibility.set-arbitrary` /
  `com.apple.private.tcc.*`) — unverifiable without handler RE;
  implementing on a guess is out of bounds per packet.
- Semantic gap: even if admitted, whether Indirect evaluates the
  *subject's* identity through the broker's chain (its purpose) or
  re-derives the subject's own responsibility/path (which would fail
  for chroot subjects identically) is unknown.

### (c) Kernel-supported namespace-relative lookup

None. `proc_pidpath_audittoken` is absolute-path, existence-only,
caller-context — there is no userspace knob for a namespace-relative
or vnode-compare variant. `vfs_context_kernel` is internal.
**No legitimate option exists in this class.**

## 3. Verdict on reachable outcomes

- Chroot-resident senders (WindowServer, apps): stage-1 fails today
  (#103); even with (a) applied they still fail stage-2 (double-root).
  **No populated verdict reply is architecturally reachable for them
  while tccd stays chrooted.** This should be accepted as a
  structural bound, not chased.
- Running tccd *unchrooted* would invert stage-2 (global paths
  resolve natively) but breaks TCC's data semantics (it would read the
  iOS TCC store, not the Ventura one) — wrong-layer trade, rejected.
- Remaining honest avenue: (b)'s brokered Indirect — gated on two
  read-only-checkable unknowns below.

## 4. Smallest discriminating next experiment (zero device, read-only)

Disassemble the `TCCAccessRequestIndirect` handler (`0x6fc0 →
0x100011aa4`) in `analysis/d050-tccd/tccd_22F82`:

1. Which message keys does it consume (`indirect_object_token`?
   subject credential/identity dict? entitlement check on the
   connection audit)?
2. Does it run the attribution chain on the *subject* token
   (→ chroot subjects still fail) or attribute via the *broker's*
   chain (→ broker design viable)?
3. Any entitlement gate we cannot honestly satisfy → (b) dies here
   and the honest final answer is "unreachable in current shape".

Second sub-experiment (device, zero TCC): enumerate writable iOS paths
whose spelling is canonical inside the macOS rootfs AND that admit
exec of an owned binary — the broker's placement constraint. Both
experiments leave the desktop untouched.

## 5. Explicit non-goals recorded

- No fake path satisfying the existence-only lookup (foreign-binary
  alias = illegitimate; a *symmetric-canonical copy of the same
  program* is the only defensible placement and still unverified).
- No fabricated responsibility entries, forced non-empty replies,
  auid/identity setters.
- No repeating the rootfs-helper placement — superseded by the
  double-root proof.
- raw1/impl_rc=0 remains collapsed; observer remains undeployed;
  helper-level results are device witnesses, not universal.
