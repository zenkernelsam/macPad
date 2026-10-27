# devin-045-frontend — corrected-address & observer-reliability audit RESULT

**Doc:** `docs/evidence/devin-d045-observer-address-reliability-review-20261019.md`
**Disposable assertions:** `analysis/devin-d045-frontend/d045_offset_assertions.py` — **ALL PASS**
**Device:** untouched. **Backend:** not interrupted. **Files reviewed:** stable
snapshot (`h=b0dd2eb5…` / `c=54b6b3d2…` / `hooks=f0720802…` — unchanged t0→end).

## Verdict: backend's corrected offsets are RIGHT; my two earlier claims are corrected

### 1. All three offsets independently re-derived — match exactly

| global | my derivation | backend constant | result |
|---|---|---|---|
| audit slot | adrp `0x1d8ba6000` + ldr `0x1b8` @+0x318484 → `0x53AC11B8` | `0x53AC11B8` | ✓ |
| rightDesc | adrp `0x1d3dc8000` + add `0x36c8` @+0x3184b0 → `0x4ECE66C8` | `0x4ECE66C8` | ✓ |
| lazyChecker | adrp `0x1d8ba6000` + ldr `0xb50` @+0x340dd0 → `0x53AC1B50` | `0x53AC1B50` | ✓ |

D043 bug class confirmed: dropped `3`-nibble / wrong descriptor page —
hand-arithmetic, not wrong insns. B1–B3 closed.

### 2. Segment coverage verified; desc is file-carried, audit/lazy are runtime cells

- audit `__DATA,__bss` / lazy `__DATA,__bss` — zero file size, pointer cells (as designed)
- rightDesc `__DATA_CONST,__const` file `0x438248` — **decodable**

### 3. rightDesc literal decode — `kTCCServicePostEvent` is file-proven

`ctx[0]→__cfstring→__cstring` = literal **`kTCCServicePostEvent`**; masks
deny=1 / allow=2 / decided=3 / pid0-allow flag=1. My D043 "unproven" flag and
"outside slice" claim (D042/D043) are both corrected in-place with
strikethrough + D045 resolution. Backend's label was accurate.

### 4. TCC polarity — EMPTY reply ≠ explicit denial (the coordinator's answer)

Preflight's return is locally synthesized: default **1** written before any
tccd contact (verified `mov w8,#1;str[sp,#0x70]` @0x1850d0400), conn-NULL
early-ret leaves it (verified `cbz` @0x1850d049c), error block writes **2**
(`w1≠0`), reply block writes **0** (`w1≠0` = granted). So `result=1` collapses
denied-verdict / never-contacted / empty-reply — **it is the API default, not
a proven denial field**. SkyLight's `impl_rc=(preflight==0)` collapses
further: {1,2}→0. The three hooks cannot distinguish these — but DO separate
resolve-failure (lazy stays NULL) from evaluated (lazy set → preflight ran).

### 5. Missing raw evidence + proposed path (not authorized)

| gap | datum | minimal fix |
|---|---|---|
| raw preflight `{0,1,2}` | `w0` post-`blraaz` mid-fn | **4th site**: permit on TCC image `TCCAccessPreflightWithAuditToken` +0x93bc, uuid `1238d7ca…3bf1`, prologue `d503237f d103c3ff 6d0b23e9 a90c57f6` — verbatim-safe; return yields raw value |
| reply grant flag / dict | inside TCC block / tccd | reply-block w1 or tccd reply body — out of SkyLight scope |

### 6. Corrected-file review — ALL D044 criteria pass

errno ✓ / orig-rc ✓ / TLS chain (ROOT/STANDALONE, depth-safe) ✓ /
release-flag publication (no partial records) ✓ / pthread_once+mutex first-open
(no leak) ✓ / private log `O_NOFOLLOW|0600|fstat|euid` ✓ / bounded write_all
≤8 EINTR-safe ✓ / default-off dedicated sentinel, no umbrella coupling ✓ /
ring ≤256, desc 40B, token 32B, fixed-size only ✓.

Minor non-blocking notes: `site_calls` unconditional (actually better),
emit under mutex (fine at diagnostic rate), `arg_ptr` is per-site (header
documents — readers must not conflate).

### 7. Deployment eligibility

Clean, stable, default-off, record-only candidate — **all three D043
blockers resolved, all D044 criteria verified.** Structurally ready for a
coordinator-elected deployment review. **This audit does NOT authorize
deployment**; the remaining discriminating gap (raw `{0,1,2}`) needs the
4th-site option in §5 if verdict-level detail is required.

---
(handover to codex now)
