# D121 — minimal evidence plan for the unresolved responsibility provider

Task: `coordination/_relay/devin-121-frontend-task.md`. Read-only
reconciliation; no cache re-search, no device/Git/build; backend files
untouched.

## 1. Inventory reconciliation (D119 vs R013/R009/R025/D117/metadata)

| Item | Status after cross-check |
|---|---|
| Slot `0x1dfef8f88` inside Ventura arm64e map RW region `0x1DD134000→0x1DFF4C000`, owned by no image segment | **FACT** (D119; `.map` identity anchored by libquarantine VAs + kernel header UUID `34A49B54`) |
| "Shared cache auth-pointer region" semantics | **Upgraded to FACT by `probe-inventory-r003*.json`**: the probe's *own* stubs' slots resolve at VAs `0x1dfef4b98`, `0x1dff00d20`, `0x1dfef2678`, `0x1dff3f358`… — the same `0x1dfe*/0x1dff*` shared region, from a *different image* — and their `fn=` values carry PAC'd pointers whose low bits map into named images (`-> libsystem_kernel.dylib:__getpid`, `-> libbsm.0.dylib:audit_token_to_pid`, `-> CoreFoundation:CFDictionaryGetValue`, `-> libmachook.dylib:…`). The shared-region interpretation and the PAC low-bits→target-VA decode are already exercised live, not theory |
| Full exact `libsystem_kernel` local | **Missing** (device-only); local = 32KB slice + 4KB header (UUID `34A49B54`) + 4KB text page `0x1803ce000` (unidentified `svc` stubs) |
| Ventura cache header/mappingInfos/subcache/fixup records | **Missing** locally; `.map` gives VA→mapping only, no file offsets |
| share-review `dyld_shared_cache_arm64e[+.01]` | Excluded: UUID `4C1223E5…` ≠ `BE59BB71` |
| **Correction to D119** | `__auth_got` page `0x5046d578+0xf0` ends `0x5046d668` **inside** the file (`0x5046E000`), unpopulated (zero hole) — not "past EOF" as I wrote |
| R013 slot-beyond-file math | Consistent: `0x1dfef8f88-0x180000000=0x5fef8f88` > file `0x5046E000` — different coordinate (cache offset vs extraction file); both true |
| R009 exports-trie/dysymtab values | Consistent with D119 (zeroed/absent) |

## 2. Minimal acquisition checklist (smallest → sufficient)

**A. The single decisive artifact: live 8 bytes at the slid slot VA**
(computed at runtime, never hardcoded)
- slid slot = `0x1dfef8f88 + cache_slide`, where `cache_slide =
  dladdr_base(any cache image) − its unslid __TEXT VA` (e.g. libquarantine
  base − `0x18b31f000`). Per-generation slide varies (`0x123ec000`,
  `0x1ba40000` observed) — measure, don't assume.
- Method **already proven**: `probe-inventory-r003*` artifacts read
  stub-slot `va=`/`fn=` pairs and attributed them to `image:symbol` via VA
  ranges — the identical operation applied to libquarantine's stub #2 slot.
- **What it proves**: the actual `braa` target — the resolved PAC'd
  pointer braa authenticates; low bits → target VA → `dladdr`/image-list
  → provider image + function. Closes the edge directly.
- Read in any chroot process with libquarantine loaded; read-only memory
  access, no breakpoint required (a breakpoint is an alternative, not a
  prerequisite).

**B. File-side alternative (only if A unavailable): cache header
   `mappingInfos`**
- First page of the Ventura `dyld_shared_cache_arm64e` (Cryptex) +
  subcache file list/sizes — translates `0x1dfef8f88` to exact
  file/subcache offset (`.map` already proves membership; header gives
  ownership).
- Then the slot's file bytes **plus** the chained-fixup/bind record
  covering it. File bytes alone may be a fixup placeholder — proves bind
  *intent*, not the runtime-resolved target.

**C. Provider disasm — conditional on A/B**
- If target VA ∈ `0x1803bf000–0x1803c5fe8` → disasm that range of the
  device-side `libsystem_kernel` `34A49B54` (expects `mov x16,#N; svc` —
  confirms/denies `__mac_syscall`; syscall number `N` itself discriminates
  `__mac_syscall` vs `__sandbox_ms`-style entries).
- If target lands elsewhere → name that image; the edge moves one hop,
  same recipe.

Not needed: broad cache dumps, kernel snapshot, KRW, fixup tables beyond
the slot's own record, or any privileged mutation.

## 3. Boot/rootfs tie (labeled)

- **FACT**: current generation (WS 10447-era evidence) has the chroot
  alive with `macws_final_composite.state=ready` and sequence advancing —
  the display pipeline is independently witnessed and does not depend on
  this edge.
- **FACT**: the unresolved slot only gates the *TCC attribution chain*
  (tccd getter → ENOPOLICY → empty reply → `sls_rc=1002`, historical
  WS 48891 generation, msgID-correlated).
- **MISSING/UNCLAIMED**: no evidence connects any GUI-start `exit 1` in
  current boot/rootfs records to TCC/responsibility; this checklist
  attributes nothing to it.

## 4. Commands / exits

| Command | Exit | Result |
|---|---|---:|
| artifact inventory `coordination/artifacts/probe-inventory-r003*.json` | 0 | live `va=`/`fn=`→`image:symbol` decode precedent |
| `.map` VA coverage + per-image enumeration | 0 | slot in RW mapping, no owner (D119) |
| kernel-header LC_UUID parse | 0 | `34A49B54` |
| share-review cache magic/UUID | 0 | `4C1223E5…` excluded |
| file-size/offset arithmetic | 0 | `0x5046D668 < 0x5046E000` — D119 EOF claim corrected |

(handover to codex now)
