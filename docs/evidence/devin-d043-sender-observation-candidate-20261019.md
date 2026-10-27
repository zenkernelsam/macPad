# D043 — dedicated record-only sender observation (source + local build)

Date 2026-10-19, packet `coordination/_relay/devin-043-backend-task.md`.
**Candidate implementation only — no device deploy, no restart, no
marker change, no commit/package/install.** The D042-review corrections
are applied: dedicated default-off sentinel (not the runtime-diagnostics
umbrella, which also activates an alternate display producer), exact
ABI/polarity audit before hook design, and no causal label assigned to
counter values alone.

**DIAGNOSTIC / TEMPORARY SCAFFOLD.** Record-only: nothing in this
change alters any return value, cache, audit token, rightDesc, TCC
state, identity, or policy. Display permits/behavior untouched.

## 1. ABI / polarity audit — FACT (capstone on `analysis/SkyLight_22F82`)

`__TEXT` va `0x1850e5000` → file offset 0. All three function bodies
disassembled directly; this supersedes the looser R004b summary.

### Gate `CGXSenderCanSynthesizeEvents` @ `+0x318464`

```
ldr x8,[gCurMsgAudit @__DATA+0x1d8ba61b8] ; container ptr
cbz x8            -> return 0            (early exit: NULL audit)
ldr w9,[x8,#4]; cmp #0x34; b.lo -> ret 0 (early exit: small container)
ldur q0,[x8,#0x14]; ldur q1,[x8,#0x24]
stp q0,q1,[sp,#0x30]                     ; 32B TOKEN at container+0x14
rightDesc @0x1d3dcb6c8 -> 40B -> sp      ; static const descriptor
paciza x16 (=0x185425da0)                ; impl, IA-signed
bl dispatcher  x0=&tok, x1=&desc, x2=impl
retab  (w0 = dispatcher result)
```

**a0/a1 are never read** — the gate is a pure gCurMsgAudit marshal.
Return polarity: w0 = dispatcher's w0 verbatim.

### Dispatcher `WSAuditTokenHoldsCachedProcessRight` @ `+0x3407ec`

Signature **RE-confirmed**: `int (audit_token_t *x0, rightDesc *x1,
impl_fn x2)`. Decision paths (exact):

- `pid = audit_token_to_pid(tok)`; `pidver = audit_token_to_pidversion(tok)`
- `pid==0` → `rc = (rightDesc[0x20] != 0)` — **auid path**
- `pid==getpid()` → `rc = 1` — **self always allowed**
- miss check: `cached = rightsCache[key=pid|pidver<<32]`;
  `cached & rightDesc[0x18]` (known mask) set →
  `rc = (cached & rightDesc[0x10]) != 0` — **cached allow iff allow-bit**
- known-mask clear → **miss**: `impl(x0=&tok-copy, x1=rightDesc[0])` —
  *a1 is the descriptor's first field (the service CFStringRef), not
  the descriptor*; `rc = impl_rc`; bits = `impl_rc!=0 ? desc[0x10]
  (allow) : desc[8] (deny)`, folded into the cache.

### Impl `audit_token_check_tcc_access` @ `+0x340da0`

Signature **RE-confirmed**: `int (audit_token_t *x0, CFStringRef x1)`.

- `lazyChecker @0x1d8ba6b50 == 0` → resolve
  `"TCCAccessPreflightWithAuditToken"` via helper @`+0x345e44`;
  still NULL → **return 0** (deny without ever consulting TCC)
- else `checker(x0=service, x1=&tok-copy, x2=0)`; `cset w0, eq` →
  **impl returns (checker_rc == 0)**

### Whole-chain polarity

`checker_rc==0` → impl=1 → dispatcher allow-bits → gate=1 → post
permitted. `checker_rc!=0` → impl=0 → deny bits → `_XPostEventRecord`
answers 1002. **`impl_rc==0` therefore covers two distinct producers —
unresolved checker OR checker denial** — discriminable only by reading
the lazyChecker cell before/after the call (this is why the impl record
carries `lazy_pre`/`lazy_post`).

Async: the lazily resolved symbol is literally
`TCCAccessPreflightWithAuditToken` — a synchronous preflight API; no
async callback exists at this boundary. The raw checker return value is
*not* observable at a SkyLight-boundary hook (`cset eq` collapses it);
separating "checker returned nonzero X" from transport-layer failure
inside TCC would need a TCC-image permit — documented limit, out of
scope.

Argument lifetime: x0 (audit) and x1 (desc) at both inner boundaries
are **caller-stack copies valid only for the call duration** — records
copy bytes (32B / 40B) verbatim inside the wrapper and never retain
pointers. The wrapper reads mirror the originals' own unconditional
derefs, so no new validity domain is introduced.

## 2. Implementation (source-level candidate)

New: `libmachook/MacWSSenderObservation.{h,c}` — arch-agnostic core
(added to `libmachook_FILES`); `misc/test_sender_observation.c` host
contract test. Changed: `libmachook/mac_hooks.m` (+236/−19).

Gating (all required, fail closed):
`isWS` executable check → dedicated sentinel
`/tmp/macws_sender_observation` (chroot; checked at install **and** per
record → removing it live-disarms) → per-site `MACWSHookPermitV2`
(SkyLight LC_UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880` + offset +
prologue). The gate boundary additionally still arms under the old
runtime-diagnostics sentinel (existing `#### CGXSYNTH` fprintf path
preserved verbatim — now explicitly conditioned so it cannot fire when
only sender-observation is armed; no per-frame logs otherwise).

Per-site pins (byte-verified in the analysis image, also asserted by
the host test):

| site | offset | prologue words |
|---|---|---|
| gate | `+0x318464` | `d503237f d101c3ff a9067bfd 910183fd` |
| dispatcher | `+0x3407ec` | `d503237f d102c3ff a90667fa a9075ff8` |
| impl | `+0x340da0` | `d503237f d10183ff a90357f6 a9044ff4` |

WS-local globals resolved image-relative like `gCurMsgAudit`:
rightDesc `0x1d3dcb6c8`, lazyChecker cell `0x1d8ba6b50`.

Bounded raw records: fixed **136B** struct (magic `MSOB`, ver, site,
flags, seq, tid=`pthread_self`, rc, audit ptr/size, lazy pre/post, arg
identity, 32B token, 40B desc) into a static 256-record ring + one raw
`write()` per record to `/tmp/macws_sender_observation.log` (lazily
opened, append). Site calls counted unconditionally; overflow and
post-disarm emits counted via `dropped`. errno saved/restored inside
`macws_sender_obs_emit`. Originals always called first, unchanged,
and their results returned verbatim.

## 3. Host test — ALL PASS (25 checks)

`cc -DMACWS_SENDER_OBS_TEST misc/test_sender_observation.c
libmachook/MacWSSenderObservation.c` on this x86_64 host:

- default-off: nothing stored/emitted, calls+dropped still counted
- armed: 136B records verbatim in ring and raw log file (byte-equal)
- budget: 302 emits → 256 stored, seq counts all, 46 ring-overflow
  drops + 1 disarm drop
- live disarm: sentinel removed mid-stream → dropped, calls continue
- errno preserved across emit
- pins: all three prologue quads byte-equal the image at the pinned
  offsets; a 1-bit corruption provably mismatches (fail-closed basis)

Not host-testable (needs provider/device, unchanged from D036/D037
coverage): real MSHook install, PAC-signed orig-pointer contract, live
UUID verification.

## 4. Build artifacts (local only — NOT the deployed set)

Fat `libmachook/.theos/obj/libmachook.dylib` (theos adhoc-signed build
object, **not** the post-signed deployable — hashes differ from any
later signed artifact by contract):

- file sha256 `135f3c96933f23074e8829409c90451a7090a2f503e371b8773d97e454b035c6`
- arm64 slice uuid `F65A7DE0-8DCB-3378-B96E-D67A068E2375`
- arm64e slice uuid `0B695A35-1394-3A99-9CAD-5B0F296CDA81`
- contains all 3 exported pin constants + 12 obs symbols per `nm`

Source sha256 (truncated): mac_hooks.m `b188b2bf…`,
MacWSSenderObservation.h `4bbdd89e…`, .c `977dffa6…`, Makefile
`a7cc7862…`, test `8450dd82…`.

## 5. Record → producing-layer discrimination matrix

For a future authorized bounded arm, each stored record set maps to
exactly one branch (token.pid = `token.u32[5]`, pidver = `token.u32[7]`
decoded offline; desc masks from the 40B snapshot):

| evidence | producing layer |
|---|---|
| gate rc=0, no disp/impl, `audit_present=0` or size<0x34 | gate early exit — audit container missing |
| disp rc=0, no impl rec, `token.pid==0` | auid path — `desc[0x20]==0` denies |
| disp rc=0, no impl rec, `pid≠0` and `pid≠self` | cached deny — known-bits set, allow-bit clear |
| impl rec rc=0, `lazy_post==0` | resolver failure — TCC never consulted |
| impl rec rc=0, `lazy_post≠0` | checker ran and returned ≠0 — TCC-side evaluation; only then does the `responsibility103` attribution theory become the next single variable |
| impl rc=1 while gate rc=0 | contradiction with the disasm — indicates an unmapped path, investigate |

No row asserts causation from counts alone — each requires the byte
fields in the same record.

## 6. Reversible future observation plan

1. On an authorized window: create `/tmp/macws_sender_observation` in
   the chroot **before** WindowServer (re)launch — the install path only
   arms at SkyLight image-load; a mid-generation create cannot arm it.
   (A lighter variant: sentinel present before one bounded generation
   start under the existing lease/watchdog contract.)
2. One canonical `SLSPostEventRecord` post (D041 shape, rc=1002) — plus
   optionally one exact AppInput record as a negative control (should
   produce no gate records).
3. Read `macws_sender_observation.log`; match §5 row; remove sentinel.
4. Display preservation: the candidate touches none of the four
   display permits and arms no alternate producer; composite
   `state=ready` evidence is expected unchanged — capture the
   before/after `macws_final_composite.state` as the health witness.

## 7. Honest limits

- Current-generation `gCurMsgAudit` remains unread by design (no safe
  WS-local read tool; §2 of the D042 doc) — these records capture it at
  the gate boundary instead, which is the WS-local truth the gate sees.
- `token`/`desc` bytes may reflect a container already updated by a
  newer concurrent post — seq/tid correlation bounds, does not
  eliminate, that window.
- `impl_rc==0` + `lazy_post≠0` localizes to "checker returned ≠0" — it
  does not prove an explicit TCC policy-deny field; that distinction
  requires the raw checker rc (TCC-image boundary, out of scope).
- This diff is **not deployed**; the installed binaries from D042
  remain the live set.
