# D036 — IOMFB publicSwapEnd relocation contract + exact relocator

Worker: `macPad D036 backend 8057`. Scope honored: **local source/build/
host-verification only — no device deployment, no generation start/stop,
no package install, no commit.** Task packet: `coordination/_relay/
devin-036-backend-task.md` ("SwapEnd含CBZ会被现有provider拒绝，先解决精确
重定位/安全挂接契约").

## 1. The refusal being fixed

`mac_hooks.m` registers a pinned permit for IOMobileFramebuffer
`publicSwapEnd` (13.4: img+0x11cc, uuid `7AC531B7-05F5-38FE-92E5-
DA6D86D22A14`, expected `b4000080 f9439401 b4000041 d61f083f`) and calls
`MSHookFunction`. The pre-D036 provider refused it: `cbz/cbnz` were an
unconditional PC-relative refusal and `braaz x1` was not classified at
all (it fell into neither terminal nor pc-relative sets — it copied
verbatim, but slot 0's `cbz` already killed the hook). Result on device:
the wrapper stayed unpatched → no `SwapCancel` → no paced completion →
no frame-info callback → `state=missing producer=0 sequence=0` (D033).

## 2. Exact wrapper disassembly (RE-confirmed 13.4, `mac_hooks.m` ABI table)

```
img+0x11cc  b4000080   cbz   x0, +0x10      ; fb == NULL -> ret path (S+0x10)
img+0x11d0  f9439401   ldr   x1, [x0,#0x728]; x1 = fb->swap_handler (signed fn ptr)
img+0x11d4  b4000041   cbz   x1, +8         ; handler == NULL -> ret path (S+0x10)
img+0x11d8  d61f083f   braaz x1             ; authenticated tail call, never returns here
img+0x11dc  ...        ret path            ; both taken cbz targets land here
```

(sf=1 → 64-bit `cbz xN`, matching the frontend's capstone decode in
`devin-d036-relocation-independent-check-20261018.md`; the relocator
passes the sf/Rt bits through verbatim — only bit24 + imm19 are
rewritten.)

Decode of `d61f083f`: BRAAZ has base `0xd61f081f` with Rn at bits[9:5] →
`(1<<5)|0xd61f081f = 0xd61f083f` = `braaz x1` — key IA, zero discriminator.
So the wrapper is a 4-instruction **authenticated tail-call shim**: two
NULL guards, then an authenticated indirect branch to the per-fb handler.
It never returns through x30 and never falls past `braaz`.

Register contract at entry: x0 = fb object (w0-tested), all arg regs pass
through; the thunk may only clobber intra-procedure-call scratch x16/x17.

Both cbz targets are `S+0x10` — the instruction immediately after the
stolen window. That makes this the benign relocation case: the original
target is *outside* the window but *inside* the same function, and the
window has no mid-window branch landing inside it.

## 3. Relocation scheme implemented (`misc/cydiasubstrate_macos.m`)

For each stolen instruction:

- `cbz/cbnz` → emit **inverted condition with imm19=5** (skips the
  4-slot payload `ldr x17,#8; br x17; .quad target` and lands on the
  next emitted instruction — the .quad occupies two slots; an earlier
  imm19=4 revision landed mid-quad and was caught by the frontend
  independent check §2b),
  where `target` = the ORIGINAL absolute branch target computed from the
  instruction's live PC. Taken and non-taken semantics preserved exactly;
  the same pair works for cbz and cbnz (bit24 inverted).
- terminal register transfers (`ret/retaa/retab`, `br/braaz/brabz`,
  `eret`) → copied **verbatim** and `control_leaves` suppresses the
  appended branch-back. `braaz` verbatim is PAC-correct: authentication
  uses the architectural key, not the PC; the signed pointer still arrives
  via the verbatim `ldr`.
- `blr/blraaz/blrabz` → **refuse** (returns into the window; untested).
- `cbz/cbnz` whose absolute target lands inside `[symbol, symbol+16)` →
  **refuse** (it would point into the patch stub after install).
- `b/bl/b.cond/tbz/tbnz/adr/adrp/lit` → **refuse**, unchanged.
- everything else → verbatim copy.
- If control never left after 4 slots: append `ldr x17,#8; br x17;
  .quad symbol+16` branch-back (unchanged).

`x17` is the designated intra-procedure-call scratch register — the
original window never touches it.

Emitted thunk for the actual wrapper (host-verified, §5):

```
+0x00  cbnz x0, +0x14          ; x0!=0 -> lands at +0x14 (non-taken)
+0x04  ldr  x17, #8
+0x08  br   x17
+0x0c  .quad S+0x10            ; x0==0 -> original target (2 slots)
+0x14  ldr  x1, [x0,#0x728]    ; verbatim
+0x18  cbnz x1, +0x14          ; x1!=0 -> lands at +0x2c
+0x1c  ldr  x17, #8
+0x20  br   x17
+0x24  .quad S+0x10            ; x1==0 -> original target
+0x2c  braaz x1                ; verbatim terminal — no branch-back
```

## 4. Classifier audit — all four D035 permit windows

| Permit | Window words | Classes | Emitted thunk |
|---|---|---|---|
| IOMFB publicSwapEnd 0x11cc | `b4000080 f9439401 b4000041 d61f083f` | cbz, ldr, cbz, braaz | relocated (above) — **was refused, now installs** |
| QC enable_frame_info_tag_list 0x29285c | `d503237f d10243ff a9036ffc a90467fa` | pacibsp, sub, stp, stp | 4 verbatim + branch-back |
| QC begin_skylight_update 0x291288 | `d503237f a9be4ff4 a9017bfd 910043fd` | pacibsp, stp, stp, add | 4 verbatim + branch-back |
| QC finish_skylight_update 0x291220 | same as begin | same | same |

`pacibsp` (`d503237f`) is hint-space — neither pc-relative nor terminal —
verbatim + branch-back is correct (it is the RETURN address signing, so
the thunk's own retaa-free layout is preserved: callers call the thunk,
which runs pacibsp and falls back to symbol+16 where the function's own
retaa/autiasp pair signs/verifies normally — see §6 note).

Frontend P2 fixed: the `brabz`/`blrabz` mask constants were initially
`0xd65f081f`/`0xd67f081f` (undecodable — bit10 is the A/B key select,
B-key forms keep `0xd61f`/`0xd63f` and set `0x0c00`). Corrected to
`0xd61f0c1f`/`0xd63f0c1f`; the blr-family refusal in the relocator uses
the fixed `0xd63f0c1f` and is test-exercised (`blrabz x0` window →
REFUSED "indirect call").

Known-bounded gap (unchanged): non-Z authenticated branches
(`braa/brab/blraa/blrab`, Rm-discriminator `0xd7xx` forms) don't match
the Z-form masks → they copy verbatim + get a dead appended branch-back.
Semantically correct (they never fall through) — just dead bytes.

## 5. Host verification (x86_64 — thunk bytes, no arm64 execution)

`misc/test_mshook_relocate_host.c` against a `-DMACWS_RELOC_TESTONLY`
x86_64 build of the provider (test-only compile gate returns the raw
thunk before the target patch — **not defined in production**; production
objects are identical, the gate is a pure `#ifdef`). Fixtures are
in-`__DATA` copies of the REAL wrapper words.

```
RESULT: ALL PASS
thunk: b50000a0 58000051 d61f0220 <sym+0x10 as quad>
       f9439401 b50000a1 58000051 d61f0220 <sym+0x10 as quad>
       d61f083f
```

Verified: inverted cbz→cbnz with **imm19=5**; computed landing-offset
assertions decode each emitted cbnz's imm19 and prove the target is the
next emitted instruction (catches the mid-quad off-by-one the frontend
found — byte-equality alone had missed it); both original absolute
targets preserved in the quads; verbatim ldr and braaz; **no
branch-back** after the terminal; verbatim window → copy + branch-back
to symbol+16; permitted `b.cond` window → REFUSED with callable
original; permitted `blrabz` window → REFUSED "indirect call"
(exercises the corrected `0xd63f0c1f` mask); permitted `cbz x0,+8`
internal-window-target → REFUSED "target inside stolen window";
unpermitted target → refused with bytes untouched; registration returns
-2 wrong-uuid / -1 wrong-prologue / -1 wrong-offset.

`misc/test_mshook_relocate.c` is the companion arm64 live-execution test
(naked shim with real `cbz/ldr/cbz/br` — runs taken/non-taken through the
thunk). It cannot execute on this x86_64 host; it is staged for the
device round or any arm64 host.

## 6. Remaining honest limitations

- **Live execution semantics** (actual branch taken/not-taken through the
  thunk on silicon, PAC auth of x1 inside the thunk) are verified
  byte-exact but not yet run — the arm64 device A/B (owned-generation
  plan) is the execution witness. The scheme is byte-verified, not
  runtime-verified.
- **IOMFB uuid `7AC531B7…`** was read from the extracted (truncated/
  repacked) analysis image — treated as the arm64e slice pending one
  live `LC_UUID` read of WS's loaded IOMFB. If it differs, the permit
  fails -2 (closed) — the plan's step 2 reads it first.
- Non-Z authenticated-branch forms verbatim-copy (correct but with dead
  trailing bytes) — §4.
- No thread-quiescence protocol — same documented limitation as before;
  install must run at constructor/add-image time.
- The R012 PAC signing of `*result` (`ptrauth_sign_unauthenticated`,
  key IA, zero disc) is preserved unchanged — on arm64e the thunk
  pointer is signed; the thunk itself is a normal code page.

## 7. Hashes (theos, signed)

- Provider fat `82e7451975781007749db91ccf46d3b5e50b1b6b303a8977b3d51a7d4be9e3a9`
  - arm64 `2a8da5bf2075b2004f22958012b4a0e1cacb6561e562910f3c5eb4b01a8468b3`
  - arm64e `3bde9239cc5590d9c098e73e7736df85b8453d2ca263b4cebe8254105d98855b`
  (post-hardening build: adds the in-window cbz-target refusal; prior
  post-fix build `8cde7566…` superseded — do not ship it either)
- libmachook unchanged since D035: arm64 `bf3ae467…d4dc30`,
  arm64e `f2143308…f025c`
- `git diff --check` clean; no commits made.

(handover to codex now)
