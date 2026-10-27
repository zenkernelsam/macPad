# D048 — frozen observer candidate + isolated arm64e forwarding probe

Date 2026-10-19, packet `coordination/_relay/devin-048-backend-task.md`.
**Scope: frozen-source validation + disposable helper-level probe.
No production deploy, no WindowServer restart, no session interruption,
no marker changes, no 4th hook.** Desktop untouched throughout —
`macws_final_composite.state` read `state=ready producer=10447
sequence=2383→2443` (advancing during the work), watchdog 9845 alive,
zero diagnostic sentinels armed before and after.

## 1. Frozen final identities (D047 reviewed a stale snapshot)

D047 reviewed `.c d43c65d9` while final is `6641d9df`. Exact delta
between the two `.c` revisions: **one line — the audit-slot
double-deref fix** (`cred = *env->audit_slot` → `cred = **env->audit_slot`,
plus the `*env->audit_slot` NULL guard). The reviewed version read the
`gCurMsgAudit` cell *address* rather than its contents — the gate's
container scalars/token would have been garbage at runtime. Caught by
the D046 host test (post_orig scalar + record-field checks failed),
fixed in the same round; D047's review snapshot predated the fix.
Everything else in the `.c` (open gates, emit, invokes, errno
envelope, snaps counter) is byte-identical between the two snapshots.

Frozen final hashes (sha256, prefix-20):

| artifact | hash |
|---|---|
| `libmachook/MacWSSenderObservation.h` | `a7c8438b3b509ff6199d` |
| `libmachook/MacWSSenderObservation.c` | `6641d9dfdf23d7273b61` |
| `libmachook/mac_hooks.m` | `7321d92c530368b737e9` |
| `misc/test_sender_observation.c` | `39d6791e4e815c15082a` |
| `.theos/obj/libmachook.dylib` (fat) | `fddc64c19cde886795ee` |
| — arm64 slice uuid | `5B9F5C19-ACBF-3236-8C67-CD43D8C7C9CB` |
| — arm64e slice uuid | `353600B3-51EB-338E-9188-000DA92108B5` |
| `misc/macws_sender_obs_probe.c` | `f435057a140ec748a894` |
| `tmp/macws_d048_probe` (arm64e, ldid-signed on device) | `f00fc48c77bec04b1465` |

Frozen host suite re-run on these exact sources: **ALL PASS**
(exit 0), including the D046 additions.

## 2. Isolated arm64e probe — REAL invoke bodies on device

`misc/macws_sender_obs_probe.c` — disposable one-shot, built locally
`clang -target arm64e-apple-ios16.0` (iPhoneOS16.5.sdk), `-DMACWS_SENDER_
OBS_TEST` on the probe's own compile only (the production `.theos`
object provably never sees the macro — build log has no `-D` flag).
Mock originals stand in for the three boundaries; a nested fake gate
original calls the dispatcher invoke exactly as the genuine gate calls
the hooked dispatcher — including a real PAC-signed function-pointer
chain. Packet-owned paths `/tmp/macws_d048_probe_{sentinel,log}`
(self-cleaned); no SkyLight, no provider, no WindowServer contact.

**15/15 PASS on device** (iPad13,11, run from `/var/jb/tmp`):

- default-off: orig ran via PAC fn ptr, errno envelope intact, zero
  captures (`g_obs_snaps` flat), nothing stored; env hooks still ran
  (umbrella path preserved)
- armed nested gate→disp→impl: 3 records published, **shared
  invocation id**, CHAIN_ROOT on gate only, inner sites unflagged;
  verbatim fields (magic, 32B token, 40B desc, rc); `arg_ptr` = the
  exact function pointer passed (PAC identity preserved)
- errno: incoming EDOM seen by orig (restored pre-orig); orig's EBADF
  forwarded over hooks' EAGAIN/EFAULT clobber; post_orig received
  pre-orig euid/pid scalars
- log: 0600 regular, exact record bytes
- disarm: capture ceased (snaps flat), orig still ran, emit
  dropped-counted

**Ordering evidence worth recording for live analysis:** in a nested
chain the *inner* record emits first (each wrapper emits post-orig) —
live WS records will appear impl→disp→gate within one invocation id,
not gate-first. The first probe revision assumed gate-first ordering
and was fixed to locate records by site.

## 3. Trustcache findings (recorded admissions + cleanup)

- `/tmp` execs are killed regardless of signing: adhoc `ldid -S` +
  `jbctl trustcache add <sha1-cdhash>` (attempted `331fa007…` for the
  first probe binary — NOT present in the 4712-entry `trustcache info`
  listing afterward; dead admission attempt for a deleted file, no
  live residue) both failed with SIGKILL(137). `macws_boot_trust.py`
  reported `already-trusted` for the same file — it *restores* prior
  registrations, it does not grant new ones.
- `/var/jb/tmp` is path-trusted: the identical binary runs there
  after `ldid -S` alone. Disposable helpers belong under `/var/jb`.
- Cleanup: probe binary, `/tmp/d048_hw` hello-world, and both
  packet-owned sentinel/log paths deleted; no leftovers verified.
- SSH via authorized endpoint `root@<device>:2222`, `sshpass -e` with
  transient `SSHPASS` env only; no credential on argv/disk/docs; no
  background jobs left.

## 4. Reviewed future deployment plan (NOT executed)

Preconditions: coordinator-elected window, user informed, frontend
sign-off on this frozen snapshot (`.c 6641d9df`).

1. **Freeze** — the exact hashes in §1; rebuild = new review.
2. **Stage** — build fat dylib locally, `lipo` confirm arm64+arm64e
   slices + uuids; `ldid -S` on device (twice per runbook);
   `macws_boot_trust.py --readd` for the new cdhashes.
3. **Atomic publish** — `install` to a NEW inode adjacent to the live
   libmachook, never `cp` over the mapped file (per-vnode signature
   cache took a generation down before); the consumer picks it up on
   the *next* WS launch — this plan does not force one.
4. **Arm** — create `/tmp/macws_sender_observation` in the chroot
   namespace only, then start a watchdog-owned generation
   (macos_gui.sh's existing ownership; the running watchdog 9845
   supervises teardown).
5. **Exercise** — ONE canonical `SLSPostEventRecord` post (bounded
   probe, same as D041/D042's); read
   `/tmp/macws_sender_observation.log`.
6. **Accept** — match the D043 discrimination matrix on the emitted
   gate→disp→impl chain (now sharing one invocation id, arriving
   inner→outer). `raw1` (the TCC default-1 return) remains ambiguous —
   **three hooks cannot prove explicit policy denial**; they separate
   lazy-resolve failure from evaluated-preflight deny. A 4th
   TCC-image site for the raw `{0,1,2}` exists as frontend design
   only, explicitly out of this plan.
7. **Disarm+revert** — remove the sentinel (live disarm ceases
   capture mid-flight); verify `state=ready` + advancing sequence +
   visible desktop pixels; leave the staged dylib inert or remove it;
   teardown under the same watchdog ownership.
8. **Observation budget** — one bounded post + log read + disarm,
   minutes not sessions; no unbounded logging.

None of §4 was executed.

## 5. Residual limits (honest, unchanged)

- `write()` bounded in pass count not wall-clock (documented).
- Disarmed per-call cost = 2 `access()` syscalls + mutex (documented).
- Gate scalar reads persist while disarmed for the umbrella CGXSYNTH
  line — governed by that separate sentinel, documented boundary.
- Foreign-owner log rejection code-enforced, untestable single-uid.
- The three hooks never prove explicit TCC policy — raw `1` is the
  API default collapsing conn-NULL/empty-reply/verdict (RE-verified).
