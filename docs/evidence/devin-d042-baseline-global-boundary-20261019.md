# D042 — provenance reconcile + global-input upstream boundary

Date 2026-10-19, packet `coordination/_relay/devin-042-backend-task.md`.
**Read-only round.** No restart, deploy, rebuild, source change, or
runtime patch. Live generation preserved throughout (WS 10447,
`state=ready`, seq 1176→1529 during the round). Device reached via
`sshpass -e` (env-carried credential, never argv); no
credentials/addresses/hostnames appear in this document.

## 1. Provenance reconciliation — FACT

D041 recorded that installed whole-file hashes differed from D037's
recorded identifiers and speculated "user's newer deployment". D042
replaces that guess with per-slice identity: Mach-O LC_UUID +
`__text`/`__const` section SHA-256 distinguish re-signing (UUID/text
unchanged) from real code changes.

Method: pulled the live binaries plus the D037 preserve/stage dirs to a
local scratch area; parsed fat headers and `__TEXT,__text`/`__const`
section extents directly; compared against every local build output
(`*/.theos/obj`, share-review extractions).

| installed file | live UUID (slice) | live `__text` sha-256 | matched artifact | verdict |
|---|---|---|---|---|
| `CydiaSubstrate` arm64 | `735E6385-D13A-3D85-B305-D91FEE8FC965` | `a2b074cc…` | `CydiaSubstrateProvider/.theos/obj/macosx/CydiaSubstrate` (built Oct-18 23:34, post-P3-hardening) | **exact same link** |
| `CydiaSubstrate` arm64e | `57DA3076-7D20-3FCA-8C25-5B23DB414CDB` | `88f65a50…` | same | **exact same link** |
| `libmachook.dylib` (arm64e) | `2DAFB6A1-D0B4-318D-B6F8-670F08E3C7F1` | `be0ed487…` | `libmachook/.theos/obj/libmachook.dylib` arm64e slice (Oct-18 23:11) | **exact same link** |
| `libmachook_arm64.dylib` | `3D8E5D89-26F6-3ACA-85EF-57CC1A460647` | `104d18e7…` | same build's arm64 slice | **exact same link** |
| `macwsinputd` | `C2AF8FA7-9145-3743-B288-8BC034C6C205` | `bdd31226…` | `.theos/obj/macosx/macwsinputd` (`__text` identical; UUID differs → same code, different link) | **same source code** |
| `macwsdisplayd` | `50BED241-E47B-3D37-AF8D-EBBB86D079D1` | `f5ba333b…` | `.theos/obj/macosx/macwsdisplayd` (`__text` identical; UUID differs) | **same source code** |

Facts established:

- The installed fat provider's slices are **byte-for-byte in `__text`
  and `__const`** the `CydiaSubstrateProvider/.theos/obj` build. The
  D037 evidence doc already mapped installed sha `134b7eb9…` ↔ unsigned
  fat build `82e74519…` — i.e. **the installed provider is the D037
  post-hardening deployment itself**, not a successor.
- The file found at chroot `/tmp/d037/staged/CydiaSubstrate`
  (uuid `657E80C1`, `__text` 5068B) is a superseded intermediate staging
  slice — not the final deployed artifact. Comparing it against the
  installed fat arm64e slice (`__text` 5156B) produced the false
  "different build" impression.
- libmachook slices on device are the local Oct-18 23:11 build;
  inputd/displayd carry `__text` identical to the Oct-18 13:13
  `.theos/obj` builds under different link UUIDs (re-link of the same
  source, or a signed re-emit — code identity pinned either way).
- Whole-file and CDHash deltas vs D037's identifiers are signing /
  fat-vs-thin packaging artifacts. **No evidence of any post-D037
  redeploy, and no foreign (non-repo) binaries on device.** The D041
  "user's newer deployment" wording is corrected in that document.
- Pre-D037 preserved originals (uuid `C439DBEC`/`957B5055` libmachook,
  `7284A084`/`5700E649` provider) are genuinely older code — superseded
  by the D037 deployment as documented.

## 2. WS-local read-only memory access — NEGATIVE RESULT

No existing safe tooling can read live WindowServer user state:

- `task_for_pid` on WindowServer: denied (D037 runtime evidence; iOS
  `system-task-ports` is platform-only, our adhoc probes can't carry it).
- `vmprobe` on device: an `mmap` self-test only — no cross-process read.
- `libkrw` (Dopamine) is present in the chroot but reads **kernel**
  virtual addresses. Reaching WS's `gCurMsgAudit` (a userland global)
  would require a *new* proc→task→map→uaddr walker with kernel-struct
  offsets — that is a new tool + deploy, not "existing safe tooling",
  and out of this packet's read-only scope.
- Probe-process globals are invalid substitutes (a probe's own SkyLight
  `gCurMsgAudit` is a different variable than WS's — R003 correction).

Consequence: current-generation `gCurMsgAudit` container/size/inner
results are **unread**. The populated-audit witness (52-byte creds,
sender pid) is **historical — a prior generation** (R002-era
`#### CGXSYNTH` telemetry). Current WS err log: `CGXSYNTH` count = 0 —
the diagnostic never armed this generation (sentinel absent, correctly).

## 3. Observation-patch design (NOT applied)

Minimal WS-local observation of the deny chain, function-boundary only,
off by default, no decision/return-value modification.

Image pin: SkyLight-22F82, LC_UUID
`96676A53-B1E0-3D7E-B98B-B73873CD1880`, `__TEXT` va `0x1850e5000`.
All three sites have verbatim-capable prologues (verified byte-exact in
`analysis/SkyLight_22F82`); all three go through the existing
`MACWSHookPermitV2` fail-closed path (uuid + offset + prologue-words).

| site | file off | prologue words (verified) | observation |
|---|---|---|---|
| `CGXSenderCanSynthesizeEvents` | `+0x318464` | `d503237f d101c3ff a9067bfd 910183fd` | call count; snapshot `gCurMsgAudit` ptr, size, euid, pid; orig rc — **already implemented** (`macws_diag_sender_can_synth`, sentinel+isWS+permit gated) |
| `WSAuditTokenHoldsCachedProcessRight` (dispatcher) | `+0x3407ec` | `d503237f d102c3ff a90667fa a9075ff8` | entry count; return value; audit.pid/pidver → distinguishes self-path vs cached vs miss |
| `audit_token_check_tcc_access` (impl) | `+0x340da0` | `d503237f d10183ff a90357f6 a9044ff4` | entry count; orig return (0=pass at this callsite, `cset eq`) → the decisive TCC boundary |

Bounded state: one fixed-size struct (per-site `u64 calls`, `u32
last_rc`, plus at the gate ≤0x34-byte audit snapshot + 4-slot ring of
recent results) in a static page; written only inside wrappers; read
out via a named diagnostic socket (new `macws_obs_dump.sock`) or stderr
`#### CGXSYNTH*` lines — same transport the CGXSYNTH diagnostic already
uses. Gating: `isWS` executable check AND `macws_runtime_diagnostics`
sentinel file AND per-target permit success — identical to the existing
diagnostic contract. Wrappers save/restore `errno`, never alter any
return value, never touch the rights-cache or audit contents.

Explicitly not part of the design: no constant-success returns, no
rightDesc/rightsCache mutation, no audit-token rewriting, no alternate
display producer sentinel, no policy or responsibility changes.

## 4. One-variable root-cause experiment

**Variable:** arm *only* the impl-boundary counter —
`audit_token_check_tcc_access @ +0x340da0` (plus the already-present
gate counter at `+0x318464` as the denominator). One bounded
`SLSPostEventRecord` canonical post (the same shape D041 used once,
`rc=1002`), nothing else changed.

**Causal acceptance criterion — exact branch discrimination:**

**D068 correction (preserved):** the two `impl_rc` rows below were
written with the direction inverted. The impl disasm
(`SkyLight_22F82:0x185425da0`, `blraaz` → `cmp w0,#0; cset w0,eq`)
returns `(preflight_rc == 0)`, so `impl_rc != 0` means TCC **allowed**
and `impl_rc == 0` means nonzero-or-unresolved. Additionally, an
absent impl record does not prove an absent call (install proof +
drop accounting required), and `impl_rc=0` is not an explicit verdict —
it conflates unresolved-symbol, evaluation-failure, and true denial
(D068 §0/§4). Corrected rows follow the originals verbatim.

- `gate_calls=1`, `impl_calls=0` → deny authored **inside the
  dispatcher's early paths** (auid/`pid==0` route or cached deny bits on
  key `pid|pidver<<32`) → next variable is the cache key / rightDesc
  bit layout, *not* TCC. *(D068: only valid with hook-install proof and
  zero drops; otherwise inconclusive.)*
- ~~`impl_calls=1`, `impl_rc≠0` → deny authored by
  `TCCAccessPreflightWithAuditToken`~~ → corrected: `impl_rc≠0` means
  preflight returned 0 = **allowed**; if gate_rc is still 0 the deny is
  authored downstream (cache-fold).
- ~~`impl_calls=1`, `impl_rc=0`, `gate_rc=0` → TCC passed~~ → corrected:
  `impl_rc=0` means preflight returned **nonzero** (denial *or*
  evaluation failure) or the symbol was never resolved — check
  `lazy_post` to split the two, and the tccd-side ERROR line /
  reply-fill to split failure-vs-verdict.

Each outcome names a different producing layer — that is the point of
one variable. **Not** a retry of the same post.

## 5. Evidence-boundary ledger (FACT vs historical vs THEORY)

- FACT (this generation, this round): composite `ready`, producer
  10447, seq 1176→1529; `SLSPostEventRecord→1002` and
  `HIDEvents→1102` stand (D041, same live generation); installed code =
  repo builds (§1); no sentinels armed; `CGXSYNTH` count 0.
- FACT (historical, prior generations — do not cite as current-gen
  proof): `gCurMsgAudit` populated 52-byte creds on posts; dispatcher
  →impl chain returning 0; `#### CGXSYNTH` telemetry.
- THEORY (unchanged): `responsibility103` as the permission-cache key
  mismatch; empty tccd reply dict (count=0) = transport-up /
  content-empty boundary — **not** an explicit TCC policy denial.
- OPEN: which of the §4 branches authors the 1002 — exactly what the
  impl-boundary counter answers.

## 6. Health + hygiene at close

- Desktop: `state=ready producer=10447 sequence=1529
  reason=validated-final-composite`; workspace graph `ready`
  (wallpaper/dock/menubar/backing), Finder 32 + Terminal 27 layers live.
- No owned jobs/probes running; no files left armed on device; nothing
  installed, patched, or restarted this round.
- D041 doc corrected: "user's newer deployment" → repo-build provenance;
  transient keyboard sentinel now disclosed in the hygiene list.
