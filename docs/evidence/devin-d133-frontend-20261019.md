# D133 — mount_bindfs vendoring suitability review (read-only)

Date 2026-10-19. Packet `coordination/_relay/devin-133-frontend-task.md`.
Audit of D132's vendoring proposal. No device, no execution, no build, no
edits. Binary facts from `otool`/`nm`/`strings` on the only local copy;
contract facts from `ensure_jb_usr_bind.sh`, `mountdevfs`, `postinst.sh`.

## 1. The binary, exactly

`analysis/share-review-20261015/extracted/host-overlay/var/jb/usr/local/
bin/mount_bindfs` — **only local copy** (filesystem scan: unique).

- sha256 `34c9c30b00a3462ec4226930960205a6799bdd53471f564090ed1486967872b5`
- fat `arm64`+`arm64e`; `LC_BUILD_VERSION platform=2 (iOS)`, minos 14.0,
  sdk 16.4, ld 1230.1; `LC_MAIN`; `LC_ENCRYPTION_INFO_64 cryptid=0`
- deps: `/usr/lib/libSystem.B.dylib` only; rpaths `/var/jb/{Library/
  Frameworks,usr/lib}` + `@loader_path/.jbroot/…` (rootless-aware)
- 2× `LC_CODE_SIGNATURE`: ldid adhoc CodeDirectory, id
  `mount_bindfs.b703f318.unsigned`, no CMS — CDHash-extractable,
  trustcacheable as-is
- imports (complete, 18 stubs): `mount`, `realpath$DARWIN_EXTSN`, `stat`,
  `getopt/optarg/optind`, `str*`, `err/errx/fprintf`, `free/strdup`,
  `_exit`, `__error`, stack_chk
- **No `symlink`, `unlink`, `link`, `mkdir`, `system`, `spawn`, `write`
  imports.** The only mutating call is `mount(2)`.
- strings: option table (userquota/groupquota/auto/exec/suid/rdonly/
  union/browse/defwrite/atime), `usage: mount_bindfs [-o options] target
  mount_point`, `realpath %s`, `identical paths`, `%s on %s`,
  `-o %s: option not supported`, `not a directory`. **Zero occurrences
  of `symlink`/`exposed`/`unsupported` in either slice (19 unique
  strings total).**

## 2. D132 overclaim — the vendored bytes cannot create the symlink

D132 wrote: "the helper itself creates the exact documented symlink on
bindfs-rejecting kernels (Oct-6 runtime output verbatim)". **Refuted for
`34c9c30b`**:

- No `symlink(2)` import → the binary is structurally incapable of the
  documented fallback. Hard capability gap, not a signing question.
- The Oct-6 verbatim output `exposed /var/jb/usr at /var/mnt/rootfs/var/
  jb/usr via symlink (bind unsupported on this kernel)` exists in **no
  binary in the share overlay and no source in this repo** — git history
  shows it only inside `c2e8373` documentation.
- Therefore the fallback-capable helper that ran on iPad14,3 Oct-6 is a
  **different build whose bytes are unavailable locally**. Provenance
  gap is real and must be documented: source unknown (stock bindfs
  mounter patched by the project or another lineage), surviving local
  artifact = stock mount-only build.

## 3. Contract comparison (static)

| Property | `34c9c30b` | `ensure_jb_usr_bind.sh` needs |
|---|---|---|
| argv | `[-o opts] target mount_point`; ensure passes exactly `SRC DST`, no opts — compatible | yes |
| failure | `err/errx` nonzero exit, mount-only side effects — no partial state, no silent success | fail-closed preserved |
| privilege | runs as caller (root); no entitlements, no libjailbreak — mount(2) with caller creds | root, fine at start/postinst |
| symlink fallback | **none** | documented as helper behavior; actually produced by ensure's acceptance of a pre-existing exact link |

The `mountdevfs/main.c` comment "Dopamine's mount-bindfs helper …
borrows the jailbreak's unsandboxed root credential" does **not**
describe this binary (no libjailbreak import). It describes mountdevfs's
own model or a different helper build. Consequence: if T8103/16.3
requires more than root for bindfs, stock helper fails → ensure exits 1.

## 4. Suitability verdict

`34c9c30b` is **suitable to vendor only as the mount path** on kernels
that admit bindfs for a trustcached root process. It does **not**
reproduce the documented contract; on a bindfs-rejecting kernel with no
pre-existing exposure it changes the failure message, not the outcome.

Additional unverified premise surfaced: **T8103/16.3 bindfs rejection
was never runtime-verified** — the only record is iPad14,3/16.5.1
(Oct-6). D127's "(this T8103 is one)" is an extrapolation; this device
may accept the mount outright.

Revised narrow fix (supersedes D132 step 1):
1. Vendor `34c9c30b` at `usr/local/bin/mount_bindfs` for kernels where
   bindfs works (real mount > symlink), sha-pinned, `add_all_trustcache`
   line added — postinst trusts via explicit per-path `jbctl trustcache
   add`, NOT a blanket pass; D132's "postinst covers it like every other
   tool" was imprecise.
2. Since no fallback-capable helper exists locally, the documented
   symlink fallback must live elsewhere: provision the exact link at
   postinst when the helper's mount fails, or keep ensure accepting the
   (already manually restored) link. Reinstating the script-side
   creation is now the honest fallback — there is no binary alternative
   to preserve.
3. Correct `ensure_jb_usr_bind.sh:23`'s comment — this binary is not
   "the packaged helper" that publishes a symlink.
4. If the true fallback helper's bytes are later found (device backup,
   older deb), prefer it; its sha must be recorded before install.

## 5. Pre-install decode checks (must all pass)

sha256 == `34c9c30b…`; `lipo` = arm64+arm64e; `LC_BUILD_VERSION`
platform=2 minos≤16.3; imports ⊆ libSystem (all resolvable iPadOS 16.3);
`LC_CODE_SIGNATURE` per slice → both CDHashes registered (`jbctl
trustcache info` contains both after install); mode 0755 at
`/var/jb/usr/local/bin/mount_bindfs`; ensure accepts it at the
helper-check line; docs updated to declare mount-only capability.

## 6. D132 overclaim ledger

| Claim | Status |
|---|---|
| helper creates documented symlink | **refuted for `34c9c30b`** — no capability, no strings; different build produced Oct-6 output |
| "postinst covers it like every other tool" | imprecise — explicit per-path `add_all_trustcache` needed |
| share-overlay binary = "the packaged helper" | it is *a* packaged copy from another lineage, not the Oct-6 fallback build |
| (inherited D127) "T8103 is bindfs-rejecting" | unverified — only iPad14,3/16.5.1 is runtime-confirmed |
| vendoring fix direction | stands, narrowed per §4 |

(handover to codex now)
