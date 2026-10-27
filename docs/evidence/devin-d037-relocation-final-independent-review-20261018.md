# D037 — final independent review: exact-relocation relocator + gated A/B acceptance

Date: 2026-10-18 · Worker: Devin frontend 9589 · Scope: read-only local
review; no device contact, no backend-terminal contact.
Task: `coordination/_relay/devin-037-frontend-task.md`.
**Consolidates and supersedes** the D035 P1 verdict and the rolling
D036 check notes — this is the single dated final approval document.

## 1. Final candidate under review

Source (working tree, uncommitted — verified at review time):

- `misc/cydiasubstrate_macos.m` — exact-relocation `MSHookFunction` +
  `MACWSHookPermitV2` (uuid/offset/prologue pins, fail-closed, cap 32),
  named-refusal telemetry, arm64e `*result` signing.
- `libmachook/mac_hooks.m` — 4 pinned `MACWSHookPermitV2` registrations
  (IOMFB `publicSwapEnd` @+0x11cc; QC `enable_frame_info_tag_list`
  @+0x29285c; QC `begin_skylight_update` @+0x291288; QC
  `finish_skylight_update` @+0x291220), per-build IOMFB/QC uuid pins.
- Tests: `misc/test_mshook_relocate_host.c` (x86_64 byte/exec-class),
  `misc/test_mshook_relocate.c` (arm64 live, device-staged).

Built artifacts (theos, post-P3 build 23:34:56; frontend re-measured):

- provider fat `82e7451975781007749db91ccf46d3b5e50b1b6b303a8977b3d51a7d4be9e3a9`
  (arm64 `2a8da5bf…a8468b3`, arm64e `3bde9239…d98855b`)
- libmachook arm64 `bf3ae467…dc30`, arm64e `f2143308…9146` (unchanged D035)

Superseded artifacts that must NOT deploy: fat `7172dd68…` /
`8cde7566…` (pre-P0/P3 fixes), D034-v1 12-permit libmachook
(`56c4bc76…`), R010-era provider backups (permit-gate, not known-good).

## 2. What was independently verified (host, rerunnable)

Frontend's own runs, not transcription of backend claims:

- x86_64 `MACWS_RELOC_TESTONLY` compile of the settled provider +
  `test_mshook_relocate_host.c` → **ALL PASS**. Emitted thunk for the
  real wrapper window: `b50000a0 58000051 d61f0220 <S+0x10> f9439401
  b50000a1 58000051 d61f0220 <S+0x10> d61f083f`. Live refusals observed:
  unrelocatable pcrel, blr-family, intra-window cbz target, no-permit.
- `analysis/devin-d035-frontend/permit_audit.py` §4 simulation of all 4
  pinned windows — every translated branch lands exactly; QC windows
  verbatim + branch-back.
- Capstone decodes: stub = `cbz x0,+0x10 / ldr x1,[x0,#0x728] /
  cbz x1,+0x10 / braaz x1` (X-register + authenticated tail, correcting
  earlier w-register/br readings); encoding space for braa/brab/blraa/
  blrab and Z-forms enumerated.

Corrections frontend contributed that are now in the tree: imm19=5 skip
(was 4 — landed mid-.quad), `brabz 0xd61f0c1f`, `blrabz 0xd63f0c1f`
(were undecodable), intra-window-target refusal (was silent
mis-relocation into the patch stub).

## 3. Coverage matrix vs the coordinator's test clauses

| Clause | Status | Evidence |
|---|---|---|
| cbz taken / non-taken | verified host+sim | emitted .quad = orig target both ways; landing-offset asserted |
| null-object / null-target path | sim-verified, live pending | both skip paths → S+0x10 |
| X-register cbz (real target shape) | classifier verified; LIVE PROOF PENDING | mask `0x7e000000` is sf-agnostic, inversion preserves sf — but existing live fixture used `cbz w0/w1`; exact X exec = backend D037 probe |
| braaz / signed-tail execution | verbatim-copy correct by construction; LIVE PROOF PENDING | braaz is position-independent; real `[x0+0x728]` is A-key/zero-signed — needs arm64e signed-target probe |
| `*result` PAC contract | verified source | `#if __arm64e__` signs thunk `(asia,0)` — matches blraaz callers; refusal returns un-stripped `orig_symbol` |
| x17 scratch | audited | IP1 only; residual = mid-window x17 read across translated cbz (same as classic Substrate; no current target) |
| intra-window negative | refused live | `g_intwin_window` fixture → REFUSED |
| call/return refusal | refused live | blr/blraaz/blrabz → REFUSED; ret/br/braaz → verbatim+control_leaves |
| uuid/prologue/offset gate | verified | -2 uuid, -1 prologue, -1 offset; re-check at hook; cap 32 |
| atomicity/quiescence | documented limit | patch isn't atomic vs concurrent callers; ctor-time install only |
| **b.cond/tbz/b/bl/adr/adrp/ldr-literal** | **still REFUSED — approval does NOT extend to these classes** | pc_relative classifier unchanged |

Approval scope is therefore: *verbatim position-independent insns,
cbz/cbnz translation with in-window refusal, non-returning register
branches verbatim + suppressed branch-back* — the four pinned targets
only. Not a general relocator endorsement.

## 4. Generation-plan validation (D034 plan v3 + D037 backend gates)

Sound: preserve-by-hash+cp; live IOMFB LC_UUID read-back abort gate
(fail-closed -2 on slice mismatch — the uuid pin may be arm64e's);
watchdog stopped only via launcher-owned path with exit verification;
one bounded ≤300s generation; byte/counter/state/pixel ladder with
stop-at-first-failure; no diagnostics-marker or alternate-producer
enablement (argv copied verbatim from watchdog = no confound);
bundle declared as one functional variable (4 permits + relocator).

**Gaps to fix in the deploy step before use** (P2 — mechanics, not design):

1. `scp` writes the file in place — the documented inode landmine:
   overwrite-truncates an inode that may still be mapped by a running
   image. Deploy must be write-to-temp + atomic `mv` (rename → new
   inode), then sign/trust as required.
2. No explicit re-sign/`ldid` step in the deploy line — backend task
   says "atomic deploy/sign/trust"; the plan's step 3 should state it.

## 5. Precise acceptance / rejection criteria for the A/B

ACCEPT (each rung, in order; all required):

1. Live probes (arm64e): exact `cbz x0/x1` + `braaz` window replays with
   correctly A-key/zero-signed target — replacement count, callable
   original, taken/non-taken, null cases, and the negative fixtures all
   behave. arm64 plain-BR variant recorded separately — no false
   equivalence claimed.
2. Live IOMFB LC_UUID == pinned `7AC531B7-05F5-38FE-92E5-DA6D86D22A14`
   for the slice WS actually loads; prologue bytes at +0x11cc ==
   `b4000080 f9439401 b4000041 d61f083f`. (Any mismatch → stop, -2/-1
   is the expected refusal, not a bug.)
3. Post-install live bytes at img+0x11cc == `58000051 d61f0220` +
   `.quad <replacement>` — changed-bytes witness, not a log line.
   Same for the three QC offsets.
4. `CANCEL-COMPLETION`/`macws_iomfb_complete_cancelled_swap` counters
   advance; `replay-receiver-ready` present.
5. `macws_final_composite.state`: `state=ready`, producer≠0, sequence
   advancing across ≥1s.
6. Pixels: real framebuffer witness — wallpaper, Dock, menu bar.

REJECT / rollback triggers: any residual `REFUSED` on the four targets
(enumerate `image+offset sym=`); no changed bytes; counters flat;
`state=missing`/`sequence=0` persists; crash-delta beyond baseline
(F11H/MTLCompilerService class excluded as separate-axis but logged);
pixel witness absent. On rejection: stop generation, restore preserved
binaries+trust, no restart loop.

## 6. Pending live inputs (backend D037, not yet delivered)

- arm64e probe transcript (X-register cbz + authenticated tail,
  null paths, blraaz callable-original).
- Live IOMFB arm64 vs arm64e slice uuid measurement.
- WS-side changed-bytes dump + named-refusal enumeration.
- displayd state/counter series + framebuffer pixel witness.

Until these exist, the approval stands for *source/build/tests only* —
exactly what the coordinator's gate requires before the single bounded
owned-generation A/B.

## 7. Post-handover fixture review — corrected `test_mshook_relocate.c`

The live fixture was corrected at 23:42 (supersedes the W-register/plain
BR version the task flagged). Frontend review of the new source:

- `swapend_shim` emits the **exact stolen window words** via `.long`:
  `b4000080 cbz x0,+0x10 / f9439401 ldr x1,[x0,#0x728] / b4000041
  cbz x1,+8 / d61f083f braaz x1` (arm64e) or `d61f0020 br x1` (arm64),
  plus `ret` continuation at S+0x10 — the real 64-bit/authenticated
  class, not the earlier w0/w1/br shape.
- fake_fb carries the tail pointer at `+0x728`, signed
  `ptrauth_sign_unauthenticated(target, asia, 0)` on arm64e — the exact
  signature class `braaz` authenticates; plain pointer on arm64.
- Case matrix covers every clause: NULL fb → cbz-x0 path, no tail;
  NULL impl → cbz-x1 path, no tail; signed impl → tail once,
  `g_tail_arg == fb` (x0 preserved through braaz); callable original
  replays all three paths with zero replacement re-entry.
- Refusal fixtures all asserted callable-orig: `win_bcond`,
  `win_blrabz` (corrected mask exercised on arm64e — `blrabz x0`;
  `blr x0` on arm64), `win_intarget` (intra-window cbz — the frontend's
  P3 case), `win_nopermit` (unregistered).
- Changed-bytes asserted: `patched[0..1] == 58000051 d61f0220` and
  `memcmp(before, patched, 16) != 0`.

Verdict on the fixture: correct and sufficient for the probe rung of
the §5 ladder — pending only its actual execution on device arm64e.
The residual gap is unchanged: the probe proves the relocator
mechanism; the production pin additionally requires the live IOMFB
uuid/prologue match (plan abort gate) before any patch lands in WS.

## 8. Residual-refusal landscape — expected noise vs signal in the A/B

`MSHookFunction` appears 38 times in `mac_hooks.m` and 34 in
`Metal_hooks.x` (call sites plus a few comment/def lines). Only the 4
pinned targets are registered — every other site the WS code paths
actually reach will produce a named `REFUSED … image+offset sym=` line.
That is EXPECTED, not regression: the currently-running frameless
watchdog generation already produces 21–58 such refusals per boot —
proving the ~50+ unpermitted sites suffice for "alive but frameless"
and are not needed to reach the desktop-start path.

Interpretation guide for the deployer:

- Refusals on the 4 pinned targets = acceptance-ladder failure (rung 3).
- Refusals on any other target = baseline noise; DIFF the list against
  the pre-A/B generation's refusal set rather than eyeballing the count.
- If all 4 install + counters advance but `state` stays missing → the
  next pin is downstream (SkyLight composite sites were deliberately
  deferred; pin narrowly, same audit).
- Note: several refused sites are the historical
  diagnostics/band-aid class (`hooked_assert_rtn`, `hooked_IOHID_noop*`,
  filecache/composite diagnostics) — the permit gate incidentally
  disarms symptom-suppression hooks AGENTS.md flags as bad practice.
  Their refusal is containment, not a bug.

This framing also sharpens causality: the delta between the live
frameless generation and the Oct-12 visible generation, at the provider
layer, is exactly the set of hooks the 4 permits restore (given the
chain audit: SwapEnd→cancel-completion→frame-info→after-swap-publish→
displayd). A positive A/B is therefore not just a fix — it is the first
direct target-level causal witness for the D033 attribution.
