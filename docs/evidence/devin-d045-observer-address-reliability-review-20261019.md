# D045 — Corrected-Address & Observer Reliability Audit (independent)

**Scope (devin-045-frontend-task.md):** independently re-derive the three
corrected global offsets from exact ADRP/LDR/ADD references in
`analysis/SkyLight_22F82` (LC_UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880`,
sha256 `13d7a0ca…906c61`, base `0x1850e5000`); verify segment coverage; review
the corrected backend observer files against all D044 reliability criteria;
deepen the TCC polarity analysis (local API encoding vs explicit policy
field); and write the result file. **No device access. No production source
edits. Disposable tests only in `analysis/devin-d045-frontend/`.**

All verification ran via one disposable script
`analysis/devin-d045-frontend/d045_offset_assertions.py`
(capstone + raw Mach-O parsing). Output reproduced in §6.

---

## 1. Offset re-derivation — ALL THREE VERIFIED, and the bug class is now understood

Independent instruction-level re-derivation (parsing the ADRP+op_str
pairs myself, not reusing any backend constant):

| global | gate/impl insns (vaddr) | derived VA | VA − base = file offset | backend's NEW constant | verdict |
|---|---|---|---|---|---|
| audit container slot | `adrp x8,#0x1d8ba6000; ldr x8,[x8,#0x1b8]` @+0x318484/88 | `0x1d8ba61b8` | **`0x53AC11B8`** | `0x53AC11B8` | ✓ |
| rightDesc (static ctx) | `adrp x8,#0x1d3dc8000; add x8,x8,#0x36c8` @+0x3184b0/b4 | `0x1d3dcb6c8` | **`0x4ECE66C8`** | `0x4ECE66C8` | ✓ |
| lazyChecker cell | `adrp x21,#0x1d8ba6000; ldr x8,[x21,#0xb50]` @+0x340dd0/d4 | `0x1d8ba6b50` | **`0x53AC1B50`** | `0x53AC1B50` | ✓ |

**Bug class confirmed:** the old D043 values (`0x5AC…`/`0x542E…`) each
dropped the `3` nibble at bit 24 (`0x53AC…`→`0x5AC…`) or changed the
descriptor page (`0x4ECE…`→`0x542E…`) — classic VA-minus-base hand
arithmetic errors, NOT wrong instruction identification. The instruction
pairs the backend comments cite are the exact same ones I disassembled.
The fix is correct; the D043 blockers B1–B3 are **closed**.

**Why this also proves the container is REAL:** the audit slot lives in
SkyLight's own `__DATA,__bss` — i.e., it is the image's own cell holding a
pointer to the per-message audit container that CGX fills per MIG
dispatch. Runtime telemetry (R002/R008) already shows it populated
(size=52). `__bss` = zero file size → the cell itself is runtime-written;
there are no file bytes to read and none are claimed.

## 2. Segment coverage — one more correction to MY OWN earlier claims

| target | VM region | file region | status |
|---|---|---|---|
| audit slot `0x1d8ba61b8` | `__DATA,__bss` | none (zero-fill) | runtime cell — pointer read only |
| lazyChecker `0x1d8ba6b50` | `__DATA,__bss` | none (zero-fill) | runtime cell — pointer read only |
| rightDesc `0x1d3dcb6c8` | `__DATA_CONST,__const` `[0x1d3d78000…+0x62d18]` | `file+0x438248` | **file-carried — bytes decodable** |

**Self-correction (material):** my D042 doc said the ctx block "lives
OUTSIDE this slice — contents not decodable", and D043 repeated it. That
was wrong: the VA IS inside this image's `__DATA_CONST` (my earlier
"another cache image" claim failed to account for `__DATA_CONST` being
mapped in the same file at `__data_const` VM `0x1d3d78000`). Both docs now
carry inline ~~strikethrough~~ + D045 resolution.

## 3. rightDesc literal bytes — decoded, service name PROVEN

40 bytes at file `0x438248`:

```
ctx[0x00] = 0x00300001dd4ab240   tagged ptr → mask to 0x1dd4ab240
ctx[0x08] = 1                    deny mask      (decided&~allow)
ctx[0x10] = 2                    allow mask
ctx[0x18] = 3                    decided mask
ctx[0x20] = 1                    pid==0 → flag byte (allow)
```

`0x1dd4ab240` lands inside `__cfstring` (va `0x1dd4ab240`, file
`0x45eae8`): a normal CFString whose `strptr` → `0x1dcbb840e` inside
`__cstring` = literal **`kTCCServicePostEvent`** (length field 20 ✓).

Consequences:

1. The checked right/service is **literally `kTCCServicePostEvent`** —
   this closes the D042 §4 open item and VALIDATES the backend's impl
   comment that `x1 = rightDesc[0]` is a kTCCServicePostEvent CFStringRef.
   My D043 "unproven" flag is WITHDRAWN (annotated in-place in the D043
   doc).
2. The mask semantics are now file-proven: deny=1, allow=2, decided=3 —
   matching the disassembly-derived roles. `decided&~allow` = 1 → deny.
3. The `pid==0→allow` flag means the dispatcher's first arm
   (fieldA==0→flag) is an **allow** for PID-0 tokens (kernel/server-internal
   callers), not a deny. Gate arm C's first early-exit is therefore an
   early-ALLOW path; my earlier "pid==0 flag arm" phrasing survives but its
   direction is now unambiguous: `[ctx+0x20]`==1 ⇒ allowed.

## 4. TCC polarity — LOCAL API encoding vs explicit policy field

Deeper than D043: the preflight return is **locally synthesized**, not a
pass-through of a tccd verdict byte. Verified on `analysis/TCC_22F82`
(LC_UUID `1238D7CA-719A-36A4-94B1-2F387E6B3BF1`, sha256 `09b84137…f2de`):

```
preflight entry @0x1850d03bc:
  … mov w8,#1 ; str w8,[sp,#0x70]   @0x1850d0400  out[0x18] DEFAULT = 1
  … cbz x0 → ret @0x1850d049c        conn-getter NULL: NO request sent;
                                     default 1 survives
reply block @0x1850d0560:
  cbz w1 → ret ; str wzr,[x8,#0x18]  w1≠0 → out[0x18]=0   (granted → 0)
error block @0x1850d0548:
  cbz w1 → ret ; mov w9,#2 ; str w9,[x8,#0x18]  w1≠0 → out[0x18]=2
```

So the local API return tri-state is:

| preflight value | authored by | meaning at the API |
|---|---|---|
| **1** | default store | **non-allow**: denied verdict OR never-contacted (conn NULL / no reply / reply grant flag 0) — collapses |
| **0** | reply block | allowed (w1 = granted flag nonzero) |
| **2** | error block | error/unknown (w1 nonzero on error path) |

**This is the answer to the coordinator's core question.** A `result=1`
from a caller-visible preflight does NOT prove "tccd explicitly denied".
It is the **API's own default**, which survives in at least three
distinct ways: no connection object, request sent but reply arrived
empty/`{count=0}` (grant flag never set), or an explicit denial. The
SkyLight boundary (`impl_rc = (preflight==0)`) then collapses it further:
denied-by-verdict, never-contacted, and error-unknown-2 ALL land on the
same `impl_rc=0` → same gate deny → same 1002.

**EMPTY reply specifically:** `{count=0}` from tccd means the transport
round-trip succeeded and returned a dictionary with zero entries — i.e.,
no explicit policy field was present at all. Whether the reply block then
wrote 0 or left the default depends on a flag inside TCC's own block that
the SkyLight boundary cannot see. Therefore:

- `result=1` is **consistent** with "EMPTY reply retained the default"
  and **consistent** with "explicit denial" — it does not distinguish.
- The `responsibility103` string is an errno (`ENOPOLICY`) that exists on
  tccd/attribution paths; SkyLight itself has no responsibility_* import —
  any role it plays is tccd-side and invisible to the three SkyLight
  boundaries. (Theory remains THEORY; the three hooks cannot decide it.)

## 5. Missing raw evidence — what the current hooks can and cannot see

| layer | datum | where | 3-hook capture? |
|---|---|---|---|
| L0 gate | container ptr, size field, return | GATE record | ✓ ptr/size/ret + token snapshot |
| L1 dispatcher | entry arms, cache hit/miss, orig rc | DISP record | ✓ ret + arg (impl ptr) + token + desc copy |
| L2 impl | lazy pre/post, impl return, service ptr | IMPL record | ✓ partial — collapses {1,2} vs {0} |
| L3 **raw preflight value** | `w0` after `blraaz` | inside impl (+0x340e0c, mid-fn) | ✗ **not captured** |
| L4 reply-block grant flag | `w1` in TCC's reply block | inside TCC image | ✗ |
| L5 tccd reply body | dictionary fields | tccd | ✗ |

So the three hooks **cannot separate** "explicit denial" from "conn-NULL
default" from "empty-reply default" — all produce `impl_rc=0`. They DO
separate: resolve-failure (lazy cell NULL both pre&post AND impl_rc==0 →
preflight never ran) from evaluated-verdict (lazy set post-call →
preflight ran, result unknown-but-nonzero). That is a real reduction of
the unknown space, but it is NOT the verdict itself.

**Path to the raw value (proposal, not implemented):** a 4th
site-permit on the **TCC image itself** — `TCCAccessPreflightWithAuditToken`
prologue `d503237f d103c3ff 6d0b23e9 a90c57f6` at its own `+0x93bc`
offset, uuid `1238d7ca…3bf1`, image-internal function entry — verbatim-safe.
Wrapping its return yields `w19` = the **raw** `{0,1,2}` before SkyLight's
cset collapse, and its `x0` descriptor + stack-resident token (by value)
are legible at entry. That is the minimal way to distinguish denied-vs-
unreached without tccd-side work. This is a DESIGN OPTION only —
deployment is not authorized by this audit.

## 6. Corrected backend files — D044 reliability criteria, all verified

Snapshot (re-hashed at audit end, unchanged from t0 — **files stable**):

```
MacWSSenderObservation.h b0dd2eb5a5efb743…  7042 B  10-19 01:11
MacWSSenderObservation.c 54b6b3d2f0e2b4e3…  7555 B  10-19 01:11
mac_hooks.m               f072080287859feb… (contains the three wrappers)
```

Checked against every D044 criterion (each now verified against the code):

| criterion | status | where verified |
|---|---|---|
| bounds (ring ≤256, desc 40B, token 32B, names) | ✓ | `OBS_RING_CAP 256`, fixed-size memcpy, no dyn alloc |
| errno preservation | ✓ | `saved_errno` captured post-orig, restored at every exit incl. emit/`chain_exit` tail; pre-orig work = TLS/atomic loads only |
| original rc forwarded | ✓ | `rc = orig(); … return rc;` in all three wrappers |
| TLS invocation IDs | ✓ | `chain_enter/exit`: depth++/`id = ++counter` at ROOT, STANDALONE otherwise; `chain_exit` decrements, clears at 0 — nesting-safe |
| complete ring-record publication | ✓ | per-slot `pub` flag set **last** with `atomic_store_explicit(release)`; readers use `pub_count` → never see a partial record |
| first-open concurrency | ✓ | `pthread_once`+mutex; `g_obs_log_fd` written once under lock, read under same mutex — no double-open, no fd leak |
| private log handling | ✓ | `O_WRONLY|O_APPEND|O_NOFOLLOW|O_CLOEXEC`, mode 0600, `fstat` regular-file + `st_uid==geteuid` checks |
| short writes | ✓ | `write_all` bounded ≤8 passes, EINTR retry, EAGAIN/other → break + count |
| default-off | ✓ | everything gated on `macws_sender_observation_enabled()` sentinel → `MACWS_SENDER_OBS_PATH` `/tmp/macws_sender_observation` |
| no display-sentinel coupling | ✓ | no reference to `macws_runtime_diagnostics` anywhere in the observer path — the umbrella sentinel is untouched |

Residual notes (non-blocking):

- `site_calls` denominator increments unconditionally (even when armed
  only by umbrella) — harmless, actually *better* (true call rate).
- Emit runs under the ring mutex — serializes emit+publish; fine given
  sentinel-gated diagnostics are off in normal runs.
- `arg_ptr` semantics differ per site (impl fnptr / service ptr) — the
  header documents each; readers must not conflate.
- A 4th-site hook (§5) would need its own permit+pin; not part of this
  diff — flagged as the missing-evidence path.

**D043 blockers B1–B3 are all closed.** The corrected candidate is a
clean, default-off, record-only, errno-safe, publication-safe, privately-
logged observer. Structurally sound for a deployment review when the
coordinator elects one — this audit does NOT authorize that step.

## 7. Script output (verbatim)

```
derived VAs: audit=0x1d8ba61b8 desc=0x1d3dcb6c8 lazy=0x1d8ba6b50
PASS audit va / desc va / lazy va
PASS AUDIT offset 0x53ac11b8 / DESC 0x4ece66c8 / LAZY 0x53ac1b50
AUDIT→__DATA,__bss   LAZY→__DATA,__bss   DESC→__DATA_CONST file+0x438248
PASS desc[8]=1 deny / [0x10]=2 allow / [0x18]=3 decided / [0x20]=1 pid0
PASS desc[0]→__cfstring → service literal 'kTCCServicePostEvent' (len 20)
PASS prologues ×3 (gate/dispatcher/impl — byte-exact)
PASS preflight default writer (mov w8,#1; str [sp,#0x70])
PASS error block writes 2 / reply block writes 0 (str wzr)
PASS conn-null cbz skips request (default 1 survives)
sha256 SkyLight=13d7a0ca…906c61  TCC=09b84137…f2de
RESULT: ALL PASS
```

(Two earlier drafts of this script flagged false FAILs from MY OWN
wrong index/immediate assumptions — w[2]/w[3] vs actual w[0]/w[1], and a
`0xb9001d09` vs actual `0xb9001909` imm12 difference — corrected; the
assertions now match the real encodings. Kept in the file's history; the
binary was never wrong — the checker was.)

## 8. Deployment eligibility — stated conservatively

| item | state |
|---|---|
| offsets | ✓ independently re-derived + executable-asserted |
| segment coverage | ✓ mapped, audit/lazy = `__bss` runtime cells, desc = file-carried |
| rightDesc | ✓ literal `kTCCServicePostEvent` + masks decoded from file |
| prologues | ✓ all three byte-exact, verbatim-safe |
| observer reliability | ✓ all D044 criteria verified in corrected files |
| polarity | ✓ local-API tri-state established; EMPTY≠explicit-denial proven |
| missing raw value | flagged — needs a 4th-site option (TCC-image permit) or tccd-side work |
| **deployment** | **NOT authorized by this audit** — coordinator's call; files are a stable clean candidate when chosen |
