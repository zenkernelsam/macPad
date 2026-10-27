# Devin D033 — visible-desktop baseline audit (2026-10-18)

Worker: Devin CLI **frontend** D033. Task:
`coordination/_relay/devin-033-frontend-task.md`.
Scope: **local, read-only reconstruction and source audit**. No generation
was started, nothing was deployed, built, signed, packaged, committed, or
sent to the device. No marker, trustcache, source, or Git state changed.
The only working-tree modification pre-existing this audit is the
uncommitted R012 `__xpc_bootstrap_services` PAC hunk in
`libmachook/mac_hooks.m` (left untouched).

Sibling device-side inventory produced in parallel:
`docs/evidence/devin-d033-device-regression-inventory-20261018.md`
(backend D033). Where this doc cites on-device log facts (`.bak` /
`.previous` hook-line counts, live-generation PIDs, installed SHAs), the
source is that inventory; this doc adds the source-side mechanism proof
and the local artifact/source-revision reconstruction.

Verdict labels: **FACT** = verbatim log/hash/diff evidence;
**OBSERVATION** = source-read structural fact; **THEORY** = inference
needing the stated confirmation; **REJECTED** = bounded out.

---

## 1. Verdict

The visible-desktop transport did not die in libmachook's producer code —
the producer code is **byte-identical** to the 10-13 baseline. It died at
the **hook-provider link**: every `MSHookFunction` target WindowServer
needs for the swap-completion → final-composite chain is now *refused* by
the deployed CydiaSubstrate provider, whereas on the last-rendered
baseline era the provider installed the full hook surface.

```
10-13 era (rendered):
  working provider → MSHook(SkyLight/QC targets) installed
    → SwapEnd hook → coexist selector-5→0x34 SwapCancel translation
    → QC cancel-completion machinery fires (registration recorded)
    → macws_publish_graphics_ready()          [start gate passes]
    → macws_publish_scanout_after_swap()       [per delivered swap]
    → MarkContentValidated → PublishSurface → replay receiver
    → displayd accepts → state=ready, seq 278+  → VISIBLE DESKTOP

R013/live era (zero frames):
  permit-gated provider, zero permits registered in production
    → every MSHookFunction target REFUSED (21–58 refusals/WS gen)
    → "SwapEnd hook installed" absent → entire chain dead at link 1
    → no registration, no publish, no replay receiver, no graphics_ready
    → displayd: state=missing producer=0 sequence=0
    → start must use --no-vnc (process-only readiness) to pass at all
```

### Regression boundary

| era | provider on device | WS hook surface | composite |
|---|---|---|---|
| ≤ Oct 12 (`WindowServer.err.bak`, 03:37) | **working MSHook** (unidentified binary) | SwapEnd ×10, SkyLight PrepareForUse/StartComposite/EndCurrentComposite/EndUpdate, QC-DESKTOP-TARGET, MACWS-METAL-TARGET …; **REFUSED=0** | `replay-receiver-ready`, `replay-received` ×6, CANCEL-COMPLETION ×654 — **chain live** |
| Oct 13 | same working era (doc: `state=ready`, seq 278+) | same | **visible desktop** |
| Oct 14 R001 | **no-op scaffold** found deployed (framework UUID `DFF425E9-1EC1-3E58-AF90-321F521CB803`; `*result=symbol`, target never written) | install lines = attempts only | dead |
| Oct 14 R002 | repaired real trampoline (`4841131` build, probe `VERDICT=INSTALLED`) | restored | last `state=ready` bounded ≤ Oct 14 16:20 (WS 59329 generation; attribution THEORETICAL — could be tail of working era) |
| Oct 14/15 R003 | **permit-hardened** (`685624f` build — fail-closed without `(uuid,offset,prologue)` permit) | all targets REFUSED except the lone CGXSYNTH diag gate (itself marker-gated → 0 permits in production) | dead since |
| Oct 18 19:30 (R012 package install) | `b2ae21da…e1423e7` (permit gate present) | REFUSED=58 (R013), 21 (live) | **zero frames** on R012 19:33 / R013 19:50 / live 22:27 gens |
| preserved "R010 baseline" backup | `7320a210…7337` — **permit gate present** | — | NOT a known-good for this defect |

The boundary for the *rendered* desktop is therefore the provider
capability transition (working → scaffold/permit-refusal), bounded
post-Oct-12 (working logs) and pre-Oct-14-R001 (scaffold found). The
`state=ready` sighting ≤ Oct 14 16:20 is plausibly the R002 repaired-
provider generation or the tail of the working era — **THEORY** pending
deploy-log correlation.

---

## 2. Reconstructed last-rendered baseline (10-13)

### 2.1 Generation and launch

- Launch: `bash /var/jb/usr/macOS/bin/macos_gui.sh start coexist`
  (VNC enabled — the era's normal mode; `--no-terminal` optional).
- Workspace graph witness (10-13 doc, `ventura-22F82-input-transport-and
  -composite-20261013.md`):
  `canvas=2732x2048 wallpaper=ready dock=ready menubar=ready backing=ready`;
  WS pid 87761, displayd 88080, Dock 88216, Terminal 88250, Finder 88150,
  ControlCenter 88156, OSXvnc 83507 (final recorded graph; earlier gen
  WS 59329).
- Composite witness: continuous `final-composite-received sequence=278+`,
  attributed in-era to `macws_publish_scanout_after_swap` (EndUpdate
  confirmed dead on 22F82: `MetalContext::EndUpdate` never reached —
  documented + runtime `ENDUPDATE-DIAG=0`).

### 2.2 Source revision

- Baseline-era HEAD ≈ `b1f9bb9` / `3cb8e21` (10-13 10:41–11:41).
- Producer commits: `c48a81d` (10-13 00:48 — `macws_hook_agxtex_iosurf_init`
  owned-scanout substitution + `try_on_clean` + fullscreen pointer) and
  `0614a5b` (10-13 02:00 — `macws_publish_scanout_after_swap` +
  per-delivered-swap publication, 124 lines in `Metal_hooks.x`).
- `git diff c48a81d..HEAD -- libmachook/Metal_hooks.x` → **empty**;
  `mac_hooks.m` diff = TCC service-name map + CGXSYNTH permit hunk +
  R012 PAC fix only. **Producer path unchanged since baseline.**

### 2.3 Deployed artifacts (best-available reconstruction)

| artifact | 10-13 identity | evidence class |
|---|---|---|
| WindowServer job | `SkyLight.framework/Resources/WindowServer -virtualonly -daemon`, UUID `96676A53-B1E0-3D7E-B98B-B73873CD1880` (22F82) | recorded |
| QuartzCore | Ventura 13.4 UUID `CF853BBD-01B6-3F46-ADA1-EC70FD2DC9DC` | recorded |
| libmachook | thin slices `/usr/local/lib/libmachook.dylib` (arm64e) + `libmachook_arm64.dylib` (arm64); **deployed SHA-256 not recorded for 10-13** — nearest recorded deploy = Oct 14 R004B: pre-sign fat `f1927e21…`, staged `fe93559c…`(arm64e)/`d81e2d0c…`(arm64), post-sign `c408dc94…`/`56224d25…` | **GAP** |
| provider | the **working** pre-scaffold binary — hash/UUID never recorded; `.bak` logs prove capability, not identity. Candidates on device: `CydiaSubstrate.old/.old2/.old3` beside the framework (unverified) | **GAP** |
| package | `com.kdt.macosbooter` — version at 10-13 not pinned in surviving docs (0.3.4 installed today) | partial |
| `macos_gui.sh` | era blob ≈ `c48a81d`+ (diff vs HEAD = TCC-lifecycle plumbing only; WS launch flags/env identical) | reconstructed |
| wallpaper | `DefaultDesktop.heic` replaced **on-device** with static PNG payload (dynamic Solar-variant decodes black in chroot); post-substitution file hash not recorded. Packaged payload today: `/usr/local/share/macws/wallpapers/macws-forest-lake.png` SHA `4931a89d…d1806`, applied via `macwsworkspacectl set-wallpaper` (20 s bound) | partial (**GAP** on file hash) |

### 2.4 Environment / flags

- `DYLD_INSERT_LIBRARIES` → thin-slice libmachook (fat dylib rejected by
  chroot dyld — historical fact).
- WS job env: `--no-vnc` era sets `MACWS_VNC_SHARE=0`; the 10-13 VNC-mode
  run did **not** set it → `macws_vnc_share_enabled()` →
  `MacWSProductionDefaultEnabled` → ON. VNC share gates only the RFB
  mmap CPU bridge, **not** `PublishSurface` — see §5.3.
- Diagnostics sentinel `/var/mnt/rootfs/(private/)tmp/
  macws_runtime_diagnostics`: era docs record it **present on device**
  (`§8.4`/`§8.6`; `DEVIN_STATUS:545` "still present"); production
  `enable_experimental_if_requested` strips it at every `start`
  (`macos_gui.sh:5725-5758`, in tree since before the baseline). Whether
  the productive 10-13 WS generation itself booted with it armed is
  **unresolved** — but is also *not load-bearing* for the primary
  producer (the SwapEnd chain is marker-independent; see §3).

---

## 3. Producer chain — source-proven links

All line cites are HEAD `c97e219`; identical at baseline unless noted.

| # | link | primitive | gate | 10-13 | R013 |
|---|---|---|---|---|---|
| 1 | `IOMobileFramebuffer`/`IOMFBServer` public SwapEnd hook → coexist translation (selector-5 `SwapEnd` → 0x34 `SwapCancel` via `IOConnectCallStructMethod_new`, real status + synthetic completion) | **MSHookFunction** `mac_hooks.m:6290-6305`, armed `:6339-6351` after exact `kern_SwapEnd` callsite verify | 22F82 ABI/prologue match; **provider capability** | installed (`SwapEnd hook installed` lines) | **REFUSED** — install absent |
| 2 | QC frame-info registration observed via `IOMFBServer::enable_frame_info_tag_list` hook → `g_macws_iomfb_frame_regs` | **MSHookFunction** `mac_hooks.m:19804-19863` (UUID+prologue validated) | provider + virtual-display bit check `:19615-19649` (`MACWS_IOMFB_FORCE_REG` = scaffold override, diagnostic-only) | **worked** — 654 `CANCEL-COMPLETION delivered` in `.bak` proves registration recorded | refused → empty table |
| 3 | cancel-completion dispatch → `macws_iomfb_complete_cancelled_swap` → `macws_publish_graphics_ready()` + `macws_publish_scanout_after_swap()` | internal call `mac_hooks.m:21690-21699`; early-return `!registration.callback` `:21609-21616` | needs #2 | live | dead |
| 4 | owned scanout: original IOSurface → project BGRA IOSurface wrap/substitution | `method_setImplementation` via `macws_hook_agxtex_iosurf_init` (`#if !__arm64e__`, `Metal_hooks.x` — WS loads the **arm64** slice; R013 MAP confirms `libmachook_arm64.dylib` in pid 80310) + `hooked_newTextureWithDescriptor` paths | ungated once AGX classes register (`getMetalPluginClassForService` %hookf; `MACWS_AGX_NATIVE` default on) | live | **structurally live** — irrelevant while #1–3 dead |
| 5 | publish → `macws_vnc_publish_owned_texture` (`Metal_hooks.x:8183-8315`): lock/scan/validate pixels → `MarkContentValidated` → `PublishSurface` | direct | valid sampled content | live | unreachable |
| 6 | replay receiver `bootstrap_check_in` + thread → `#### FINAL-COMPOSITE replay-receiver-ready` | `MacWSFinalCompositePublisher.m:736-780`; started by `MarkContentValidated` `:187-205` / `PublishSurface` | needs #5 | `replay-receiver-ready port=120379`, `replay-received` ×6 (`.bak`) | absent — receiver never started |
| 7 | consumer `macwsdisplayd` → `state` file + workspace graph | Mach service | needs #5/#6 | `state=ready` seq 278+ | `state=missing producer=0 sequence=0 displayd-start` |

### Dead parallel producers (both eras)

- **`MetalContext::EndUpdate` enqueue/drain** (`Metal_hooks.x:10024+`):
  target never invoked on 22F82 (runtime `ENDUPDATE-DIAG=0`) **and** its
  hook is MSHook-installed → dead twice over.
- **`didComplete:` swizzle path** (`macws_iogpu_buffer_complete` →
  `try_on_clean` → `vnc_publish_owned_texture`, `Metal_hooks.x:6880-6905`):
  `method_setImplementation` (provider-independent) but install is
  **diagnostics-gated** — `macws_install_iogpu_callback_diagnostics`
  (`:7001-7050`, AGXG13GFamily{CommandQueue,CommandBuffer} swizzles)
  runs only under `macws_iogpu_callback_diag_enabled()` → runtime
  diagnostics / sentinel (`:7223-7231`, image-add gate `:25141`). This is
  a *secondary* producer: on 10-13 the primary was link #1–#3 (in-era
  attribution + `.bak` cancel-completion volume). Marker absence in R013
  kills this path too, but restoring markers alone would only re-arm a
  redundant producer — the canonical chain stays refused.
- **replay-copy publishes** (`MacWSFinalCompositePublisher.m:513/823/879`):
  require a prior accepted surface (`CanPublish`) → dead-start.
- `macws_publish_graphics_ready` writers: registered path (`:21691`) and
  EndUpdate drain (`:9966`) only → both dead without provider → VNC-mode
  `start` graphics gate is **unwinnable** today; `--no-vnc`'s
  process-readiness branch (`macos_gui.sh:680-694`) is the only passable
  start — explains the flag's adoption in R012/R013 without it being the
  suppressor.

### Diagnostic-signal provenance

`fprintf` is `#define`d to `macws_filtered_fprintf`
(`mac_hooks.m:330-340`, `Metal_hooks.x:6259`) — returns 0 unless the
diagnostics sentinel exists. **Every install/`####`-via-fprintf line is
invisible in production** — including `SwapEnd hook installed`. Reliable
production witnesses are only: provider `macws_hook_log` lines (raw
`write()`), `dprintf` lines, the displayd state files, and displayd's own
counters. Corollary corrections:

- R013's missing `SwapEnd hook installed` line is **not** proof of
  refusal (suppressed either way); refusal stands on the 21–58
  `REFUSED` lines (raw-write, always visible) + `state=missing
  producer=0` + zero displayd accepts.
- `.bak`-era install lines were visible **because that generation had
  the sentinel armed** — consistent with the era's documented marker
  presence, and it means the marker-gated `didComplete` swizzle producer
  was likely armed then too (secondary, non-load-bearing).
- `replay-receiver-ready` lives in `MacWSFinalCompositePublisher.m`
  (no fprintf redefine) — its absence in R013 is a genuine
  publisher-never-ran signal.

---

## 4. Generation comparison — R008 / R010 / R012 / R013

| | R008 | R010 | R012 | R013 |
|---|---|---|---|---|
| HEAD | `8e470ba` era | pre-package | `b717fb9`+ | `c97e219f…` |
| provider | permit-hardened build (post-`685624f`) | baseline backup `7320a210…` (permit-gated); package later merged | →`b2ae21da…` installed 19:30 | `b2ae21da…` (installed) |
| libmachook | era build | `4fa53411…`(e)/`d2f110fd…`(64) | `47f78880…`(e)/`6de9b688…`(64) | same as R012 |
| WS hooks | refused | refused | refused (58) | refused |
| start flags | `start coexist --no-vnc` variants | same | same | `timeout 300 … start coexist --no-vnc` |
| witness | XPC svc crash fixed-attempt | merged-provider pkg | `_xpc_bootstrap_services` PAC fix verified; **no pixels** | WS 80310 live 300 s, displayd 80405, **0 composites**; 33 crash artifacts |
| readiness | process-only | process-only | process-only | `ready (pid=80310, stable 8s; graphics not yet witnessed)` |

Composite outcome identical across all four: **zero**. The regression
predates all of them; they share the refused-provider regime.

---

## 5. What changed vs baseline — ranked suppressors

### 5.1 PRIMARY — provider capability (FACT)

Every production `MSHookFunction` call is refused: permits enter
`g_permits` only via `MACWSHookPermitV2`, whose sole call site is the
CGXSYNTH diagnostic gate (`mac_hooks.m:6035-6093`, additionally
`isWS && runtime_diagnostics` gated → **zero permits in production**).
`REFUSED`=21–58/gen counted; `SwapEnd hook installed`=0. On 10-13 the
provider installed everything (`.bak`: REFUSED=0, full surface, 654
cancel-completions). **This single change severs links #1–#3 — the whole
primary producer — with zero libmachook source delta.**

### 5.2 SECONDARY — diagnostics-sentinel absence (FACT for the swizzle path)

Production preflight strips the marker set (`macos_gui.sh:5725-5758`);
R013 ran it (`PRODUCTION-PREFLIGHT` logged). The `didComplete:` swizzle
producer (#4 alt) therefore never installs. Demoted from primary: 10-13's
recorded producer is the SwapEnd chain, which needs no marker. Restoring
the marker alone would at best re-arm the redundant path — a useful
A/B datapoint, not the fix.

### 5.3 NOT a suppressor — `--no-vnc` (REJECTED)

`--no-vnc` sets `MACWS_VNC_SHARE=0` → disables the RFB CPU bridge
(`macws_vnc_share_enabled` consumers: mmap mirror/update paths only).
`PublishSurface`, `MarkContentValidated`, replay receiver, and displayd
acceptance are **not** gated by it (source: `Metal_hooks.x:8183-8315`,
publisher `MarkContentValidated`/`PublishSurface` have no share check).
Its real effect: selects the process-readiness branch and bypasses the
now-unwinnable `graphics_ready` gate — it *masks* the symptom, doesn't
cause it.

### 5.4 CONTRIBUTING/UNATTRIBUTED — F11H/MPS decode failures (THEORY)

Live gen: `MTLCompilerService` 25 SIGABRT (iOS-native binary,
`readModuleFromBinaryRequest` decode failure — F11H class) +
`iconservicesagent` chroot MPS abort loop (17+, still cycling). Display
transport is dead upstream regardless (refused hooks); whether WS's own
compile requests would hit the same wall *after* transport is restored
is **unproven** — flagged as the likely next blocker class, not today's
cause of `sequence=0`.

### 5.5 Wallpaper — resolved, not a suppressor (FACT)

`workspace-controller.log` (live gen): `wallpaper-ready screens=1
path=/usr/local/share/macws/wallpapers/macws-forest-lake.png` — payload
resolved and applied; compositor simply never delivers it. Only
regression risk = a rootfs reinstall reverting `DefaultDesktop.heic`
(device-side substitution, hash unrecorded).

---

## 6. Rejected / bounded theories

| theory | verdict |
|---|---|
| libmachook source change broke the producer | **REJECTED** — producer files byte-identical `c48a81d..HEAD` (TCC/permit/PAC only) |
| `--no-vnc` disables final-composite transport | **REJECTED** — §5.3 |
| displayd-side failure is primary | **REJECTED** — `state=missing producer=0 sequence=0`; receiver never saw a frame; `lookup/send` counters with no WS-side `replay-received` = producer silence, not consumer rejection |
| R012 PAC fix should have restored pixels | **REJECTED** — fixed the `_xpc_bootstrap_services` crash class only |
| restoring R010-baseline provider is a known-good | **REJECTED** — `7320a210…` contains the permit gate too |
| EndUpdate path can be revived as producer | **REJECTED** — never invoked on this 22F82 WS (runtime) |
| R013 "no composite" = stopped-session artifact | **REJECTED** — live bounded generation (WS 21 min, 300 s timeout wrap, exit 0) |
| provider *replacement date* = a specific commit | **THEORY** — bounded post-Oct-12 / pre-Oct-14; no install log pins it; `.old/.old2/.old3` are the only candidate preserves |
| Oct-12 registration on virtual display contradicts the bit-gate | **OBSERVATION** — 654 cancel-completions prove QC did register for the relevant display in that configuration; the img+0x10ab04/bit35 gate analysis may cover a different display object or the bit was set — resolves itself once hooks work again; listed as a check in the A/B readout |
| F11H compiler abort suppresses composite | **THEORY** — plausible next-layer blocker, no WS-attributed compile request captured |

---

## 7. Missing identity evidence (gaps to close next round)

1. **Working-era provider binary** — hash/UUID of the ≤Oct-12
   CydiaSubstrate never recorded. On-device candidates:
   `CydiaSubstrate.old/.old2/.old3` beside the framework — SHA-256 +
   `grep -c "no permit for target"` each, next round.
2. **10-13 deployed libmachook SHAs** (arm64/arm64e) — never recorded;
   nearest = Oct-14 R004B set (§2.3). `git` can rebuild `c48a81d`-era
   source but deployed binary equality is unprovable retroactively.
3. **R002 `4841131` provider build's deployed SHA** — probe confirmed
   INSTALLED on Oct 14 but no hash logged.
4. **Identifier reconciliation** — recorded "old provider" values differ
   across docs: UUID `DFF425E9` (Oct-14 live probe), `EC16F01C` /
   `1d3c525a` (R010 doc baseline), `7320a210` (r010-baseline backup
   file). Map each to a path+date before any rollback claim.
5. **`DefaultDesktop.heic` post-substitution hash** (device-side
   wallpaper payload).
6. **10-13 `macos_gui.sh` on-device SHA** — source diff vs `c48a81d` is
   TCC-only, but the graphics-gate question (§3, graphics_ready writers)
   makes the deployed-script identity worth one hash.
7. **Sentinel state of the productive 10-13 WS generation** — for
   secondary-path completeness only (§2.4).
8. **macOS-side image UUIDs** on device today vs recorded SkyLight
   `96676A53…` / QC `CF853BBD…` (verify the ABI pins still match).

---

## 8. Minimal reversible one-variable restoration plan

**Variable: provider authorization for the working-era hook surface**
(everything else held at R013 state: same package, same libmachook,
same flags).

- **B0 (preflight, read-only, next round):** hash
  `CydiaSubstrate.old/.old2/.old3` + the r010-baseline files; if any
  lacks the permit gate → it is a candidate known-good binary and the
  plan becomes a pure binary A/B.
- **A (control):** current state already witnessed — R013 + live gen:
  `start coexist --no-vnc`, REFUSED>0, `SwapEnd`=0,
  `state=missing producer=0 sequence=0`. No re-run needed; frozen
  evidence stands.
- **B (one change):** register exact `MACWSHookPermitV2` permits in
  libmachook for the working-era targets — minimum witness set:
  (a) the QC `publicSwapEnd`/`IOMFBServer` target (offset already pinned
  per-build in `kMacWSIOMFBABIs`), (b) `enable_frame_info_tag_list`
  (UUID+prologue already validated at the site). Optionally extend to
  the full `.bak`-observed SkyLight set (PrepareForUse/StartComposite/
  EndCurrentComposite/EndUpdate/completion/tolerate-nil) if the minimum
  pair installs but the chain still stalls. This preserves the
  fail-closed contract — it *authorizes* the intended targets rather
  than bypassing the gate — so it is a fix candidate, not a scaffold.
  (Code change belongs to a future implementation round, not this
  packet.)
  **Update (same day):** backend D034 implemented this as a 4-site
  minimum set (SwapEnd + tag_list + pacing ×2) — audited and approved in
  `docs/evidence/devin-d034-permit-audit-frontend-20261018.md`, which
  also proves chain completeness (no unpermitted MSHook link remains:
  the SwapCancel translation rides `DYLD_INTERPOSE`, the scanout pool
  rides `method_setImplementation`).
- **Run:** one bounded owned generation, e.g.
  `timeout 300 bash macos_gui.sh start coexist --no-vnc` (identical
  flags to control → clean A/B).
- **Readout ladder (each milestone = evidence):**
  1. `WindowServer.err`: `SwapEnd hook installed` present, `REFUSED`
     count drops for the permitted targets.
  2. `#### … CANCEL-COMPLETION`/`COEXIST SwapCancel` lines — swap flow
     live (resolves the §6 registration observation).
  3. `#### FINAL-COMPOSITE replay-receiver-ready` + `replay-received`
     — publisher invoked.
  4. displayd: `final-composite-accepted`, state file
     `state=ready … sequence` advancing.
  5. workspace graph `wallpaper/dock/menubar/backing=ready`; pixel
     witness (VNC capture if VNC-mode, else MacWSHost readback).
- **Outcomes:** (1) milestones 1–5 pass → provider refusal confirmed as
  sole suppressor; (2) install ok but no CANCEL-COMPLETION → next gate
  is the virtual-display registration check (`mac_hooks.m:19615`) —
  read before touching `FORCE_REG` (scaffold, not fix); (3) publish
  attempts but `reject-output` → content invalid → investigate AGX
  compositing/F11H next layer; (4) still REFUSED → permit registration
  path itself failed → check permit ABI ordering.
- **Reversion:** restore provider `b2ae21da…` (hash-pinned, on-device
  backup exists) and/or revert the permit hunk; `macos_gui.sh stop`.
  Permit additions are inert under the old no-op provider as well —
  rollback is safe either direction.
- **Stop conditions:** WS crash-loop, native SpringBoard/backboardd/
  tccd impact, thermal > nominal.
- **Explicitly excluded:** provider downgrade to scaffold-era binaries,
  package rollback, wallpaper file changes, `MACWS_IOMFB_FORCE_REG` or
  any assertion/check bypass, compiler candidates, R031 parked work,
  `--diagnostics` alone (re-arms only the redundant swizzle path —
  valid as a *secondary* datapoint, not the fix).

Secondary optional datapoint (independent variable, separate round):
`start coexist --no-vnc --diagnostics` — if the didComplete swizzle path
then publishes (`#### VNC-OWNED published` / `FINAL-COMPOSITE`), it
independently confirms the composite transport+consumer are healthy and
isolates the refusal as the only break. Do not combine with B in one
run.

---

## 9. Reproducible local commands used (no device, no writes)

```sh
# producer source deltas since baseline
git diff c48a81d..HEAD -- libmachook/Metal_hooks.x        # → empty
git diff c48a81d..HEAD -- libmachook/mac_hooks.m | less   # TCC/permit/PAC only
git show 0614a5b --stat;  git show c48a81d --stat
git log --oneline --format='%h %ad %s' --date=short

# producer chain call sites
grep -n 'macws_publish_scanout_after_swap\|macws_iomfb_complete_cancelled_swap\|publish_graphics_ready\|try_on_clean\|MSHookFunction\|MACWSHookPermitV2\|enable_frame_info_tag_list\|method_setImplementation' \
  libmachook/mac_hooks.m libmachook/Metal_hooks.x libmachook/MacWSFinalCompositePublisher.m

# marker / launch gates
grep -n 'macws_runtime_diagnostics\|production_preflight\|WANT_DIAGNOSTICS\|--no-vnc\|graphics_ready' layout/usr/macOS/bin/macos_gui.sh

# artifact hashes (recorded identities)
shasum -a 256 layout/usr/local/share/macws/wallpapers/macws-forest-lake.png   # 4931a89d…
git log --oneline -- misc/cydiasubstrate_macos.m   # fa76294 4841131 685624f 3c4d581
```

On-device facts (installed SHAs, `.bak`/`.previous` hook counts, live-gen
state, crash inventory) are cited from the sibling backend inventory —
reproduce via its transcript, not this doc.

---

## 10. Limits

- Read-only local audit: installed-on-device claims rely on the backend
  inventory's verbatim `sha256sum`/log greps; this session opened no
  SSH, ran no generation, took no process maps.
- `REFUSED` lines don't name targets; the SwapEnd-site refusal is proven
  by the *absent* install line + source contract, not a named-target log.
- The working-era provider binary is not among confirmed preserves; its
  capability is proven by logs, its identity is GAP #1.
- How the productive 10-13 WS generation passed the VNC graphics gate is
  now explained by the working-provider model (graphics_ready written at
  `mac_hooks.m:21691` via the live chain) — self-consistent, but the
  on-device `macos_gui.sh` hash remains an open identity check.
- Whether the marker was armed inside the productive generation is
  unproven and non-load-bearing (primary producer is marker-free).
- 10-13 libmachook deploy hashes, wallpaper file hash, R002 provider
  hash: unrecoverable retroactively unless device backups hold them.
