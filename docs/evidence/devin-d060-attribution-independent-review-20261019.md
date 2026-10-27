# D060 — attribution-branch independent audit + #103 witness identity

Date 2026-10-19, packet `coordination/_relay/devin-060-frontend-task.md`.
**Local read-only. Binary: `analysis/d050-tccd/tccd_arm64e` sha256
`039f64b7…` UUID `9845D17F-0409-3884-8A17-260C729636E5` (the arm64e
slice backend audited; `tccd_22F82_live` `42c388e8` is the same image
fat). No device/TCC/setters.**

## 1. D059 report corrections applied (history retained)

- Endpoint literal replaced with `${MACWS_DEVICE}:${MACWS_DEVICE_PORT}`
  placeholders in both the doc and result — no credentials persisted.
- "883 collision = existence-only" softened: identical spelling alone
  does not prove which inode the chrooted lookup hit or the lookup
  implementation; the relative-form contrast on 10300/10447 is the
  actual witness. macwshostd 897 marked unproven (any stale-name
  condition shares the errno). "#103 sole blocker" and
  "both-gates readiness" phrasing removed — stage-2 path success is
  compatible with, not proof of, the stage-1 state.

## 2. D057 CFG claims — independently verified on the binary

Selector resolution via fixed-selref stubs (each stub = one selector,
decoded `0x08`-tagged selref pointers → methname file offsets):

| callsite | stub | selector | role |
|---|---|---|---|
| `0x66d4` `[x22+0xb30] {x21 msg, sp+0x118 out}` | `0x59760` | `serviceFromMessage:error:` | builds the **service** object x25 — runs *before* the flag checks |
| `0x66f0` | — | `cbz x25 → 0x6824` | nil service → flag-check leg |
| `0x66f8` x25 | `0x58560` | `macos_isPerSystem` | service flag → w28 (`0x670c cbz w28 → 0x69e8`) |
| `0x6704`, `0x6828` `[x22+0xb30]` | `0x58580` | `macos_isSystemServer` | member/manager flag — `tbnz bit0 → 0x6838` / `cbz → 0x6ba0` dispatch |
| `0x683c` | — | `alloc` class-ref `0x8e420` = **TCCDAttributionChain** (corrected D061: class_ro name fileoff `0x78c03`; D060's `TCCDAccessIdentity` label was wrong) | |
| `0x6848` `x2=x21` | `0x57c60` | `initWithMessage:` | attribution builder — kernel-provided peer token, not dict-forgeable (D061: builder class = TCCDAttributionChain) |

IMP `0x10002d0e4` = `ldrb w8,[x0,#0x29]; and w0,w8,#1; ret` ✓ — the
`+0x29` bit0 byte flag, per-object. Setter `0x2d0f0`
(`strb w2,[x0,#0x29]`) exists — `setMacos_isSystemServer:` is a real
property; where it's set remains unmapped (open detail carried from
D057).

**Refined CFG** (corrects both earlier partial reads):
- `serviceFromMessage:error:` (0x66d4) precedes both flag gates —
  service resolution failure lands on 0x6824 too.
- `macos_isPerSystem` (service property) is a *second* gate distinct
  from `macos_isSystemServer` (manager/member property) — leg
  selection is two-flagged, not single.
- Attribution (`initWithMessage:`) precedes `function` strcmp
  dispatch ✓. The `evaluateResponsibility:` /
  `:evaluateResponsibility:processInfo:` selnames exist
  (`0x6d0ce`/`0x6d0f6`) — call sites unmapped (consistent with
  backend's open item).

## 3. #103 witness identity — belongs to a probe, not current WS

The only recorded `#103` (D050 capture): `sender_pid=46525`,
`msgID=46525.1` — the R002-era **chroot-spawned probe** of that
generation, described in D050 as `auid=-1`/unregistered. **No witness
ever attributed #103 to the WindowServer sender itself** — WS's posts
showed zero tccd log lines in their windows (1002 denials; a local
refusal can't prove tccd reached the responsibility leg at all).
Generation discipline: probe pid 46525 ≠ WS 10447 ≠ tccd 10300 — no
cross-generation carry claimed.

## 4. Skipped-vs-evaluated responsibility — the discriminator

Verified structure supports a clean distinction:

- **leg skipped** (isSystemServer=0 path → `0x6ba0` direct dispatch):
  the request reaches the function handler → reply carries handler
  output (`auth_*` keys or a function-specific error).
- **leg evaluated, responsibility failed**: `initWithMessage:` bails
  → `(#103)` ERROR + unconditional empty `x23` — the observed shape.
- **leg evaluated, responsibility succeeded**: builder returns
  non-nil → proceeds to dispatch — no `#103` ERROR line.

So for any future sender: correlated `(#103)` line + empty reply =
evaluated-and-failed; empty reply WITHOUT the ERROR on a known
sysserver connection = builder bailed for another reason (path,
token format); non-empty = dispatch reached. Absent log lines prove
nothing either way (INFO gating).

## 5. Launch-identity audit — what survives

Three preserved binaries all carry SETEXEC (`w1=0x40`→setflags;
source `main.m:119/133`, in-tree since d4160fc). The live WS job is a
direct `UIKitApplication:` launchd job whose ProgramArguments exec
`launchdchrootexec` → under deployed-SETEXEC, WS runs as the
registered job pid. **The deployed binary's flags stay unverified**
(no on-device pull hash exists) — do not infer from symbol or source
alone. If installed carries SETEXEC, WS's `#103`-class failure (if it
ever reached the leg — §3 says unwitnessed) would be a provider-side
registration question, not a spawn-structure one.

## 6. Minimal next discriminator (all local, zero device)

1. Map every `initWithMessage:evaluateResponsibility:` callsite
   (selrefs decoded the same way — the stubs for those selnames are
   enumerable); if any caller passes evaluate=0 for the sender
   connection, a responsibility-skipping leg exists in-code —
   distinguishing "skipped" from "failed" for a given sender.
2. Find the `+0x29` setter callsite (`strb w2,[x0,#0x29]` at
   `0x2d0f0`) — per-instance init vs per-connection classification
   decides whether any of our senders could ever take the
   non-attributed path legitimately.
3. Cross-check `responsibility_get_responsible_audit_token` provider
   (`0xb6` dispatch, D056-BK) against `p_responsible_pid` kernel
   field semantics (xnu `kern_fork.c:1096` fork-inherit,
   `kern_proc.c` KPI) — the missing witness is whether iOS launchd
   populates it for job classes like `UIKitApplication:`.

Missing inputs, exact: installed `launchdchrootexec` hash/flags
(device pull); a tccd-log window containing a WS-originated request
(does WS's sender ever reach the leg?); selref→callsite map for the
`evaluateResponsibility:` variants (mechanical, this packet's method).

(handover to codex now)
