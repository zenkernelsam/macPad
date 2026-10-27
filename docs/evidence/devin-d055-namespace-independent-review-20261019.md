# D055 — namespace-control & errno-provenance independent audit

Date 2026-10-19, packet `coordination/_relay/devin-055-frontend-task.md`.
**Read-only review of D054 raw outputs + helper ABI + errno semantics;
design review of the D055 same-process chroot-before/after query. No
device, no resend, no backend contact.**

## 1. D054 helper — ABI/hygiene verified from source

`analysis/devin-d054/macws_d054_pathprobe.c` (`d8245b71`):

- `errno=0` immediately before each call, captured into a local
  immediately after — no stale-errno conflation; `rc` and `errno`
  reported separately; `buf` printed only when `rc>0`. **Correct
  errno discipline.**
- `weak_import` + direct calls — no dlsym'd fptrs (D052-v1 PAC fault
  documented). Matches tccd callsite `0x4b9d8` (`&token, buf, 0x400`)
  and the source ABI (`libproc.c:286`: `val[5]=pid`, `val[7]=pidversion`
  → `PIF_COMPARE_IDVERSION`).
- `task_for_pid` denial → recorded missing, never synthesized;
  `proc_pidpath(pid)` runs before token acquisition — a denied token
  still yields the pid→path datapoint. Good ordering.

## 2. Reconciliation — what D054 observed vs my source table

Raw results (verbatim from capture):

| query | caller ctx | result |
|---|---|---|
| self `/var/jb/tmp` (byPID + byToken) | native | **rc=164** → `/private/preboot/CFD92CED…/dopamine-…/procursus/tmp/…` |
| platform 864 byPID | native | `/usr/libexec/UserEventAgent`; token unobtainable (tfp denied) — recorded missing |
| macwshostd 897 (byPID + byToken) | native | **ENOENT both** |
| live hold 28769 byPID | native | resolves → preboot path |
| `ls /private/preboot` under `launchdchrootexec` | chroot | **ENOENT — mount absent in chroot** |

**Reconciliation (packet-required, unresolved — not both-proven):**

- My xnu-8792.81.2 error map says NULL/dead textvp, stale pidversion,
  namecache-miss → **ESRCH**; ENOENT only from `vnode_lookup` in the
  caller's vfs context (or the kernel-context retry after EACCES).
- Backend's "macwshostd 897 = dead exec vnode (deploy inode
  replacement)" is **consistent but not proven**: a replaced inode can
  keep a namecache entry whose global lookup then fails → ENOENT fits;
  but so does any stale-name→missing-path condition. Same errno, two
  producers. Under my model, a *natively-queried* ENOENT (897) means
  "resolved name no longer resolves globally" — narrower than
  "dead vnode" but overlapping.
- **Version-drift caveat absorbed**: 8792.81.2 source vs device
  8792.82.2 — the errno→site map is a source model, not verified
  against the running kernel text. The .82.2 revision could shift
  specific legs; hold "ENOENT⇒lookup-failure" as RE-informed THEORY.

## 3. The mechanism the evidence now supports

`vn_getpath_fsenter` resolved the `/var/jb` vnode to its **global**
name (`/private/preboot/…` — proven by self-call success); `vnode_lookup`
of that name under tccd's chrooted root (`/var/mnt/rootfs`) finds no
`private/preboot` → ENOENT. tccd's own `ls` witness confirms the mount
is absent in that namespace.

**Consequence the backend's next experiment must carry:** the resolved
name is *global*, so even a binary living **inside** `/var/mnt/rootfs`
(e.g. `…/rootfs/tmp/X`) resolves globally to `/var/mnt/rootfs/tmp/X` —
which under the chroot becomes a double-rooted path
(`rootfs/var/mnt/rootfs/tmp/X`) → predicted ENOENT too. A chroot-
resident trusted helper likely *still* fails stage-2 unless its
namecache path happens to exist under the rootfs prefix inside the
rootfs. Worth one line in their plan before spending the trust
admission.

(No path-alias recommendation: stage-2's lookup is existence-only —
`vnode_put(nvp)` never compares vnodes — a same-name placeholder inside
the rootfs would satisfy it while leaving identity wrong; per packet,
not proposed.)

## 4. D055 same-process A/B — design review

**Their shape is right:** fork a bounded child of the already-running
helper; `chroot("/var/mnt/rootfs")` + `chdir("/")` in the child; repeat
identical queries. No second exec, no chroot-exec trust admission —
the child inherits the parent's textvp (same vnode class tccd couldn't
resolve). Child exits instead of un-chrooting.

**Sharpest form (recommended to backend):** the child queries the
*parent's* live pid — literally one identity, two caller contexts —
plus its own pid and (optionally) a platform pid:

| probe | parent (native root) | child (chrooted) |
|---|---|---|
| `proc_pidpath(parent_pid)` | path | **ENOENT predicted** |
| `proc_pidpath_audittoken(&parent_tok)` | path | **ENOENT predicted** |
| `proc_pidpath(self_child_pid)` | n/a (child only) | ENOENT (same class) |
| `proc_pidpath(platform_pid)` | `/usr/libexec/…` | **may still resolve** — `/usr/libexec/UserEventAgent` exists in the macOS rootfs → existence-only lookup can pass spuriously; record, don't call it success |
| stale saved token (exited pid) | ESRCH | ESRCH — negative control |

**Matched success→ENOENT = caller-namespace mechanism proven for this
API/build.** Remaining unproven even then: whether tccd's `#2` was
emitted by the kernel call or a tccd userspace post-validation stat —
the log attributes errno to the call, but tccd's code could `stat()`
the returned path in its own root and reuse the error label
(sub-THEORY, only separable by seeing which leg ran inside tccd).

## 5. errno-vs-errno distinctions (packet asks)

- **API errno**: `__proc_info_extended_id` retval<0 → wrapper leaves
  errno, returns 0; `rc>0` = strlen. Capture errno immediately.
- **Userspace path-validation errno**: if tccd stats the path itself,
  ENOENT could come from that — distinguishable only inside tccd; our
  A/B can't split it, only bound it.
- **Stale errno after success**: `rc>0` does not clear errno — any
  `#N` printed after a successful call is a later call's error. Helper
  correctly zeroes errno pre-call; that discipline is what makes the
  `#2` readings trustworthy.

## 6. Race/namespace/vnode assessment

- **pidversion race**: token carries `val[7]`; exit→reuse →
  `PIF_COMPARE_IDVERSION` mismatch → **ESRCH** — distinct from ENOENT,
  and the self/parent-live-pid design removes the race window anyway
  (sync call while alive).
- **Namespace**: only the caller's vfs context differs — token bytes,
  pid, vnode identical across the A/B. Any ENOENT delta is therefore
  attributable to context for this build — *not* generalized to all
  senders or all tccd failures (packet bound kept).
- **Vnode**: bind spelling irrelevant (same vnode — D053); dead/replaced
  vnodes are a separate ENOENT producer (897) — unresolved, kept as its
  own class.
- **Logging**: REQUEST line INFO-gated, ERROR near-always on — absence
  of REQUEST ≠ absence of request; the correlated ERROR pair is what
  attributes an EMPTY reply (D053 rule carried forward).

## 7. What remains missing / pending

- Whether tccd emits `#2` from the kernel call vs its own post-call
  stat — needs the sub-THEORY check (a chrooted-context replication is
  the cheapest witness; same-process A/B already covers it).
- Platform/rootless-daemon token controls stay **missing** — tfp denied
  on both classes; never synthesized.
- D055 backend execution artifacts not yet landed at review time —
  pending inputs: child stdout before/after chroot, parent's matching
  queries, errno/rc pairs, desktop health lines.

(handover to codex now)
