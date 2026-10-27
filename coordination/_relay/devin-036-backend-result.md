# devin-036 backend result — SwapEnd relocation contract

Worker: `macPad D036 backend 8057`. Scope: local only — **no device
deploy, no generation start/stop, no install, no commit.** Packet:
`coordination/_relay/devin-036-backend-task.md`.

## Chosen solution: exact wrapper-aware relocation (option A)

The `publicSwapEnd` window `b4000080 f9439401 b4000041 d61f083f` is a
4-insn authenticated tail-call shim: `cbz x0,+0x10 / ldr x1,[x0,#0x728] /
cbz x1,+8 / braaz x1`. Both cbz targets are S+0x10 (the post-window ret
path); `braaz x1` is terminal. The provider now relocates this class
instead of refusing:

- `cbz/cbnz` → inverted condition (imm19=5, skipping `ldr x17,#8; br x17;
  .quad <original absolute target>` = 4 slots) — taken/non-taken preserved
  exactly, absolute target preserved verbatim. The earlier imm19=4
  revision landed mid-quad — caught by the frontend independent check
  §2b and now covered by a computed landing-offset assertion in the test.
- terminal `ret/retaa/retab/br/braaz/brabz/eret` → verbatim, branch-back
  suppressed. Frontend P2 fixed: brabz/blrabz constants were undecodable
  (`0xd65f081f`/`0xd67f081f` → `0xd61f0c1f`/`0xd63f0c1f`; bit10 = A/B key).
- `blr/blraaz/blrabz` → still refused (return-into-window untested);
  `blrabz x0` fixture proves the corrected mask fires.
- `cbz/cbnz` targeting an address inside `[symbol, symbol+16)` → refused
  (post-install it would land inside the patch stub); `cbz x0,+8` fixture
  proves it.
- `b/bl/b.cond/tbz/tbnz/adr/adrp/ldr-literal` → still refused.
- Everything else verbatim; branch-back to S+16 only when control didn't
  leave the window.

Nothing NOPed, no condition forced, refusal not weakened globally, no
bytes copied blindly — the three forbidden shapes are all untouched.

## Proof artifacts

- `misc/cydiasubstrate_macos.m` — relocator + extended terminal
  classifier + `MACWS_RELOC_TESTONLY` compile gate (test-only; absent in
  production builds).
- `misc/test_mshook_relocate_host.c` — x86_64 thunk-byte verification,
  **ALL PASS**: emitted thunk for the real wrapper window is byte-exact
  (`b50000a0 58000051 d61f0220 <S+0x10> f9439401 b50000a1 58000051
  d61f0220 <S+0x10> d61f083f`), decoded-imm19 landing-offset assertions,
  verbatim window copies + branch-backs, permitted b.cond → refused/
  callable-original, permitted blrabz → refused, permitted in-window
  cbz target → refused, unpermitted → refused/untouched, wrong
  uuid/prologue/offset → -2/-1/-1.
- `misc/test_mshook_relocate.c` — arm64 live-execution test staged
  (can't run on x86_64 host; for the device round).
- `docs/evidence/devin-d036-swapend-trampoline-20261018.md` — full
  disassembly, scheme, per-permit classifier audit, limitations.

## Permit classifier audit (all 4 D035 pins)

IOMFB SwapEnd → relocates (was refused). QC enable/begin/finish → pure
verbatim windows (pacibsp/sub|stp/stp/add) + branch-back — already
worked. No D035 permit needed changes; R012 PAC hunk on `*result`
untouched.

## Hashes (theos, signed)

- Provider fat `82e7451975781007749db91ccf46d3b5e50b1b6b303a8977b3d51a7d4be9e3a9`
  (arm64 `2a8da5bf…a8468b3`, arm64e `3bde9239…d98855b`) — supersedes
  `8cde7566…`; adds the in-window-target refusal.
- libmachook unchanged from D035 (arm64 `bf3ae467…`, arm64e `f2143308…`)
- `git diff --check` clean; working tree only, no commit.

## Bounded limitations (in evidence doc §6)

Thunk bytes verified exactly; live arm64 execution + PAC-auth-in-thunk
witness deferred to the authorized device A/B. IOMFB uuid still treated
as arm64e-slice-pending (fail-closed -2 if the live image differs).
Non-Z `braa/brab` forms verbatim-copy with dead branch-back (correct,
suboptimal). Owned-generation plan updated: acceptance now requires
changed target bytes + callback counters + pixels, not fprintf.

Stopping per packet — awaiting coordinator's authorized device round.

(handover to codex now)
