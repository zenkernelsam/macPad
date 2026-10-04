# macOS 15.6.1 / 24G90 research freeze — 2026-10-04

This document freezes the macOS 15.6.1 / 24G90 branch of the iPad13,11
T8103 investigation before returning to the validated Ventura path. It is the
first document to read when resuming this branch. It contains no credentials
or raw chat transcript.

## Target and acceptance

Target: jailbroken iPad13,11 (M1/T8103), iPadOS 16.3 / 20D47, Dopamine
rootless, using the real macOS 15.6.1 / 24G90 userspace, dyld, system
libraries, and shared cache to run:

```text
/var/jb/usr/bin/chroot /var/mnt/rootfs /bin/echo HI
```

Only a real `HI` on stdout is the first milestone. GUI and WindowServer were
intentionally deferred.

## Highest point reached

The branch reached the real macOS dyld and real 24G90 cache mappings. The
4-GB shared-region/gap obstacle was crossed by the historical `emptysr +
highreserve` dyld diagnostic, but the command still failed at the first cache
text execution page. The final reproducible failure on original dyld is:

```text
fault VA 0x18047dc9c
code0=0x32  (KERN_CODESIGN_ERROR)
pagein_error=0
no HI
```

The page bytes, CodeDirectory slot[287], SHA-256, page validation state,
outer PMAP-CS association, trust=8, and 24G90 CDHash all match. The failure
is below ordinary trustcache, file hash, and dyld-page content.

## Final root-cause boundary

The exact T8103 kernel IDA (Instance1) shows `sub_FFFFFE00086A0924` selecting
the PMAP owner for a shared-region VA using only `nested_region_addr/size`.
It does not consult `nested_region_asid_bitmap`.

Fresh post-reboot runtime evidence shows:

```text
outer pmap association: [0x180000000,0x1e7f5c000), trust=8, 24G90 CD
nested region: [0x180000000,0x280000000)
nested bitmap target twig: set (word[0]=0xffffffff)
nested pmap association-tree root: 0
```

Public XNU `vm_shared_region_remove()` uses fixed overwrite and the VM delete
path calls `pmap_unnest_options()` for the original nested submap. Thus the
twig is already unnested. T8103 nevertheless routes the fault to the empty
nested pmap and the PMAP/PPL path returns 50.

This is **RE-confirmed via IDA 13337 + runtime-confirmed**. It is more precise
than the earlier hypothesis that unnest had not run.

## Experiments and status

- Trustcache restoration: 24G90 pair is present after explicit temporary
  thermal-bypass trust diagnostic; normal `macos_gui.sh trust` can pause at
  `thermal-state=serious`. The bypass did not change source files.
- Original dyld baseline: SHA-256
  `b8fdbc1b7cfd15cccbcd110c0c3cb1ff91d135d6664b84770d42df843381b91e`.
- `emptysr + highreserve`: crossed the high-address guard, then reached the
  main-cache data fault; not a fix.
- TPRO removal, MAP_SHARED, CodeSignature retention, and other dyld-only A/Bs:
  no milestone.
- Child-scoped `pmap+0xc9=3`, `nested_pmap=NULL`, and
  `nested_region_size=0`: all writes succeeded and were restored; all still
  produced code0=50. These are negative diagnostics, not fixes.
- Direct kernel text write at runtime instruction
  `0xfffffe002de1c934` (original word `0xf9402c08`): blocked by PPL/KTRR; the
  byte remained unchanged.
- `fmt13_patch.py`: never run on the device. Its pager-format work is not the
  current first-fault path.
- System-wide/main-only dyld A/B: failed earlier at dynamic-region address
  `0x100000000`; original dyld was restored.

## Offline kernel candidate

IDA maps `sub_86A0924+0x10` to raw kernelcache file offset `0x169c934`. The
offline candidate changes:

```text
original: 0xf9402c08  LDR X8,[X0,#0x58]
candidate: 0xd2800008 MOV X8,#0
raw SHA-256: ed42a7ab1b0b63c438c6cd2d026d27629939fdf8cb989b49e688e3afff97f283
```

The candidate was recompressed into an offline IMG4 of the original size, but
its serialized IM4P SHA-384 is different while the original IM4M `krnl` DGST
is unchanged. The mismatch is cryptographically proven; the candidate has
never been copied to preboot or booted.

There is no PongoOS, kexec, IMG4 signing, or bootloader patch tool in the
current workspace/device. Rootless Dopamine runtime KRW cannot modify kernel
text. Do not replace `/private/preboot/.../kernelcache` with an unsigned
candidate.

## Repository state and resume procedure

Latest freeze-supporting commits on `main` include:

```text
94f74c0 docs: prove IMG4 kernel digest mismatch
6e782f7 docs: record offline IMG4 repackaging boundary
3a43504 docs: audit kernelcache boot chain boundary
c51a7d5 docs: record offline T8103 kernelcache candidate
96c5a50 docs: bound nested owner negative A/Bs
```

Read this document, `docs/porting/dyld-15.6.1-state.md` from its final
2026-10-03 entries, and `docs/porting/T8103-PMAP-NESTED-OWNER-FIX-PROPOSAL.md`.
Re-check exact device kernel/dyld identity and trust state before future work.
Never assume a post-reboot trustcache or a historical child PID.

## Return to the safer branch

The recommended next branch is the original MacWS/macPad Ventura 13.4
userspace, build 22F66 (22F82 is the other documented Ventura pair), using the
known dyld/interpose/runtime pipeline. `AGENTS.md` records broad visible
validation on the primary M1 Ventura target, including WindowServer, native
AGX, windows, input, VS Code and Steam workloads.

This is a lower-risk, evidence-backed return, but the current device is
iPad13,11 / 20D47 while the broadest matrix entry is iPad13,6 / 20D67. A
fresh inventory and bounded `/bin/echo` acceptance are still required; do not
call Ventura “directly ready” solely from the historical matrix. Available
13.2.1 assets are not equivalent to the author-validated 13.4/22F66 rootfs.

## Device cleanup contract before Ventura work

Before deleting anything on the iPad, create and retain a dated manifest of
path, type, size, inode, SHA-256 where practical, and classification:

1. 15.6-only temporary probes/candidates: eligible for deletion after review.
2. Shared runtime/tools required by Ventura: retain.
3. Rootfs payload and original dyld/cache: retain until Ventura replacement is
   verified.
4. Unknown or user-owned files: retain and report.

The cleanup must scan `/var/mobile`, `/var/jb/var/mobile`, `/var/jb/usr/macOS`,
rootfs temporary paths, and any 15.6-specific staging directories. It must
not recursively delete `/var/jb`, `/var/mnt/rootfs`, or shared helpers. After
deletion, repeat the scan and verify no runner/SSH/debug process remains.

No passwords, SSH keys, raw transcripts, Apple payload archives, or private
credentials belong in this repository.
