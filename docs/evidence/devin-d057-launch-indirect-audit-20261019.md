# D057 — launch-premise correction + Indirect admission audit (local-only)

2026-10-19, packet `coordination/_relay/devin-057-backend-task.md`.
Read-only local work only: no device contact, no TCC traffic, no
trust/identity writes, no source/build/Git changes. Binary under audit:
`analysis/d050-tccd/tccd_22F82` arm64e slice, UUID
`9845D17F-0409-3884-8A17-260C729636E5`, sha256 `42c388e8…` (matches the
live chroot daemon fingerprint). `__text` va `0x100004588`/foff `0x4588`;
addresses below are file-relative offsets shown in prior rounds
(vaddr = off + 0x100000000).

## 1. Launch-premise correction (D056 retraction)

**Source**: `launchdchrootexec/main.m` — `short spawn_flags =
POSIX_SPAWN_SETEXEC;` at :119 is active; `posix_spawnattr_setflags(&attr,
spawn_flags)` at :133 applies it; the `setflags(SETEXEC|START_SUSPENDED)`
call at :114 is a dead comment. Optional `POSIX_SPAWN_START_SUSPENDED`
(0x80) is or'd in only under `MACWS_SUSPEND_AT_EXEC` + target match
(:127-:131). `posix_spawn` at :141 therefore **execs in place** — same
pid, the launchd job's tracked process becomes the macOS binary.
`perror`/`return 1` at :143-:144 is reachable only on spawn failure.
One Git revision exists (`d4160fc`, upload blob) — no pre-SETEXEC source
history in-repo.

**Binary cross-check** (disasm of the setflags callsite, all three
preserved copies): `mov w1,#0x40` unconditionally; `mov w1,#0xc0` taken
only through the suspend-env conditional — matching the source:
- `analysis/dyldwork/launchdchrootexec_bin` sha256 `eecf7c3c…`
- `.theos/_/var/jb/usr/macOS/bin/launchdchrootexec` sha256 `1004c790…`
- `analysis/share-review-20261015/extracted/host-overlay/…/launchdchrootexec`
  sha256 `438c6482…`

**Unknown kept unknown**: no preserved hash of the *device-installed*
`launchdchrootexec` exists in the artifacts I found, so whether the
installed binary is one of these builds is unverified. If it is, all
chroot-resident launchd jobs (tccd, WindowServer, etc.) run as their
tracked launchd pid — not as orphans.

**Consequences of the correction**:
- Retract: "orphaned stray", "spawn-die explains #103", "(a) SETEXEC is
  the missing fix", "architecturally unreachable".
- Stage-1 (`responsibility_get… → #103`) is now unexplained by launch
  mechanics: a tracked pid under SETEXEC carries the same launchd
  relationship D052's bootstrap'd native helper had — yet chroot senders
  fail stage-1 and the native one passed. Whether launchd's
  responsibility registration differs by job type/spawn path, or whether
  the provider query distinguishes the post-exec proc, is open — kernel
  internals stay source-informed, not live-RE'd (D055 boundary kept).
- Stage-2 (caller-namespace `vnode_lookup`, double-root) is unaffected
  by this correction — it was proven by a namespace-only variable in
  D055, independent of launch mechanics.

## 2. Exact admission order in the request handler

Connection handler (message receive path), verified CFG:

| Off | Op | Meaning |
|---|---|---|
| `0x6508` | `xpc_dictionary_create_reply` → `x23` | the one reply dict, created empty |
| `0x6824-0x6834` | `ldr x0,[x22,#0xb30]`; `bl objc_stub`; `cbz w0 → 0x6ba0` | **`[member macos_isSystemServer]` gate** — sel `macos_isSystemServer` (selref `0x8d658`→offset `0x6d9d2`); IMP `0x10002d0e4` = `ldrb w8,[x0,#0x29]; and w0,w8,#1; ret` — a per-object flag, not a policy computation. Return 0 → **skip attribution entirely, straight to dispatch** |
| `0x683c-0x6850` | `ldr x0,#0x8e420` (class global); `bl alloc`; `mov x2,x21`; `bl init-stub` (sel `initWithMessage:`); `cbz x0 → 0x69ac` | **sender attribution chain built from the XPC message, pre-dispatch**; nil → failure tail |
| `0x69ac-0x69cc` | `os_log_type_enabled` gate → cold thunk `0x7928` logs *"Failed to create attribution chain from message."* (cstr `0x70c88`); then `0x69cc` sends empty `x23` | the observed EMPTY-reply producer — unconditional after the flag check |
| `0x6894-0x68b8` | `"function"` (cstr `0x5dec5`) `xpc_dictionary_get_string` + first strcmp | operation-key extraction on success path |
| `0x6ba0+` | strcmp chain on `x20` (function) | `TCCAccessRequest` → `0x6d24`; `TCCAccessRequestIndirect` → `0x6fc0`; plus `TCCAccessSetPidResponsibleForPid` → `0x7b30` (re-checks `isSystemServer`), `TCCCreateDesignatedRequirementIdentityFromAuditTokenForService` → `0x7074`, `TCCCreateIndirectObjectIdentityForFileProviderDomainFromPath` → `0x721c`, `TCCAccessCopyInformation*` family, `TCCAccessSet*`, `TCCAccessReset*`, `TCCAccessDeclarePolicy`, etc. |

Key structural facts:

- **Attribution precedes dispatch** for `isSystemServer` connections.
  The three observed failures (R002, D052, D054-era logs) all bailed in
  `initWithMessage:` → nothing function-specific ran — consistent with
  the identical `count=0` shape across request types.
- **Genuine caller identity**: the builder input is the XPC *message*
  (x21) — the sender's audit token comes from the kernel-provided
  connection peer credentials, not from attacker-controlled dict keys.
  Dict fields cannot substitute a forged sender token.
- **`isSystemServer` is the only admission bypass at this level** and it
  is an object flag (`+0x29` bit0) read off `[x22+0xb30]` — plausibly a
  per-instance property of `tccd system` vs user-session tccd (the flag
  semantics/setter site were not chased this round — open detail).
- The failure tail is **not** a verdict path: nothing after `0x69ac`
  writes `auth_*` keys; the reply goes out as created.

## 3. `TCCAccessRequestIndirect` handler audit

Dispatch `0x6fc0`: `mov x0,x21 (msg); mov x1,x23 (conn); bl
0x100011aa4` — handler self-names in its os_log format:
`"do_TCCAccessRequestIndirect"` (cstr `0x5ee56`),
`"%s: processing incoming request"` (`0x72bee`).

Observed in its prologue/body (partial trace, `0x11aa4`-`0x11e00+`):

- Builds request state: PAC-signed block-method tables on stack,
  retained `msg`/`conn`, out-params.
- Consumes message keys: `target_csreq` (`0x5ee72`, via
  `xpc_dictionary_get_data`-style accessor → `csreq` blob),
  `target_identifier` (`0x5ee7f`), `target_path` (`0x5ee91`), and
  `function` (`0x5dec5`). These construct the **subject** identity from
  message-supplied fields — the brokered-request surface.
- Subject keys are *subject* material, not sender material: sender
  attribution already completed pre-dispatch from the connection peer
  token.
- `TCCD_MSG_SPI_VERSION` (`0x5e312`) gate exists on a sibling path
  (`0x6fd8` block — the non-attributed/dispatch-direct flow).

Not yet resolved (explicit gaps, not guesses):
- Whether `do_TCCAccessRequestIndirect` enforces an entitlement on the
  connection (`responsibility.set-arbitrary` / `com.apple.private.tcc.*`
  strings exist in the binary but their xref into this handler is
  unproven).
- Whether the subject's own responsibility/path chain is re-derived
  (subject-side `initWithMessage:`-equivalent) or attributed through
  the broker's chain — the discriminator for any broker design.
- Class identity of the `0x8e420` global used by `initWithMessage:` and
  which selector variant (`initWithMessage:` vs
  `initWithMessage:evaluateResponsibility:` vs
  `initWithMessage:evaluateResponsibility:processInfo:`) each call site
  uses — the `evaluateResponsibility:` variants prove a
  responsibility-skipping chain exists in-code; its call sites are
  unmapped.
- The `proc_pidpath_audittoken` IMP site `0x4b9d8` lives in function
  `0x4b8a0`; reached via ObjC dispatch, direct `bl` callers = none.
  Its method name/class wasn't resolved in this pass.
- Whether the `isSystemServer` flag is per-instance or per-connection,
  and what sets byte `+0x29`.

## 4. What this changes for designs

- **Sender-side attribution is a hard precondition to any request** on
  `tccd system` (if the flag is per-instance). The Indirect surface
  cannot bypass it — the coordinator's warning ("indirect cannot fix an
  earlier connection bail") is confirmed structurally: `0x6850` runs
  before `0x6bb8`.
- A broker design must therefore pass the **sender** chain itself
  (responsibility + caller-namespace path check) before its indirect
  payloads are evaluated — the broker needs both a registered
  responsibility identity and a global exec path resolvable in tccd's
  root (the D054/D055 constraints still apply to the *broker*).
- `TCCAccessSetPidResponsibleForPid` is a real operation in this
  daemon's table — meaning the protocol itself contemplates legitimate
  responsibility wiring; admission for it requires `isSystemServer`=1
  per `0x7b30`'s re-check. Whether our sender could use it is an open
  admission question, not an assumed capability.
- Rejected designs preserved: rootfs-helper placement (double-root,
  D055/D056), foreign-path aliases (existence-only check still
  illegitimate), SETEXEC-as-missing-fix (already present), universal
  impossibility claim (retracted).

## 5. Smallest discriminating next experiment (all local, zero device)

1. **Map the `initWithMessage:` selector family** (local disasm):
   enumerate the class behind `0x8e420` and all call sites of
   `initWithMessage:evaluateResponsibility:` — determine whether
   tccd's own internal paths ever build chains with responsibility
   evaluation off, and which token source each variant uses
   (connection peer token vs message `target_token` vs
   `indirect_object_token`).
2. **Trace subject attribution inside `do_TCCAccessRequestIndirect`**:
   does it call the `0x4b8a0` identity-ctor on the *subject* token —
   i.e., does a chroot subject still need path/responsibility to pass —
   or is the subject evaluated purely from message identity fields?
   This single branch decides whether a native broker can vouch for
   chroot subjects at all.
3. **`isSystemServer` setter**: find writes to `[obj+0x29]` bit0 —
   per-instance constant or per-connection classification; determines
   whether a native broker connection could ever take the
   non-attributed dispatch path.
4. Only if (2) shows subject evaluation bypasses the sender-style
   chain: a device-side placement-feasibility probe (writable iOS path
   whose spelling is canonical inside the rootfs + exec-admitted) —
   deferred, needs its own packet.
