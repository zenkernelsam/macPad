# D119 — independent audit of D117 cache-slot conclusion (frontend)

Task: `coordination/_relay/devin-119-frontend-task.md`. Read-only; no
device/build/Git; D116 transport and backend files untouched.

## Verdict summary

D117's conclusion (slot `0x1dfef8f88` unresolvable from local bytes) is
**correct**, but two of its characterizations needed correction and one
in-tree artifact was **missed** — `analysis/r025-libxpc-guard/`
(created 2026-10-18 20:5x, predating D117) materially tightens the
evidence.

## 1. Is the slot region a "mapped shared-cache auth-pointer region"?

**Now provable as a mapped cache RW region** — via the missed artifact:

- `analysis/r025-libxpc-guard/dyld_shared_cache_arm64e.map` (874,394 B) is
  the **Ventura arm64e cache map**: its libquarantine entry
  (`__TEXT 0x18B31F000→0x18B322000`, `__AUTH_CONST 0x1DD78C558→0x1DD78C668`,
  `__DATA_CONST 0x1D41AC958→…9A8`) matches the exact extraction
  byte-for-byte in VA layout, and its `libsystem_kernel` entry is
  corroborated by `libsystem_kernel-header-0x3bf000.bin` (4 KB header page)
  whose LC_UUID parses to **`34A49B54-82B2-37A1-9314-F6A4A2BB3FF8`** —
  identical to R005 metadata's exact Ventura dependency. Identity =
  same cache layout (UUID BE59BB71 itself is not printed in the map;
  VA-layout identity is the basis, labeled accordingly).
- The map's top-level table shows mapping `RW 46MB
  0x1DD134000 → 0x1DFF4C000`; `0x1dfef8f88` is **inside it** (FACT).
- Enumerating every per-image segment in the map: **no image owns
  `0x1dfef8f88`** — it sits in inter-image shared space of that RW
  mapping (FACT).
- What remains THEORY: calling it an "auth-pointer region". The stubs
  demonstrably compute those VAs (FACT, instruction decode — verified
  twice), but the semantics of the region (shared auth-GOT vs other
  cache-built pointer space) and "permuted" ordering are interpretation;
  the observed order is merely non-monotonic. Naming requires the fixup
  record or slot contents.

So D117's phrasing was directionally right but overstated: what was then
proven was only "clustered VA window outside image segments"; the `.map`
now upgrades "mapped shared cache region" to FACT while "auth-pointer"
semantics stays THEORY.

## 2. R005 `device_full` libsystem_kernel — NOT local

`metadata.json` `device_full` paths are **device paths**
(`/var/mobile/macws_r005_evidence_20261018/…`) — the full exact kernel is
not in the checkout. Locally present for UUID `34A49B54`:

- `analysis/r005-responsibility-inputs/slices/libsystem_kernel.dylib.slice`
  — 32,768 B; LC_SYMTAB/LC_DYSYMTAB zeroed; early `__TEXT` only.
- `analysis/r025-libxpc-guard/libsystem_kernel-header-0x3bf000.bin` —
  4,096 B header page (UUID verified above).
- `analysis/r025-libxpc-guard/frame0-block-0x3ce000.bin` — 4,096 B kernel
  text page at `0x1803ce000`; `disasm-libsystem_kernel-frame0-correct.txt`
  shows generic `mov x16,#N; svc #0x80` BSD-syscall stubs — **not
  identified as `__mac_syscall`**, insufficient for provider disasm.
- `analysis/device-leftovers-20261006/…/libsystem_kernel.dylib` — UUID
  `6E4A96AD…` (15.6) and WS-side arm64 `C76E6BED…` are both excluded by
  UUID/arch.

D117's wording ("device copies already staged") was accurate only as a
device-path statement; the full kernel remains device-only.

## 3. Cache artifacts — missed vs excluded

| Artifact | Status |
|---|---|
| `analysis/r025-libxpc-guard/dyld_shared_cache_arm64e.map` | **was missed in D117** — Ventura arm64e map (see §1) |
| `analysis/r025-libxpc-guard/libsystem_kernel-header-0x3bf000.bin`, `frame0-block-0x3ce000.bin`, `disasm-*`, `libxpc-text-*.bin`, `launchd-ios*` | found; kernel pieces too small to resolve provider; launchd-ios is native iOS (20D47), different domain |
| `analysis/share-review-20261015/extracted/.../dyld_shared_cache_arm64e[+.01]` | **excluded by identity**: `dyld_v1 arm64e`, UUID `4C1223E5-CACE-3982-A003-6110A7A8A25C` ≠ `BE59BB71` (Sequoia share cache) |
| `analysis/ios163cache/dyld_shared_cache_arm64e[+.19]` | excluded (iOS 16.3, UUIDs `136ECC70`/`3C5E5C44`) — R013 already |
| cache header/mappingInfos/subcache/.a2s/fixup records for the Ventura cache | **still absent locally** — `.map` gives VA→mapping but no file offsets; subcache ownership of `0x1dfef8f88` unprovable in-tree |

## 4. Is the 8-byte slot sufficient? — No, per R013 and independently here

- On the cache **file** the slot may hold a chained-fixup encoded value or
  unbound placeholder, not the resolved PAC'd pointer; `braa x16,x17`
  authenticates with `x17`=slot address as modifier, so the stored bytes
  are not directly the target VA.
- Definitive binding needs **either** the fixup/bind record covering the
  slot **or** a live read of the slid VA (`0x1f22e4f88` at slide
  `0x123ec000`) + image-lookup — R013 §4/B says the same; my D117
  "(or the fixup record)" stands but should read as *required*, not
  optional, when interpreting file bytes.

## 5. Smallest justified next artifact (refined)

1. Cache header `mappingInfos` (Ventura `dyld_shared_cache_arm64e` main
   file header page) → translates `0x1dfef8f88` to an exact file/subcache
   offset (the `.map` confirms the VA is cache-mapped but carries no file
   offsets); **or** a live read at slid `0x1f22e4f88` — R013's B path.
2. The 8 slot bytes at that location **plus** the chained-fixup record
   covering it (without the fixup, raw bytes may be placeholder).
3. Then the provider image (device-side `libsystem_kernel` full copy if
   the target VA lands in `0x1803bf000+0x39fe8`).

## 6. Commands / exits

| Command | Exit | Result |
|---|---|---:|
| `find analysis -name "dyld_shared_cache*"` etc. | 0 | ios163 + share-review + r025 `.map` only |
| `.map` coverage enumeration (all image segments vs `0x1dfef8f88`) | 0 | zero covering image entries; inside mapping `0x1DD134000→0x1DFF4C000` |
| `.map` libquarantine/kernel entry grep + kernel-bin LC_UUID parse | 0 | VAs match extraction; UUID `34A49B54` |
| share-review cache magic/UUID | 0 | `dyld_v1 arm64e` `4C1223E5…` — excluded |
| local `libsystem_kernel*` inventory | 0 | slice 32KB + header 4KB + page 4KB + 15.6 only |
| stub-slot decode re-check (D117 reproduction) | 0 | `0x1dfef8f88` confirmed |

No edits to sources, no device, no Git.

(handover to codex now)
