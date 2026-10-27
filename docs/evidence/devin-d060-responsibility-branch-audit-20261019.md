# D060 — exact responsibility branch + connection-flag audit (local-only)

2026-10-19, packet `coordination/_relay/devin-060-backend-task.md`.
Read-only local RE of `analysis/d050-tccd/tccd_22F82` (arm64e, UUID
`9845D17F-0409-3884-8A17-260C729636E5`, sha256 `42c388e8…` = live
fingerprint). No device contact, no TCC traffic, no responsibility
setters, no source/build/Git mutation. File-off = vaddr −0x100000000.

## 1. Correction: stage order is INVERTED vs prior docs

`TCCDAttributionChain initWithMessage:` (IMP `0x10004ffa8`) →
`w3=1` → `initWithMessage:evaluateResponsibility:` (IMP `0x4ffb0`) →
`x4=0` → `initWithMessage:evaluateResponsibility:processInfo:`
(IMP `0x10005001c`). Inside that body, exact call order:

| Off | Call | Role |
|---|---|---|
| `0x500a8` | `getAuditToken:fromMessage:` | extract **kernel-provided connection peer audit token** (IMP `0x4ffb8` tail-forwards `(msg)` to import `0x557b0` — an xpc token accessor; not dict-forgeable) |
| `0x500c0` | `createProcessFromAuditToken:processInfo:` | build requesting process — **contains the path lookup** (`proc_pidpath_audittoken` @`0x4b9d8` ⊂ fn `0x4b8a0` reached via this chain; IMP `0x4ffc4` → `initWithAuditToken:`-style alloc on class `@0x8e678`) |
| `0x500d8`/`0x500e8` | `setRequestingProcess:`/`requestingProcess` | store + validity gates (`cbz x23 → 0x503f4` early-out on nil) |
| `0x50198` | `createProcessFromAuditToken:` (2nd) | accessing/target process |
| `0x50244` | `cbz w22 → 0x50254` | **`evaluateResponsibility` arg gates** |
| `0x5024c` | `_evaluateResponsibleProcess` (IMP `0x4fac4`) | **the `#103` producer — runs LAST** |

**Consequence (verified ordering, not inference):**
`proc_pidpath_audittoken` executes *before* the responsibility getter.
- D052 iOS sender's `proc_pidpath_audittoken (#2) ENOENT` means it died
  at the PATH stage — **the responsibility getter was never reached for
  it**. "Passed stage-1" was never proven (frontend's D058/D059
  suspicion confirmed). Its responsibility behavior is *unknown*.
- R002 chroot sender's `responsibility_get… (#103)` means its PATH
  stage PASSED — consistent with D059's live proof that a chrooted
  query on an in-root vnode returns the caller-relative `/System/…`
  spelling. Chroot-resident senders fail only at the responsibility
  step.
- Prior docs' "stage-1 responsibility / stage-2 path" ordering is
  renamed: **path-first, responsibility-last** inside the chain.

## 2. Class + token source

- Builder class `@0x8e420` = **`TCCDAttributionChain`** (class_ro name
  at fileoff `0x78c03`; baseMethods `0x5d6d8`, 22 methods).
- `getAuditToken:fromMessage:` IMP `0x4ffb8`: `mov x0,x3; mov x1,x2;
  b 0x557b0` — forwards `(msg, &token)` to an `__auth_stubs` import —
  the xpc accessor yielding the kernel-attached peer token. Sender
  identity is kernel-provided; dict fields cannot substitute it.
- `initWithMessage:evaluateResponsibility:` /
  `:evaluateResponsibility:processInfo:` IMPs exist
  (`0x4ffb0`/`0x5001c`) but the *evaluateResponsibility:NO* route has
  **zero `bl` callsites** — dead API surface in this image; the one
  live entry (`initWithMessage:`) always sets the flag.

## 3. Connection-leg selection (`isSystemServer` + service property)

Handler CFG (revised vs D057 — `0x6708` leg sits *before* the `0x6824`
block, on the service-resolved path):

| Off | Op | Leg |
|---|---|---|
| `0x66c4-0x66f0` | `serviceFromMessage:error:` (x0=`[x22+0xb30]` server, x2=msg, x3=&out) → x25 service obj; `cbz x25 → 0x6824` | service resolution fails → secondary leg |
| `0x66f8` | `bl 0x58560` on service → w28 | service property (attribution-related) |
| `0x6704-0x6708` | `[server macos_isSystemServer]`; `tbnz w0,#0 → 0x6838` | **flag=1 → attribution leg** |
| `0x670c` | `cbz w28 → 0x69e8` (w24=0, `b 0x6ba0`) | flag=0 + prop=0 → **direct dispatch, no attribution** |
| `0x6710-0x6744` | log thunk `0x7934` → `xpc_connection_send_message` + exit | flag=0 + prop≠0 → **early-bail send** (distinct producer, not the attribution log) |
| `0x6824-0x6834` | `[server macos_isSystemServer]` again; `cbz w0 → 0x6ba0` | service-fail leg: flag=0 → dispatch anyway; flag=1 → `0x6838` |
| `0x6838-0x6850` | `[[TCCDAttributionChain alloc] initWithMessage:]` → `cbz x0 → 0x69ac` | attribution → nil → empty-reply tail |

`macos_isSystemServer` IMP `0x10002d0e4` = `ldrb w8,[x0,#0x29]; and
w0,#1` — getter on **TCCDServer** (method list at fileoff `0x5bc88`,
62 methods, contains `init`@`0x25a58`, getter, and setter
`setMacos_isSystemServer:` IMP `0x10002d0f0`).

**Flag write provenance — unresolved, stated as gap:** a byte scan of
the whole image finds *zero* `strb/str …,#0x29` writes outside the
setter IMP itself, and no `bl` callsite loads the
`setMacos_isSystemServer:` selref (`0x8de38`) through any stub in this
binary. The flag is therefore set either (a) outside this image (not
observable here), (b) via a setter caller my scan missed, or (c) never —
in which case `+0x29` reads 0 and the observed senders took a
leg where attribution still ran — impossible under this CFG, so (a) or
(b) must hold. Distinction matters: if the flag is per-*instance*
(tccd system) all connections attribute; if per-connection property set
by listener type, leg selection differs per endpoint. **Not resolved.**

## 4. `do_TCCAccessRequestIndirect` (`0x100011aa4`) — admission detail

- Rebuilds the **requester** chain from the same message: `0x128e8`
  `[[TCCDAttributionChain alloc] initWithMessage:x19]` — same sender,
  same gates; no second subject attribution.
- Subject identity from **dict fields**: `target_csreq`, `target_identifier`,
  `target_path`, `indirect_object_*`; branch at `0x128f4` on
  indirect-object type: `==3` → `initWithAttributionChain:
  preferMostSpecificIdentifier:` (subject through the requester chain);
  else → `initWithIdentifier:type:executableURL:SDKVersion:
  platformType:` (`0x12948`). **Subject is never re-deriv​ed through a
  kernel token** — Indirect is the protocol-native broker surface.
- **Sender-side entitlement gate found**: `0x13494`
  `[chain requestingProcess]` → `boolValueForEntitlement:` with
  name = CFString literal `com.apple.private.tcc.override-prompt-policy`
  (`ldr #0x10007d6b8` → cfstring @`0x82b60`). Result ANDed into `w21`
  at `0x134bc` — a broker without this private entitlement does not
  proceed on the normal path.
- `serviceFromMessage:error:` + `TCCD_MSG_SPI_VERSION` checks precede;
  `evaluateOverridePolicyForAccessByIdentity:toService:
  withIndirectObjectIdentityIdentity:authorizationReason:` @`0x12984`
  is the policy eval.

## 5. Implications for designs

- For `tccd system` senders the only reachable bail is the attribution
  leg (flag=1 assumed per §3 gap). Both observed failures are *inside*
  `initWithMessage:`: chroot senders at `_evaluateResponsibleProcess`
  (#103), the D052 iOS sender at `createProcessFromAuditToken` (#2).
- **The empty reply now has exactly one producer**: a nil
  `TCCDAttributionChain` — either leg's early-bail or the builder.
  `responsibility` is the *last* step; a chroot sender with a passing
  path only needs the responsibility entry resolved.
- The discriminator the packet wants — leg-selection vs genuine
  lookup failure — is now resolvable *by log line*: `#103` can only be
  emitted from the getter inside the builder (leg-selected in, genuine
  ENOPOLICY). A sender on the flag=0/prop=0 leg produces no
  attribution log at all and reaches a handler (different signature:
  populated or handler-level error). All observed failures show the
  `#103`/`#2` getter errors → attribution leg taken in every case.
- Still-open split under "genuine #103": registration-level (launchd
  never recorded a responsibility entry for the pid) vs provider-level
  (entry exists but the query can't resolve it — e.g. namespace/audit-
  session semantics). WS being a launchd-tracked job makes
  registration-level questionable → the open sub-question.

## 6. Smallest concrete next read-only observation

**Read the live responsibility fields on device — zero TCC, zero tccd
traffic:** `launchctl procinfo <pid>` (or `proc_pidinfo` RESPONSIBLE
flavor / `task_inspect` equivalents — whichever the iOS userland
exposes) for three already-running pids:
- `10447` (WindowServer — the #103 sender),
- `10300` (chroot `tccd system` — same launch shape),
- any pre-existing native user/501 launchd job pid (the class that
  produced D052's stage-advance — a same-class control, not a new send).

Predicted outcomes:
- `responsible_pid`/entry absent or self-only for WS+tccd vs present
  for the native job → **registration-level**: launchd's responsibility
  wiring differs by job shape (points at launch contract, not kernel
  query semantics).
- Entries present for all three → **provider-level**: the
  `responsibility_get…` SPI itself fails in the chroot context —
  points at namespace/audit-session semantics of the provider call.
- If no read-only interface exposes the field → the missing input is
  recorded as "no symbol/API on iOS to enumerate responsibility table"
  (consistent with D052's `responsibility_*` SYMBOL_MISSING finding).

Prerequisite note: none of this touches the desktop; it's process-info
reads only. Execution deferred to a packet that authorizes device
reads.

## 7. Preserved rejected interpretations

- "D052 passed responsibility" — disproven by §1 ordering.
- "SETEXEC missing / orphan pid" — retracted in D056/D057 corrections.
- "Rootfs helper as fix" — disproven (D055/D059 relative-path model).
- "isSystemServer flag is a policy computation" — it's a stored bit.
- "Indirect rescues sender attribution" — it can't; it runs
  post-dispatch on the already-attributed connection and adds an
  entitlement gate on top.
