# Devin D034 frontend review — permit-chain change-set audit (2026-10-18)

Reviewer: Devin CLI **frontend** D033/D034 (independent audit of the
uncommitted backend change). Scope: local source/binary audit only — no
device access, no generation, no edits, no builds of my own. The change
set was **revised while under review**; both versions were captured and
are compared below. Audited final state = the 4-permit minimum set.

Files reviewed (uncommitted working tree, on top of HEAD `c97e219`):
- `libmachook/mac_hooks.m` — +90/-9 (permit registrations + ABI uuid pins
  + the pre-existing R012 `_xpc_bootstrap_services` PAC hunk)
- `misc/cydiasubstrate_macos.m` — +28/-4 (named-refusal logging)
- Built objects: `libmachook/.theos/obj/{arm64,arm64e}/libmachook.dylib`,
  `CydiaSubstrateProvider/.theos/obj/macosx/CydiaSubstrate`

## 1. Two captured versions

| | v1 (seen ~23:03) | v2 — CURRENT (built 23:11) |
|---|---|---|
| permit sites | 12 (IOMFB SwapEnd, QC tag_list, QC pacing ×2, **8 SkyLight**) | **4** (IOMFB SwapEnd, QC tag_list, QC pacing ×2) |
| SkyLight approach | `macws_sl_hook_permitted` read live 16-byte prologue at resolved sym, pinned hardcoded `kMacWSSkyLight22F82UUID` | **removed entirely** — SkyLight sites stay refused |
| IOMFB identity | live LC_UUID read at runtime (`expected_uuid16==NULL` fallback) | **pinned in ABI table**: Ventura row `7AC531B7-05F5-38FE-92E5-DA6D86D22A14`; 15.6.1 row `{0}` → fail-closed by design |
| helper | `macws_macho_image_uuid` LC_UUID walker (kept) | removed (no live self-pinning — stricter) |
| built arm64 | `56c4bc76…0a76c` | **`bf3ae467…dc30`** |
| built arm64e | `bc2b549c…a6700` | **`f2143308…9146`** |
| provider fat | `694be5a2…d41d2d` | `694be5a2…d41d2d` (unchanged — provider diff identical) |

**v2 is the better design.** Live-prologue self-pinning for
symbol-resolved SkyLight sites added no verification strength (expected
bytes were copied from the target itself) while adding a PAC-read caveat
on arm64e and widening the first A/B's surface. The IOMFB uuid moving
from runtime-read to table-pinned closes the "permit whatever happens to
be loaded" gap. The doc `devin-d034-permit-chain-20261018.md` describes
v1 — **its artifact table and the "12 sites" claim are now stale**;
backend should update it or a follow-up doc should pin v2.

## 2. Contract verification (v2)

Provider contract (`misc/cydiasubstrate_macos.m`, unchanged semantics):
`MACWSHookPermitV2(image_base, expected_uuid16, image_offset, expected4)`
→ -1 bad-args/prologue-mismatch, -2 live-uuid≠pin, -3 no LC_UUID, -4
table full (32). `macws_permit_for` resolves at patch time by
`symbol − image_header == permit.image_offset` AND live-uuid==pin.

| site (mac_hooks.m) | permit args | ordering | verdict |
|---|---|---|---|
| IOMFB `publicSwapEnd` :6336 | `iomfbAbi->uuid` (pinned `7AC531B7…` on Ventura), `publicSwapEndOffset`, `expectedSwapEndWrapper` (4 RE'd words) | after `memcmp` wrapper match, before `MSHookFunction` — offset convention identical (`header+off` → `sym−header`) | ✅ correct; refusal → skip + named log |
| QC `enable_frame_info_tag_list` :19903 | `abi->uuid` (QC table row, already live-uuid matched at :19897), `enableTagListOffset`, `enablePrologue` | after double verification, before install; refusal → resets `g_macws_qc_frame_info_abi`/header/`installed` → retry-capable | ✅ correct |
| QC pacing begin/finish :20038-40 | same row's `begin/finishSkylightUpdateOffset`+prologues | both permits before either `MSHookFunction`, after existing double memcmp; pair-skipped on any refusal, `_Atomic installed` reset | ✅ correct |
| `macws_register_hook_permit` :1785 | dlsym-cached `MACWSHookPermitV2`; absent → -9 → caller skips | per-process `dispatch_once`; provider is a linked dependency → resolved before use | ✅ fail-closed |

Structural facts confirmed:
- `FORCE_M1_DRIVER=1` compiled (in-source `#define` :1705 + Makefile
  `-DFORCE_M1_DRIVER=1`) → the QC install block at :6620-6625 is live;
  gated at runtime by `g_macws_iomfb_coexist_swap_cancel` (armed :6388
  on kern_SwapEnd callsite verification — pre-existing semantics,
  independent of the new permit outcome).
- Owned-scanout pool (`swizzle2`/`method_setImplementation` texture
  hooks) and `IOConnectCallStructMethod` interposes are **not**
  provider-gated — the publish side downstream of cancel-completion is
  intact.
- Permit budget: 4 + 1 (CGXSYNTH diag, marker-gated) ≪ 32.
- Provider log path safe: `macws_hook_log` = raw `write()` (no
  printf-format on the snprintf buffer); `dladdr` gets the
  PAC-**stripped** symbol while `target=%p` prints `orig_symbol` (the
  caller's signed-as-passed pointer) — both correct.
- Binary provenance: v2 objects contain exactly the 3 new permit strings
  (SwapEnd/QC-observer/pacing) and none of the v1 SkyLight strings —
  build matches current tree.

## 3. Chain-completeness proof (no unpermitted MSHook link remains)

Every provider-gated link of the publish chain is covered by the 4-site
set; every other link is provider-independent:

| chain step | mechanism | provider gate |
|---|---|---|
| QC → `IOMobileFramebufferSwapEnd` | MSHook `publicSwapEnd` (permitted #1) | ✅ covered |
| SwapEnd → SwapCancel(sel5→0x34) | `DYLD_INTERPOSE(IOConnectCallStructMethod)` `:27967`, armed by `g_macws_iomfb_coexist_swap_cancel` | none — dyld |
| QC cancel-completion registration | MSHook `enable_frame_info_tag_list` (permitted #2) | ✅ covered |
| pacing outside QC server locks | MSHook begin/finish `skylight_update` (permitted #3-4) | ✅ covered |
| `macws_iomfb_complete_cancelled_swap` → `publish_graphics_ready`/`publish_scanout_after_swap` | internal calls `:21690-21699` | none |
| owned BGRA scanout pool | `method_setImplementation`/`swizzle2` texture hooks | none — objc |
| `MarkContentValidated`/`PublishSurface`/replay receiver | publisher + Mach service | none |
| displayd consume | Mach service | none |

Corollary: if the A/B generation shows the four installs but still no
composite, the gap is **not** a provider refusal — it is upstream swap
flow (no cancels issued) or content validation — and the remaining named
`REFUSED`/`MSG-REFUSED` lines plus marker-free `####` lines will show
which.

`MSG-REFUSED bad args` ×2 pre-attributed (F4) so they aren't misread as
chain failures during the A/B readout.

## 3. Findings

**F1 — deploy coupling (must document):** every gated site now requires
a V2-capable provider. New libmachook + pre-V2 provider → all sites
"permit refused rc=-9" → nothing installs. The installed device provider
`b2ae21da…` already speaks V2, so the *strictest* one-variable A/B is
**libmachook-only**; shipping the new provider is justified solely by
named-refusal diagnosability (agree with backend recommendation) — but
never deploy new libmachook against a pre-V2 provider.

**F2 — SkyLight set dropped (accepted risk, monitor):** the .bak-era
SkyLight tolerate-nil/composite surface stays refused. Mitigating fact:
the live WS gen survived 21 min+ with all of them refused → not needed
for WS liveness. Remaining risk: if normal compositing hits the
tolerate-nil path (`'&b38'` compressed-surface wrap → nil), frames still
won't land even with transport restored. The A/B readout must therefore
watch for `composite_destination`-nil/assert lines in addition to the
publish milestones; if pixels still fail after transport is green, the
8-site list (offsets via symbol resolution, uuid `96676A53…` pin) is the
queued next increment.

**F3 — stale artifact identities in the D034 doc:** recorded
`56c4bc76`/`bc2b549c` are the v1 build; v2 objects are
`bf3ae467…dc30`/`f2143308…9146`. Re-record at deploy time; treat the
doc's "12 permits" table as superseded.

**F4 — MSG-REFUSED ×2 attribution (resolved in-source):** the two
`MSG-REFUSED bad args` are the two methods of `%hook NSScreen`
(`Metal_hooks.x:24223-24232`, `maximum*ExtendedDynamicRangeColor
ComponentValue`). NSScreen is an AppKit class; WindowServer does not
link AppKit → theos-generated init calls `MSHookMessageEx(NULL,…)` ×2.
`%hook MTLTextureDescriptorInternal` resolves (Metal is loaded in WS).
All explicit `MSHookMessageEx` call sites in libmachook are NULL-guarded
— none can produce bad args. The `QC-DESKTOP-TARGET`/`MACWS-METAL-TARGET`
swizzles install via `class_addMethod`/`method_setImplementation`
(`Metal_hooks.x:22890-23010`) — **provider-independent**, never refused;
they either install or silently skip on missing selectors behind
marker-gated "missing selector" logs. Harmless-but-noisy: the refusals
will read `class=(null) sel=maximumPotential…` under the new named log —
expected, not a new defect. Optional hygiene: wrap the `%hook NSScreen`
in a class-existence gate so the refusal telemetry stays clean.

**F5 — minor robustness:** `MSG-REFUSED` log calls
`class_getName(_class)`/`sel_getName(sel)` on non-NULL pointers — a
garbage non-NULL class arg could fault inside the log path. Observed
failures are NULL-arg cases; low risk, worth a one-line guard later.

**F6 — IOMFB uuid pin unverifiable locally:** `7AC531B7-05F5-38FE-92E5-
DA6D86D22A14` is sourced from the device-side rootfs extraction
(`.tbd`-only locally). Failure mode is benign (-2 → named skip), but the
owned-generation preflight should read the live IOMFB LC_UUID and
compare — catches a typo'd pin before interpreting a silent chain.

**F7 — 15.6.1 row `{0}` uuid:** deliberate fail-closed; no behavioral
delta under the permit-gated provider (it refused anyway). Consistent.

**F8 — ordering dependency (pre-existing, unchanged):** QC observer
install requires `g_macws_iomfb_coexist_swap_cancel` armed, which needs
IOMFB's image-add to precede QuartzCore's — held on Oct-12; a changed
load order would silently skip the observer (log line present).

**Non-findings:** no symptom-suppression patterns (no NOP/constant-return
/bypass); every change either authorizes through the existing contract or
adds attribution; permit refusals skip installs rather than forcing
them — the fail-closed invariant is preserved end-to-end.

## 4. Audit of `devin-034-owned-generation-plan.md`

Sound as written (backup-first, live-uuid verify, watchdog-aware stop,
bounded start, milestone ladder, evidence discipline, rollback).
Refinements to suggest, none blocking:

1. Preflight step 2 should compare the live **IOMFB** LC_UUID against the
   pinned `7AC531B7…` (and QC against `CF853BBD…`) — not only SkyLight;
   under v2 the SkyLight uuid no longer gates anything.
2. Deploy step should re-record the actual v2 object hashes
   (`bf3ae467…`/`f2143308…`, provider `694be5a2…`) — the doc's table is
   v1.
3. Deployment mechanics (fresh inode for the signed mapped file, `ldid
   -S/path`, trustcache refresh, iOS→macOS platform marker) are load-
   bearing historical requirements — restate in the plan to avoid a
   CODESIGNING Invalid Page or trustcache miss being mistaken for a
   permit failure.
4. Add a negative-witness readout: `REFUSED` lines that *remain* are
   expected (non-permitted sites) and are now named — diff them against
   the R013-era 21-line set to prove the permitted four are no longer
   among them.
5. Keep `--no-vnc` for the A/B (identical to control); VNC-mode re-test
   of the `graphics_ready` gate is a *second* experiment — do not
   combine.

## 5. Verdict

**Approve for deploy as a bounded owned-generation A/B** — the change set
correctly implements the D033 recommendation, preserves the fail-closed
contract, and every residual risk is either detectable in the readout
ladder or already flagged. Conditions: land the doc corrections (F3),
honor the provider-coupling constraint (F1), and treat any pixel failure
after a green transport chain as the queued SkyLight-permit increment
(F2) rather than a refutation.

Reproducible audit commands:

```sh
git diff libmachook/mac_hooks.m misc/cydiasubstrate_macos.m
grep -n 'macws_register_hook_permit\|MACWSHookPermitV2\|uuid\[16\]' \
  libmachook/mac_hooks.m misc/cydiasubstrate_macos.m
shasum -a 256 libmachook/.theos/obj/arm64/libmachook.dylib \
              libmachook/.theos/obj/arm64e/libmachook.dylib \
              CydiaSubstrateProvider/.theos/obj/macosx/CydiaSubstrate
strings libmachook/.theos/obj/arm64/libmachook.dylib | grep 'permit'
strings CydiaSubstrateProvider/.theos/obj/macosx/CydiaSubstrate | grep REFUSED
```

Limits: no runtime validation; uuid pins F6-verified only by fail-closed
behavior; the v1→v2 mid-review revision means the D034 evidence doc and
this audit describe different trees — v2 (23:11 objects) is the one this
verdict covers.
