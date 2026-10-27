# D042 — independent input-acceptance audit + exact global decision map

Date: 2026-10-19 · Devin frontend 9589 · read-only. Task:
`coordination/_relay/devin-042-frontend-task.md`. No device/production/
build/Git changes; no backend contact. FACT = saved artifacts /
verified source bytes; THEORY = marked inference with disconfirmer.

## 1. D041 artifact verification (reviewer-inspected)

| Artifact | Claim | Independent check |
|---|---|---|
| `frame-after-text.png` `81bebbc6…` | 'd' glyph in live Finder search field | **VERIFIED visually** — "Searching 'This M…'" window, focused search field containing `d` + clear-X; Finder menu bar live; dead Terminal `[Process completed]` behind (validates target rejection); Dock + `Contacts` tooltip; MACWSHook REFUSED noise in Terminal (expected) |
| `frame-after-cmdf.png` `02363cb1…` | search bar opened | hash differs from before; consistent with Searching title change |
| `frame-after-d/right.png` `e07d0eca…` | no visible change | **byte-identical pair** — internally consistent with both no-ops |
| `frame-before/after-reverted` | revert clean | distinct hashes; stated diff = menubar repaint noise region only — plausible per bbox (466,2–2720,552 excludes Finder content) |
| `keyboard-latency.trace` `49c7c2b1…` | per-record ack chain | internally consistent: kind 11/12 down/up pairs; keycodes 124/2/3/51/53 = right/'d'/'f'/backspace/escape matching the action set; sample=1,2 pairing; `producer-to-app` 0.65–79.6 ms |

Accepted at FACT level: **exact AppInput PID/window key delivery to a
verified AppKit endpoint works** — real `sendEvent:` dispatch +
menu-equivalent action + text insertion + pixel consequence.

### What the injector path does and does not prove

`misc/host_key_probe.py` writes `MacWSInputRecord`s **directly** to
`/var/mnt/rootfs/private/tmp/macws_host_input.sock`. Exercised:
inputd validate+route (`exactWindowRecord` arm — targetPID=10595>1 +
sceneID window set, so `IsNativeKeyboardProxyRecord`=false and no VNC
proxy detour) → `macws_app_input.10595.sock` → bridge thread →
contract classify → `postEvent:atStart:` → `-[NSApplication
sendEvent:]` → Finder menu action + insertText → composite → pixels.

**Bypassed (unproven)**: UIKit touch/key production in MacWSHost
(`emittedInput` → `MacWSSendInputRecord`), `routeFullscreenInputRecord`
decisions, Host-side latency/queueing — i.e., the *record production*
half of the real user path. The delivery half is proven.
`route=uncorrelated` in the trace is honest: dispatch events aren't
identity-bound to wire records (sample=0); binding is
sequence/timing + pixel consequence — adequate here but not
cryptographic. `post-to-app=-1.000ms` = unmeasured stage.

## 2. Exact global denial map (capstone, `analysis/SkyLight_22F82` uuid `96676A53…`)

```
_XPostEventRecord +0x388e64                    (MIG server routine)
  header/size checks → mov w0,#-0x130 (-304) on structural fail
  CGXConnectionForPort → conn
  bl CGXSenderCanSynthesizeEvents +0x388f08
    cbz w0 → +0x388f94: mov w0,#0x3ea (1002) → str w0,[reply+0x20]
    else → paciza'd cb → CGSDecodeEventRecord (dispatch)

CGXSenderCanSynthesizeEvents +0x318464
  ldr x8,[0x53ac11b8]          container (per-message audit ctx)
  cbz x8 → return 0            (arm A: no audit container)
  [x8+4] < 0x34 → return 0     (arm B: undersized token block)
  copy creds [x8+0x14..0x33] + static 40B ctx (cache addr 0x4ece66c8 —
  ~~lives OUTSIDE this slice, contents not decodable~~ CORRECTED D045:
  inside __DATA_CONST,__const file+0x438248; decoded = {CFSTR(
  "kTCCServicePostEvent"), deny=1, allow=2, decided=3, pid0flag=1})
  → WSAuditTokenHoldsCachedProcessRight(creds, ctx,
        paciza(&audit_token_check_tcc_access))

WSAuditTokenHoldsCachedProcessRight +0x3407ec
  audit_token_to_* conversions (unnamed stubs; import set confirms
    audit_token_to_pid/auid/ruid/euid/asid + sandbox_check_by_audit_token
    + SecTask*/entitlement + proc_pidinfo + xpc entitlement copies)
  owner fast-path + flag byte [ctx+0x20]
  capability bitmask tests [ctx+0x18]/[ctx+0x10] → cset allow
  miss → blraaz audit_token_check_tcc_access (+0x340914)
  result picks bucket csel ctx[0x10]/ctx[8]; updates cached-rights obj
  [0x53ac1b40]; miss arm builds PAC-signed async continuation (pacda/
  pacia) — TCC request may complete ASYNC (first post could deny while
  a request is still in flight)

audit_token_check_tcc_access +0x340da0
  lazy-resolve TCCAccessPreflightWithAuditToken via _tcc_lookup_symbol
    (TCC.framework path string at +0x3eec5f; symbol at +0x3eec3e;
    cached in [0x53ac1b50]); resolution fail → return 0
  blraaz fn(ctx_field=[x22], token_copy, 0)
  cmp w0,#0; cset w0,eq  → returns 1 iff preflight returned 0
```

### Branch→artifact map (what D041 observed vs what remains dark)

| Point | D041 evidence | Status |
|---|---|---|
| 1102 dead name | `HIDEvents` lookup rc=1102 | FACT, second denial surface, independent |
| record arrives | canonical 248B record, rc=1002 | FACT — structural arm passed (would be −304) |
| gate runs | (not instrumented) | INFERRED — 1002 can only come from the cbz path |
| arm A container | gCurMsgAudit populated (R002 hist) | historical only; current generation UNOBSERVED |
| arm B size | same | UNOBSERVED |
| inner return | `0x1854257ec` reached in R002 telemetry | historical; CURRENT generation unobserved |
| TCC cb invoked | — | **UNOBSERVED — the key discriminator** |
| TCC verdict on SENDER token | — | UNOBSERVED. probe's SELF-token preflight = 1 for all 3 services is a DIFFERENT datum (probe process ≠ the token WS evaluates; the gate checks the audit token from the MIG trailer) |

### responsibility103 evaluation (required by task)

`#103 = ENOPOLICY` ("No such policy registered") — an errno from the
responsibility API, NOT a TCC policy field (r005 audit, confirmed).
**SkyLight imports NO responsibility_* symbol** (1819-symbol undef
list checked: audit_token_to_*, sandbox_check_by_audit_token, SecTask*,
xpc entitlement, proc_pidinfo — no responsibility_*).

- ~~responsibility103 CANNOT be a producing step inside this gate —
  no call path exists in the binary. FACT (negative import evidence)~~
  **CORRECTED (D043):** the import-table check proves only that **no
  direct statically-bound responsibility_* call appears in the checked
  undefined-import list**. It does NOT exclude indirect involvement:
  this same binary lazy-resolves `TCCAccessPreflightWithAuditToken` via
  dlopen/dlsym at +0x340da0 (proving dynamic resolution exists inside
  the gate's own call tree), and responsibility attribution can run
  inside tccd's evaluation of whatever token the gate submits.
  responsibility103 stays THEORY — not refuted.
- Any role it plays is most plausibly in **tccd's attribution layer**
  (responsible-process resolution during evaluation of the sender's
  audit token) or in audit token contents authored at spawn —
  testable only via sender-token bytes + tccd-side observation.
- Sender-vs-self token difference remains THEORY until actual token
  bytes are compared. **Weakened further by the D043 polarity audit:**
  `TCCAccessPreflightWithAuditToken` returns `out[0x18]` default 1,
  error-block 2, reply-block 0 → **0 = allowed, 1 = denied,
  2 = unknown/error** (verified in analysis/TCC_22F82 @0x1850d03bc +
  blocks @0x1850d0548/0x1850d0560, and required by SkyLight-internal
  consistency: impl_rc≠0 selects ctx[0x10] which is provably the
  allow-mask). The probe's `result=1` on its self-token therefore
  means **denied**, not allowed — the apparent "self-allowed vs
  gate-denied" asymmetry that motivated this theory likely never
  existed. Token identity is still unverified bytes-wise, but no
  asymmetry demands it.

## 3. Backend observation-design review + minimal-input manifest

Backend proposed: permit-instrument `CGXSenderCanSynthesizeEvents`
(+0x318464) and `_XPostEventRecord` (+0x388e64) with entry counters +
byte readback. Assessment:

- Both prologues verified verbatim-safe (FACT, my disasm);
  permit-installable under the current relocator.
- **Insufficient alone**: entry counters prove execution, not WHICH
  arm produced the 0. The gate's three decision points need data, not
  presence.
- **Minimal discriminating set** (all prologues verified verbatim-safe
  this review):
  1. gate +0x318464 hook → log `[0x53ac11b8]`, `[x8+4]`, gate return →
     arms A/B/inner;
  2. inner +0x3407ec hook (`pacibsp sub stp stp` ✓) → entry + return +
     whether the `blraaz` at +0x340914 fires → fast-path/cache arm vs
     TCC arm;
  3. tcc-callback +0x340da0 hook (`pacibsp sub stp stp` ✓) → entry
     count + resolved-fn return + **dump the 32-byte sender audit token
     (x1 arg)** → the single most discriminating datum: what identity
     TCC is asked about and what it answers.
- Permits required for all three (same SkyLight uuid; three new
  uuid+offset+prologue pins — static pins, no dynamic self-capture).

Negative/acceptance tests for that design: install shows patch bytes
`58000051 d61f0220` at all three offsets; one bounded `SLSPostEventRecord`
post must fire gate entry (counter>0); cb-entry=0 with inner-return=0
puts the denial in fast-path/cache arm (no TCC consulted — narrows to
identity/keying); cb-entry>0 with fn-return logged gives TCC's actual
verdict on the actual token. Accept claims only with all three logs;
no acceptance from counter presence alone.

## 4. Remaining unobserved items in the CURRENT WS generation

1. The sender audit token bytes the gate reads (top discriminator).
2. Whether `_audit_token_check_tcc_access` is invoked at all (fast-path
   vs TCC arm split).
3. The cached-rights object state `[0x53ac1b40]` and whether any
   process holds the right.
4. ~~The ctx-block contents at cache-addr `0x4ece66c8`~~ RESOLVED D045:
   the 40 bytes ARE in this slice (`__DATA_CONST,__const` file+0x438248):
   `{ 0x00300001dd4ab240→CFSTR("kTCCServicePostEvent"), 1, 2, 3, 1 }` —
   the checked service is literally **kTCCServicePostEvent**, deny-mask 1,
   allow-mask 2, decided-mask 3, pid==0→flag 1 (allow). The nearby
   `kTCCServiceListenEvent`/`secureTap` strings were unrelated.
5. HIDEvents bootstrap registration: suppressed by `-virtualonly` or
   living on a different domain — separate axis B question.

## 5. Rejected/withdrawn hypotheses (preserved per discipline)

| Hypothesis | Status | Deciding observation |
|---|---|---|
| Gate denies because container NULL (structural) | WITHDRAWN — R002 chain reached inner fn | hook will show container non-NULL anyway |
| "TCC PostEvent refuses" (backend phrasing) | UNSUPPORTED as stated — accessor1/EMPTY dict carries no denial field; gate 0 ≠ TCC verdict | need the cb-entry + fn-return log |
| responsibility103 inside gate | ~~REFUTED~~ → narrowed (D043): no direct static call in checked import table; indirect/dynamic/tccd-mediated NOT excluded — stays THEORY | sender-token bytes + tccd-side trace |
| Old verbatim-trampoline gate hook | OBSOLETE — all three prologues verified verbatim-safe under exact relocator | install would refuse if wrong |

(handover to codex now)
