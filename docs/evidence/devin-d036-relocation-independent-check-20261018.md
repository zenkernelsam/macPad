# D036 independent check — SwapEnd wrapper decode + exact-relocation thunk review

Date: 2026-10-18 (Devin frontend, independent verification of the in-flight
D036 backend implementation; tree still changing — reviewed against the
`misc/cydiasubstrate_macos.m` diff visible at the time of writing).

This note is the frontend's independently derived ground truth for the
publicSwapEnd stub and the new "exact-relocation" thunk semantics. All
instruction decodes below were produced with capstone 5 (arm64), not by
hand-reading hex.

## 1. Capstone-verified stub decode (corrects two earlier hand-decodes)

IOMFB `publicSwapEnd` @ `img + 0x11cc` (Ventura, expected prologue
`b4000080 f9439401 b4000041 d61f083f`):

```
+0x00  cbz   x0, #0x10          ; NULL receiver -> return (skip whole stub)
+0x04  ldr   x1, [x0, #0x728]  ; impl pointer (PAC-signed, see below)
+0x08  cbz   x1, #0x10         ; unbound       -> return
+0x0c  braaz x1                ; authenticated tail-call, A-key, zero disc
```

Corrections vs. the D035 note:

- insn2 target is `sym+0x10` (capstone prints the absolute target; both
  skip paths converge exactly past the 16-byte window).
- insn3 is `braaz x1`, not `br x1` — `0xd61f083f & 0xfffffc1f = 0xd61f081f`.
  Consequence: `[x0 + 0x728]` holds a **PAC-signed code pointer** (A-key,
  zero diversity). On arm64 the insn degrades to `br`; on arm64e it
  authenticates. Any future plan that dereferences `x0+0x728` to find the
  real impl must ptrauth-strip it — a reason the "keep hooking the wrapper"
  route is preferable to the kern_SwapEnd retarget.
- 15.6.1 row `b4000080 f9447001 b4000041 d61f083f` is the same shape with
  `ldr x1, [x0, #0x880]` — same analysis applies.

## 2. Exact-relocation design — independent semantics check

The in-flight provider change replaces the pure verbatim+branch-back thunk
with a per-instruction translator:

- `cbz/cbnz` → inverted-condition skip over an absolute `ldr x17; br x17;
  .quad <orig-target>` pair. Encoding check: `insn ^ (1<<24)` flips
  cbz↔cbnz (bit 24), imm19 rewritten to +16 skips the 4 emitted slots.
  **Semantically exact for ANY cbz target** — strictly more general than
  the frontend's earlier "target must be ≤ sym+0x10" suggestion, and it
  removes the coincidence dependence.
- non-returning register branches/returns (`br`, `braaz/brabz`, `ret`,
  `retaa`, `eret`) → verbatim copy, loop terminates, appended branch-back
  suppressed. Correct: control never falls through, thunk's caller gets
  the original's return/tail-call behaviour identically.
- `blr/blraaz/blrabz` family → explicitly refused (LR-setting would return
  into the window). Conservative and defensible (a verbatim copy would in
  practice still work since LR=thunk+4 lands on the next emitted slot, but
  refusing keeps the contract narrow).
- `b/bl/b.cond/tbz/adr/adrp/ldr-lit` → still refused. Correct — none can
  be faithfully re-expressed verbatim (b.cond/tbz could later get the same
  inverted-skip treatment as cbz if ever needed; not needed now).

Replay of the stub under the new translator:

```
thunk+0x00  cbnz x0, +0x10      ; x0!=0 -> skip to thunk+0x10
thunk+0x04  ldr  x17, #8
thunk+0x08  br   x17
thunk+0x0c  .quad sym+0x10      ; x0==0 -> original target
thunk+0x10  ldr  x1, [x0,#0x728]            ; verbatim
thunk+0x14  cbnz x1, +0x10      ; x1!=0 -> skip to thunk+0x24
thunk+0x18  ldr  x17, #8
thunk+0x1c  br   x17
thunk+0x20  .quad sym+0x10      ; x1==0 -> original target
thunk+0x24  braaz x1            ; verbatim — authenticates + tail-calls impl
```

All three original control paths (x0==0, x1==0, tail-call) are preserved
exactly. **P1 from the D035 review is resolved by this design** — keeping
`publicSwapEnd` as the hook target is now viable and is what the current
`mac_hooks.m` diff does (permit stays at `publicSwapEndOffset`). The
kern_SwapEnd retarget is no longer needed for this specific target.

## 2b. P0 finding — cbz-skip constant off by one (live in tree)

The simulation above used the backend's own emitted bytes — and capstone
disassembly of the resulting thunk exposes an off-by-one at
`misc/cydiasubstrate_macos.m:516`:

```c
*tp++ = ((insn ^ (1u << 24)) & ~(0x7ffffu << 5)) | (4u << 5);
```

The comment says the inverted cbnz must "skip over the 4-slot
abs-branch pair+data". The emitted region after the cbnz is
`ldr x17; br x17; .quad` = **4 slots — but imm19 counts instructions
starting from the branch itself**. imm=4 (+16B) lands at
`thunk+0x10`/`thunk+0x28` — the HIGH word of the `.quad` target data
(`udf`):

```
imm=4:  cbnz x0,#0x10 @0x00 -> 0x10 (mid-quad)   cbnz x1,@0x18 -> 0x28 (mid-quad)
imm=5:  cbnz x0,#0x14 @0x00 -> 0x14 (ldr x1)     cbnz x1,@0x18 -> 0x2c (braaz x1)
```

As emitted, the not-taken path (x0!=0 / x1!=0 — i.e. **every real call**)
would execute data as an instruction and crash inside the thunk on the
very first original-call replay. Fix: `| (4u << 5)` → `| (5u << 5)`
(skip 5 slots: the 4-slot payload plus landing past it), or equivalently
re-emit imm19 as (1 + number of emitted slots). Verified by simulation in
`analysis/devin-d035-frontend/permit_audit.py` §4 — re-run after fix.

## 3. P2 finding — two wrong authenticated-branch mask constants

Capstone-verified encoding space (bit 10 of the opcode = A/B key select):

| insn    | real encoding          | backend constant | status |
|---------|------------------------|------------------|--------|
| braaz   | `0xd61f081f \| Rn<<5`  | `0xd61f081f`     | OK     |
| brabz   | `0xd61f0c1f \| Rn<<5`  | `0xd65f081f`     | WRONG — undecodable, never matches |
| blraaz  | `0xd63f081f \| Rn<<5`  | `0xd63f081f`     | OK     |
| blrabz  | `0xd63f0c1f \| Rn<<5`  | `0xd67f081f`     | WRONG — undecodable, never matches |
| braa    | `0xd71f08xx` (key reg bits[20:16]) | not masked | non-Z forms also absent |
| blraa   | `0xd73f08xx`           | not masked       | same |

Impact, honestly scoped: LOW for the four current targets (none of the
pinned prologues contain brabz/blrabz — the static checker confirms all
four windows now translate cleanly), but the constants as written are dead
code that mis-document the classifier. A `blrabz` in a future prologue
would escape the intended blr-family refusal and be copied verbatim; a
`brabz` would copy verbatim without `control_leaves`, leaving a dead
branch-back appended — functionally harmless but not what the comment
promises. Non-Z `braa`/`brab`/`blraa`/`blrab` (key-register forms,
`0xd7xx`) are likewise unmasked — same story.

Fix: `0xd65f081f → 0xd61f0c1f`, `0xd67f081f → 0xd63f0c1f`; optionally add
`(i & 0xffe0ffff) == 0xd700081f/0xd7000c1f`-style masks for the non-Z
forms if the intent is to classify the whole authenticated space.

## 4. What the D036 tests must prove (frontend acceptance bar)

Per the coordinator's test clause, applied concretely:

- branch taken: x0=NULL call through the thunk must return without
  touching x1 or calling impl; x1=NULL path identical.
- branch not-taken: x0!=0 && x1!=0 → impl invoked with original args,
  callee LR intact, return value propagated.
- PAC: on arm64e the verbatim `braaz x1` must authenticate the signed
  pointer — a test using a PAC-signed fnptr in `[x0+0x728]` is required;
  a plain br substitute would NOT be equivalent.
- negative: a window containing `b/bl/b.cond/tbz/adrp/ldr-lit` must still
  REFUSE; a `blraaz`/`blrabz` must REFUSE (catches the §3 constants too).
- callable original: `*result` thunk must be callable and replay exactly.
- icache: `sys_icache_invalidate(thunk, thunk_used)` — now sized to the
  emitted code, verify no stale-cache hit on second install.

## 4b. P3 finding — intra-window cbz target silently mis-relocates

The coordinator's "internal-window behavior" test clause covers a case
the current emitter does NOT guard: a `cbz/cbnz` whose target lands
INSIDE the stolen window `[sym, sym+16)`. The emitted `.quad` carries
the ORIGINAL absolute target — but after the symbol is patched, that
address is the middle of the patch stub:

    orig:  cbz x0, sym+0x08   (targets window insn2)
    thunk: cbnz x0,+0x14 / ldr x17; br x17 / .quad = sym+0x08
    x0==0 -> jumps to sym+0x08 = mid patch-stub -> executes patch data
    (or re-enters the replacement if target==sym+0)

Verified by simulation: emitted `.quad = 0x1008` inside `[0x1000,0x1010)`.
Correct handling: refuse intra-window targets
(`target >= sym_pc && target < sym_pc + 16` -> REFUSE), or remap the
.quad to the corresponding emitted thunk offset (requires tracking
emitted_offset[i] per source insn — added complexity for zero current
benefit). Forward targets `>= sym+16` and backward targets `< sym` are
already correct (untouched code). NOT triggered by the 4 pinned targets
(publicSwapEnd targets exactly sym+0x10; QC prologues branch-free) —
latent class, one-line guard recommended before the next broader permit.

Scratch-register audit (coordinator's clause): thunk + patch stub clobber
x17 only (IP1, the Substrate-convention scratch). Residual edge: an
original window that READS x17 across a translated cbz expansion would
observe the thunk's clobber — a br x17 consumer is already terminal and
safe; x17 read by a non-branch mid-window insn is the only unhandled
shape, same exposure as classic Substrate. No current target does this.

## 5. Status (updated post-backend-fix)

Backend incorporated both findings before settle. Final state verified
independently by this worker:

- **P0 fixed**: `(5u << 5)` now emitted; comment corrected. Frontend
  re-simulation of all 4 pinned windows: every inverted cbnz lands
  exactly on the next emitted insn (publicSwapEnd: 0x00→0x14, 0x18→0x2c;
  15.6.1 row identical at #0x8e0 offset). QC trio = verbatim + branch-back.
- **P2 fixed**: `brabz 0xd61f0c1f`, `blrabz 0xd63f0c1f` (both classifier
  and blr-refusal sites); braaz/blraaz were already correct.
- **Independent test run**: compiled `misc/cydiasubstrate_macos.m` for
  x86_64 with `MACWS_RELOC_TESTONLY` and ran
  `misc/test_mshook_relocate_host.c` against it — **ALL PASS**. Emitted
  wrapper thunk: `b50000a0 58000051 d61f0220 <S+0x10> f9439401
  b50000a1 58000051 d61f0220 <S+0x10> d61f083f` (imm19=5, correct).
  Refusals verified live: unrelocatable PC-relative, blr-family
  ("indirect call in stolen window"), unpermitted target.
- **Stale-hash flag**: backend's result doc + D034 plan recorded the
  PRE-FIX build (`fat 7172dd68…`, arm64 `1a5448fe…`, arm64e `6d667314…`)
  and pre-fix thunk bytes (`b5000080`). Post-fix provider rebuild at
  23:28:34-38 produces: arm64 `84ef0b01…ccf7`, arm64e `8f4c7239…f63d`,
  fat `8cde7566…068e`. The D034 staged-hash line and result-doc bytes
  must be updated before any deploy — deploying per the old hash would
  either ship the buggy thunk or fail hash verification.

Verifier's verdict on the settled design: approve for the authorized
device A/B. Residual risks unchanged: live IOMFB slice uuid (fails closed
-2 if arm64/arm64e mismatch), PAC-authenticated thunk execution witness
is deferred to device (host test is byte-level only).

- **arm64 live-execution attempted, impossible on this host**: compiled
  `misc/test_mshook_relocate.c` for arm64 and tried to run it against the
  post-fix `macosx/arm64/CydiaSubstrate` slice — `Bad CPU type in
  executable`; this machine is x86_64 (AMD Ryzen 7 5825U, no Rosetta).
  The live-execution + PAC-auth witness remains device-only, matching
  the coordinator's gate ("no deployment approval until meaningful
  live-execution witness"). Backend result v2 is now internally
  consistent (post-fix hashes `8cde7566/84ef0b01/8f4c7239` match
  frontend-measured values; imm19=5; corrected masks; blrabz fixture).
