# D053 — independent audit: native-sender result & ENOENT attribution

Date 2026-10-19, packet `coordination/_relay/devin-053-frontend-task.md`.
**Read-only review of saved `analysis/devin-d052/` artifacts + tccd
disasm. No device, no resend, no backend contact, no source/Git changes.**

Artifacts reconciled (on-disk, re-hashed):
`macws_d052_sender.c` `77dd7d67…` source; `com.macws.d052sender.plist`
(user/501 job, `ProgramArguments=/var/jb/tmp/macws_d052_sender`,
RunAtLoad, no UserName → euid=0, no KeepAlive);
`d052-device-capture.txt` + `d052-oslog-segment.txt` (ANSI-stripped
witnesses); device-signed binary `1cc8636e…` recorded in capture.

## 1. Request/reply/error classification — from the server log itself

- Request well-formed & parsed: REQUEST_MSG echoes all 7 keys verbatim;
  `target_token={pid:27108, auid:-1, euid:0}` byte-matches the helper's
  raw `task_info` self-report (`SELF= ffffffff … 000069e4 … 0003c271`).
- Reply `type=dictionary count=0` — the unconditional empty `x23`
  (D050 `0x69e0` send).
- Server failure signature for this sender: `proc_pidpath_audittoken()
  failed from PID[27108]: (#2) ENOENT` → `Failed to create attribution
  chain` — the ERROR pair appears in-window; REQUEST (Notice) +
  REQUEST_MSG (Info) + both ERRORs all captured, so the capture did
  cover the INFO gate for this subsystem/window.
- **Absent `#103`** for the launchd-spawned sender is *indirect* evidence
  stage-1 (responsibility) passed — precisely: absence of the ERROR line
  bounds the bail; downstream progression (the stage-2 `pidpath` ERROR
  fired instead) supports it. Not a client-side responsibility proof
  (getters remain `SYMBOL_MISSING` on iOS — recorded, unsubstituted).
- Three pre-send crashes (raw dlsym-fptr PAC fault; xpc_copy_description
  null-node; `&_xpc_type_null` as object) = **helper failures** — no
  request left the process; correctly excluded from server evidence.

## 2. `sender_auid=501` — forged value cross-validated both directions

- Ventura tccd `10300` (injected): `sender_auid=501` for the iOS helper
  whose raw token shows `auid=-1` — the print goes through interposed
  `audit_token_to_auid` (`mac_hooks.m:13721` → always 501; stub call at
  `tccd_arm64e 0x65b8`).
- Native tccd `883` (not injected), same window: `sender_auid=-1` for
  UserEventAgent — the *same* log statement printing the genuine value.
- Conclusion: the 501 print is sender-independent interpose output —
  not "connection-inferred from domain". And `auid=-1` on a stock
  platform daemon's own token proves **auid was never the differing
  axis** — hardening the correction both ways.

## 3. ENOENT producing function — exact site

`tccd_arm64e 0x4b9d8`: `proc_pidpath_audittoken(x0=&token, x1=buf,
w2=0x400)` inside the `TCCDProcess`/attribution-ctor family (same
0x4b9xx/0x4fb48 region that also holds the responsibility getter).
Failure → `0x4cef8` log → attribution-chain bail → empty send.

Semantics: the SPI wraps `__proc_info`-family path resolution — kernel
walks the sender's **exec text vnode → namecache → path**. `#2 ENOENT`
means no resolvable pathname for that vnode at call time.

**Honest producer set for ENOENT on a live pid** (not established to
any single cause):

- vnode's namecache entry absent/evicted for the bind-resolved backing
  path (bind exec shares the underlying vnode — resolution is of the
  *vnode*, not the mount spelling);
- trust-admitted exec flagged such that textvp path is suppressed
  (jailbreak-trust artifact);
- unlinked text vnode (deleted/replaced post-exec — *mostly* excluded
  here: `ldid -S` preceded exec per build order);
- any platform-specific namecache restriction in `proc_pidpath` path
  build.

**ENOENT alone does NOT distinguish bind vs trust vs unlink** — that
boundary is exactly the packet's required correction, now anchored to
the producing function.

## 4. Canonical-path A/B — assessed

**Design flaw found:** `/var/jb/tmp/macws_d052_sender` and its backing
`/private/preboot/…/procursus/tmp/macws_d052_sender` are **the same
vnode** — the kernel resolves vnodes, not argv paths. An A/B that only
changes the mount spelling exercises the identical textvp → guaranteed
same ENOENT → discriminates nothing.

A **different file at a different fs location** (e.g. `/private/var/
mobile` copy) does create a new vnode — but then two things change:
vnode identity AND exec-trust context. Feasibility prerequisite:
exec-admission at that path is **unverified** — D048's evidence shows
`/var/jb` path-trusted after `ldid -S`, `/tmp` untrusted even with
cdhash. If `/private/var/mobile` isn't exec-trusted, the job fails at
posix_spawn = helper failure, not server evidence. The binary cdhash
stays identical (content-keyed), so signing context is preserved —
that's the one clean part.

## 5. Minimal next observation (zero TCC traffic)

Before any resend: **self-call `proc_pidpath_audittoken` from a
`/var/jb`-exec'd helper** (same disposable class; no launchd job needed —
any `/var/jb` exec works; bounded, no server contact):

- self-call → ENOENT → kernel cannot resolve bind-exec'd vnodes
  generically → the tccd bail is downstream of a kernel-level property
  (bind/trust-admission artifact) — A/B-over-path spelling moot; fix
  direction = make the vnode resolvable (a canonical-path *install*,
  not spelling).
- self-call → path resolved → ENOENT is specific to tccd's call context
  (e.g., its own chroot/namespace interaction or token handling) —
  reopen server-context analysis.
- Optional second data point same helper: `proc_pidpath_audittoken` on
  a known `/var/jb`-resident daemon pid vs a platform pid — widens the
  cross-section with zero tccd traffic.

Only if kernel-resolvability is proven for the vnode does a resend
carry new information.

## 6. Native reference semantics (from preserved segment)

tccd[883] UserEventAgent request: 9-key SPI_VERSION=2 schema vs our
legacy 7-key — daemon accepts both. Its identity refusal still produced
a **populated 6-key reply** (`auth_value=0 auth_error_code=2 result=
false auth_version=1 auth_reason=1 auth_error_string`) — confirming
populated-reply = post-attribution signature; EMPTY = pre-Result bail.
Classification rule now evidence-backed: `auth_error_code` key present
⇒ reached identity stage; `count=0` ⇒ attribution-layer bail.

## 7. Confounders & limits preserved

- `sender_uid=0` genuine (euid not interposed); `sender_auid=501` forged.
- Sender-platform difference (iOS libxpc vs chroot) and schema richness
  (7-key legacy vs 9-key SPIv2) remain recorded confounders — daemon
  accepted the legacy form.
- `auid` axis retired; the operative axes are responsibility
  registration (stage-1, passed) and vnode path resolvability
  (stage-2, failed).
- No evidence whether `proc_pidpath_audittoken` ENOENT is
  vnode-namecache-specific vs trust-flag-specific; the A/B over mere
  spelling cannot establish it (same vnode). The self-call SPI probe is
  the required discriminator.
- No WS-side implication yet: `raw1`/`impl_rc=0` still collapsed; the
  observer is still undeployed; zero tccd lines for WS posts remains
  non-discriminating (INFO-gated REQUEST log).
