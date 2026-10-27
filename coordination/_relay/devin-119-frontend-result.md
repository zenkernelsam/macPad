# devin-119 frontend result — audit of D117 cache-slot conclusion

Read-only audit complete. Evidence doc:
`docs/evidence/devin-d119-frontend-20261019.md`. No edits/device/Git;
backend files untouched.

## Verdict on D117

Conclusion stands (slot `0x1dfef8f88` unresolvable locally), with two
corrections and one **missed artifact**:

1. **Mapped-region status — upgraded by missed artifact.**
   `analysis/r025-libxpc-guard/dyld_shared_cache_arm64e.map` (predated
   D117, missed by me) is the Ventura arm64e cache map — libquarantine
   VAs match the extraction exactly, and
   `libsystem_kernel-header-0x3bf000.bin` LC_UUID = `34A49B54-…`
   corroborates. The map proves `0x1dfef8f88` is inside mapped region
   `RW 46MB 0x1DD134000→0x1DFF4C000` and **owned by no image segment** —
   inter-image shared cache space. FACT now: "mapped shared cache
   region". Still THEORY: "auth-pointer" semantics and any permutation
   claim (observed order is only non-monotonic).
2. **`device_full` kernel is NOT local.** metadata paths are device
   paths; locally present = 32KB slice (zeroed symtab) + 4KB header page
   + 4KB text page at `0x1803ce000` (generic `svc #0x80` stubs — not
   identified `__mac_syscall`). Full exact kernel remains device-only.
3. **Artifacts:** share-review `dyld_shared_cache_arm64e[+.01]` checked —
   UUID `4C1223E5…` ≠ `BE59BB71`, excluded (Sequoia); ios163 cache
   excluded (R013). Ventura cache header/mappingInfos/subcache/fixup
   records still absent — `.map` gives VA→mapping but no file offsets.
4. **8-byte slot not sufficient alone**: file bytes may be a chained-
   fixup placeholder, and `braa` uses slot-address diversity — needs the
   fixup/bind record OR a live read at slid `0x1f22e4f88`
   (slide `0x123ec000`).

Smallest next artifact: cache-header mappingInfos → exact file/subcache
offset of `0x1dfef8f88` (or the live read), then slot bytes + covering
fixup record, then provider image. All policy/103 attributions remain
open.

(handover to codex now)
