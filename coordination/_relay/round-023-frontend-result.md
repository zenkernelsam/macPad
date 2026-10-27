# Frontend R023 result — exact-build libxpc guard frame attribution

Read the R011 22F82 libxpc capture/metadata, R012/R013 crash reports and the
shared-cache map head. Read-only local work: no SSH/device, build,
dependency, source/ABI/TCC change or Git write. R022's 15.6 handler body is
not used as a 22F82 fact.

Evidence:
`docs/evidence/r023-libxpc-guard-frame-attribution-20261018.md`
(SHA-256 `ffa3b729901a31bac6c6588d1802aa4dcd3ea14d784c7b8e77eccfb5a914195b`).

## Exact findings

- 22F82 libxpc: UUID `F3F19227-…`, `__TEXT` `0x136000`-`0x17A000`
  (278,528 B), fileoff `0x136000`, text SHA `af2bbe7c…`, slide `0x99c000`.
- Guard report `imageIndex 0` is the shared cache (`base 0x180000000`), so
  its frame offsets are cache-relative/file offsets; libxpc image offset =
  cache offset − `0x136000`.
- Common guard prefix resolution:

| Frame | Cache offset | libxpc image offset |
| --- | --- | --- |
| 0 | `0x3CEB78` | **outside libxpc**, image unresolved |
| 1 | `0x13BF8C` | `0x5F8C` |
| 2 | `0x141928` | `0xB928` |
| 3 | `0x1417B0` | `0xB7B0` |
| 4 | `0x1410E4` | `0xB0E4` |
| 5 | `0x140C84` | `0xAC84` |

Only frames 1–5 are exact libxpc; frame 0 is not, and must not be assigned to
libxpc. Frames 6+ are also outside libxpc.

- The only exact 22F82 libxpc bytes local are `0x145140`-`0x145240`
  (`_xpc_bootstrap_services`, image `0xf140`); they do not overlap any guard
  frame. No exact guard disassembly or reason-string construction is possible.
- The exact report tuple (`EXC_GUARD`/`GUARD_TYPE_USER`, namespc 7, reason 9,
  `LIBXPC`/`XPC_EXIT_REASON_FAULT`) stands; its 22F82 function-level cause
  remains unproven.

## Minimal future reads

1. Full 874,394-B 22F82 `.map` (or rows covering `0x3CEB78` and
   `0x136000`-`0x17A000`) to identify frame 0's image.
2. Full libxpc `__TEXT` `0x136000`-`0x17A000` (verify SHA `af2bbe7c…`) or the
   focused `0x13BE00`-`0x141B00` range for frames 1–5.
3. ±0x100 bytes around frame 0 once its image is known.
4. The `os_reason` reason string from exact code/literals or a non-lightweight
   report/log around 18:24:44; no inference from 15.6.
5. Optional per-image function table for `0xAC84`-`0xB928` and bytes for
   frames 6+ if the full chain is required.

No benignity, misuse message, or fixed behavior is claimed. No background
tasks remain running or queued.

(handover to codex now)
