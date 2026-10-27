# D073 — go/no-go memo on the D072 responsibility/TCC approval packet

Read-only review against AGENTS.md and
`coordination/reviews/share-lessons-active-checklist-20261018.md`.
No device contact, no credentials, no syscalls, no source/Git changes.

## Verdict: CONDITIONAL GO — pending one in-run arm check + explicit task

The D072 packet satisfies the checklist's evidence discipline and the
AGENTS.md no-bypass rules: it is read-only, bounded, fail-closed,
sanitized, and it does not import any share-package mechanism. It is
approvable as specified **only if** the physrw-arm question is resolved
inside the same authorized run as a fail-closed precondition (P5), not
as a separate probe. If the arm check fails, the packet terminates
"missing input" — that is the correct terminal, per the frontend
contract's own wording.

## 1. Already closed by existing evidence (do not re-open)

- **Kernel identity chain**: device boot kernelcache byte-identical to
  repo img4 (SHA `78959dd9…`, D064); `kc_raw` = `xnu-8792.82.2~1/
  RELEASE_ARM64_T8103`; live `uname` matches.
- **Static impossibility**: zero `"Quarantine"` NUL strings in the
  image + `mpc_name` is a `const char*` into image memory + no
  post-boot kexts (`/System/Library/Extensions` absent) +
  `mac_late=1` ⇒ no *static* registration path for `"Quarantine"`
  exists this boot epoch.
- **Kernel dispatch model** (instruction-verified, D067/D068):
  precheck `ops+0x388`, name-match dispatch `ops+0x3a8`, literal
  ENOPOLICY producer `w21=0x67` @ `0x865de80`.
- **Namespace excluded** (R004): native live-token and chroot-token
  calls both return 103 through the real Ventura getter.
- **Route map** (D065): `com.apple.tccd.system` →
  `com.apple.macosbooter.tccd.system` → live pid-10300 endpoint;
  `__mac_syscall` interpose is AMFI-only and forwards Quarantine
  verbatim; per-system services cannot legitimately use a user-domain
  tccd.
- **Desktop health**: ready, producer=WS pid 10447, seq advancing;
  same generation preserved across D069 (5955→6055) — process state is
  a valid witness channel already on file.

## 2. Requires explicit user authorization (new device read)

- **The one bounded `mac_policy_list` snapshot** — the only remaining
  kernel-side missing input. Needs: SSH session, `SSHPASS` env
  credential, jailbreakd RPCs (`sysinfo` + `get_physrw`), and `kread64`
  reads of the list header + entries. Authorization must name the
  packet, the credential mechanism, and accept that `get_physrw` is an
  RPC *grant* (read-only, no state change, but it is a jailbreakd
  request — must be inside the authorized scope).
- **Contingency clause**: if physrw does not arm under procursus
  python's entitlements, the only alternative is a signed helper whose
  cdhash admission mutates the trustcache — **out of contract**; the
  run must then stop as "missing input," never degrade.
- **Frontend-side witness** (separate packet, not this one): the
  bounded sender-observation post. Out of backend scope entirely.

## 3. Locally answerable without a new read — residuals

- **Which policies have `ops+0x388` precheck set** is determinable
  *offline* per policy conf — but conf locations require either the
  PAC-aware fixup decoder (blocked, D072 §1a) or symbol-anchored
  location like AMFI's (one conf located, verified). Per-policy effort;
  optional, not blocking.
- **Precheck-as-#103-producer**: the error ladder's fixed candidate set
  `{EAGAIN,EINVAL,ESRCH,ENOENT,EACCES,EPERM,last-nonzero}` is
  image-verified; a hook returning ENOPOLICY propagates via the
  last-nonzero leg. So the precheck arm remains *technically* open —
  no local read closes it without knowing what Sandbox's precheck
  returns for name="Quarantine" (runtime behavior — not a static
  property; **cannot** be settled locally).
- **Dynamic-name registration edge**: only a PAC-aware stub decoder
  would enumerate all `mac_policy_register` callers offline. Bounded
  feasibility unknown; the live snapshot is strictly cheaper.

## 4. Evidence + cleanup contract for a future run

Before any device contact, the authorizing packet must carry:

- Credential via `SSHPASS` env only; placeholdered in docs.
- Executor already trusted (python3) — **zero trustcache mutation**;
  if a helper is ever required, its signing+admission must be named in
  the packet itself.
- Pre/post witnesses: WS+tccd pids & lstart,
  `macws_final_composite.state` seq before and after; generation
  unchanged ⇒ cleanup accepted.
- Output only as slide-relative offsets + presence flags (frontend §4).
- Stop conditions verbatim from D072 §1b; on any failure, emit the
  failing check name and nothing else.
- Cleanup: remove all staged files under `/var/jb/tmp`, verify zero
  leftovers with `find` (no globs — D069's glob-abort bug), confirm
  no owned process remains, record all command exits.
- Evidence doc must state the D069 precedent: nothing printed to
  stdout/stderr during this run may contain raw KASLR addresses.

## Provenance

D062–D065 (string scan, image identity, route map), D067/D068 (layout,
offsets, mock), D069 backend result + admin closeout + frontend
acceptance contract, D070 checkpoint, D071 spec, D072 approval packet.
No new analysis performed; this memo only organizes existing facts.
