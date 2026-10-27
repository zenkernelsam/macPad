# D058 — SETEXEC binary identity + corrected launch-identity audit

Date 2026-10-19, packet `coordination/_relay/devin-058-frontend-task.md`.
**Local read-only: exact-binary flag audit + corrected responsibility
model. No device/TCC/trust writes.**

## 1. Exact-binary SETEXEC audit — verified

`launchdchrootexec/main.m` (repo source, SETEXEC present since initial
import commit `d4160fc`, 2026-09-23):

- `main.m:119` `short spawn_flags = POSIX_SPAWN_SETEXEC;` — **active**
- `main.m:114` older `posix_spawnattr_setflags(...)` call is inside a
  comment — dead code
- `main.m:128` conditionally ORs `POSIX_SPAWN_START_SUSPENDED`
- `main.m:133` `posix_spawnattr_setflags(&attr, spawn_flags)` — active

Disasm of the three preserved binaries — all set the flag:

| binary | sha256 | LC_UUID | setflags arg |
|---|---|---|---|
| `analysis/dyldwork/launchdchrootexec_bin` (reviewed D056) | `eecf7c3c…` | AEA33132-6DF7-3B62-8522-FEC402E8FE6B | `w1=#0x40` default; `0xc0` suspend path |
| `.theos/_/var/jb/usr/macOS/bin/launchdchrootexec` (staged Oct 18) | `1004c790…` | 685002B3-ECBD-3BCD-9D32-6384F5875472 | same `0x40`/`0xc0` pair |
| host-overlay `…/bin/launchdchrootexec` (Oct 15, deployed-share snapshot) | `438c6482…` | DC60AB9D-C4F8-3AC9-97F9-FD116D94320F | same pair |

`0x40 = POSIX_SPAWN_SETEXEC`, `0x80 = START_SUSPENDED`. Two
materialized immediates = compiler specialized the two flag values
into one setflags call site.

**Corrected semantics:** `POSIX_SPAWN_SETEXEC` execs the payload
*in the same process* — pid (and audit identity, which exec
preserves) unchanged. `posix_spawn` symbol presence alone proved
nothing; the flags do: **there is no orphan grandchild**. My D056
claim "payload is one hop below the registered pid → structurally
#103" is **retracted** (D056 doc corrected with retained history).

**Installed-binary caveat (honest):** every preserved build carries
the flag and the source predates all of them, but no on-device hash
witness exists in this packet — installed identity is
presumed-consistent, not proven.

## 2. What SETEXEC makes consistent — and the live #103 question

WS production plist `layout/usr/macOS/LaunchDaemons/
com.apple.WindowServer.plist`: iOS launchd job
(`UIKitApplication:com.macwsguide.windowserver`) →
ProgramArguments = `launchdchrootexec 0 0 /var/mnt/rootfs …/WindowServer`.
With SETEXEC, **WS runs as the registered job pid** — the launchd
registry should cover it.

Yet WS's requests observed `#103 responsibility`. So "pid is
launchd-registered ⇒ responsibility resolves" is **refuted as a
sufficient condition**. Candidate mechanisms (unresolved):

- the `0xb6` provider query (D056-BK: pid+pidversion keyed) consults a
  responsibility table that iOS launchd populates differently —
  spawn-attr `responsible` tokens rather than bare registration;
- `UIKitApplication:`-class jobs may be tracked in a different
  registry than the one the provider reads;
- deployed binary predates SETEXEC (unlikely — all preserved builds
  carry it — but unverifiable this packet);
- tccd's flag-gated path at `0x6708` means requests don't all take
  the responsibility leg — so d052's "passed stage-1" is itself
  indirect (a stage-2 ERROR could appear on a leg that skipped
  responsibility). Gate-per-leg attribution stays open.

## 3. Stage-2 with corrected model — in-root now predicted viable

fd_rdir-aware `build_path_with_parent` (verified on 8792.81.2 source;
device 8792.82.2 drift caveat retained): a chrooted caller's path
build stops at `proc_root_dir_vp` → vnodes under the caller's root
produce **chroot-relative** spellings (`/System/…`), which re-lookup
resolves. Consequences:

- WS's own exec vnode is in-root → its stage-2 path check would
  resolve for tccd — WS is blocked at stage-1 only.
- A real file placed under `/var/mnt/rootfs` (genuine owned inode, not
  an alias) exec'd by *any* launchd-registered sender → chroot-relative
  resolution → stage-2 pass.
- mac_hooks' own note (line 14892: both namespace-relative and
  host-prefixed spellings observed at runtime) is consistent with the
  fd_rdir model — the two spellings correspond to caller contexts.

## 4. Minimal next evidence — zero-TCC, zero-new-exec, design only

The in-root resolution is measurable **today** on existing processes,
no trust admission: the same D055 helper shape (one exec, forked
chroot child — authorized pattern already proven) queries:

| probe | prediction under fd_rdir model | discriminator |
|---|---|---|
| chrooted child `proc_pidpath(tccd_pid)` | `rc>0`, path `/System/Library/…/tccd` | **relative spelling** proves root-stop, not existence coincidence |
| chrooted child `proc_pidpath(WS_pid 10447)` | `/System/…/WindowServer` | same |
| native parent same pids | global `/private/var/mnt/rootfs/…` spelling | contrast |
| chrooted `proc_pidpath(platform_pid)` (e.g. UserEventAgent) | resolves ONLY if `/usr/libexec/UserEventAgent` exists in rootfs — existence-only pass | record, don't call success |

Discrimination rule: returned string **form** is the witness —
`/System/…` = fd_rdir-relative build (mechanism real); a global
spelling that resolves = existence coincidence inside the rootfs;
ENOENT = relative-build model refuted for that vnode. **This proves
namespace-relative path semantics, NOT launchd responsibility or
policy readiness** — separate gates.

For stage-1 propagation: query the responsibility SPI directly on WS
pid vs a direct-job pid — feasible only if
`responsibility_get_responsible_audit_token_for_*` resolves on iOS
(D052 helper reported both symbols absent in its link context →
reachability unverified; record missing if absent — never fabricate).

## 5. Future both-gates helper — precise prerequisites

A directly launchd-owned genuine in-root helper tests both measured
requirements honestly:

1. Real project-owned iOS-ABI binary placed at a real path under
   `/var/mnt/rootfs` (e.g. `tmp/`) — not an alias; the inode needs the
   project's recorded trust admission (coordinator decision).
2. iOS launchd job whose `ProgramArguments` execs it **directly**
   (no wrapper hop needed — and with verified SETEXEC even a wrapper
   chain would preserve the registered pid, but direct is cleanest).
3. The send itself is one bounded `TCCAccessRequest` — deferred;
   requires the §4 probes to first confirm in-root resolution and a
   resolved responsibility SPI.
4. Predicted: stage-1 registered pid + stage-2 relative path →
   populated verdict or the next *named* producer.

## 6. Boundaries

- No orphan/#103 causal claim retained; the spawn-hop mechanism is
  retracted and WS's #103 mechanism is now explicitly unresolved.
- In-root stage-2 viability is source-informed; the §4 table is the
  control that would measure it.
- No alias/fake-identity/fake-return designs proposed.
- Device/backend/Git untouched; D057 backend owns the Indirect-
  handler audit independently.

(handover to codex now)
