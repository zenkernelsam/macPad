# D036 frontend — independent check of in-flight exact-relocation thunk

From: devin (frontend reviewer). Scope: independent capstone decode +
thunk simulation of the D036 implementation as visible in the tree at
~23:2x. Full detail: docs/evidence/devin-d036-relocation-independent-check-20261018.md
Repro: analysis/devin-d035-frontend/permit_audit.py §4 (rerunnable).

## P0 — cbz-skip off-by-one, live at misc/cydiasubstrate_macos.m:516

`| (4u << 5)` makes the inverted cbnz skip 4 slots FROM the branch — it
lands INSIDE the emitted .quad (udf), not on the next translated insn.
Every not-taken path (i.e. every real call) executes data -> thunk crash
on first original-call replay. Fix: `| (5u << 5)` (skip = 1 + emitted
slots). Simulated both variants; imm=5 lands exactly on next insn for all
4 pinned windows. publicSwapEnd thunk with imm=5:

    cbnz x0,+0x14 / ldr x17; br x17 / .quad sym+0x10 / ldr x1,[x0,#0x728]
    cbnz x1,+0x14 / ldr x17; br x17 / .quad sym+0x10 / braaz x1

## P2 — two dead mask constants

brabz is 0xd61f0c1f (backend wrote 0xd65f081f — undecodable);
blrabz is 0xd63f0c1f (backend wrote 0xd67f081f — undecodable).
braaz/blraaz constants correct. Impact low for current targets but the
constants never match anything; non-Z braa/brab/blraa/blrab (0xd7xx)
unmasked entirely.

## Corrections to earlier frontend decode (capstone, verified)

- stub insn3 is `braaz x1` (0xd61f083f), not `br x1` — [x0+0x728] is a
  PAC-signed fnptr (A-key, zero disc). Wrapper-retarget now viable under
  the new translator; kern_SwapEnd retarget unnecessary for this target.
- insn2 cbz target is sym+0x10 (capstone absolute form).

## Design verdict

Exact-relocation approach approved-in-principle — semantically exact for
any cbz target (better than the frontend's sym+0x10-only suggestion).
Hold P0 fix + the §4 acceptance-bar tests in the evidence doc before
build/A-B. No device action implied.

## UPDATE (post-backend-settle, independently verified)

Backend fixed both findings (imm→5, brabz→0xd61f0c1f, blrabz→0xd63f0c1f).
Frontend independently re-verified: re-simulated all 4 pinned windows
(cbnz lands exactly on next emitted insn), compiled the provider x86_64
MACWS_RELOC_TESTONLY and ran test_mshook_relocate_host.c — ALL PASS.
Emitted thunk now b50000a0/b50000a1 with correct .quad targets.

STALE-HASH ALERT: backend result + devin-034-owned-generation-plan record
the PRE-FIX build (fat 7172dd68…, arm64 1a5448fe…, arm64e 6d667314…) and
pre-fix bytes (b5000080). Post-fix rebuild 23:28:34-38:
arm64 84ef0b017860847dbd2649e6229e48a3cb409c4b9b2153160734d745a08cccf7
arm64e 8f4c723908c98f43499f4bc66ac6d6b831c5d50dfc5ed62e33f02bcfe92ef63d
fat   8cde7566b2fee933dd3c9f8c24199460bf5f057b78ff7414055d10740928068e
Update the staged-hash lines before any deploy; the buggy pre-fix fat
must not ship. Verdict: approve for the authorized device A/B once the
hash lines are corrected. Residual: live IOMFB uuid slice check (fails
closed), PAC-auth thunk execution is device-witnessed only.

## UPDATE 2 — hashes confirmed corrected; one latent guard hole (P3)

Backend v2 verified consistent (hashes match frontend-measured values).
arm64 live-execution attempted locally — impossible: this host is x86_64
(AMD, no Rosetta); remains device-only per coordinator's gate.

NEW P3 (latent, not triggered by the 4 pins): a cbz/cbnz whose target
lands INSIDE the stolen window [sym,sym+16) emits .quad = original
target, which post-patch points at mid-patch-stub bytes -> crash/
re-entry. Verified by simulation (cbz x0,sym+0x08 -> .quad=0x1008).
One-line guard recommended at the emit site: refuse
target in [sym_pc, sym_pc+16) — or remap .quad to the emitted thunk
offset. Coordinator's "internal-window behavior" clause now has a
concrete fixture. Scratch-register audit: thunk clobbers x17 only;
residual = original window reading x17 across a translated cbz
(same exposure as classic Substrate; no current target does this).

## UPDATE 3 — P3 fixed, converged, final verification clean

Guard landed verbatim as recommended (refuse target in [sym,sym+16))
plus a g_intwin_window test fixture that provably fires the new refusal.
Fresh post-P3 build 23:34:56 — frontend re-measured, matches backend v3:
arm64 2a8da5bf2075b2004f22958012b4a0e1cacb6561e562910f3c5eb4b01a8468b3
arm64e 3bde9239cc5590d9c098e73e7736df85b8453d2ca263b4cebe8254105d98855b
fat   82e7451975781007749db91ccf46d3b5e50b1b6b303a8977b3d51a7d4be9e3a9
Re-ran host test against MY OWN x86_64 TESTONLY compile of the settled
source: ALL PASS (thunk byte-exact; refusals live: pcrel, blr-family,
intra-window target, no-permit). All frontend findings closed.
Verdict unchanged: approve for the authorized device A/B. Device-only
residuals: IOMFB live-slice uuid (fails closed -2), PAC-auth thunk
execution, real changed-bytes/counter/pixel ladder per the plan.

(handover to codex now)
