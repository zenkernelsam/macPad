# Devin D035 frontend — independent review of the pinned display-permit candidate

Date: 2026-10-18. Task: `coordination/_relay/devin-035-frontend-task.md`.
Read-only independent audit of the uncommitted backend change set at its
final ("v2"/D035) state: `libmachook/mac_hooks.m` +90/-9,
`misc/cydiasubstrate_macos.m` +28/-4, objects arm64 `bf3ae467…dc30` /
arm64e `f2143308…9146` / provider `694be5a2…d41d2d`. Required reading
applied: `coordination/reviews/git-visible-desktop-regression-audit-
20261018.md`, `coordination/reviews/share-lessons-active-checklist-
20261018.md`. Disposable artifact: `analysis/devin-d035-frontend/
permit_audit.py` (output inlined below). No device, no edits to backend
files, no builds.

## Verdict

**Mechanism approved; goal NOT yet reached — one P1 blocker.**
The permit integration itself is correct (ordering, pins, fail-closed
rollback, budget). But the load-bearing target `publicSwapEnd` can never
be patched by this trampoline family: its pinned 16-byte prologue is a
branchy tail-call stub, and the provider's stolen-window check refuses
PC-relative/terminal instructions *after* the permit registers. The
permit chain will therefore still produce `SwapEnd` refusal → no
cancel-completion → no composite — and an A/B without this fix would
return a misleading "permits didn't help" result. Fix is small and
upstream (retarget to `kern_SwapEnd`, §P1).

## Review-focus answers (task's explicit checklist)

| focus | result |
|---|---|
| dynamic prologue copied from live target | **fixed in v2** — all expected bytes now come from RE'd tables |
| dynamic offset | **fixed in v2** — table offsets only |
| NULL-uuid live self-capture | **fixed in v2** — `expected_uuid16` required non-NULL; IOMFB pins `7AC531B7…`, 15.6.1 `{0}` fails closed |
| re-enabled nil-skip/constant-return/symptom hooks | none — the dropped SkyLight 8-site set stays refused; nothing suppressed was re-armed |
| original PAC pointer handling | refusal returns `orig_symbol` signed-as-passed (`:440`,`:467`); success signs thunk `ptrauth_key_asia,0` on arm64e (`:548-555`) matching blraaz callers |
| Mach-O bounds | **P3 nit** — provider `macws_lc_uuid`/`macws_permit_for` walk `ncmds`/`cmdsize` with no `cmdsize >= sizeof(lc)` or bound check (libmachook's removed walker had one) — mirror it |
| stolen-window relocation safety | verifier is *correct* to refuse — verbatim copies can't relocate branches; see P1 |
| hook-install witness independent of fprintf | **P2** — install lines are `fprintf`→`macws_filtered_fprintf` → production-silent; propose provider-side raw `INSTALLED image+off` line |
| essential completion/frame-info dependency | chain proof in §B — the 4-site set covers every MSHook link; rest is interpose/objc |

## P1 (blocker) — `publicSwapEnd` is unrelocatable; retarget to `kern_SwapEnd`

Evidence (`misc/cydiasubstrate_macos.m:482-485`):

```c
for (int i = 0; i < 4; i++)
    if (macws_insn_pc_relative(src[i]) ||
        macws_insn_terminal(src[i]))
        MACWS_REFUSE("PC-relative/terminal in stolen window");
```

Pinned `publicSwapEnd` prologue (both ABI rows):

| insn | word | decode | classifier |
|---|---|---|---|
| 0 | `b4000080` | `cbz x0, +0x10` | **pc-relative → REFUSE at i=0** |
| 1 | `f9439401` | `ldr x1, [x0, #0x728]` | ok |
| 2 | `b4000041` | `cbz x1, +0x8` | pc-relative |
| 3 | `d61f083f` | `br x1` | terminal |

`analysis/devin-d035-frontend/permit_audit.py` output (run from repo
root, mirrors the provider's masks):

```
MSHookFunction call sites: 36
permit registration calls: 4 at [6336, 19903, 20038, 20040]
  REFUSE  IOMFB publicSwapEnd (15.6.1): insn0 0xb4000080 = cbz/cbnz
  REFUSE  IOMFB publicSwapEnd (13.4):   insn0 0xb4000080 = cbz/cbnz
  OK      QC enable_tag_list: window relocatable
  OK      QC begin_update:    window relocatable
  OK      QC finish_update:   window relocatable
```

The wrapper is a forwarder (`if(!x0)return; x1=x0->[+0x728]; if(x1) br x1`)
— a verbatim 16-byte steal cannot preserve it. **The Oct-12 `.bak` era
had `SwapEnd hook installed … trampoline=%p` — so the working-era
provider had real branch relocation** (or was full CydiaSubstrate).
The minimal scaffold lineage (`fa76294`→`4841131`→`685624f`) never
supported it. *This refines the regression story*: even the R002-era
repaired provider could not have installed this particular hook —
consistent with composite never returning after the Oct-12 generation
(THEORY, bounded by the missing working-provider binary).

**Smallest upstream candidate — retarget to `kern_SwapEnd` @ img+0x4400**
(the real function the stub tail-calls; the ABI table already pins its
offset, verifies its +0x30 callsite, and patches its +0x24 `mov w3`):

- Same interception semantics: the stub forwards args untouched;
  `MacwsIOMobileFramebufferSwapEnd_new` works identically as the impl's
  replacement; `g_macws_orig_iomfb_swap_end` = thunk→kern_SwapEnd.
- +0x24 `movW3` patch and +0x30 callsite check lie **outside** the
  stolen 16-byte window — no conflict.
- Real function → expected `paciasp/stp` class prologue → relocatable.
- Missing inputs (exact binary ranges needed):
  1. **16 bytes @ IOMFB-13.4 image+0x4400** — read from the extracted
     `dscout_ventura` IOMobileFramebuffer; verify all 4 words pass the
     two classifiers before adding to the table.
  2. **Forwarding evidence** — disasm the entity stored at `[x0+0x728]`
     (or the class init that fills it) to confirm it resolves to
     0x4400; alternatively one runtime check in the owned generation.
  3. **arm64-slice LC_UUID** of the live IOMFB image in WS — the
     `7AC531B7…` pin came from an extraction whose cpusubtype may read
     arm64e; wrong-slice uuid → permit `-2` → skip (fail-closed, named).
- Rejected alternatives: pointer substitution on `[x0+0x728]` (runtime
  object field, not a static slot); extending the provider with branch
  relocation (real Substrate scope — contradicts the minimal contract);
  hooking a QC callsite inside `finish_skylight_update` (mid-function —
  incompatible with prologue-steal model).

## P2/P3 findings

- **P2 — install witness**: every `hook installed` line is `fprintf` →
  silent in production. Add a provider-side raw `write()` success line
  (`MACWSHook: INSTALLED image=%s+%#llx`) — bounded telemetry of the same
  class as named-refusal; gives the hook-install witness the reviewer
  asked for without enabling the sentinel. Until then the honest
  production witnesses are: named-refusal absence for the 4 targets,
  `macws_final_composite.state` `ready`+sequence, displayd
  `final-composite-received`/`accepted` counters, and pixels.
- **P3 — Mach-O walk bounds**: add `cmdsize >= sizeof(struct
  load_command)` / slide-bounded termination to `macws_lc_uuid` and the
  `macws_permit_for` inline walk (the removed libmachook walker had it).
- **`MSG-REFUSED bad args` ×2** = `%hook NSScreen`'s two methods
  (`Metal_hooks.x:24223`, AppKit class absent in WS → `MSHookMessageEx
  (NULL,…)` ×2). All explicit `MSHookMessageEx` sites are NULL-guarded;
  `QC-DESKTOP-TARGET`/`MACWS-METAL-TARGET` use
  `class_addMethod`/`method_setImplementation` — provider-independent.
  Expected new-log form: `class=(null) sel=maximumPotential…` — noise,
  not a defect. Hygiene: gate the `%hook` on class existence.
- **36 vs 4**: 36 `MSHookFunction` call sites vs 4 permit calls is
  *intentional* — the remainder are env/diag-gated and stay refused;
  named-refusal logging now enumerates them instead of guessing.
- **15.6.1 `{0}` uuid**: fails closed by design; no delta vs the
  deployed regime.

## B — Chain-completeness (independent verification)

| step | mechanism | provider gate |
|---|---|---|
| QC→publicSwapEnd | MSHook | **permitted but unrelocatable — P1** |
| SwapEnd→SwapCancel | `DYLD_INTERPOSE(IOConnectCallStructMethod)` :27967 | none |
| QC frame-info registration | MSHook `enable_frame_info_tag_list` | ✅ relocatable |
| pre-lock pacing | MSHook begin/finish | ✅ relocatable |
| `complete_cancelled_swap`→`publish_graphics_ready`/`_scanout_after_swap` | internal calls | none |
| owned BGRA scanout pool | `method_setImplementation`/`swizzle2` | none |
| `MarkContentValidated`/`PublishSurface`/replay | Mach service | none |
| displayd accept | Mach service | none |

## Suggested tests (negative + functional, no device needed for most)

1. **Static callsite↔permit coverage** — `permit_audit.py` (delivered):
   enumerates MSHookFunction sites vs registrations and classifies every
   pinned prologue through the provider's own masks. Would have caught
   both the consumer-migration gap and the P1 stub statically.
2. **Host-side contract harness** — provider already builds for
   `macosx`: dlopen it in a test binary with (a) a clean-prologue
   function → register permit → `MSHookFunction` → assert target bytes
   patched, `*result` callable and returns original value; (b) wrong
   uuid → `-2`; wrong offset → `-1`; drifted prologue →
   `REFUSED prologue drift`; (c) a `cbz;br` stub →
   `REFUSED PC-relative/terminal` — reproduces P1 in-process.
3. **A/B readout ladder (device)** — ordered witnesses, none relying on
   fprintf: named `REFUSED` set shrinks by the permitted targets (and
   shows the publicSwapEnd refusal *by name* if P1 is unfixed) →
   `CANCEL-COMPLETION`/behavioral lines → `macws_final_composite.state`
   `ready` + advancing `sequence` → displayd accepted counters → pixels.
   Keep `--no-vnc` identical to control; do not combine with
   `--diagnostics` (would arm the didComplete secondary producer and
   confound attribution).

## Supplier lessons applied (per checklist)

Layered witness (decode→producer→displayd→Host→pixels) used throughout;
deployed-identity-before-reimplementation (uuid pins audited, gaps
listed); one-variable frozen-hash A/B preserved and narrowed; no
assertion/TCC/validation bypass proposed or endorsed; the working-era
provider binary was verified absent before any rollback claim.

## Limits

Review is static + one disposable script; no runtime proof yet that
`[x0+0x728]`→`kern_SwapEnd` or that kern's prologue is clean (both are
the listed missing inputs). causal claims for the *composite* outcome
stay provisional until the owned generation runs — consistent with the
reviewer correction that no controlled A/B has yet linked the minimal
permit to pixels.
